// MDX-1 phone link protocol: Kotlin mirror of firmware/core/link/link.h (see docs/MDX-1_link_protocol.md).
// Frame: 0xA5 len type seq payload[len] crc16(big-endian, CCITT-FALSE over type..payload). Payload fields little-endian.
package com.mdx1.companion.link

import java.nio.ByteBuffer
import java.nio.ByteOrder

object Link {
    const val SOF = 0xA5
    const val PROTO_VERSION = 1
    const val MAX_PAYLOAD = 240
    const val STATUS_LENGTH = 55

    object Msg {
        const val HELLO = 0x01; const val ITEM = 0x02; const val VALUES = 0x03; const val SET = 0x04; const val ACTION = 0x05
        const val STATUS = 0x06; const val FIND = 0x07; const val LEARNED = 0x08; const val DIAG = 0x09; const val EVENT = 0x10; const val ERROR = 0x7F
        fun response(t: Int) = t or 0x80
    }
    object Action {
        const val NOISE_CANCEL = 1; const val GROUND_GRAB = 2; const val PINPOINT_ON = 3; const val PINPOINT_OFF = 4; const val LEARN = 5; const val FORGET = 6
        const val MARK_FIND = 7; const val SAVE = 8; const val WIZARD_START = 9; const val WIZARD_STEP = 10; const val WIZARD_CANCEL = 11; const val POWER_OFF = 12
        const val DEFAULTS = 13; const val ACCEPT_ALL = 14
    }
    object Event { const val NC_DONE = 1; const val FIND = 2; const val LOW_BATT = 3; const val WIZARD = 4; const val COIL = 5; const val SETTINGS = 6 }
    object Flag {
        const val PRESENT = 1; const val ACCEPTED = 2; const val FERROUS = 4; const val PINPOINT = 8
        const val CHARGING = 16; const val LOW_BATT = 32; const val COIL = 64; const val NC_ACTIVE = 128
    }
    fun errorText(code: Int) = when (code) {
        1 -> "corrupted frame"; 2 -> "not authenticated"; 3 -> "wrong PIN"; 4 -> "no such item"
        5 -> "value out of range"; 6 -> "malformed request"; 7 -> "unknown request"; 8 -> "detector is busy"; else -> "error $code"
    }

    fun crc16(b: ByteArray, from: Int = 0, to: Int = b.size): Int {
        var crc = 0xFFFF
        for (i in from until to) {
            crc = crc xor ((b[i].toInt() and 0xFF) shl 8)
            repeat(8) { crc = if (crc and 0x8000 != 0) ((crc shl 1) xor 0x1021) and 0xFFFF else (crc shl 1) and 0xFFFF }
        }
        return crc
    }

    fun frame(type: Int, seq: Int, payload: ByteArray): ByteArray {
        require(payload.size <= MAX_PAYLOAD)
        val out = ByteArray(payload.size + 6)
        out[0] = SOF.toByte(); out[1] = payload.size.toByte(); out[2] = type.toByte(); out[3] = seq.toByte()
        payload.copyInto(out, 4)
        val c = crc16(out, 2, 4 + payload.size)
        out[4 + payload.size] = (c shr 8).toByte(); out[5 + payload.size] = (c and 0xFF).toByte()
        return out
    }

    fun le16(v: Int) = byteArrayOf((v and 0xFF).toByte(), ((v shr 8) and 0xFF).toByte())
    fun le32(v: Long) = byteArrayOf((v and 0xFF).toByte(), ((v shr 8) and 0xFF).toByte(), ((v shr 16) and 0xFF).toByte(), ((v shr 24) and 0xFF).toByte())
    fun pad(s: String, n: Int): ByteArray { val b = ByteArray(n); val src = s.toByteArray(Charsets.UTF_8); src.copyInto(b, 0, 0, minOf(src.size, n - 1)); return b }
}

class LinkFrame(val type: Int, val seq: Int, val payload: ByteArray)

/** Streaming parser: feed bytes in any chunking, get CRC-checked frames back. */
class LinkFrameParser {
    private var state = 0
    private var len = 0
    private val buf = ArrayList<Byte>()
    var badFrames = 0; private set

