// MDX-1 phone link protocol: Swift mirror of firmware/core/link/link.h.
// Frame: 0xA5 len type seq payload[len] crc16(big-endian, CCITT-FALSE over type..payload).
// All payload integers are little-endian. See docs/MDX-1_link_protocol.md.
import Foundation

enum Link {
    static let sof: UInt8 = 0xA5
    static let protoVersion: UInt8 = 1
    static let maxPayload = 240
    static let statusLength = 55

    enum MsgType: UInt8 {
        case hello = 0x01, item = 0x02, values = 0x03, set = 0x04, action = 0x05
        case status = 0x06, find = 0x07, learned = 0x08, diag = 0x09, event = 0x10, error = 0x7F
        var response: UInt8 { rawValue | 0x80 }
    }
    enum Action: UInt8 {
        case noiseCancel = 1, groundGrab, pinpointOn, pinpointOff, learn, forget
        case markFind, save, wizardStart, wizardStep, wizardCancel, powerOff, defaults, acceptAll
    }
    enum ErrorCode: UInt8 {
        case badCRC = 1, notAuthenticated, badPIN, badIndex, badValue, badLength, unknown, busy
        var text: String {
            switch self {
            case .badCRC: return "corrupted frame"
            case .notAuthenticated: return "not authenticated"
            case .badPIN: return "wrong PIN"
            case .badIndex: return "no such item"
            case .badValue: return "value out of range"
            case .badLength: return "malformed request"
            case .unknown: return "unknown request"
            case .busy: return "detector is busy"
            }
        }
    }
    enum Event: UInt8 { case noiseCancelDone = 1, find = 2, lowBattery = 3, wizard = 4, coil = 5, settings = 6 }

    struct StatusFlags: OptionSet {
        let rawValue: UInt8
        static let present   = StatusFlags(rawValue: 1)
        static let accepted  = StatusFlags(rawValue: 2)
        static let ferrous   = StatusFlags(rawValue: 4)
        static let pinpoint  = StatusFlags(rawValue: 8)
        static let charging  = StatusFlags(rawValue: 16)
        static let lowBatt   = StatusFlags(rawValue: 32)
        static let coil      = StatusFlags(rawValue: 64)
        static let ncActive  = StatusFlags(rawValue: 128)
    }

    static func crc16(_ bytes: [UInt8]) -> UInt16 {
        var crc: UInt16 = 0xFFFF
        for b in bytes {
            crc ^= UInt16(b) << 8
            for _ in 0..<8 { crc = (crc & 0x8000) != 0 ? (crc << 1) ^ 0x1021 : crc << 1 }
        }
        return crc
    }

    static func frame(type: UInt8, seq: UInt8, payload: [UInt8]) -> Data {
        precondition(payload.count <= maxPayload)
        var b: [UInt8] = [sof, UInt8(payload.count), type, seq]
        b.append(contentsOf: payload)
        let c = crc16(Array(b[2...]))
        b.append(UInt8(c >> 8)); b.append(UInt8(c & 0xFF))
        return Data(b)
    }
}

/// One decoded frame from the detector.
struct LinkFrame {
    let type: UInt8
    let seq: UInt8
    let payload: [UInt8]
    var isResponse: Bool { type & 0x80 != 0 }
    var requestType: Link.MsgType? { Link.MsgType(rawValue: type & 0x7F) }
}

/// Streaming parser: feed bytes in any chunking, get whole, CRC-checked frames back.
struct LinkFrameParser {
    private var state = 0
    private var len = 0
    private var buf: [UInt8] = []
    private(set) var badFrames = 0

    mutating func feed(_ data: Data) -> [LinkFrame] {
        var out: [LinkFrame] = []
        for c in data {
            switch state {
            case 0:
                if c == Link.sof { buf.removeAll(keepingCapacity: true); state = 1 }
            case 1:
                if Int(c) > Link.maxPayload { state = 0; badFrames += 1 } else { len = Int(c); state = 2 }
            default:
                buf.append(c)
                if buf.count == len + 4 {
                    let want = UInt16(buf[len + 2]) << 8 | UInt16(buf[len + 3])
                    if Link.crc16(Array(buf[0..<(len + 2)])) == want {
                        out.append(LinkFrame(type: buf[0], seq: buf[1], payload: Array(buf[2..<(len + 2)])))
                    } else { badFrames += 1 }
                    state = 0
                }
            }
        }
        return out
    }
}

