import SwiftUI

struct DetectView: View {
    @EnvironmentObject var session: DetectorSession
    @State private var learnName = ""
    @State private var showLearn = false
    @State private var pinpointHeld = false

    var body: some View {
        NavigationStack {
            ScrollView {
                if let s = session.status {
                    VStack(spacing: 12) {
                        header(s)
                        idPanel(s)
                        HStack(spacing: 12) {
                            Panel(title: "Signature kHz") { SpectrumBars(values: s.spectrum) }
                            Panel(title: "Depth") {
                                VStack(alignment: .leading, spacing: 6) {
                                    Text(depthText(s)).font(.system(size: 34, weight: .semibold, design: .rounded).monospacedDigit())
                                    if s.piDepthCM > 0 { Text("PI \(s.piDepthCM) cm").font(.caption).foregroundStyle(.secondary) }
                                    Text("swing \(s.swing, specifier: "%.1f") m/s").font(.caption).foregroundStyle(.secondary)
                                }
                            }
                        }
                        groundPanel(s)
                        controls(s)
                    }
                    .padding()
                } else {
                    ContentUnavailableView("No detector", systemImage: "antenna.radiowaves.left.and.right.slash", description: Text(session.state.text))
                        .padding(.top, 80)
                }
            }
            .background(Color(.systemGroupedBackground))
            .navigationTitle("MDX-1")
            .navigationBarTitleDisplayMode(.inline)
            .sheet(isPresented: $showLearn) { learnSheet }
        }
    }

    // MARK: pieces
    func header(_ s: StatusPacket) -> some View {
        HStack {
            Text(programName(s)).font(.subheadline.weight(.semibold))
            Text(modeName(s)).font(.subheadline).foregroundStyle(.secondary)
            Spacer()
            Text("N\(s.noiseM)").font(.caption.monospacedDigit()).foregroundStyle(.secondary)
            Image(systemName: s.gnssValid ? "location.fill" : "location.slash").font(.caption).foregroundStyle(s.gnssValid ? Palette.accept : .secondary)
            BatteryGlyph(pct: s.battPct, charging: s.flags.contains(.charging))
        }
    }

    func idPanel(_ s: StatusPacket) -> some View {
        let tint: Color = !s.present ? .secondary : (s.ferrous ? Palette.ferrous : (s.accepted ? Palette.accept : Palette.ground))
        return VStack(spacing: 10) {
            HStack(alignment: .firstTextBaseline) {
                Text(s.present ? String(format: "%.0f", s.condID) : "--")
                    .font(.system(size: 96, weight: .bold, design: .rounded).monospacedDigit())
                    .foregroundStyle(tint)
                    .contentTransition(.numericText())
                VStack(alignment: .leading, spacing: 4) {
                    Text(targetName(s)).font(.title3.weight(.semibold)).foregroundStyle(tint)
                    Text("Fe \(s.feIndex)  ·  fit \(s.confidence)%").font(.footnote.monospacedDigit()).foregroundStyle(.secondary)
                    Text(s.present ? "\(s.strengthDB) dB over threshold" : "searching").font(.footnote.monospacedDigit()).foregroundStyle(.secondary)
                }
                Spacer()
            }
            Bar(value: s.pinpoint ? s.pinpointLevel : s.signal, tint: tint, height: 16)
            HStack {
                Text(s.pinpoint ? "PINPOINT" : "SIGNAL").font(.caption2.weight(.semibold)).tracking(1.2).foregroundStyle(.secondary)
                Spacer()
                if s.flags.contains(.ncActive) { Label("noise cancel running", systemImage: "waveform.path").font(.caption2).foregroundStyle(Palette.copper) }
                if s.flags.contains(.lowBatt) { Label("low battery", systemImage: "battery.25percent").font(.caption2).foregroundStyle(Palette.ferrous) }
                if !s.flags.contains(.coil) { Label("no coil", systemImage: "exclamationmark.triangle").font(.caption2).foregroundStyle(Palette.ferrous) }
                if s.inWizard { Label("coil wizard step \(s.wizardStep)", systemImage: "wand.and.stars").font(.caption2).foregroundStyle(Palette.copper) }
            }
        }
        .padding(16)
        .background(Palette.panel, in: RoundedRectangle(cornerRadius: 14, style: .continuous))
    }

