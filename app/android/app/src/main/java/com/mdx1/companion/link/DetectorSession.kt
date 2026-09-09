// The app's model of one detector: connection, PIN handshake, item table, live status, actions.
package com.mdx1.companion.link

import android.app.Application
import android.content.Context
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeout

sealed class SessionState {
    object Disconnected : SessionState(); object Scanning : SessionState(); object Connecting : SessionState()
    object Authenticating : SessionState(); object Ready : SessionState(); data class Failed(val message: String) : SessionState()
    val text: String get() = when (this) {
        Disconnected -> "Not connected"; Scanning -> "Scanning…"; Connecting -> "Connecting…"
        Authenticating -> "Checking PIN…"; Ready -> "Connected"; is Failed -> message
    }
}

class LinkException(message: String) : Exception(message)

class DetectorSession(app: Application) : AndroidViewModel(app) {
    private val prefs = app.getSharedPreferences("mdx1", Context.MODE_PRIVATE)

    private val _state = MutableStateFlow<SessionState>(SessionState.Disconnected); val state: StateFlow<SessionState> = _state.asStateFlow()
    private val _info = MutableStateFlow<HelloInfo?>(null); val info = _info.asStateFlow()
    private val _items = MutableStateFlow<List<ItemInfo>>(emptyList()); val items = _items.asStateFlow()
    private val _values = MutableStateFlow<List<Int>>(emptyList()); val values = _values.asStateFlow()
    private val _status = MutableStateFlow<StatusPacket?>(null); val status = _status.asStateFlow()
    private val _finds = MutableStateFlow<List<FindRecord>>(emptyList()); val finds = _finds.asStateFlow()
    private val _learned = MutableStateFlow<List<LearnedEntry>>(emptyList()); val learned = _learned.asStateFlow()
    private val _diag = MutableStateFlow<DiagPacket?>(null); val diag = _diag.asStateFlow()
    private val _events = MutableStateFlow<List<LinkEvent>>(emptyList()); val events = _events.asStateFlow()
    private val _discovered = MutableStateFlow<List<DiscoveredDetector>>(emptyList()); val discovered = _discovered.asStateFlow()
    private val _lastError = MutableStateFlow<String?>(null); val lastError = _lastError.asStateFlow()

    var pin: Int
        get() = prefs.getInt("pin", 1234)
        set(v) { prefs.edit().putInt("pin", v).apply() }
    var demoMode: Boolean
        get() = prefs.getBoolean("demo", false)
        set(v) { prefs.edit().putBoolean("demo", v).apply(); _demo.value = v }
    var autoConnect: Boolean
        get() = prefs.getBoolean("auto", true)
        set(v) { prefs.edit().putBoolean("auto", v).apply(); _auto.value = v }
    private val _demo = MutableStateFlow(demoMode); val demoFlow = _demo.asStateFlow()
    private val _auto = MutableStateFlow(autoConnect); val autoFlow = _auto.asStateFlow()

    private var transport: LinkTransport? = null
    private var parser = LinkFrameParser()
    private var seq = 0
    private val pending = HashMap<Int, Pair<Int, CompletableDeferred<ByteArray>>>()
    val isReady get() = _state.value == SessionState.Ready

    fun clearError() { _lastError.value = null }

    // ---- connection
    fun startScan() {
        disconnect()
        val t: LinkTransport = if (demoMode) DemoTransport() else BleTransport(getApplication())
        transport = t
        t.onReceive = { receive(it) }
        t.onState = { transportChanged(it) }
        t.onDiscover = { list -> _discovered.value = list; if (autoConnect && _state.value == SessionState.Scanning && list.isNotEmpty()) connect(list.first()) }
        _state.value = SessionState.Scanning
        t.startScan()
    }
    fun connect(d: DiscoveredDetector) { _state.value = SessionState.Connecting; transport?.connect(d.id) }
    fun disconnect() {
        transport?.disconnect(); transport = null
        pending.values.forEach { it.second.completeExceptionally(LinkException("disconnected")) }; pending.clear()
        parser = LinkFrameParser()
        _state.value = SessionState.Disconnected; _info.value = null; _items.value = emptyList(); _values.value = emptyList(); _status.value = null; _diag.value = null; _discovered.value = emptyList()
    }
    private fun transportChanged(s: TransportState) {
        when (s) {
            is TransportState.Connected -> viewModelScope.launch { handshake() }
            is TransportState.Failed -> _state.value = SessionState.Failed(s.message)
            TransportState.Idle -> if (_state.value != SessionState.Disconnected) disconnect()
            TransportState.Scanning -> _state.value = SessionState.Scanning
            is TransportState.Connecting -> _state.value = SessionState.Connecting
        }
    }