    fun feed(data: ByteArray): List<LinkFrame> {
        val out = ArrayList<LinkFrame>()
        for (byte in data) {
            val c = byte.toInt() and 0xFF
            when (state) {
                0 -> if (c == Link.SOF) { buf.clear(); state = 1 }
                1 -> if (c > Link.MAX_PAYLOAD) { state = 0; badFrames++ } else { len = c; state = 2 }
                else -> {
                    buf.add(byte)
                    if (buf.size == len + 4) {
                        val arr = buf.toByteArray()
                        val want = ((arr[len + 2].toInt() and 0xFF) shl 8) or (arr[len + 3].toInt() and 0xFF)
                        if (Link.crc16(arr, 0, len + 2) == want) out.add(LinkFrame(arr[0].toInt() and 0xFF, arr[1].toInt() and 0xFF, arr.copyOfRange(2, len + 2)))
                        else badFrames++
                        state = 0
                    }
                }
            }
        }
        return out
    }
}

class ByteReader(private val b: ByteArray) {
    private val bb: ByteBuffer = ByteBuffer.wrap(b).order(ByteOrder.LITTLE_ENDIAN)
    val remaining get() = bb.remaining()
    fun u8() = if (bb.hasRemaining()) bb.get().toInt() and 0xFF else 0
    fun i8() = u8().toByte().toInt()
    fun u16() = if (bb.remaining() >= 2) bb.short.toInt() and 0xFFFF else 0
    fun i16() = u16().toShort().toInt()
    fun u32() = if (bb.remaining() >= 4) bb.int.toLong() and 0xFFFFFFFFL else 0L
    fun i32() = if (bb.remaining() >= 4) bb.int else 0
    fun f32() = if (bb.remaining() >= 4) bb.float else 0f
    fun str(n: Int): String { val a = ByteArray(minOf(n, bb.remaining())); bb.get(a); val end = a.indexOf(0.toByte()).let { if (it < 0) a.size else it }; return String(a, 0, end, Charsets.UTF_8) }
    fun cstr(): String { val sb = StringBuilder(); val bytes = ArrayList<Byte>(); while (bb.hasRemaining()) { val c = bb.get(); if (c == 0.toByte()) break; bytes.add(c) }; sb.append(String(bytes.toByteArray(), Charsets.UTF_8)); return sb.toString() }
}

data class HelloInfo(val protoVersion: Int, val authenticated: Boolean, val firmware: String, val hardware: String, val itemCount: Int, val findCount: Int, val statusLength: Int, val name: String) {
    companion object {
        fun decode(p: ByteArray): HelloInfo? {
            if (p.size < 30) return null
            val r = ByteReader(p)
            return HelloInfo(r.u8(), r.u8() == 1, r.str(8), r.str(4), r.u8(), r.u16(), r.u8(), r.str(12))
        }
    }
}

data class ItemInfo(val id: Int, val name: String, val isAction: Boolean, val lo: Int, val hi: Int, val labels: List<String>) {
    val isLabelled get() = labels.isNotEmpty()
    fun label(v: Int): String { val k = v - lo; return if (k in labels.indices) labels[k] else v.toString() }
    companion object {
        fun decode(p: ByteArray): ItemInfo? {
            if (p.size < 7) return null
            val r = ByteReader(p)
            val id = r.u8(); val act = r.u8() == 1; val lo = r.i16(); val hi = r.i16(); val n = r.u8(); val name = r.cstr()
            val labels = ArrayList<String>(); repeat(n) { if (r.remaining > 0) labels.add(r.cstr()) }
            return ItemInfo(id, name, act, lo, hi, labels)
        }
    }
}

