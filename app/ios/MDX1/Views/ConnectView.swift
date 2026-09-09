import SwiftUI

struct ConnectView: View {
    @EnvironmentObject var session: DetectorSession
    @State private var pinText = ""

    var body: some View {
        NavigationStack {
            List {
                Section {
                    HStack {
                        Image(systemName: session.isReady ? "antenna.radiowaves.left.and.right" : "antenna.radiowaves.left.and.right.slash")
                            .font(.title2).foregroundStyle(session.isReady ? Palette.accept : .secondary)
                        VStack(alignment: .leading) {
                            Text(session.state.text).font(.body.weight(.medium))
                            if let i = session.info { Text("\(i.name) · firmware \(i.firmware) · rev \(i.hardware)").font(.footnote).foregroundStyle(.secondary) }
                        }
                    }
                    if session.isReady {
                        Button("Disconnect", role: .destructive) { session.disconnect() }
                    } else {
                        Button(session.state == .scanning ? "Scanning…" : "Scan for detectors") { session.startScan() }
                            .disabled(session.state == .scanning || session.state == .connecting || session.state == .authenticating)
                    }
                }
                if !session.discovered.isEmpty && !session.isReady {
                    Section("Detectors nearby") {
                        ForEach(session.discovered) { d in
                            Button { session.connect(to: d) } label: {
                                HStack { Text(d.name); Spacer(); Text("\(d.rssi) dBm").font(.caption.monospacedDigit()).foregroundStyle(.secondary) }
                            }
                        }
                    }
                }
                Section {
                    HStack {
                        Text("PIN")
                        Spacer()
                        TextField("1234", text: $pinText)
                            .keyboardType(.numberPad).multilineTextAlignment(.trailing).frame(width: 100)
                            .onChange(of: pinText) { _, v in if let n = Int(v), n >= 0, n <= 9999 { session.pin = n } }
                    }
                    Toggle("Connect automatically", isOn: $session.autoConnect)
                    Toggle("Demo detector (no hardware)", isOn: $session.demoMode)
                } header: { Text("Pairing") } footer: {
                    Text("The PIN is shown on the detector under Menu → Phone PIN (factory 1234). The demo detector runs inside the app and answers the same protocol, so every screen can be tried without the hardware.")
                }
                Section("How it works") {
                    Text("The detector's Bluetooth audio module also carries a small serial channel. The app asks the detector for its menu, so anything new in the firmware shows up here without an app update. Live status arrives ten times a second.")
                        .font(.footnote).foregroundStyle(.secondary)
                }
                if session.isReady {
                    Section {
                        Button("Power off the detector", role: .destructive) { Task { await session.action(.powerOff) } }
                    }
                }
            }
            .navigationTitle("Detector")
            .onAppear { pinText = String(session.pin) }
        }
    }
}