    private suspend fun handshake() {
        _state.value = SessionState.Authenticating
        try {
            val h = HelloInfo.decode(request(Link.Msg.HELLO, byteArrayOf(Link.PROTO_VERSION.toByte(), (pin and 0xFF).toByte(), (pin shr 8).toByte()))) ?: throw LinkException("bad HELLO reply")
            _info.value = h
            if (!h.authenticated) { _state.value = SessionState.Failed("Wrong PIN. The detector shows its PIN under Menu → Phone PIN."); return }
            val list = ArrayList<ItemInfo>()
            for (i in 0 until h.itemCount) ItemInfo.decode(request(Link.Msg.ITEM, byteArrayOf(i.toByte())))?.let { list.add(it) }
            _items.value = list
            refreshValues()
            request(Link.Msg.STATUS, Link.le16(100))
            _state.value = SessionState.Ready
            refreshFinds(); refreshLearned()
        } catch (e: Exception) { _state.value = SessionState.Failed(e.message ?: "error") }
    }

    // ---- requests
    private suspend fun request(type: Int, payload: ByteArray = ByteArray(0), timeoutMs: Long = 2000): ByteArray {
        val t = transport ?: throw LinkException("not connected")
        seq = (seq + 1) and 0xFF; if (seq == 0) seq = 1
        val s = seq
        val d = CompletableDeferred<ByteArray>()
        pending[s] = type to d
        t.send(Link.frame(type, s, payload))
        return try { withTimeout(timeoutMs) { d.await() } } catch (e: kotlinx.coroutines.TimeoutCancellationException) { pending.remove(s); throw LinkException("no answer from the detector") }
    }

    private fun receive(data: ByteArray) {
        for (f in parser.feed(data)) {
            if (f.type == Link.Msg.response(Link.Msg.ERROR)) {
                val failed = f.payload.getOrNull(0)?.toInt()?.and(0xFF) ?: 0
                val code = f.payload.getOrNull(1)?.toInt()?.and(0xFF) ?: 0
                val p = pending[f.seq]
                if (p != null && p.first == failed) { pending.remove(f.seq); p.second.completeExceptionally(LinkException(Link.errorText(code))) }
                else if (code != 3) _lastError.value = Link.errorText(code)
                continue
            }
            val p = if (f.seq != 0) pending[f.seq] else null
            if (p != null && f.type == Link.Msg.response(p.first)) { pending.remove(f.seq); p.second.complete(f.payload); continue }
            when (f.type) {
                Link.Msg.response(Link.Msg.STATUS) -> StatusPacket.decode(f.payload)?.let { _status.value = it }
                Link.Msg.response(Link.Msg.EVENT) -> if (f.payload.size >= 4) {
                    val ev = LinkEvent(f.payload[0].toInt() and 0xFF, f.payload[1].toInt() and 0xFF, (f.payload[2].toInt() and 0xFF) or ((f.payload[3].toInt() and 0xFF) shl 8))
                    _events.value = (listOf(ev) + _events.value).take(20)
                    when (ev.kind) {
                        Link.Event.FIND -> viewModelScope.launch { refreshFinds() }
                        Link.Event.NC_DONE, Link.Event.SETTINGS -> viewModelScope.launch { runCatching { refreshValues() } }
                    }
                }
            }
        }
    }

