// Byte transports: BLE (Microchip Transparent UART on the detector's BM83) and an in-app demo detector.
package com.mdx1.companion.link

import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import java.util.UUID
import java.util.ArrayDeque
import kotlin.math.abs
import kotlin.math.min
import kotlin.math.sin

sealed class TransportState {
    object Idle : TransportState()
    object Scanning : TransportState()
    data class Connecting(val name: String) : TransportState()
    data class Connected(val name: String) : TransportState()
    data class Failed(val message: String) : TransportState()
    val text: String get() = when (this) {
        Idle -> "Not connected"; Scanning -> "Scanning for MDX-1…"
        is Connecting -> "Connecting to $name…"; is Connected -> "Connected to $name"; is Failed -> message
    }
}

data class DiscoveredDetector(val id: String, val name: String, val rssi: Int)

interface LinkTransport {
    var onReceive: ((ByteArray) -> Unit)?
    var onState: ((TransportState) -> Unit)?
    var onDiscover: ((List<DiscoveredDetector>) -> Unit)?
    val state: TransportState
    fun startScan()
    fun connect(id: String)
    fun disconnect()
    fun send(data: ByteArray)
}

// ---------------------------------------------------------------- BLE
@SuppressLint("MissingPermission")   // permissions are requested by MainActivity before any scan
class BleTransport(private val context: Context) : LinkTransport {
    companion object {
        val SERVICE: UUID = UUID.fromString("49535343-FE7D-4AE5-8FA9-9FAFD205E455")
        val RX_CHAR: UUID = UUID.fromString("49535343-8841-43F4-A8D4-ECBE34729BB3")   // phone -> module
        val TX_CHAR: UUID = UUID.fromString("49535343-1E4D-4BD9-BA61-23C647249616")   // module -> phone
        val CCCD: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }
    override var onReceive: ((ByteArray) -> Unit)? = null
    override var onState: ((TransportState) -> Unit)? = null
    override var onDiscover: ((List<DiscoveredDetector>) -> Unit)? = null
    override var state: TransportState = TransportState.Idle
        private set(v) { field = v; main.post { onState?.invoke(v) } }

    private val main = Handler(Looper.getMainLooper())
    private val manager = context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    private val adapter get() = manager.adapter
    private val found = LinkedHashMap<String, Pair<BluetoothDevice, Int>>()
    private var gatt: BluetoothGatt? = null
    private var writeChar: BluetoothGattCharacteristic? = null
    private val queue = ArrayDeque<ByteArray>()
    private var busy = false
    private var mtu = 20

