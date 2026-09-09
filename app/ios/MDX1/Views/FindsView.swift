import SwiftUI
import MapKit

struct FindsView: View {
    @EnvironmentObject var session: DetectorSession
    @State private var camera: MapCameraPosition = .automatic

    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                Map(position: $camera) {
                    ForEach(session.finds.filter(\.hasPosition)) { f in
                        Marker("ID \(f.condID)", systemImage: f.fe > 55 ? "wrench" : "dollarsign.circle", coordinate: CLLocationCoordinate2D(latitude: f.lat, longitude: f.lon))
                            .tint(f.fe > 55 ? Palette.ferrous : Palette.accept)
                    }
                    if let s = session.status, s.gnssValid {
                        Annotation("Detector", coordinate: CLLocationCoordinate2D(latitude: s.lat, longitude: s.lon)) {
                            Image(systemName: "dot.radiowaves.left.and.right").padding(6).background(Palette.copper, in: Circle()).foregroundStyle(.white)
                        }
                    }
                }
                .frame(height: 300)
                List {
                    Section("Finds this session (\(session.finds.count))") {
                        if session.finds.isEmpty { Text("None yet. Mark a find on the Detect tab, or hold a target for 1.5 s.").foregroundStyle(.secondary).font(.footnote) }
                        ForEach(session.finds) { f in
                            HStack {
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(f.name.isEmpty ? "ID \(f.condID)" : "\(f.name) · ID \(f.condID)").font(.body.weight(.medium))
                                    Text("Fe \(f.fe) · \(f.depthCM) cm · fit \(f.confidence)%").font(.footnote.monospacedDigit()).foregroundStyle(.secondary)
                                }
                                Spacer()
                                VStack(alignment: .trailing, spacing: 2) {
                                    Text(Date(timeIntervalSince1970: TimeInterval(f.utc)), style: .time).font(.footnote)
                                    if f.hasPosition { Text(String(format: "%.5f, %.5f", f.lat, f.lon)).font(.caption2.monospacedDigit()).foregroundStyle(.secondary) }
                                    else { Text("no fix").font(.caption2).foregroundStyle(.secondary) }
                                }
                            }
                        }
                    }
                }
            }
            .navigationTitle("Finds")
            .toolbar {
                ToolbarItem(placement: .primaryAction) {
                    Button { Task { await session.refreshFinds() } } label: { Image(systemName: "arrow.clockwise") }
                }
            }
        }
    }
}
