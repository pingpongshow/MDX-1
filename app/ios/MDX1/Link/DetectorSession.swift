// The app's model of one detector: connection, authentication, the item table, live status and actions.
import Foundation
import Combine

enum SessionState: Equatable {
    case disconnected, scanning, connecting, authenticating, ready, failed(String)
    var text: String {
        switch self {
        case .disconnected: return "Not connected"
        case .scanning: return "Scanning…"
        case .connecting: return "Connecting…"
        case .authenticating: return "Checking PIN…"
        case .ready: return "Connected"
        case .failed(let e): return e
        }
    }
}

struct LinkError: LocalizedError {
    let message: String
    var errorDescription: String? { message }
}

@MainActor
final class DetectorSession: ObservableObject {
    @Published private(set) var state: SessionState = .disconnected
    @Published private(set) var info: HelloInfo?
    @Published private(set) var items: [ItemInfo] = []
    @Published private(set) var values: [Int] = []
    @Published private(set) var status: StatusPacket?
    @Published private(set) var finds: [FindRecord] = []
    @Published private(set) var learned: [LearnedEntry] = []
    @Published private(set) var diag: DiagPacket?
    @Published private(set) var events: [LinkEvent] = []
    @Published private(set) var discovered: [DiscoveredDetector] = []
    @Published var lastError: String?
    @Published var pin: Int = UserDefaults.standard.object(forKey: "pin") as? Int ?? 1234 { didSet { UserDefaults.standard.set(pin, forKey: "pin") } }
    @Published var demoMode: Bool = UserDefaults.standard.bool(forKey: "demo") { didSet { UserDefaults.standard.set(demoMode, forKey: "demo") } }
    /// connect to the strongest MDX-1 as soon as one is seen (and at launch)
    @Published var autoConnect: Bool = UserDefaults.standard.object(forKey: "auto") as? Bool ?? true { didSet { UserDefaults.standard.set(autoConnect, forKey: "auto") } }

    private var transport: LinkTransport?
    private var parser = LinkFrameParser()
    private var seq: UInt8 = 0
    private var pending: [UInt8: (UInt8, CheckedContinuation<[UInt8], Error>)] = [:]
    private var statusStreaming = false
    var isReady: Bool { state == .ready }

    // MARK: connection
    func startScan() {
        disconnect()
        let t: LinkTransport = demoMode ? DemoTransport() : BLETransport()
        transport = t
        t.onReceive = { [weak self] d in Task { @MainActor in self?.receive(d) } }
        t.onState = { [weak self] s in Task { @MainActor in self?.transportChanged(s) } }
        t.onDiscover = { [weak self] list in Task { @MainActor in
            guard let self else { return }
            self.discovered = list
            if self.autoConnect, self.state == .scanning, let first = list.first { self.connect(to: first) }
        } }
        state = .scanning
        t.startScan()
    }
    func connect(to d: DiscoveredDetector) {
        state = .connecting
        transport?.connect(to: d.id)
    }
    func disconnect() {
        transport?.disconnect(); transport = nil
        for (_, (_, c)) in pending { c.resume(throwing: LinkError(message: "disconnected")) }
        pending.removeAll()
        parser = LinkFrameParser()
        state = .disconnected; info = nil; items = []; values = []; status = nil; diag = nil; statusStreaming = false; discovered = []
    }

    private func transportChanged(_ s: TransportState) {
        switch s {
        case .connected: Task { await handshake() }
        case .failed(let e): state = .failed(e)
        case .idle: if state != .disconnected { disconnect() }
        case .scanning: state = .scanning
        case .connecting: state = .connecting
        }
    }

    private func handshake() async {
        state = .authenticating
        do {
            let h = try await hello()
            info = h
            guard h.authenticated else { state = .failed("Wrong PIN. The detector shows its PIN under Menu → Phone PIN."); return }
            var list: [ItemInfo] = []
            for i in 0..<h.itemCount { if let it = ItemInfo(try await request(.item, [UInt8(i)])) { list.append(it) } }
            items = list
            try await refreshValues()
            try await startStatusStream(ms: 100)
            state = .ready
            await refreshFinds()
            await refreshLearned()
        } catch {
            state = .failed(error.localizedDescription)
        }
    }

    // MARK: requests
    private func nextSeq() -> UInt8 { seq = seq &+ 1; if seq == 0 { seq = 1 }; return seq }

    private func request(_ type: Link.MsgType, _ payload: [UInt8] = [], timeout: TimeInterval = 2.0) async throws -> [UInt8] {
        guard let t = transport else { throw LinkError(message: "not connected") }
        let s = nextSeq()
        return try await withCheckedThrowingContinuation { (c: CheckedContinuation<[UInt8], Error>) in
            pending[s] = (type.rawValue, c)
            t.send(Link.frame(type: type.rawValue, seq: s, payload: payload))
            Task { [weak self] in
                try? await Task.sleep(nanoseconds: UInt64(timeout * 1e9))
                await MainActor.run {
                    if let (_, cont) = self?.pending.removeValue(forKey: s) { cont.resume(throwing: LinkError(message: "no answer from the detector")) }
                }
            }
        }
    }