// MARK: - little-endian readers
struct ByteReader {
    let b: [UInt8]
    var i = 0
    init(_ b: [UInt8]) { self.b = b }
    var remaining: Int { b.count - i }
    mutating func u8() -> UInt8 { defer { i += 1 }; return i < b.count ? b[i] : 0 }
    mutating func i8() -> Int8 { Int8(bitPattern: u8()) }
    mutating func u16() -> UInt16 { let lo = UInt16(u8()); let hi = UInt16(u8()); return lo | hi << 8 }
    mutating func i16() -> Int16 { Int16(bitPattern: u16()) }
    mutating func u32() -> UInt32 { let lo = UInt32(u16()); let hi = UInt32(u16()); return lo | hi << 16 }
    mutating func i32() -> Int32 { Int32(bitPattern: u32()) }
    mutating func f32() -> Float { Float(bitPattern: u32()) }
    mutating func str(_ n: Int) -> String {
        let end = min(i + n, b.count); let slice = b[i..<end]; i = end
        let cut = slice.prefix { $0 != 0 }
        return String(decoding: cut, as: UTF8.self)
    }
    mutating func cstr() -> String {
        var s: [UInt8] = []
        while i < b.count { let c = b[i]; i += 1; if c == 0 { break }; s.append(c) }
        return String(decoding: s, as: UTF8.self)
    }
}

// MARK: - decoded messages
struct HelloInfo {
    let protoVersion: Int, authenticated: Bool, firmware: String, hardware: String
    let itemCount: Int, findCount: Int, statusLength: Int, name: String
    init?(_ p: [UInt8]) {
        guard p.count >= 30 else { return nil }
        var r = ByteReader(p)
        protoVersion = Int(r.u8()); authenticated = r.u8() == 1
        firmware = r.str(8); hardware = r.str(4)
        itemCount = Int(r.u8()); findCount = Int(r.u16()); statusLength = Int(r.u8()); name = r.str(12)
    }
}

struct ItemInfo: Identifiable, Hashable {
    let id: Int
    let name: String
    let isAction: Bool
    let lo: Int, hi: Int
    let labels: [String]
    init?(_ p: [UInt8]) {
        guard p.count >= 7 else { return nil }
        var r = ByteReader(p)
        id = Int(r.u8()); isAction = r.u8() == 1; lo = Int(r.i16()); hi = Int(r.i16())
        let n = Int(r.u8()); name = r.cstr()
        var l: [String] = []
        for _ in 0..<n where r.remaining > 0 { l.append(r.cstr()) }
        labels = l
    }
    init(id: Int, name: String, isAction: Bool = false, lo: Int = 0, hi: Int = 0, labels: [String] = []) {
        self.id = id; self.name = name; self.isAction = isAction; self.lo = lo; self.hi = hi; self.labels = labels
    }
    var isLabelled: Bool { !labels.isEmpty }
    func label(for v: Int) -> String {
        let k = v - lo
        return (k >= 0 && k < labels.count) ? labels[k] : String(v)
    }
}

struct StatusPacket {
    let flags: Link.StatusFlags
    let condID: Double, feIndex: Int, confidence: Int, strengthDB: Int, depthCM: Int, signal: Double
    let groundPhase: Double, groundStrength: Int, salt: Int, hotRock: Int
    let noiseM: Int, noiseUV: Int, itxMA: Int, vtx: Double
    let battPct: Int, battMV: Int, battHours: Double
    let spectrum: [Double]
    let gnssValid: Bool, lat: Double, lon: Double, sats: Int, hdop: Double
    let finds: Int, piDepthCM: Int, matchIndex: Int, program: Int, mode: Int, sensitivity: Int
    let uptime: UInt32, pinpointLevel: Double, swing: Double, piTarget: Bool, wizardStep: Int, screen: Int

    init?(_ p: [UInt8]) {
        guard p.count >= Link.statusLength else { return nil }
        var r = ByteReader(p)
        flags = Link.StatusFlags(rawValue: r.u8())
        condID = Double(r.i16()) / 10; feIndex = Int(r.u8()); confidence = Int(r.u8()); strengthDB = Int(r.i8())
        depthCM = Int(r.u8()); signal = Double(r.u8()) / 255
        groundPhase = Double(r.i16()) / 10; groundStrength = Int(r.u8()); salt = Int(r.u8()); hotRock = Int(r.u8())
        noiseM = Int(r.u8()); noiseUV = Int(r.u16()); itxMA = Int(r.u16()); vtx = Double(r.u8()) / 10
        battPct = Int(r.u8()); battMV = Int(r.u16()); battHours = Double(r.u8()) / 10
        spectrum = (0..<5).map { _ in Double(r.u8()) / 255 }
        gnssValid = r.u8() == 1; lat = Double(r.i32()) / 1e7; lon = Double(r.i32()) / 1e7
        sats = Int(r.u8()); hdop = Double(r.u8()) / 10
        finds = Int(r.u16()); piDepthCM = Int(r.u8()); matchIndex = Int(r.i8())
        program = Int(r.u8()); mode = Int(r.u8()); sensitivity = Int(r.u8())
        uptime = r.u32(); pinpointLevel = Double(r.u8()) / 255; swing = Double(r.u8()) / 10
        piTarget = r.u8() == 1; wizardStep = Int(r.u8()); screen = Int(r.u8())
    }
    var present: Bool { flags.contains(.present) }
    var accepted: Bool { flags.contains(.accepted) }
    var ferrous: Bool { flags.contains(.ferrous) }
    var pinpoint: Bool { flags.contains(.pinpoint) }
    var inWizard: Bool { wizardStep != 0xFF }
}

