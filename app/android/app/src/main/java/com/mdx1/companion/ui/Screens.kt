// All screens of the companion app: Detect, Settings (from the detector's item table), Finds, Diagnostics, Detector.
package com.mdx1.companion.ui

import android.content.Intent
import android.net.Uri
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.mdx1.companion.link.*
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

private val Copper = Color(0xFFB86B33)
private val Accept = Color(0xFF2A855C)
private val Ferrous = Color(0xFF9E3329)
private val Ground = Color(0xFF6B5C47)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun MainScreen(session: DetectorSession, onNeedPermissions: () -> Unit) {
    var tab by rememberSaveable { mutableIntStateOf(4) }
    val state by session.state.collectAsStateWithLifecycle()
    val events by session.events.collectAsStateWithLifecycle()
    val lastError by session.lastError.collectAsStateWithLifecycle()
    val snackbar = remember { SnackbarHostState() }
    LaunchedEffect(events.firstOrNull()?.time) { events.firstOrNull()?.let { snackbar.showSnackbar(it.text) } }
    LaunchedEffect(lastError) { lastError?.let { snackbar.showSnackbar(it); session.clearError() } }
    LaunchedEffect(state) { if (state == SessionState.Ready && tab == 4) tab = 0 }

    Scaffold(
        snackbarHost = { SnackbarHost(snackbar) },
        bottomBar = {
            NavigationBar {
                val tabs = listOf("Detect" to Icons.Default.Sensors, "Settings" to Icons.Default.Tune, "Finds" to Icons.Default.Map, "Diagnostics" to Icons.Default.MonitorHeart, "Detector" to Icons.Default.Bluetooth)
                tabs.forEachIndexed { i, (label, icon) -> NavigationBarItem(selected = tab == i, onClick = { tab = i }, icon = { Icon(icon, label) }, label = { Text(label) }) }
            }
        }
    ) { pad ->
        Box(Modifier.padding(pad).fillMaxSize()) {
            when (tab) {
                0 -> DetectScreen(session)
                1 -> SettingsScreen(session)
                2 -> FindsScreen(session)
                3 -> DiagnosticsScreen(session)
                else -> DetectorScreen(session, onNeedPermissions)
            }
        }
    }
}

// ---------------------------------------------------------------- shared pieces
@Composable
private fun Panel(title: String, modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    Card(modifier.fillMaxWidth(), shape = RoundedCornerShape(12.dp)) {
        Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(title.uppercase(), style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant, letterSpacing = 1.2.sp)
            content()
        }
    }
}

@Composable
private fun Bar(value: Double, tint: Color, height: Int = 12) {
    Box(Modifier.fillMaxWidth().height(height.dp).clip(RoundedCornerShape(50)).background(MaterialTheme.colorScheme.onSurface.copy(alpha = 0.08f))) {
        Box(Modifier.fillMaxHeight().fillMaxWidth(value.coerceIn(0.0, 1.0).toFloat()).clip(RoundedCornerShape(50)).background(tint))
    }
}

