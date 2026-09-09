// Byte transports for the link: BLE (Microchip Transparent UART on the BM83) and an in-app demo detector.
import Foundation
import CoreBluetooth

enum TransportState: Equatable {
    case idle, scanning, connecting(String), connected(String), failed(String)
    var text: String {
        switch self {
        case .idle: return "Not connected"
        case .scanning: return "Scanning for MDX-1…"
        case .connecting(let n): return "Connecting to \(n)…"
        case .connected(let n): return "Connected to \(n)"
        case .failed(let e): return e
        }
    }
    var isConnected: Bool { if case .connected = self { return true }; return false }
}

struct DiscoveredDetector: Identifiable, Hashable {
    let id: UUID
    let name: String
    let rssi: Int
}

protocol LinkTransport: AnyObject {
    var onReceive: ((Data) -> Void)? { get set }
    var onState: ((TransportState) -> Void)? { get set }
    var onDiscover: (([DiscoveredDetector]) -> Void)? { get set }
    var state: TransportState { get }
    func startScan()
    func connect(to id: UUID)
    func disconnect()
    func send(_ data: Data)
}

// MARK: - BLE
final class BLETransport: NSObject, LinkTransport, CBCentralManagerDelegate, CBPeripheralDelegate {
    static let service = CBUUID(string: "49535343-FE7D-4AE5-8FA9-9FAFD205E455")
    static let rxChar  = CBUUID(string: "49535343-8841-43F4-A8D4-ECBE34729BB3")   // phone -> module (write)
    static let txChar  = CBUUID(string: "49535343-1E4D-4BD9-BA61-23C647249616")   // module -> phone (notify)

    var onReceive: ((Data) -> Void)?
    var onState: ((TransportState) -> Void)?
    var onDiscover: (([DiscoveredDetector]) -> Void)?
    private(set) var state: TransportState = .idle { didSet { onState?(state) } }

