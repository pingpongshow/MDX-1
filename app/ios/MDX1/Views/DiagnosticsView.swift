import SwiftUI

struct DiagnosticsView: View {
    @EnvironmentObject var session: DetectorSession

    var body: some View {
        NavigationStack {
            List {
                if let d = session.diag {
                    Section("Rails") {
                        row("VSYS", "%.2f V", d.vsys); row("+3V3", "%.2f V", d.v3v3); row("+5VA", "%.2f V", d.v5va); row("−5VA", "%.2f V", d.vm5va)
                        row("+12V", "%.1f V", d.v12); row("VTX", "%.1f V", d.vtx); row("VBRIDGE", "%.1f V", d.vbridge)
                    }
                    Section("Transmit and receive") {
                        row("TX current", "%.0f mA rms", d.itxA * 1000); row("Temperature", "%.1f °C", d.tempC)
                        row("Noise floor", "%.1f µΩ", d.noiseFloor * 1e6); row("Threshold", "%.1f µΩ", d.threshold * 1e6)
                        flag("TX fault", d.txFault); flag("ADC clipping", d.clipped)
                    }
                    Section("Ground model") {
                        row("Magnetic a", "%.4f", d.groundA); row("Salt b", "%.4f", d.groundB); row("Phase", "%.1f°", d.groundPhase)
                    }
                    Section("Coil") {
                        LabeledContent("Name", value: d.coilName.isEmpty ? "—" : d.coilName)
                        row("TX inductance", "%.3f mH", d.coilLmH); row("TX resistance", "%.2f Ω", d.coilR)
                        flag("Coil present", d.coilPresent); flag("Calibrated", d.calibrated)
                    }
                    Section("Transmit lines") {
                        ForEach(Array(d.lineHz.enumerated()), id: \.offset) { i, f in LabeledContent("Line \(i + 1)", value: String(format: "%.0f Hz", f)) }
                    }
                    Section("Counters") {
                        LabeledContent("Periods", value: "\(d.periods)"); LabeledContent("PI records", value: "\(d.piRecords)"); LabeledContent("Uptime", value: "\(d.uptime) s")
                    }
                } else {
                    ContentUnavailableView("No diagnostics yet", systemImage: "waveform.path.ecg", description: Text(session.isReady ? "Pull to refresh" : session.state.text))
                }
                if let i = session.info {
                    Section("Detector") {
                        LabeledContent("Firmware", value: i.firmware); LabeledContent("Hardware", value: "rev \(i.hardware)"); LabeledContent("Protocol", value: "v\(i.protoVersion)")
                    }
                }
                if !session.events.isEmpty {
                    Section("Events") { ForEach(session.events) { e in Text(e.text).font(.footnote) } }
                }
            }
            .refreshable { await session.refreshDiag() }
            .task { if session.isReady { await session.refreshDiag() } }
            .onChange(of: session.isReady) { _, ready in if ready { Task { await session.refreshDiag() } } }
            .navigationTitle("Diagnostics")
            .toolbar {
                ToolbarItem(placement: .primaryAction) { Button { Task { await session.refreshDiag() } } label: { Image(systemName: "arrow.clockwise") } }
            }
        }
    }
    func row(_ l: String, _ fmt: String, _ v: Float) -> some View { LabeledContent(l, value: String(format: fmt, v)) }
    func flag(_ l: String, _ on: Bool) -> some View {
        LabeledContent(l) { Image(systemName: on ? "checkmark.circle.fill" : "circle").foregroundStyle(on ? Palette.accept : .secondary) }
    }
}