    private val scanCb = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val name = result.scanRecord?.deviceName ?: result.device.name ?: return
            val hasService = result.scanRecord?.serviceUuids?.contains(ParcelUuid(SERVICE)) == true
            if (!hasService && !name.uppercase().startsWith("MDX")) return
            found[result.device.address] = result.device to result.rssi
            val list = found.map { DiscoveredDetector(it.key, it.value.first.name ?: name, it.value.second) }.sortedByDescending { it.rssi }
            main.post { onDiscover?.invoke(list) }
        }
        override fun onScanFailed(errorCode: Int) { state = TransportState.Failed("Scan failed ($errorCode)") }
    }

    override fun startScan() {
        val a = adapter ?: run { state = TransportState.Failed("No Bluetooth"); return }
        if (!a.isEnabled) { state = TransportState.Failed("Bluetooth is off"); return }
        found.clear(); state = TransportState.Scanning
        val settings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        a.bluetoothLeScanner.startScan(listOf(ScanFilter.Builder().setServiceUuid(ParcelUuid(SERVICE)).build()), settings, scanCb)
        // modules that do not advertise the service UUID: widen after 3 s
        main.postDelayed({ if (state == TransportState.Scanning && found.isEmpty()) { a.bluetoothLeScanner.stopScan(scanCb); a.bluetoothLeScanner.startScan(null, settings, scanCb) } }, 3000)
    }

    override fun connect(id: String) {
        val dev = found[id]?.first ?: adapter?.getRemoteDevice(id) ?: return
        adapter?.bluetoothLeScanner?.stopScan(scanCb)
        state = TransportState.Connecting(dev.name ?: "MDX-1")
        gatt = if (Build.VERSION.SDK_INT >= 23) dev.connectGatt(context, false, gattCb, BluetoothDevice.TRANSPORT_LE) else dev.connectGatt(context, false, gattCb)
    }

    override fun disconnect() {
        adapter?.bluetoothLeScanner?.stopScan(scanCb)
        gatt?.close(); gatt = null; writeChar = null; queue.clear(); busy = false
        state = TransportState.Idle
    }

    override fun send(data: ByteArray) {
        var off = 0
        while (off < data.size) { val end = min(off + mtu, data.size); queue.add(data.copyOfRange(off, end)); off = end }
        pump()
    }
    private fun pump() {
        val g = gatt ?: return; val c = writeChar ?: return
        if (busy || queue.isEmpty()) return
        val chunk = queue.poll() ?: return
        busy = true
        val type = if (c.properties and BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE != 0) BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE else BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        if (Build.VERSION.SDK_INT >= 33) g.writeCharacteristic(c, chunk, type)
        else { c.writeType = type; @Suppress("DEPRECATION") c.value = chunk; @Suppress("DEPRECATION") g.writeCharacteristic(c) }
    }

    private val gattCb = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) { g.requestMtu(185); g.discoverServices() }
            else if (newState == BluetoothProfile.STATE_DISCONNECTED) { writeChar = null; queue.clear(); busy = false; state = if (status == 0) TransportState.Idle else TransportState.Failed("Disconnected ($status)") }
        }
        override fun onMtuChanged(g: BluetoothGatt, m: Int, status: Int) { if (status == 0) mtu = m - 3 }
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            val s = g.getService(SERVICE) ?: run { state = TransportState.Failed("Transparent UART service not found"); return }
            writeChar = s.getCharacteristic(RX_CHAR)
            val tx = s.getCharacteristic(TX_CHAR)
            if (tx != null) {
                g.setCharacteristicNotification(tx, true)
                val d = tx.getDescriptor(CCCD)
                if (d != null) { if (Build.VERSION.SDK_INT >= 33) g.writeDescriptor(d, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE) else { @Suppress("DEPRECATION") d.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE; @Suppress("DEPRECATION") g.writeDescriptor(d) } }
            }
            state = if (writeChar != null) TransportState.Connected(g.device.name ?: "MDX-1") else TransportState.Failed("Write characteristic not found")
        }
        override fun onCharacteristicWrite(g: BluetoothGatt, c: BluetoothGattCharacteristic, status: Int) { busy = false; main.post { pump() } }
        override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic, value: ByteArray) { if (c.uuid == TX_CHAR) main.post { onReceive?.invoke(value) } }
        @Deprecated("pre-33") override fun onCharacteristicChanged(g: BluetoothGatt, c: BluetoothGattCharacteristic) {
            @Suppress("DEPRECATION") val v = c.value ?: return
            if (c.uuid == TX_CHAR) main.post { onReceive?.invoke(v) }
        }
    }
}

// ---------------------------------------------------------------- Demo detector (no hardware)
class DemoTransport : LinkTransport {
    override var onReceive: ((ByteArray) -> Unit)? = null
    override var onState: ((TransportState) -> Unit)? = null
    override var onDiscover: ((List<DiscoveredDetector>) -> Unit)? = null
    override var state: TransportState = TransportState.Idle
        private set(v) { field = v; onState?.invoke(v) }

    private val main = Handler(Looper.getMainLooper())
    private val parser = LinkFrameParser()
    private var authed = false
    private var streamMs = 0L
    private val t0 = System.currentTimeMillis()
    private val finds = ArrayList<FindRecord>()
    private val learned = arrayListOf(LearnedEntry(0, "Silver", 88, 3), LearnedEntry(1, "Pull tab", 42, 8))
    private var pinpoint = false
    private var ncUntil = 0L
    private var wizardStep = 0xFF
    private val values = DemoDetector.defaults.toMutableList()
    private var running = false

    override fun startScan() { state = TransportState.Scanning; main.postDelayed({ onDiscover?.invoke(listOf(DiscoveredDetector("demo", "MDX-1 demo", -48))) }, 600) }
    override fun connect(id: String) {
        state = TransportState.Connecting("MDX-1 demo")
        main.postDelayed({ state = TransportState.Connected("MDX-1 demo"); running = true; main.post(ticker) }, 500)
    }
    override fun disconnect() { running = false; authed = false; streamMs = 0; state = TransportState.Idle }
    override fun send(data: ByteArray) { parser.feed(data).forEach { handle(it) } }