    private var central: CBCentralManager!
    private var peripherals: [UUID: (CBPeripheral, Int)] = [:]
    private var peripheral: CBPeripheral?
    private var writeChar: CBCharacteristic?
    private var wantScan = false
    private var pendingConnect: UUID?
    private var writeQueue: [Data] = []
    private var writeBusy = false

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: .main)
    }

    func startScan() {
        wantScan = true
        peripherals.removeAll()
        guard central.state == .poweredOn else { state = .failed("Bluetooth is off"); return }
        state = .scanning
        central.scanForPeripherals(withServices: [BLETransport.service], options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
        // modules that advertise the name but not the service UUID
        DispatchQueue.main.asyncAfter(deadline: .now() + 3) { [weak self] in
            guard let self, self.wantScan, self.peripherals.isEmpty else { return }
            self.central.scanForPeripherals(withServices: nil, options: nil)
        }
    }

    func connect(to id: UUID) {
        guard let (p, _) = peripherals[id] else { pendingConnect = id; return }
        central.stopScan(); wantScan = false
        peripheral = p; p.delegate = self
        state = .connecting(p.name ?? "MDX-1")
        central.connect(p, options: nil)
    }

    func disconnect() {
        wantScan = false
        central.stopScan()
        if let p = peripheral { central.cancelPeripheralConnection(p) }
        peripheral = nil; writeChar = nil; writeQueue.removeAll(); writeBusy = false
        state = .idle
    }

    func send(_ data: Data) {
        guard let p = peripheral, let c = writeChar else { return }
        // chunk to the negotiated MTU (BM83 default 20 bytes)
        let type: CBCharacteristicWriteType = c.properties.contains(.writeWithoutResponse) ? .withoutResponse : .withResponse
        let mtu = max(20, p.maximumWriteValueLength(for: type))
        var off = 0
        while off < data.count {
            let end = min(off + mtu, data.count)
            writeQueue.append(data.subdata(in: off..<end)); off = end
        }
        pumpWrites()
    }
    private func pumpWrites() {
        guard let p = peripheral, let c = writeChar, !writeBusy, !writeQueue.isEmpty else { return }
        let chunk = writeQueue.removeFirst()
        if c.properties.contains(.writeWithoutResponse) {
            p.writeValue(chunk, for: c, type: .withoutResponse)
            if p.canSendWriteWithoutResponse { pumpWrites() } else { writeBusy = true }
        } else {
            writeBusy = true
            p.writeValue(chunk, for: c, type: .withResponse)
        }
    }

    // CBCentralManagerDelegate
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn: if wantScan { startScan() }
        case .unauthorized: state = .failed("Bluetooth permission denied")
        case .poweredOff: state = .failed("Bluetooth is off")
        default: break
        }
    }
    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral, advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let name = peripheral.name ?? (advertisementData[CBAdvertisementDataLocalNameKey] as? String) ?? ""
        let hasService = (advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID])?.contains(BLETransport.service) ?? false
        guard hasService || name.uppercased().hasPrefix("MDX") else { return }
        peripherals[peripheral.identifier] = (peripheral, RSSI.intValue)
        onDiscover?(peripherals.map { DiscoveredDetector(id: $0.key, name: $0.value.0.name ?? name, rssi: $0.value.1) }.sorted { $0.rssi > $1.rssi })
        if pendingConnect == peripheral.identifier { pendingConnect = nil; connect(to: peripheral.identifier) }
    }
    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        peripheral.discoverServices([BLETransport.service])
    }
    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        state = .failed("Connection failed: \(error?.localizedDescription ?? "unknown")")
    }
    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        self.peripheral = nil; writeChar = nil; writeQueue.removeAll(); writeBusy = false
        state = error == nil ? .idle : .failed("Disconnected: \(error!.localizedDescription)")
    }

    // CBPeripheralDelegate
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard let s = peripheral.services?.first(where: { $0.uuid == BLETransport.service }) else { state = .failed("Transparent UART service not found"); return }
        peripheral.discoverCharacteristics([BLETransport.rxChar, BLETransport.txChar], for: s)
    }
    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        for c in service.characteristics ?? [] {
            if c.uuid == BLETransport.txChar { peripheral.setNotifyValue(true, for: c) }
            if c.uuid == BLETransport.rxChar { writeChar = c }
        }
        if writeChar != nil { state = .connected(peripheral.name ?? "MDX-1") } else { state = .failed("Write characteristic not found") }
    }
    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        if let d = characteristic.value, characteristic.uuid == BLETransport.txChar { onReceive?(d) }
    }
    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        writeBusy = false; pumpWrites()
    }
    func peripheralIsReady(toSendWriteWithoutResponse peripheral: CBPeripheral) {
        writeBusy = false; pumpWrites()
    }
}

// MARK: - Demo detector (no hardware): answers the same protocol with a simulated sweep.
final class DemoTransport: LinkTransport {
    var onReceive: ((Data) -> Void)?
    var onState: ((TransportState) -> Void)?
    var onDiscover: (([DiscoveredDetector]) -> Void)?
    private(set) var state: TransportState = .idle { didSet { onState?(state) } }

    private var parser = LinkFrameParser()
    private var timer: Timer?
    private var streamPeriod: TimeInterval = 0
    private var lastStream = Date.distantPast
    private var authed = false
    private var t0 = Date()
    private var finds: [FindRecord] = []
    private var learned: [LearnedEntry] = [LearnedEntry(id: 0, name: "Silver", condID: 88, fe: 3), LearnedEntry(id: 1, name: "Pull tab", condID: 42, fe: 8)]
    private var pinpoint = false
    private var ncUntil: Date?
    private var wizardStep = 0xFF
    private var values: [Int]
    static let items: [ItemInfo] = DemoDetector.items
    private let id = UUID()

    init() { values = DemoDetector.defaults }