    suspend fun refreshValues() {
        val p = request(Link.Msg.VALUES)
        val n = p.getOrNull(0)?.toInt()?.and(0xFF) ?: throw LinkException("bad VALUES reply")
        if (p.size < 1 + 2 * n) throw LinkException("bad VALUES reply")
        _values.value = List(n) { ((p[1 + 2 * it].toInt() and 0xFF) or ((p[2 + 2 * it].toInt() and 0xFF) shl 8)).toShort().toInt() }
    }

    // ---- public operations
    fun valueOf(item: ItemInfo): Int = _values.value.getOrNull(item.id) ?: item.lo
    fun item(name: String): ItemInfo? = _items.value.firstOrNull { it.name == name }

    fun set(item: ItemInfo, v: Int) {
        if (item.isAction) { viewModelScope.launch { run(item) }; return }
        val c = v.coerceIn(item.lo, item.hi)
        _values.value = _values.value.toMutableList().also { if (item.id < it.size) it[item.id] = c }
        viewModelScope.launch {
            try {
                val r = request(Link.Msg.SET, byteArrayOf(item.id.toByte(), (c and 0xFF).toByte(), ((c shr 8) and 0xFF).toByte()))
                if (r.size >= 3) { val applied = ((r[1].toInt() and 0xFF) or ((r[2].toInt() and 0xFF) shl 8)).toShort().toInt(); _values.value = _values.value.toMutableList().also { if (item.id < it.size) it[item.id] = applied } }
            } catch (e: Exception) { _lastError.value = "${item.name}: ${e.message}"; runCatching { refreshValues() } }
        }
    }

    suspend fun run(item: ItemInfo) {
        when (item.name) {
            "Noise cancel" -> action(Link.Action.NOISE_CANCEL)
            "Coil wizard" -> action(Link.Action.WIZARD_START)
            "Save settings" -> action(Link.Action.SAVE)
            else -> runCatching { request(Link.Msg.SET, byteArrayOf(item.id.toByte(), 0, 0)) }
        }
    }

    suspend fun action(a: Int, arg: ByteArray = ByteArray(0)): Boolean {
        return try {
            val r = request(Link.Msg.ACTION, byteArrayOf(a.toByte()) + arg)
            if (r.size >= 2 && r[1].toInt() != 0) { _lastError.value = Link.errorText(r[1].toInt() and 0xFF); return false }
            when (a) { Link.Action.LEARN, Link.Action.FORGET -> refreshLearned(); Link.Action.DEFAULTS, Link.Action.SAVE -> runCatching { refreshValues() } }
            true
        } catch (e: Exception) { _lastError.value = e.message; false }
    }
    fun actionAsync(a: Int, arg: ByteArray = ByteArray(0)) { viewModelScope.launch { action(a, arg) } }
    fun learn(name: String) = actionAsync(Link.Action.LEARN, name.toByteArray().take(11).toByteArray())
    fun forget(e: LearnedEntry) = actionAsync(Link.Action.FORGET, byteArrayOf(e.id.toByte()))
    fun setPinpoint(on: Boolean) = actionAsync(if (on) Link.Action.PINPOINT_ON else Link.Action.PINPOINT_OFF)

    suspend fun refreshFinds() {
        val list = ArrayList<FindRecord>()
        var i = 0
        while (i < 32) { val f = runCatching { FindRecord.decode(request(Link.Msg.FIND, Link.le16(i))) }.getOrNull() ?: break; list.add(f); i++ }
        _finds.value = list
    }
    fun refreshFindsAsync() { viewModelScope.launch { refreshFinds() } }
    suspend fun refreshLearned() { runCatching { _learned.value = LearnedEntry.decodeList(request(Link.Msg.LEARNED)) } }
    fun refreshLearnedAsync() { viewModelScope.launch { refreshLearned() } }
    fun refreshDiag() { viewModelScope.launch { runCatching { _diag.value = DiagPacket.decode(request(Link.Msg.DIAG)) } } }
}