    private val ticker = object : Runnable {
        private var last = 0L
        override fun run() {
            if (!running) return
            val now = System.currentTimeMillis()
            if (authed && streamMs > 0 && now - last >= streamMs) { last = now; emit(Link.Msg.response(Link.Msg.STATUS), 0, status()) }
            if (ncUntil != 0L && now > ncUntil) { ncUntil = 0; emit(Link.Msg.response(Link.Msg.EVENT), 0, byteArrayOf(Link.Event.NC_DONE.toByte(), 101, 12, 0)) }
            main.postDelayed(this, 50)
        }
    }
    private fun emit(type: Int, seq: Int, payload: ByteArray) { val f = Link.frame(type, seq, payload); main.post { onReceive?.invoke(f) } }
    private fun reply(type: Int, seq: Int, payload: ByteArray) = emit(Link.Msg.response(type), seq, payload)
    private fun err(type: Int, seq: Int, code: Int) = reply(Link.Msg.ERROR, seq, byteArrayOf(type.toByte(), code.toByte()))

    private fun handle(f: LinkFrame) {
        val p = f.payload
        if (f.type != Link.Msg.HELLO && !authed) { err(f.type, f.seq, 2); return }
        when (f.type) {
            Link.Msg.HELLO -> {
                val pin = if (p.size >= 3) (p[1].toInt() and 0xFF) or ((p[2].toInt() and 0xFF) shl 8) else -1
                authed = pin == values[DemoDetector.PIN_INDEX]
                val r = byteArrayOf(Link.PROTO_VERSION.toByte(), if (authed) 1 else 0) + Link.pad("0.5.0", 8) + Link.pad("A5", 4) +
                    byteArrayOf(DemoDetector.items.size.toByte()) + Link.le16(finds.size) + byteArrayOf(Link.STATUS_LENGTH.toByte()) + Link.pad("MDX-1", 12)
                reply(f.type, f.seq, r); if (!authed) err(f.type, f.seq, 3)
            }
            Link.Msg.ITEM -> {
                val i = if (p.isNotEmpty()) p[0].toInt() and 0xFF else 255
                val it = DemoDetector.items.getOrNull(i) ?: run { err(f.type, f.seq, 4); return }
                var r = byteArrayOf(i.toByte(), if (it.isAction) 1 else 0) + Link.le16(it.lo) + Link.le16(it.hi) + byteArrayOf(it.labels.size.toByte()) + it.name.toByteArray() + byteArrayOf(0)
                for (l in it.labels) r += l.toByteArray() + byteArrayOf(0)
                reply(f.type, f.seq, r)
            }
            Link.Msg.VALUES -> { var r = byteArrayOf(values.size.toByte()); for (v in values) r += Link.le16(v); reply(f.type, f.seq, r) }
            Link.Msg.SET -> {
                if (p.size < 3) { err(f.type, f.seq, 6); return }
                val idx = p[0].toInt() and 0xFF; val it = DemoDetector.items.getOrNull(idx) ?: run { err(f.type, f.seq, 4); return }
                val v = ((p[1].toInt() and 0xFF) or ((p[2].toInt() and 0xFF) shl 8)).toShort().toInt()
                if (it.isAction) { reply(f.type, f.seq, byteArrayOf(idx.toByte(), 0, 0)); return }
                if (v < it.lo || v > it.hi) { err(f.type, f.seq, 5); return }
                values[idx] = v; reply(f.type, f.seq, byteArrayOf(idx.toByte()) + Link.le16(v))
            }
            Link.Msg.ACTION -> {
                val a = if (p.isNotEmpty()) p[0].toInt() and 0xFF else 0; var res = 0
                val tg = DemoDetector.target((System.currentTimeMillis() - t0) / 1000.0)
                when (a) {
                    Link.Action.NOISE_CANCEL -> ncUntil = System.currentTimeMillis() + 2500
                    Link.Action.PINPOINT_ON -> pinpoint = true
                    Link.Action.PINPOINT_OFF -> pinpoint = false
                    Link.Action.LEARN -> if (tg.present) { val name = String(p.copyOfRange(1, minOf(p.size, 12))).ifEmpty { "P${tg.id.toInt()}" }; learned.add(LearnedEntry((learned.maxOfOrNull { it.id } ?: -1) + 1, name, tg.id.toInt(), tg.fe)) } else res = 8
                    Link.Action.FORGET -> if (p.size >= 2) learned.removeAll { it.id == (p[1].toInt() and 0xFF) }
                    Link.Action.MARK_FIND -> { finds.add(0, FindRecord(0, System.currentTimeMillis() / 1000, 51.5074 + finds.size * 0.0002, -0.1278 + finds.size * 0.0003, tg.id.toInt(), tg.fe, tg.depth, 90, "")); emit(Link.Msg.response(Link.Msg.EVENT), 0, byteArrayOf(Link.Event.FIND.toByte(), 0) + Link.le16(finds.size)) }
                    Link.Action.WIZARD_START -> wizardStep = 0
                    Link.Action.WIZARD_STEP -> if (wizardStep != 0xFF) { wizardStep = min(wizardStep + 1, 7); if (wizardStep == 7) wizardStep = 0xFF }
                    Link.Action.WIZARD_CANCEL -> wizardStep = 0xFF
                    Link.Action.DEFAULTS -> { values.clear(); values.addAll(DemoDetector.defaults) }
                    else -> {}
                }
                reply(f.type, f.seq, byteArrayOf(a.toByte(), res.toByte()))
            }
            Link.Msg.STATUS -> {
                if (p.size >= 2) { val per = (p[0].toInt() and 0xFF) or ((p[1].toInt() and 0xFF) shl 8); if (per != 0xFFFF) streamMs = if (per == 0) 0 else maxOf(per, 50).toLong() }
                reply(f.type, f.seq, status())
            }
            Link.Msg.FIND -> {
                val idx = if (p.size >= 2) (p[0].toInt() and 0xFF) or ((p[1].toInt() and 0xFF) shl 8) else 0
                val r = finds.getOrNull(idx) ?: run { err(f.type, f.seq, 4); return }
                reply(f.type, f.seq, Link.le16(idx) + Link.le32(r.utc) + Link.le32((r.lat * 1e7).toLong()) + Link.le32((r.lon * 1e7).toLong()) + Link.le16(r.condId) + byteArrayOf(r.fe.toByte(), r.depthCm.toByte(), r.confidence.toByte()) + Link.pad(r.name, 12))
            }
            Link.Msg.LEARNED -> { var r = byteArrayOf(learned.size.toByte()); for (e in learned) r += byteArrayOf(e.id.toByte()) + Link.pad(e.name, 11) + byteArrayOf(e.condId.toByte(), e.fe.toByte()); reply(f.type, f.seq, r) }
            Link.Msg.DIAG -> {
                val floats = floatArrayOf(3.92f, 3.30f, 5.01f, -5.00f, 12.0f, 14.9f, 14.4f, 0.42f, 24.5f, 11.5e-6f, 64e-6f, 0.0103f, 0.0009f, 62.4f, 0.362f, 0.63f, 5020f, 10040f, 15060f, 20080f, 41164f)
                var r = ByteArray(0); for (x in floats) r += Link.le32(java.lang.Float.floatToIntBits(x).toLong() and 0xFFFFFFFFL)
                val up = (System.currentTimeMillis() - t0) / 1000
                r += Link.le32(up * 5120) + Link.le32(up * 3) + Link.le32(up) + byteArrayOf(12) + Link.pad("MDX DD11", 12)
                reply(f.type, f.seq, r)
            }
        }
    }