    func startScan() {
        state = .scanning
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.6) { [weak self] in
            guard let self else { return }
            self.onDiscover?([DiscoveredDetector(id: self.id, name: "MDX-1 demo", rssi: -48)])
        }
    }
    func connect(to id: UUID) {
        state = .connecting("MDX-1 demo")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { [weak self] in
            guard let self else { return }
            self.state = .connected("MDX-1 demo")
            self.timer = Timer.scheduledTimer(withTimeInterval: 0.05, repeats: true) { [weak self] _ in self?.tick() }
        }
    }
    func disconnect() { timer?.invalidate(); timer = nil; authed = false; streamPeriod = 0; state = .idle }

    func send(_ data: Data) {
        for f in parser.feed(data) { handle(f) }
    }

    private func reply(_ type: UInt8, _ seq: UInt8, _ payload: [UInt8]) {
        let d = Link.frame(type: type | 0x80, seq: seq, payload: payload)
        DispatchQueue.main.async { [weak self] in self?.onReceive?(d) }
    }
    private func err(_ type: UInt8, _ seq: UInt8, _ code: Link.ErrorCode) { reply(Link.MsgType.error.rawValue, seq, [type, code.rawValue]) }

    private func handle(_ f: LinkFrame) {
        guard let t = Link.MsgType(rawValue: f.type) else { return }
        let p = f.payload
        if t != .hello && !authed { err(f.type, f.seq, .notAuthenticated); return }
        switch t {
        case .hello:
            let pin = p.count >= 3 ? Int(p[1]) | Int(p[2]) << 8 : -1
            authed = pin == values[DemoDetector.pinIndex]
            var r: [UInt8] = [Link.protoVersion, authed ? 1 : 0]
            r += DemoDetector.pad("0.4.0", 8); r += DemoDetector.pad("A4", 4)
            r += [UInt8(DemoTransport.items.count)]; r += DemoDetector.le16(finds.count); r += [UInt8(Link.statusLength)]; r += DemoDetector.pad("MDX-1", 12)
            reply(f.type, f.seq, r)
            if !authed { err(f.type, f.seq, .badPIN) }
        case .item:
            guard let i = p.first, Int(i) < DemoTransport.items.count else { err(f.type, f.seq, .badIndex); return }
            let it = DemoTransport.items[Int(i)]
            var r: [UInt8] = [i, it.isAction ? 1 : 0]; r += DemoDetector.le16(it.lo); r += DemoDetector.le16(it.hi); r += [UInt8(it.labels.count)]
            r += Array(it.name.utf8) + [0]
            for l in it.labels { r += Array(l.utf8) + [0] }
            reply(f.type, f.seq, r)
        case .values:
            var r: [UInt8] = [UInt8(values.count)]
            for v in values { r += DemoDetector.le16(v) }
            reply(f.type, f.seq, r)
        case .set:
            guard p.count >= 3, Int(p[0]) < DemoTransport.items.count else { err(f.type, f.seq, .badIndex); return }
            let it = DemoTransport.items[Int(p[0])]; let v = Int(Int16(bitPattern: UInt16(p[1]) | UInt16(p[2]) << 8))
            if it.isAction { reply(f.type, f.seq, [p[0], 0, 0]); return }
            guard v >= it.lo && v <= it.hi else { err(f.type, f.seq, .badValue); return }
            values[Int(p[0])] = v
            reply(f.type, f.seq, [p[0]] + DemoDetector.le16(v))
        case .action:
            guard let a = p.first.flatMap(Link.Action.init) else { err(f.type, f.seq, .unknown); return }
            var res: UInt8 = 0
            switch a {
            case .noiseCancel: ncUntil = Date().addingTimeInterval(2.5)
            case .pinpointOn: pinpoint = true
            case .pinpointOff: pinpoint = false
            case .learn:
                let name = String(decoding: p.dropFirst().prefix(11), as: UTF8.self)
                if DemoDetector.target(at: Date().timeIntervalSince(t0)).present {
                    learned.append(LearnedEntry(id: (learned.map(\.id).max() ?? -1) + 1, name: name.isEmpty ? "P\(Int(DemoDetector.target(at: Date().timeIntervalSince(t0)).id))" : name, condID: 61, fe: 4))
                } else { res = Link.ErrorCode.busy.rawValue }
            case .forget: if p.count >= 2 { learned.removeAll { $0.id == Int(p[1]) } }
            case .markFind:
                let tg = DemoDetector.target(at: Date().timeIntervalSince(t0))
                finds.insert(FindRecord(id: 0, utc: UInt32(Date().timeIntervalSince1970), lat: 51.5074 + Double(finds.count) * 0.0002, lon: -0.1278 + Double(finds.count) * 0.0003,
                                        condID: Int(tg.id), fe: tg.fe, depthCM: tg.depth, confidence: 90, name: ""), at: 0)
                reply(Link.MsgType.event.rawValue, 0, [Link.Event.find.rawValue, 0] + DemoDetector.le16(finds.count))
            case .wizardStart: wizardStep = 0
            case .wizardStep: wizardStep = wizardStep == 0xFF ? 0xFF : min(wizardStep + 1, 7); if wizardStep == 7 { wizardStep = 0xFF }
            case .wizardCancel: wizardStep = 0xFF
            case .defaults: values = DemoDetector.defaults
            default: break
            }
            reply(f.type, f.seq, [a.rawValue, res])
        case .status:
            if p.count >= 2 {
                let per = Int(p[0]) | Int(p[1]) << 8
                if per != 0xFFFF { streamPeriod = per == 0 ? 0 : Double(max(per, 50)) / 1000 }
            }
            reply(f.type, f.seq, statusPacket())
        case .find:
            let idx = p.count >= 2 ? Int(p[0]) | Int(p[1]) << 8 : 0
            guard idx < finds.count else { err(f.type, f.seq, .badIndex); return }
            let r = finds[idx]
            var b = DemoDetector.le16(idx); b += DemoDetector.le32(Int(r.utc)); b += DemoDetector.le32(Int(r.lat * 1e7)); b += DemoDetector.le32(Int(r.lon * 1e7))
            b += DemoDetector.le16(r.condID); b += [UInt8(r.fe), UInt8(r.depthCM), UInt8(r.confidence)]; b += DemoDetector.pad(r.name, 12)
            reply(f.type, f.seq, b)
        case .learned:
            var b: [UInt8] = [UInt8(learned.count)]
            for e in learned { b += [UInt8(e.id)]; b += DemoDetector.pad(e.name, 11); b += [UInt8(e.condID), UInt8(e.fe)] }
            reply(f.type, f.seq, b)
        case .diag:
            let floats: [Float] = [3.92, 3.30, 5.01, -5.00, 12.0, 14.9, 14.4, 0.42, 24.5, 11.5e-6, 64e-6, 0.0103, 0.0009, 62.4, 0.362, 0.63, 5020, 10040, 15060, 20080, 41164]
            var b: [UInt8] = []
            for f in floats { b += DemoDetector.le32(Int(Int32(bitPattern: f.bitPattern))) }
            let up = Int(Date().timeIntervalSince(t0))
            b += DemoDetector.le32(up * 5120); b += DemoDetector.le32(up * 3); b += DemoDetector.le32(up); b += [UInt8(12)]; b += DemoDetector.pad("MDX DD11", 12)
            reply(f.type, f.seq, b)
        case .event, .error: break
        }
    }

    private func tick() {
        guard authed, streamPeriod > 0, Date().timeIntervalSince(lastStream) >= streamPeriod else { return }
        lastStream = Date()
        onReceive?(Link.frame(type: Link.MsgType.status.response, seq: 0, payload: statusPacket()))
        if let n = ncUntil, Date() > n {
            ncUntil = nil; values[DemoDetector.noiseIndex] = 0
            onReceive?(Link.frame(type: Link.MsgType.event.response, seq: 0, payload: [Link.Event.noiseCancelDone.rawValue, 101, 12, 0]))
        }
    }

    private func statusPacket() -> [UInt8] {
        let t = Date().timeIntervalSince(t0)
        let tg = DemoDetector.target(at: t)
        let feLimit = values[DemoDetector.feLimitIndex]
        var flags: Link.StatusFlags = [.coil]
        if tg.present { flags.insert(.present); if tg.fe <= feLimit { flags.insert(.accepted) } else { flags.insert(.ferrous) } }
        if pinpoint { flags.insert(.pinpoint) }
        if ncUntil != nil { flags.insert(.ncActive) }
        let batt = max(20, 78 - Int(t / 120))
        var b: [UInt8] = [flags.rawValue]
        b += DemoDetector.le16(Int(tg.id * 10)); b += [UInt8(tg.fe), UInt8(tg.present ? 92 : 0), UInt8(bitPattern: Int8(tg.present ? tg.db : -12)), UInt8(tg.depth)]
        b += [UInt8(pinpoint ? tg.level * 255 : min(255, max(0, Double(tg.present ? tg.db : 0) / 40 * 255)))]
        b += DemoDetector.le16(624); b += [10, 8, 0, 101]; b += DemoDetector.le16(12); b += DemoDetector.le16(420); b += [149, UInt8(batt)]
        b += DemoDetector.le16(3300 + batt * 9); b += [UInt8(min(255, batt * 15 / 100 * 10))]
        b += tg.spectrum.map { UInt8($0 * 255) }
        b += [1]; b += DemoDetector.le32(Int(51.5074e7)); b += DemoDetector.le32(Int(-0.1278e7)); b += [9, 11]
        b += DemoDetector.le16(finds.count); b += [UInt8(tg.present ? tg.depth + 3 : 0), UInt8(bitPattern: Int8(tg.present && tg.id > 80 ? 0 : -1))]
        b += [UInt8(values[0]), UInt8(values[1]), UInt8(values[2])]
        b += DemoDetector.le32(Int(t)); b += [UInt8(tg.level * 255), UInt8(tg.swing * 10), 0, UInt8(wizardStep & 0xFF), pinpoint ? 2 : 0]
        return b
    }
}

