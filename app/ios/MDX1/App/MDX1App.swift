import SwiftUI

@main
struct MDX1App: App {
    @StateObject private var session = DetectorSession()

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(session)
                .preferredColorScheme(nil)
        }
    }
}