    private fun status(): ByteArray {
        val t = (System.currentTimeMillis() - t0) / 1000.0
        val tg = DemoDetector.target(t)
        val feLimit = values[DemoDetector.FE_LIMIT_INDEX]
        var flags = Link.Flag.COIL
        if (tg.present) { flags = flags or Link.Flag.PRESENT; flags = flags or (if (tg.fe <= feLimit) Link.Flag.ACCEPTED else Link.Flag.FERROUS) }
        if (pinpoint) flags = flags or Link.Flag.PINPOINT
        if (ncUntil != 0L) flags = flags or Link.Flag.NC_ACTIVE
        val batt = maxOf(20, 78 - (t / 120).toInt())
        val signal = if (pinpoint) (tg.level * 255).toInt() else ((if (tg.present) tg.db else 0) / 40.0 * 255).toInt().coerceIn(0, 255)
        var b = byteArrayOf(flags.toByte()) + Link.le16((tg.id * 10).toInt()) + byteArrayOf(tg.fe.toByte(), (if (tg.present) 92 else 0).toByte(), (if (tg.present) tg.db else -12).toByte(), tg.depth.toByte(), signal.toByte())
        b += Link.le16(624) + byteArrayOf(10, 8, 0, 101) + Link.le16(12) + Link.le16(420) + byteArrayOf(149.toByte(), batt.toByte()) + Link.le16(3300 + batt * 9) + byteArrayOf(min(255, batt * 15 / 100 * 10).toByte())
        b += tg.spectrum.map { (it * 255).toInt().toByte() }.toByteArray()
        b += byteArrayOf(1) + Link.le32(515074000L) + Link.le32(-1278000L) + byteArrayOf(9, 11) + Link.le16(finds.size)
        b += byteArrayOf((if (tg.present) tg.depth + 3 else 0).toByte(), (if (tg.present && tg.id > 80) 0 else -1).toByte(), values[0].toByte(), values[1].toByte(), values[2].toByte())
        b += Link.le32(t.toLong()) + byteArrayOf((tg.level * 255).toInt().toByte(), (tg.swing * 10).toInt().toByte(), 0, (wizardStep and 0xFF).toByte(), (if (pinpoint) 2 else 0).toByte())
        return b
    }
}