@Composable
private fun SpectrumBars(values: List<Double>) {
    val labels = listOf("5", "10", "15", "20", "41")
    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        values.forEachIndexed { i, v ->
            Column(horizontalAlignment = Alignment.CenterHorizontally, modifier = Modifier.weight(1f)) {
                Box(Modifier.height(56.dp).fillMaxWidth().clip(RoundedCornerShape(3.dp)).background(MaterialTheme.colorScheme.onSurface.copy(alpha = 0.06f)), contentAlignment = Alignment.BottomCenter) {
                    Box(Modifier.fillMaxWidth().height((v * 56).coerceAtLeast(2.0).dp).clip(RoundedCornerShape(3.dp)).background(Copper))
                }
                Text(labels.getOrElse(i) { "" }, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
    }
}

@Composable
private fun Stat(label: String, value: String, modifier: Modifier = Modifier) {
    Column(modifier) {
        Text(label, style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(value, style = MaterialTheme.typography.bodyMedium, fontWeight = FontWeight.Medium)
    }
}

// ---------------------------------------------------------------- Detect
@Composable
fun DetectScreen(session: DetectorSession) {
    val s by session.status.collectAsStateWithLifecycle()
    val state by session.state.collectAsStateWithLifecycle()
    val learned by session.learned.collectAsStateWithLifecycle()
    val values by session.values.collectAsStateWithLifecycle()
    val scope = rememberCoroutineScope()
    var showLearn by remember { mutableStateOf(false) }
    var learnName by remember { mutableStateOf("") }
    var pinpointHeld by remember { mutableStateOf(false) }
    val st = s
    if (st == null) {
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) { Column(horizontalAlignment = Alignment.CenterHorizontally) { Icon(Icons.Default.BluetoothDisabled, null, Modifier.size(48.dp)); Text("No detector", style = MaterialTheme.typography.titleMedium); Text(state.text, color = MaterialTheme.colorScheme.onSurfaceVariant) } }
        return
    }
    val tint = if (!st.present) MaterialTheme.colorScheme.onSurfaceVariant else if (st.ferrous) Ferrous else if (st.accepted) Accept else Ground
    val programName = session.item("Program")?.label(st.program) ?: "Program ${st.program}"
    val modeName = session.item("Mode")?.label(st.mode) ?: "Mode ${st.mode}"
    val targetName = when { !st.present -> ""; st.matchIndex >= 0 -> learned.firstOrNull { it.id == st.matchIndex }?.name ?: "target"; st.ferrous -> "IRON"; st.accepted -> "target"; else -> "rejected" }
    val inches = session.item("Depth units")?.let { session.valueOf(it) == 1 } ?: false
    val depthText = if (!st.present) "--" else if (inches) String.format(Locale.US, "%.0f in", st.depthCm / 2.54) else "${st.depthCm} cm"

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(programName, fontWeight = FontWeight.SemiBold); Spacer(Modifier.width(8.dp)); Text(modeName, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Spacer(Modifier.weight(1f))
            Text("N${st.noiseM}", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Spacer(Modifier.width(8.dp)); Icon(if (st.gnssValid) Icons.Default.LocationOn else Icons.Default.LocationOff, null, Modifier.size(16.dp), tint = if (st.gnssValid) Accept else MaterialTheme.colorScheme.onSurfaceVariant)
            Spacer(Modifier.width(8.dp)); Text("${st.battPct}%${if (st.charging) " ⚡" else ""}", style = MaterialTheme.typography.labelMedium)
        }
        Card(Modifier.fillMaxWidth(), shape = RoundedCornerShape(14.dp)) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Row(verticalAlignment = Alignment.Bottom) {
                    Text(if (st.present) String.format(Locale.US, "%.0f", st.condId) else "--", fontSize = 88.sp, fontWeight = FontWeight.Bold, color = tint, lineHeight = 88.sp)
                    Spacer(Modifier.width(12.dp))
                    Column(Modifier.padding(bottom = 10.dp)) {
                        Text(targetName, style = MaterialTheme.typography.titleMedium, color = tint, fontWeight = FontWeight.SemiBold)
                        Text("Fe ${st.feIndex}  ·  fit ${st.confidence}%", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        Text(if (st.present) "${st.strengthDb} dB over threshold" else "searching", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                }
                Bar(if (st.pinpoint) st.pinpointLevel else st.signal, tint, 16)
                Row {
                    Text(if (st.pinpoint) "PINPOINT" else "SIGNAL", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant, letterSpacing = 1.2.sp)
                    Spacer(Modifier.weight(1f))
                    if (st.ncActive) Text("noise cancel running", style = MaterialTheme.typography.labelSmall, color = Copper)
                    if (st.lowBatt) Text("  low battery", style = MaterialTheme.typography.labelSmall, color = Ferrous)
                    if (!st.coil) Text("  no coil", style = MaterialTheme.typography.labelSmall, color = Ferrous)
                    if (st.inWizard) Text("  coil wizard step ${st.wizardStep}", style = MaterialTheme.typography.labelSmall, color = Copper)
                }
            }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Panel("Signature kHz", Modifier.weight(1.2f)) { SpectrumBars(st.spectrum) }
            Panel("Depth", Modifier.weight(1f)) {
                Text(depthText, fontSize = 30.sp, fontWeight = FontWeight.SemiBold)
                if (st.piDepthCm > 0) Text("PI ${st.piDepthCm} cm", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                Text(String.format(Locale.US, "swing %.1f m/s", st.swing), style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
        Panel("Ground and noise") {
            Row { Stat("ground phase", String.format(Locale.US, "%.1f°", st.groundPhase), Modifier.weight(1f)); Stat("mineralisation", "${st.groundStrength} mΩ", Modifier.weight(1f)); Stat("salt", "${st.salt}%", Modifier.weight(1f)) }
            Row { Stat("hot rock", "${st.hotRock}", Modifier.weight(1f)); Stat("noise floor", "${st.noiseUv} µΩ", Modifier.weight(1f)); Stat("transmit", String.format(Locale.US, "%.1f V · %d mA", st.vtx, st.itxMa), Modifier.weight(1f)) }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Box(Modifier.weight(1f).height(40.dp).clip(RoundedCornerShape(8.dp)).background(if (pinpointHeld) Copper else Copper.copy(alpha = 0.18f))
                .pointerInput(Unit) { detectTapGestures(onPress = { pinpointHeld = true; session.setPinpoint(true); tryAwaitRelease(); pinpointHeld = false; session.setPinpoint(false) }) },
                contentAlignment = Alignment.Center) { Text("Pinpoint", color = if (pinpointHeld) Color.White else Copper, fontWeight = FontWeight.Medium) }
            OutlinedButton({ session.actionAsync(Link.Action.GROUND_GRAB) }, Modifier.weight(1f)) { Text("Ground grab", maxLines = 1) }
            OutlinedButton({ session.actionAsync(Link.Action.NOISE_CANCEL) }, Modifier.weight(1f)) { Text("Noise cancel", maxLines = 1) }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            OutlinedButton({ session.actionAsync(Link.Action.MARK_FIND) }, Modifier.weight(1f)) { Text("Mark find", maxLines = 1) }
            OutlinedButton({ learnName = ""; showLearn = true }, Modifier.weight(1f), enabled = st.present) { Text("Learn", maxLines = 1) }
            session.item("Sensitivity")?.let { it ->
                val v = values.getOrNull(it.id) ?: it.lo
                Row(Modifier.weight(1.2f), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.Center) {
                    IconButton({ session.set(it, v - 1) }, Modifier.size(32.dp)) { Icon(Icons.Default.Remove, "less") }
                    Text("S$v", fontWeight = FontWeight.Medium)
                    IconButton({ session.set(it, v + 1) }, Modifier.size(32.dp)) { Icon(Icons.Default.Add, "more") }
                }
            }
        }
    }
    if (showLearn) {
        AlertDialog(onDismissRequest = { showLearn = false }, title = { Text("Learn target") },
            text = { Column { Text("Name for the target under the coil"); OutlinedTextField(learnName, { learnName = it }, singleLine = true, placeholder = { Text("e.g. Silver dime") }) } },
            confirmButton = { TextButton({ session.learn(learnName); showLearn = false }) { Text("Learn") } },
            dismissButton = { TextButton({ showLearn = false }) { Text("Cancel") } })
    }
    LaunchedEffect(Unit) { while (true) { delay(30_000); if (session.isReady) scope.launch { session.refreshLearned() } } }
}

// ---------------------------------------------------------------- Settings
private val GROUPS = listOf(
    "Detection" to listOf("Program", "Mode", "Sensitivity", "Auto sensitivity", "Recovery speed", "TX power", "Ferrous limit"),
    "Ground" to listOf("Ground balance", "Salt mode", "Hybrid PI rate"),
    "Audio" to listOf("Tones", "Volume", "Threshold hum", "Iron volume", "Iron grunt"),
    "Tools" to listOf("Noise cancel at start", "Noise cancel", "Coil wizard", "Learn target"),
    "Device" to listOf("Front light", "GPS logging", "Depth units", "Phone PIN", "Save settings"),
)

@Composable
fun SettingsScreen(session: DetectorSession) {
    val items by session.items.collectAsStateWithLifecycle()
    val values by session.values.collectAsStateWithLifecycle()
    val state by session.state.collectAsStateWithLifecycle()
    var wizardOpen by remember { mutableStateOf(false) }
    var learnedOpen by remember { mutableStateOf(false) }
    if (items.isEmpty()) { Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) { Text(state.text, color = MaterialTheme.colorScheme.onSurfaceVariant) }; return }
    if (wizardOpen) { CoilWizard(session) { wizardOpen = false }; return }
    if (learnedOpen) { LearnedList(session) { learnedOpen = false }; return }
    val grouped = GROUPS.flatMap { it.second }
    LazyColumn(Modifier.fillMaxSize(), contentPadding = PaddingValues(16.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
        item { Text("Settings", style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Bold) }
        for ((title, names) in GROUPS) {
            item { Text(title, style = MaterialTheme.typography.titleSmall, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.padding(top = 12.dp)) }
            items(names.mapNotNull { n -> items.firstOrNull { it.name == n } }) { it -> SettingRow(it, values.getOrNull(it.id) ?: it.lo, session, onWizard = { wizardOpen = true }, onLearned = { learnedOpen = true }) }
        }
        val rest = items.filter { it.name !in grouped && it.name !in listOf("Find log", "Diagnostics") }
        if (rest.isNotEmpty()) {
            item { Text("Everything else the detector offers", style = MaterialTheme.typography.titleSmall, color = MaterialTheme.colorScheme.onSurfaceVariant, modifier = Modifier.padding(top = 12.dp)) }
            items(rest) { SettingRow(it, values.getOrNull(it.id) ?: it.lo, session, {}, {}) }
        }
        item {
            Column(Modifier.padding(top = 16.dp)) {
                TextButton({ session.actionAsync(Link.Action.ACCEPT_ALL) }) { Text("Accept all IDs") }
                TextButton({ session.actionAsync(Link.Action.DEFAULTS) }) { Text("Restore factory settings", color = Ferrous) }
                Text("Changes apply immediately on the detector and are saved to its flash with Save settings or when its menu is closed.", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
    }
}

@Composable
private fun SettingRow(item: ItemInfo, value: Int, session: DetectorSession, onWizard: () -> Unit, onLearned: () -> Unit) {
    val scope = rememberCoroutineScope()
    Card(Modifier.fillMaxWidth()) {
        Row(Modifier.padding(horizontal = 14.dp, vertical = 8.dp).fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            when {
                item.isAction -> {
                    Text(item.name, Modifier.weight(1f))
                    when (item.name) {
                        "Coil wizard" -> TextButton(onWizard) { Text("Open") }
                        "Learn target" -> TextButton(onLearned) { Text("Open") }
                        else -> TextButton({ scope.launch { session.run(item) } }) { Text("Run") }
                    }
                }
                item.isLabelled -> {
                    var open by remember { mutableStateOf(false) }
                    Text(item.name, Modifier.weight(1f))
                    Box {
                        TextButton({ open = true }) { Text(item.label(value), color = Copper); Icon(Icons.Default.ArrowDropDown, null, tint = Copper) }
                        DropdownMenu(open, { open = false }) { for (v in item.lo..item.hi) DropdownMenuItem(text = { Text(item.label(v)) }, onClick = { session.set(item, v); open = false }) }
                    }
                }
                item.name == "Phone PIN" -> {
                    var text by remember(value) { mutableStateOf(value.toString()) }
                    Text(item.name, Modifier.weight(1f))
                    OutlinedTextField(text, { t -> text = t.filter { c -> c.isDigit() }.take(4); text.toIntOrNull()?.let { n -> if (n in 0..9999) session.set(item, n) } },
                        Modifier.width(110.dp), singleLine = true, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number))
                }
                item.hi - item.lo > 12 -> {
                    Column(Modifier.weight(1f)) {
                        Row { Text(item.name, Modifier.weight(1f)); Text(value.toString(), color = MaterialTheme.colorScheme.onSurfaceVariant) }
                        Slider(value.toFloat(), { session.set(item, Math.round(it)) }, valueRange = item.lo.toFloat()..item.hi.toFloat(), steps = (item.hi - item.lo - 1).coerceAtLeast(0))
                    }
                }
                else -> {
                    Text(item.name, Modifier.weight(1f))
                    IconButton({ session.set(item, value - 1) }, enabled = value > item.lo) { Icon(Icons.Default.Remove, "less") }
                    Text(value.toString(), fontWeight = FontWeight.Medium)
                    IconButton({ session.set(item, value + 1) }, enabled = value < item.hi) { Icon(Icons.Default.Add, "more") }
                }
            }
        }
    }
}

private val WIZARD_STEPS = listOf(
    "Hold the coil in the air, away from metal. The detector measures the transmit winding.",
    "Measuring L and R… (advances by itself)",
    "Air balance recorded.",
    "Now hold a ferrite (or clean, iron-free ground) under the coil and continue.",
    "Ferrite response recorded; residual null and phase rotation computed.",
    "Place a coin 10 cm under the coil and continue: this sets the depth reference.",
    "Depth reference stored. The coil record is written to the coil's memory.",
    "Done.",
)

@Composable
private fun CoilWizard(session: DetectorSession, onClose: () -> Unit) {
    val s by session.status.collectAsStateWithLifecycle()
    val step = s?.wizardStep ?: 0xFF
    Column(Modifier.fillMaxSize().padding(16.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) { IconButton(onClose) { Icon(Icons.Default.ArrowBack, "back") }; Text("Coil wizard", style = MaterialTheme.typography.headlineSmall) }
        if (step == 0xFF) {
            Text("The wizard measures the coil, records its residual null and phase per frequency, and sets the depth reference. The result is written into the coil's own memory chip so it follows the coil.", color = MaterialTheme.colorScheme.onSurfaceVariant)
            Button({ session.actionAsync(Link.Action.WIZARD_START) }) { Text("Start") }
        } else {
            LinearProgressIndicator(progress = { minOf(step, 7) / 7f }, Modifier.fillMaxWidth())
            Text("Step ${step + 1} of 8", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            Text(WIZARD_STEPS[minOf(step, 7)], style = MaterialTheme.typography.titleMedium)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Button({ session.actionAsync(Link.Action.WIZARD_STEP) }, enabled = step != 1) { Text("Continue") }
                OutlinedButton({ session.actionAsync(Link.Action.WIZARD_CANCEL) }) { Text("Cancel") }
            }
        }
    }
}

@Composable
private fun LearnedList(session: DetectorSession, onClose: () -> Unit) {
    val learned by session.learned.collectAsStateWithLifecycle()
    LaunchedEffect(Unit) { session.refreshLearnedAsync() }
    Column(Modifier.fillMaxSize().padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) { IconButton(onClose) { Icon(Icons.Default.ArrowBack, "back") }; Text("Learned targets", style = MaterialTheme.typography.headlineSmall) }
        Text("Hold the coil over the target and use Learn on the Detect tab to store its signature. The detector then names the target when it sees it again.", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        if (learned.isEmpty()) Text("None yet", color = MaterialTheme.colorScheme.onSurfaceVariant)
        LazyColumn(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            items(learned) { e ->
                Card(Modifier.fillMaxWidth()) {
                    Row(Modifier.padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
                        Text(e.name, Modifier.weight(1f)); Text("ID ${e.condId} · Fe ${e.fe}", color = MaterialTheme.colorScheme.onSurfaceVariant)
                        IconButton({ session.forget(e) }) { Icon(Icons.Default.Delete, "forget") }
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------- Finds
@Composable
fun FindsScreen(session: DetectorSession) {
    val finds by session.finds.collectAsStateWithLifecycle()
    val s by session.status.collectAsStateWithLifecycle()
    val ctx = LocalContext.current
    val fmt = remember { SimpleDateFormat("HH:mm", Locale.getDefault()) }
    Column(Modifier.fillMaxSize().padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("Finds", style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f))
            IconButton({ session.refreshFindsAsync() }) { Icon(Icons.Default.Refresh, "refresh") }
        }
        s?.let { st -> if (st.gnssValid) TextButton({ ctx.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("geo:${st.lat},${st.lon}?q=${st.lat},${st.lon}(Detector)"))) }) { Icon(Icons.Default.LocationOn, null); Text("  Detector position ${String.format(Locale.US, "%.5f, %.5f", st.lat, st.lon)}") } }
        Text("Finds this session (${finds.size})", style = MaterialTheme.typography.titleSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        if (finds.isEmpty()) Text("None yet. Mark a find on the Detect tab, or hold a target for 1.5 s.", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        LazyColumn(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            items(finds) { f ->
                Card(Modifier.fillMaxWidth().clickable(enabled = f.hasPosition) { ctx.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse("geo:${f.lat},${f.lon}?q=${f.lat},${f.lon}(ID ${f.condId})"))) }) {
                    Row(Modifier.padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
                        Icon(if (f.fe > 55) Icons.Default.Build else Icons.Default.MonetizationOn, null, tint = if (f.fe > 55) Ferrous else Accept)
                        Spacer(Modifier.width(10.dp))
                        Column(Modifier.weight(1f)) {
                            Text(if (f.name.isEmpty()) "ID ${f.condId}" else "${f.name} · ID ${f.condId}", fontWeight = FontWeight.Medium)
                            Text("Fe ${f.fe} · ${f.depthCm} cm · fit ${f.confidence}%", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                        Column(horizontalAlignment = Alignment.End) {
                            Text(fmt.format(Date(f.utc * 1000)), style = MaterialTheme.typography.bodySmall)
                            Text(if (f.hasPosition) String.format(Locale.US, "%.5f, %.5f", f.lat, f.lon) else "no fix", style = MaterialTheme.typography.labelSmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------- Diagnostics
@Composable
fun DiagnosticsScreen(session: DetectorSession) {
    val d by session.diag.collectAsStateWithLifecycle()
    val info by session.info.collectAsStateWithLifecycle()
    val events by session.events.collectAsStateWithLifecycle()
    val state by session.state.collectAsStateWithLifecycle()
    LaunchedEffect(state) { if (state == SessionState.Ready) session.refreshDiag() }
    @Composable fun row(l: String, v: String) { Row(Modifier.fillMaxWidth().padding(vertical = 4.dp)) { Text(l, Modifier.weight(1f)); Text(v, color = MaterialTheme.colorScheme.onSurfaceVariant) } }
    @Composable fun f(v: Float, fmt: String) = String.format(Locale.US, fmt, v)
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) { Text("Diagnostics", style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f)); IconButton({ session.refreshDiag() }) { Icon(Icons.Default.Refresh, "refresh") } }
        val dd = d
        if (dd == null) Text(if (session.isReady) "Loading…" else state.text, color = MaterialTheme.colorScheme.onSurfaceVariant) else {
            Panel("Rails") { row("VSYS", f(dd.vsys, "%.2f V")); row("+3V3", f(dd.v3v3, "%.2f V")); row("+5VA", f(dd.v5va, "%.2f V")); row("−5VA", f(dd.vm5va, "%.2f V")); row("+12V", f(dd.v12, "%.1f V")); row("VTX", f(dd.vtx, "%.1f V")); row("VBRIDGE", f(dd.vbridge, "%.1f V")) }
            Panel("Transmit and receive") { row("TX current", f(dd.itxA * 1000, "%.0f mA rms")); row("Temperature", f(dd.tempC, "%.1f °C")); row("Noise floor", f(dd.noiseFloor * 1e6f, "%.1f µΩ")); row("Threshold", f(dd.threshold * 1e6f, "%.1f µΩ")); row("TX fault", if (dd.txFault) "yes" else "no"); row("ADC clipping", if (dd.clipped) "yes" else "no") }
            Panel("Ground model") { row("Magnetic a", f(dd.groundA, "%.4f")); row("Salt b", f(dd.groundB, "%.4f")); row("Phase", f(dd.groundPhase, "%.1f°")) }
            Panel("Coil") { row("Name", dd.coilName.ifEmpty { "—" }); row("TX inductance", f(dd.coilLmH, "%.3f mH")); row("TX resistance", f(dd.coilR, "%.2f Ω")); row("Present", if (dd.coilPresent) "yes" else "no"); row("Calibrated", if (dd.calibrated) "yes" else "no") }
            Panel("Transmit lines") { dd.lineHz.forEachIndexed { i, hz -> row("Line ${i + 1}", f(hz, "%.0f Hz")) } }
            Panel("Counters") { row("Periods", dd.periods.toString()); row("PI records", dd.piRecords.toString()); row("Uptime", "${dd.uptime} s") }
        }
        info?.let { Panel("Detector") { row("Firmware", it.firmware); row("Hardware", "rev ${it.hardware}"); row("Protocol", "v${it.protoVersion}") } }
        if (events.isNotEmpty()) Panel("Events") { events.forEach { Text(it.text, style = MaterialTheme.typography.bodySmall) } }
    }
}

// ---------------------------------------------------------------- Detector (connection)
@Composable
fun DetectorScreen(session: DetectorSession, onNeedPermissions: () -> Unit) {
    val state by session.state.collectAsStateWithLifecycle()
    val info by session.info.collectAsStateWithLifecycle()
    val discovered by session.discovered.collectAsStateWithLifecycle()
    val demo by session.demoFlow.collectAsStateWithLifecycle()
    val auto by session.autoFlow.collectAsStateWithLifecycle()
    var pinText by remember { mutableStateOf(session.pin.toString()) }
    val ready = state == SessionState.Ready
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text("Detector", style = MaterialTheme.typography.headlineMedium, fontWeight = FontWeight.Bold)
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(14.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(if (ready) Icons.Default.Bluetooth else Icons.Default.BluetoothDisabled, null, tint = if (ready) Accept else MaterialTheme.colorScheme.onSurfaceVariant)
                    Spacer(Modifier.width(10.dp))
                    Column { Text(state.text, fontWeight = FontWeight.Medium); info?.let { Text("${it.name} · firmware ${it.firmware} · rev ${it.hardware}", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant) } }
                }
                if (ready) TextButton({ session.disconnect() }) { Text("Disconnect", color = Ferrous) }
                else Button({ if (demo) session.startScan() else { onNeedPermissions(); session.startScan() } }, enabled = state != SessionState.Scanning && state != SessionState.Connecting && state != SessionState.Authenticating) { Text(if (state == SessionState.Scanning) "Scanning…" else "Scan for detectors") }
            }
        }
        if (discovered.isNotEmpty() && !ready) Panel("Detectors nearby") {
            discovered.forEach { d -> Row(Modifier.fillMaxWidth().clickable { session.connect(d) }.padding(vertical = 6.dp)) { Text(d.name, Modifier.weight(1f)); Text("${d.rssi} dBm", color = MaterialTheme.colorScheme.onSurfaceVariant) } }
        }
        Panel("Pairing") {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("PIN", Modifier.weight(1f))
                OutlinedTextField(pinText, { t -> pinText = t.filter { it.isDigit() }.take(4); pinText.toIntOrNull()?.let { session.pin = it } }, Modifier.width(110.dp), singleLine = true, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number))
            }
            Row(verticalAlignment = Alignment.CenterVertically) { Text("Connect automatically", Modifier.weight(1f)); Switch(auto, { session.autoConnect = it }) }
            Row(verticalAlignment = Alignment.CenterVertically) { Text("Demo detector (no hardware)", Modifier.weight(1f)); Switch(demo, { session.demoMode = it }) }
            Text("The PIN is shown on the detector under Menu → Phone PIN (factory 1234). The demo detector runs inside the app and answers the same protocol, so every screen can be tried without the hardware.", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        Panel("How it works") {
            Text("The detector's Bluetooth audio module also carries a small serial channel. The app asks the detector for its menu, so anything new in the firmware shows up here without an app update. Live status arrives ten times a second. The detector also runs without its own screen: this app, headphones and the power button are all it needs.", style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        if (ready) TextButton({ session.actionAsync(Link.Action.POWER_OFF) }) { Text("Power off the detector", color = Ferrous) }
    }
}