/// The demo's idea of the detector: the firmware menu table and a sweep that passes over three targets.
enum DemoDetector {
    static let items: [ItemInfo] = [
        ItemInfo(id: 0, name: "Program", lo: 0, hi: 4, labels: ["Park", "Field", "Beach", "Gold", "Relic"]),
        ItemInfo(id: 1, name: "Mode", lo: 0, hi: 6, labels: ["Multi 5-41k", "5.1 kHz", "10.2 kHz", "20.5 kHz", "41 kHz", "PI", "Hybrid V+PI"]),
        ItemInfo(id: 2, name: "Sensitivity", lo: 1, hi: 30),
        ItemInfo(id: 3, name: "Auto sensitivity", lo: 0, hi: 1, labels: ["Off", "On"]),
        ItemInfo(id: 4, name: "Recovery speed", lo: 1, hi: 8),
        ItemInfo(id: 5, name: "TX power", lo: 1, hi: 10),
        ItemInfo(id: 6, name: "Ferrous limit", lo: 20, hi: 95),
        ItemInfo(id: 7, name: "Ground balance", lo: 0, hi: 2, labels: ["Auto track", "Manual", "Off"]),
        ItemInfo(id: 8, name: "Salt mode", lo: 0, hi: 1, labels: ["Off", "On"]),
        ItemInfo(id: 9, name: "Hybrid PI rate", lo: 0, hi: 32),
        ItemInfo(id: 10, name: "Tones", lo: 0, hi: 3, labels: ["VCO", "2 tone", "5 tone", "Pitch"]),
        ItemInfo(id: 11, name: "Volume", lo: 0, hi: 10),
        ItemInfo(id: 12, name: "Threshold hum", lo: 0, hi: 10),
        ItemInfo(id: 13, name: "Iron volume", lo: 0, hi: 10),
        ItemInfo(id: 14, name: "Iron grunt", lo: 0, hi: 1, labels: ["Off", "On"]),
        ItemInfo(id: 15, name: "Noise cancel at start", lo: 0, hi: 1, labels: ["Off", "On"]),
        ItemInfo(id: 16, name: "Noise cancel", isAction: true),
        ItemInfo(id: 17, name: "Coil wizard", isAction: true),
        ItemInfo(id: 18, name: "Learn target", isAction: true),
        ItemInfo(id: 19, name: "Find log", isAction: true),
        ItemInfo(id: 20, name: "Diagnostics", isAction: true),
        ItemInfo(id: 21, name: "Front light", lo: 0, hi: 100),
        ItemInfo(id: 22, name: "GPS logging", lo: 0, hi: 1, labels: ["Off", "On"]),
        ItemInfo(id: 23, name: "Depth units", lo: 0, hi: 1, labels: ["cm", "inch"]),
        ItemInfo(id: 24, name: "Phone PIN", lo: 0, hi: 9999),
        ItemInfo(id: 25, name: "Save settings", isAction: true),
    ]
    static let defaults = [1, 0, 20, 0, 4, 4, 55, 0, 0, 0, 2, 7, 2, 3, 1, 1, 0, 0, 0, 0, 0, 30, 1, 0, 1234, 0]
    static let pinIndex = 24, feLimitIndex = 6, noiseIndex = 16