struct FindRecord: Identifiable, Hashable {
    let id: Int
    let utc: UInt32
    let lat: Double, lon: Double
    let condID: Int, fe: Int, depthCM: Int, confidence: Int
    let name: String
    init?(_ p: [UInt8]) {
        guard p.count >= 31 else { return nil }
        var r = ByteReader(p)
        id = Int(r.u16()); utc = r.u32(); lat = Double(r.i32()) / 1e7; lon = Double(r.i32()) / 1e7
        condID = Int(r.i16()); fe = Int(r.u8()); depthCM = Int(r.u8()); confidence = Int(r.u8()); name = r.str(12)
    }
    init(id: Int, utc: UInt32, lat: Double, lon: Double, condID: Int, fe: Int, depthCM: Int, confidence: Int, name: String) {
        self.id = id; self.utc = utc; self.lat = lat; self.lon = lon; self.condID = condID; self.fe = fe
        self.depthCM = depthCM; self.confidence = confidence; self.name = name
    }
    var hasPosition: Bool { lat != 0 || lon != 0 }
}

struct LearnedEntry: Identifiable, Hashable {
    let id: Int
    let name: String
    let condID: Int, fe: Int
    static func decodeList(_ p: [UInt8]) -> [LearnedEntry] {
        guard let n = p.first else { return [] }
        var r = ByteReader(p); _ = r.u8()
        var out: [LearnedEntry] = []
        for _ in 0..<Int(n) where r.remaining >= 14 {
            let idx = Int(r.u8()); let name = r.str(11); let cid = Int(r.u8()); let fe = Int(r.u8())
            out.append(LearnedEntry(id: idx, name: name, condID: cid, fe: fe))
        }
        return out
    }
}

struct DiagPacket {
    let vsys: Float, v3v3: Float, v5va: Float, vm5va: Float, v12: Float, vtx: Float, vbridge: Float
    let itxA: Float, tempC: Float, noiseFloor: Float, threshold: Float, groundA: Float, groundB: Float, groundPhase: Float
    let coilLmH: Float, coilR: Float, lineHz: [Float]
    let periods: UInt32, piRecords: UInt32, uptime: UInt32
    let txFault: Bool, clipped: Bool, coilPresent: Bool, calibrated: Bool
    let coilName: String
    init?(_ p: [UInt8]) {
        guard p.count >= 109 else { return nil }
        var r = ByteReader(p)
        vsys = r.f32(); v3v3 = r.f32(); v5va = r.f32(); vm5va = r.f32(); v12 = r.f32(); vtx = r.f32(); vbridge = r.f32()
        itxA = r.f32(); tempC = r.f32(); noiseFloor = r.f32(); threshold = r.f32(); groundA = r.f32(); groundB = r.f32(); groundPhase = r.f32()
        coilLmH = r.f32(); coilR = r.f32(); lineHz = (0..<5).map { _ in r.f32() }
        periods = r.u32(); piRecords = r.u32(); uptime = r.u32()
        let f = r.u8(); txFault = f & 1 != 0; clipped = f & 2 != 0; coilPresent = f & 4 != 0; calibrated = f & 8 != 0
        coilName = r.str(12)
    }
}

struct LinkEvent: Identifiable {
    let id = UUID()
    let kind: Link.Event
    let a: Int, b: Int
    let time = Date()
    var text: String {
        switch kind {
        case .noiseCancelDone: return "Noise cancel done: channel \(a), \(b) µΩ"
        case .find: return "Find #\(b) logged"
        case .lowBattery: return "Low battery: \(a) %"
        case .wizard: return a == 0xFF ? "Coil wizard closed" : "Coil wizard step \(a)"
        case .coil: return "Coil changed"
        case .settings: return "Settings changed on the detector"
        }
    }
}