    private func receive(_ d: Data) {
        for f in parser.feed(d) {
            if f.type == Link.MsgType.error.response {
                let failed = f.payload.first ?? 0
                let code = f.payload.count > 1 ? Link.ErrorCode(rawValue: f.payload[1]) : nil
                if let (want, c) = pending[f.seq], want == failed { pending.removeValue(forKey: f.seq); c.resume(throwing: LinkError(message: code?.text ?? "error")) }
                else if code != .badPIN { lastError = code?.text }
                continue
            }
            if f.seq != 0, let (want, c) = pending[f.seq], f.type == (want | 0x80) {
                pending.removeValue(forKey: f.seq); c.resume(returning: f.payload); continue
            }
            switch f.type {
            case Link.MsgType.status.response: if let s = StatusPacket(f.payload) { status = s }
            case Link.MsgType.event.response:
                guard f.payload.count >= 4, let k = Link.Event(rawValue: f.payload[0]) else { continue }
                let ev = LinkEvent(kind: k, a: Int(f.payload[1]), b: Int(f.payload[2]) | Int(f.payload[3]) << 8)
                events.insert(ev, at: 0); if events.count > 20 { events.removeLast() }
                switch k {
                case .find: Task { await refreshFinds() }
                case .noiseCancelDone: Task { try? await refreshValues() }
                case .settings: Task { try? await refreshValues() }
                default: break
                }
            default: break
            }
        }
    }

    private func hello() async throws -> HelloInfo {
        let p = try await request(.hello, [Link.protoVersion, UInt8(pin & 0xFF), UInt8(pin >> 8)])
        guard let h = HelloInfo(p) else { throw LinkError(message: "bad HELLO reply") }
        return h
    }
    func refreshValues() async throws {
        let p = try await request(.values)
        guard let n = p.first, p.count >= 1 + 2 * Int(n) else { throw LinkError(message: "bad VALUES reply") }
        values = (0..<Int(n)).map { Int(Int16(bitPattern: UInt16(p[1 + 2 * $0]) | UInt16(p[2 + 2 * $0]) << 8)) }
    }
    private func startStatusStream(ms: Int) async throws {
        _ = try await request(.status, [UInt8(ms & 0xFF), UInt8(ms >> 8)])
        statusStreaming = true
    }

    // MARK: public operations
    func value(of item: ItemInfo) -> Int { item.id < values.count ? values[item.id] : item.lo }

    func set(_ item: ItemInfo, to v: Int) {
        guard !item.isAction else { Task { await run(item) }; return }
        let clamped = min(max(v, item.lo), item.hi)
        if item.id < values.count { values[item.id] = clamped }        // optimistic
        Task {
            do {
                let r = try await request(.set, [UInt8(item.id), UInt8(clamped & 0xFF), UInt8((clamped >> 8) & 0xFF)])
                if r.count >= 3, item.id < values.count { values[item.id] = Int(Int16(bitPattern: UInt16(r[1]) | UInt16(r[2]) << 8)) }
            } catch { lastError = "\(item.name): \(error.localizedDescription)"; try? await refreshValues() }
        }
    }

    /// Menu actions the detector's own menu offers (noise cancel, wizard, save…).
    func run(_ item: ItemInfo) async {
        switch item.name {
        case "Noise cancel": await action(.noiseCancel)
        case "Coil wizard": await action(.wizardStart)
        case "Save settings": await action(.save)
        default: _ = try? await request(.set, [UInt8(item.id), 0, 0])
        }
    }

    @discardableResult
    func action(_ a: Link.Action, arg: [UInt8] = []) async -> Bool {
        do {
            let r = try await request(.action, [a.rawValue] + arg)
            if r.count >= 2, r[1] != 0 { lastError = Link.ErrorCode(rawValue: r[1])?.text ?? "action failed"; return false }
            switch a {
            case .learn, .forget: await refreshLearned()
            case .defaults, .save: try? await refreshValues()
            default: break
            }
            return true
        } catch { lastError = error.localizedDescription; return false }
    }
    func learn(name: String) async { await action(.learn, arg: Array(name.utf8.prefix(11))) }
    func forget(_ e: LearnedEntry) async { await action(.forget, arg: [UInt8(e.id)]) }
    func setPinpoint(_ on: Bool) { Task { await action(on ? .pinpointOn : .pinpointOff) } }

    func refreshFinds() async {
        guard let t = transport, state == .ready || state == .authenticating else { return }
        _ = t
        var list: [FindRecord] = []
        var i = 0
        while i < 32 {
            guard let p = try? await request(.find, [UInt8(i & 0xFF), UInt8(i >> 8)]), let f = FindRecord(p) else { break }
            list.append(f); i += 1
        }
        finds = list
    }
    func refreshLearned() async {
        if let p = try? await request(.learned) { learned = LearnedEntry.decodeList(p) }
    }
    func refreshDiag() async {
        if let p = try? await request(.diag) { diag = DiagPacket(p) }
    }
    func item(named n: String) -> ItemInfo? { items.first { $0.name == n } }
}