data class StatusPacket(
    val flags: Int, val condId: Double, val feIndex: Int, val confidence: Int, val strengthDb: Int, val depthCm: Int, val signal: Double,
    val groundPhase: Double, val groundStrength: Int, val salt: Int, val hotRock: Int, val noiseM: Int, val noiseUv: Int, val itxMa: Int, val vtx: Double,
    val battPct: Int, val battMv: Int, val battHours: Double, val spectrum: List<Double>, val gnssValid: Boolean, val lat: Double, val lon: Double,
    val sats: Int, val hdop: Double, val finds: Int, val piDepthCm: Int, val matchIndex: Int, val program: Int, val mode: Int, val sensitivity: Int,
    val uptime: Long, val pinpointLevel: Double, val swing: Double, val piTarget: Boolean, val wizardStep: Int, val screen: Int
) {
    val present get() = flags and Link.Flag.PRESENT != 0
    val accepted get() = flags and Link.Flag.ACCEPTED != 0
    val ferrous get() = flags and Link.Flag.FERROUS != 0
    val pinpoint get() = flags and Link.Flag.PINPOINT != 0
    val charging get() = flags and Link.Flag.CHARGING != 0
    val lowBatt get() = flags and Link.Flag.LOW_BATT != 0
    val coil get() = flags and Link.Flag.COIL != 0
    val ncActive get() = flags and Link.Flag.NC_ACTIVE != 0
    val inWizard get() = wizardStep != 0xFF
    companion object {
        fun decode(p: ByteArray): StatusPacket? {
            if (p.size < Link.STATUS_LENGTH) return null
            val r = ByteReader(p)
            return StatusPacket(
                flags = r.u8(), condId = r.i16() / 10.0, feIndex = r.u8(), confidence = r.u8(), strengthDb = r.i8(), depthCm = r.u8(), signal = r.u8() / 255.0,
                groundPhase = r.i16() / 10.0, groundStrength = r.u8(), salt = r.u8(), hotRock = r.u8(), noiseM = r.u8(), noiseUv = r.u16(), itxMa = r.u16(), vtx = r.u8() / 10.0,
                battPct = r.u8(), battMv = r.u16(), battHours = r.u8() / 10.0, spectrum = List(5) { r.u8() / 255.0 }, gnssValid = r.u8() == 1, lat = r.i32() / 1e7, lon = r.i32() / 1e7,
                sats = r.u8(), hdop = r.u8() / 10.0, finds = r.u16(), piDepthCm = r.u8(), matchIndex = r.i8(), program = r.u8(), mode = r.u8(), sensitivity = r.u8(),
                uptime = r.u32(), pinpointLevel = r.u8() / 255.0, swing = r.u8() / 10.0, piTarget = r.u8() == 1, wizardStep = r.u8(), screen = r.u8()
            )
        }
    }
}

data class FindRecord(val id: Int, val utc: Long, val lat: Double, val lon: Double, val condId: Int, val fe: Int, val depthCm: Int, val confidence: Int, val name: String) {
    val hasPosition get() = lat != 0.0 || lon != 0.0
    companion object {
        fun decode(p: ByteArray): FindRecord? {
            if (p.size < 31) return null
            val r = ByteReader(p)
            return FindRecord(r.u16(), r.u32(), r.i32() / 1e7, r.i32() / 1e7, r.i16(), r.u8(), r.u8(), r.u8(), r.str(12))
        }
    }
}

data class LearnedEntry(val id: Int, val name: String, val condId: Int, val fe: Int) {
    companion object {
        fun decodeList(p: ByteArray): List<LearnedEntry> {
            if (p.isEmpty()) return emptyList()
            val r = ByteReader(p); val n = r.u8(); val out = ArrayList<LearnedEntry>()
            repeat(n) { if (r.remaining >= 14) out.add(LearnedEntry(r.u8(), r.str(11), r.u8(), r.u8())) }
            return out
        }
    }
}

data class DiagPacket(
    val vsys: Float, val v3v3: Float, val v5va: Float, val vm5va: Float, val v12: Float, val vtx: Float, val vbridge: Float,
    val itxA: Float, val tempC: Float, val noiseFloor: Float, val threshold: Float, val groundA: Float, val groundB: Float, val groundPhase: Float,
    val coilLmH: Float, val coilR: Float, val lineHz: List<Float>, val periods: Long, val piRecords: Long, val uptime: Long,
    val txFault: Boolean, val clipped: Boolean, val coilPresent: Boolean, val calibrated: Boolean, val coilName: String
) {
    companion object {
        fun decode(p: ByteArray): DiagPacket? {
            if (p.size < 109) return null
            val r = ByteReader(p)
            val f = FloatArray(16) { r.f32() }
            val lines = List(5) { r.f32() }
            val periods = r.u32(); val pi = r.u32(); val up = r.u32(); val fl = r.u8(); val name = r.str(12)
            return DiagPacket(f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11], f[12], f[13], f[14], f[15], lines, periods, pi, up,
                fl and 1 != 0, fl and 2 != 0, fl and 4 != 0, fl and 8 != 0, name)
        }
    }
}

data class LinkEvent(val kind: Int, val a: Int, val b: Int, val time: Long = System.currentTimeMillis()) {
    val text: String get() = when (kind) {
        Link.Event.NC_DONE -> "Noise cancel done: channel $a, $b µΩ"
        Link.Event.FIND -> "Find #$b logged"
        Link.Event.LOW_BATT -> "Low battery: $a %"
        Link.Event.WIZARD -> if (a == 0xFF) "Coil wizard closed" else "Coil wizard step $a"
        Link.Event.COIL -> "Coil changed"
        Link.Event.SETTINGS -> "Settings changed on the detector"
        else -> "Event $kind"
    }
}