    struct Target { let present: Bool; let id: Double; let fe: Int; let depth: Int; let db: Int; let level: Double; let spectrum: [Double]; let swing: Double }
    /// Three targets in a 12 s loop: a silver coin, a nail, a pull tab; each visible for ~1.2 s.
    static func target(at t: TimeInterval) -> Target {
        let phase = t.truncatingRemainder(dividingBy: 12)
        let sweep = abs(sin(t * 1.6))
        let specs: [(Double, Double, Int, Int, [Double])] = [
            (1.0, 88, 3, 14, [0.35, 0.55, 0.75, 0.9, 1.0]),
            (5.0, 12, 78, 9, [1.0, 0.9, 0.75, 0.6, 0.35]),
            (9.0, 42, 6, 7, [0.6, 0.9, 1.0, 0.85, 0.5]),
        ]
        for (start, id, fe, depth, spec) in specs where phase >= start && phase < start + 1.2 {
            let env = sin((phase - start) / 1.2 * .pi)
            return Target(present: env > 0.15, id: id + (env - 0.5) * 1.5, fe: fe, depth: depth, db: Int(env * 28), level: env, spectrum: spec.map { $0 * (0.4 + 0.6 * env) }, swing: 0.8 + sweep)
        }
        return Target(present: false, id: 0, fe: 0, depth: 0, db: -12, level: 0, spectrum: [0.05, 0.05, 0.06, 0.05, 0.04], swing: 0.8 + sweep)
    }

    static func le16(_ v: Int) -> [UInt8] { let u = UInt16(truncatingIfNeeded: v); return [UInt8(u & 0xFF), UInt8(u >> 8)] }
    static func le32(_ v: Int) -> [UInt8] { let u = UInt32(truncatingIfNeeded: v); return [UInt8(u & 0xFF), UInt8((u >> 8) & 0xFF), UInt8((u >> 16) & 0xFF), UInt8(u >> 24)] }
    static func pad(_ s: String, _ n: Int) -> [UInt8] { var b = Array(s.utf8.prefix(n - 1)); while b.count < n { b.append(0) }; return b }
}
