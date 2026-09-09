import SwiftUI

struct SettingsView: View {
    @EnvironmentObject var session: DetectorSession

    static let groups: [(String, [String])] = [
        ("Detection", ["Program", "Mode", "Sensitivity", "Auto sensitivity", "Recovery speed", "TX power", "Ferrous limit"]),
        ("Ground", ["Ground balance", "Salt mode", "Hybrid PI rate"]),
        ("Audio", ["Tones", "Volume", "Threshold hum", "Iron volume", "Iron grunt"]),
        ("Tools", ["Noise cancel at start", "Noise cancel", "Coil wizard", "Learn target"]),
        ("Device", ["Front light", "GPS logging", "Depth units", "Phone PIN", "Save settings"]),
    ]

    var body: some View {
        NavigationStack {
            Group {
                if session.items.isEmpty {
                    ContentUnavailableView("No detector", systemImage: "slider.horizontal.3", description: Text(session.state.text))
                } else {
                    List {
                        ForEach(SettingsView.groups, id: \.0) { title, names in
                            Section(title) {
                                ForEach(names.compactMap { session.item(named: $0) }) { item in row(item) }
                            }
                        }
                        Section("Everything else the detector offers") {
                            ForEach(session.items.filter { it in !SettingsView.groups.flatMap(\.1).contains(it.name) && !["Find log", "Diagnostics"].contains(it.name) }) { row($0) }
                        }
                        Section {
                            Button("Accept all IDs") { Task { await session.action(.acceptAll) } }
                            Button("Restore factory settings", role: .destructive) { Task { await session.action(.defaults) } }
                        } footer: {
                            Text("Changes apply immediately on the detector and are saved to its flash when you use Save settings or leave its menu.")
                        }
                    }
                }
            }
            .navigationTitle("Settings")
        }
    }

    @ViewBuilder
    func row(_ item: ItemInfo) -> some View {
        if item.isAction {
            actionRow(item)
        } else if item.isLabelled {
            Picker(item.name, selection: Binding(get: { session.value(of: item) }, set: { session.set(item, to: $0) })) {
                ForEach(item.lo...item.hi, id: \.self) { v in Text(item.label(for: v)).tag(v) }
            }
        } else if item.name == "Phone PIN" {
            HStack {
                Text(item.name)
                Spacer()
                TextField("PIN", value: Binding(get: { session.value(of: item) }, set: { session.set(item, to: $0) }), format: .number)
                    .keyboardType(.numberPad).multilineTextAlignment(.trailing).frame(width: 90)
            }
        } else if item.hi - item.lo > 12 {
            VStack(alignment: .leading, spacing: 4) {
                HStack { Text(item.name); Spacer(); Text("\(session.value(of: item))").monospacedDigit().foregroundStyle(.secondary) }
                Slider(value: Binding(get: { Double(session.value(of: item)) }, set: { session.set(item, to: Int($0.rounded())) }),
                       in: Double(item.lo)...Double(item.hi), step: 1)
            }
        } else {
            Stepper(value: Binding(get: { session.value(of: item) }, set: { session.set(item, to: $0) }), in: item.lo...item.hi) {
                HStack { Text(item.name); Spacer(); Text("\(session.value(of: item))").monospacedDigit().foregroundStyle(.secondary) }
            }
        }
    }

    @ViewBuilder
    func actionRow(_ item: ItemInfo) -> some View {
        switch item.name {
        case "Coil wizard": NavigationLink(item.name) { CoilWizardView() }
        case "Learn target": NavigationLink(item.name) { LearnedListView() }
        default: Button(item.name) { Task { await session.run(item) } }
        }
    }
}

struct LearnedListView: View {
    @EnvironmentObject var session: DetectorSession
    var body: some View {
        List {
            Section {
                Text("Hold the coil over the target and use Learn target on the Detect tab to store its signature. The detector then names the target when it sees it again.")
                    .font(.footnote).foregroundStyle(.secondary)
            }
            Section("Stored targets") {
                if session.learned.isEmpty { Text("None yet").foregroundStyle(.secondary) }
                ForEach(session.learned) { e in
                    HStack { Text(e.name); Spacer(); Text("ID \(e.condID) · Fe \(e.fe)").foregroundStyle(.secondary).font(.footnote.monospacedDigit()) }
                }
                .onDelete { idx in Task { for i in idx { await session.forget(session.learned[i]) } } }
            }
        }
        .navigationTitle("Learned targets")
        .task { await session.refreshLearned() }
    }
}

struct CoilWizardView: View {
    @EnvironmentObject var session: DetectorSession
    static let steps = [
        "Hold the coil in the air, away from metal. The detector measures the transmit winding.",
        "Measuring L and R… (advances by itself)",
        "Air balance recorded.",
        "Now hold a ferrite (or clean, iron-free ground) under the coil and continue.",
        "Ferrite response recorded; residual null and phase rotation computed.",
        "Place a coin 10 cm under the coil and continue: this sets the depth reference.",
        "Depth reference stored. The coil record is written to the coil's memory.",
        "Done.",
    ]
    var step: Int { session.status?.wizardStep ?? 0xFF }
    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            if step == 0xFF {
                Text("The wizard measures the coil, records its residual null and phase per frequency, and sets the depth reference. It writes the result into the coil's own memory chip so it follows the coil.")
                    .font(.callout).foregroundStyle(.secondary)
                Button("Start") { Task { await session.action(.wizardStart) } }.buttonStyle(.borderedProminent)
            } else {
                ProgressView(value: Double(min(step, 7)), total: 7)
                Text("Step \(step + 1) of 8").font(.caption).foregroundStyle(.secondary)
                Text(CoilWizardView.steps[min(step, 7)]).font(.title3)
                HStack {
                    Button("Continue") { Task { await session.action(.wizardStep) } }.buttonStyle(.borderedProminent).disabled(step == 1)
                    Button("Cancel", role: .cancel) { Task { await session.action(.wizardCancel) } }.buttonStyle(.bordered)
                }
            }
            Spacer()
        }
        .padding()
        .navigationTitle("Coil wizard")
    }
}