/** The demo's idea of the detector: the firmware menu table and a sweep passing over three targets. */
object DemoDetector {
    private val ONOFF = listOf("Off", "On")
    val items = listOf(
        ItemInfo(0, "Program", false, 0, 4, listOf("Park", "Field", "Beach", "Gold", "Relic")),
        ItemInfo(1, "Mode", false, 0, 6, listOf("Multi 5-41k", "5.1 kHz", "10.2 kHz", "20.5 kHz", "41 kHz", "PI", "Hybrid V+PI")),
        ItemInfo(2, "Sensitivity", false, 1, 30, emptyList()),
        ItemInfo(3, "Auto sensitivity", false, 0, 1, ONOFF),
        ItemInfo(4, "Recovery speed", false, 1, 8, emptyList()),
        ItemInfo(5, "TX power", false, 1, 10, emptyList()),
        ItemInfo(6, "Ferrous limit", false, 20, 95, emptyList()),
        ItemInfo(7, "Ground balance", false, 0, 2, listOf("Auto track", "Manual", "Off")),
        ItemInfo(8, "Salt mode", false, 0, 1, ONOFF),
        ItemInfo(9, "Hybrid PI rate", false, 0, 32, emptyList()),
        ItemInfo(10, "Tones", false, 0, 3, listOf("VCO", "2 tone", "5 tone", "Pitch")),
        ItemInfo(11, "Volume", false, 0, 10, emptyList()),
        ItemInfo(12, "Threshold hum", false, 0, 10, emptyList()),
        ItemInfo(13, "Iron volume", false, 0, 10, emptyList()),
        ItemInfo(14, "Iron grunt", false, 0, 1, ONOFF),
        ItemInfo(15, "Noise cancel at start", false, 0, 1, ONOFF),
        ItemInfo(16, "Noise cancel", true, 0, 0, emptyList()),
        ItemInfo(17, "Coil wizard", true, 0, 0, emptyList()),
        ItemInfo(18, "Learn target", true, 0, 0, emptyList()),
        ItemInfo(19, "Find log", true, 0, 0, emptyList()),
        ItemInfo(20, "Diagnostics", true, 0, 0, emptyList()),
        ItemInfo(21, "Front light", false, 0, 100, emptyList()),
        ItemInfo(22, "GPS logging", false, 0, 1, ONOFF),
        ItemInfo(23, "Depth units", false, 0, 1, listOf("cm", "inch")),
        ItemInfo(24, "Phone PIN", false, 0, 9999, emptyList()),
        ItemInfo(25, "Save settings", true, 0, 0, emptyList()),
    )
    val defaults = listOf(1, 0, 20, 0, 4, 4, 55, 0, 0, 0, 2, 7, 2, 3, 1, 1, 0, 0, 0, 0, 0, 30, 1, 0, 1234, 0)
    const val PIN_INDEX = 24; const val FE_LIMIT_INDEX = 6

    class Target(val present: Boolean, val id: Double, val fe: Int, val depth: Int, val db: Int, val level: Double, val spectrum: List<Double>, val swing: Double)
    private data class Spec(val start: Double, val id: Double, val fe: Int, val depth: Int, val spec: List<Double>)
    private val specs = listOf(
        Spec(1.0, 88.0, 3, 14, listOf(0.35, 0.55, 0.75, 0.9, 1.0)),
        Spec(5.0, 12.0, 78, 9, listOf(1.0, 0.9, 0.75, 0.6, 0.35)),
        Spec(9.0, 42.0, 6, 7, listOf(0.6, 0.9, 1.0, 0.85, 0.5)),
    )
    fun target(t: Double): Target {
        val phase = t % 12.0; val sweep = abs(sin(t * 1.6))
        for (s in specs) if (phase >= s.start && phase < s.start + 1.2) {
            val env = sin((phase - s.start) / 1.2 * Math.PI)
            return Target(env > 0.15, s.id + (env - 0.5) * 1.5, s.fe, s.depth, (env * 28).toInt(), env, s.spec.map { it * (0.4 + 0.6 * env) }, 0.8 + sweep)
        }
        return Target(false, 0.0, 0, 0, -12, 0.0, listOf(0.05, 0.05, 0.06, 0.05, 0.04), 0.8 + sweep)
    }
}
