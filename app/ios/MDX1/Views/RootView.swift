import SwiftUI

struct RootView: View {
    @EnvironmentObject var session: DetectorSession
    @State private var tab = 0
    @State private var toast: LinkEvent?

    var body: some View {
        TabView(selection: $tab) {
            DetectView().tabItem { Label("Detect", systemImage: "dot.radiowaves.left.and.right") }.tag(0)
            SettingsView().tabItem { Label("Settings", systemImage: "slider.horizontal.3") }.tag(1)
            FindsView().tabItem { Label("Finds", systemImage: "map") }.tag(2)
            DiagnosticsView().tabItem { Label("Diagnostics", systemImage: "waveform.path.ecg") }.tag(3)
            ConnectView().tabItem { Label("Detector", systemImage: session.isReady ? "antenna.radiowaves.left.and.right" : "antenna.radiowaves.left.and.right.slash") }.tag(4)
        }
        .tint(Palette.copper)
        .overlay(alignment: .top) {
            if let t = toast { EventToast(event: t).padding(.top, 8).transition(.move(edge: .top).combined(with: .opacity)) }
        }
        .onChange(of: session.events.first?.id) { _, _ in
            guard let e = session.events.first else { return }
            withAnimation { toast = e }
            DispatchQueue.main.asyncAfter(deadline: .now() + 3) { withAnimation { if toast?.id == e.id { toast = nil } } }
        }
        .onAppear {
            if let t = UserDefaults.standard.object(forKey: "startTab") as? Int { tab = t } else if !session.isReady { tab = 4 }
            if session.autoConnect && !session.isReady { session.startScan() }
        }
        .alert("Link", isPresented: Binding(get: { session.lastError != nil }, set: { if !$0 { session.lastError = nil } })) {
            Button("OK") { session.lastError = nil }
        } message: { Text(session.lastError ?? "") }
    }
}