    func groundPanel(_ s: StatusPacket) -> some View {
        Panel(title: "Ground and noise") {
            LazyVGrid(columns: Array(repeating: GridItem(.flexible(), alignment: .leading), count: 3), alignment: .leading, spacing: 10) {
                Stat(label: "ground phase", value: String(format: "%.1f°", s.groundPhase))
                Stat(label: "mineralisation", value: "\(s.groundStrength) mΩ")
                Stat(label: "salt", value: "\(s.salt)%")
                Stat(label: "hot rock", value: "\(s.hotRock)")
                Stat(label: "noise floor", value: "\(s.noiseUV) µΩ")
                Stat(label: "transmit", value: String(format: "%.1f V · %d mA", s.vtx, s.itxMA))
            }
        }
    }

    func controls(_ s: StatusPacket) -> some View {
        VStack(spacing: 10) {
            LazyVGrid(columns: Array(repeating: GridItem(.flexible()), count: 3), spacing: 10) {
                pinpointButton(s)
                actionButton("Ground grab", "arrow.down.to.line") { await session.action(.groundGrab) }
                actionButton("Noise cancel", "waveform.path") { await session.action(.noiseCancel) }
                actionButton("Mark find", "mappin.and.ellipse") { await session.action(.markFind) }
                Button { learnName = ""; showLearn = true } label: { Label("Learn", systemImage: "brain").frame(maxWidth: .infinity) }
                    .buttonStyle(.bordered).disabled(!s.present)
                sensitivityStepper(s)
            }
        }
    }

    func pinpointButton(_ s: StatusPacket) -> some View {
        Label("Pinpoint", systemImage: "scope")
            .padding(.vertical, 7).frame(maxWidth: .infinity)
            .background(pinpointHeld ? Palette.copper : Palette.copper.opacity(0.18), in: RoundedRectangle(cornerRadius: 8))
            .foregroundStyle(pinpointHeld ? .white : Palette.copper)
            .gesture(DragGesture(minimumDistance: 0)
                .onChanged { _ in if !pinpointHeld { pinpointHeld = true; session.setPinpoint(true) } }
                .onEnded { _ in pinpointHeld = false; session.setPinpoint(false) })
            .accessibilityHint("Hold for non-motion pinpoint")
    }

    func actionButton(_ title: String, _ icon: String, _ work: @escaping () async -> Void) -> some View {
        Button { Task { await work() } } label: { Label(title, systemImage: icon).frame(maxWidth: .infinity).lineLimit(1).minimumScaleFactor(0.7) }
            .buttonStyle(.bordered)
    }

    func sensitivityStepper(_ s: StatusPacket) -> some View {
        Group {
            if let it = session.item(named: "Sensitivity") {
                Stepper(value: Binding(get: { session.value(of: it) }, set: { session.set(it, to: $0) }), in: it.lo...it.hi) {
                    Text("Sens \(session.value(of: it))").font(.footnote.monospacedDigit())
                }
                .labelsHidden().overlay(alignment: .leading) { Text("S\(session.value(of: it))").font(.caption.monospacedDigit()).offset(x: -34) }
                .padding(.leading, 34)
            }
        }
    }

    var learnSheet: some View {
        NavigationStack {
            Form {
                Section("Name for the target under the coil") {
                    TextField("e.g. Silver dime", text: $learnName)
                }
                Section {
                    Button("Learn") { Task { await session.learn(name: learnName); showLearn = false } }
                }
                if !session.learned.isEmpty {
                    Section("Stored targets") {
                        ForEach(session.learned) { e in
                            HStack { Text(e.name); Spacer(); Text("ID \(e.condID) · Fe \(e.fe)").foregroundStyle(.secondary).font(.footnote.monospacedDigit()) }
                        }
                        .onDelete { idx in Task { for i in idx { await session.forget(session.learned[i]) } } }
                    }
                }
            }
            .navigationTitle("Learn target")
            .toolbar { ToolbarItem(placement: .cancellationAction) { Button("Close") { showLearn = false } } }
        }
        .presentationDetents([.medium, .large])
    }

    // MARK: text helpers
    func programName(_ s: StatusPacket) -> String { session.item(named: "Program")?.label(for: s.program) ?? "Program \(s.program)" }
    func modeName(_ s: StatusPacket) -> String { session.item(named: "Mode")?.label(for: s.mode) ?? "Mode \(s.mode)" }
    func targetName(_ s: StatusPacket) -> String {
        guard s.present else { return "" }
        if s.matchIndex >= 0, let e = session.learned.first(where: { $0.id == s.matchIndex }) { return e.name }
        if s.ferrous { return "IRON" }
        return s.accepted ? "target" : "rejected"
    }
    func depthText(_ s: StatusPacket) -> String {
        guard s.present else { return "--" }
        let inches = (session.item(named: "Depth units")).map { session.value(of: $0) == 1 } ?? false
        return inches ? String(format: "%.0f in", Double(s.depthCM) / 2.54) : "\(s.depthCM) cm"
    }
}
