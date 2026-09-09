import SwiftUI

enum Palette {
    static let ink = Color(red: 0.11, green: 0.13, blue: 0.15)
    static let copper = Color(red: 0.72, green: 0.42, blue: 0.20)
    static let ferrous = Color(red: 0.62, green: 0.20, blue: 0.16)
    static let accept = Color(red: 0.16, green: 0.52, blue: 0.36)
    static let ground = Color(red: 0.42, green: 0.36, blue: 0.28)
    static let panel = Color(.secondarySystemGroupedBackground)
}

struct Panel<Content: View>: View {
    let title: String
    @ViewBuilder let content: Content
    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(title.uppercased()).font(.caption.weight(.semibold)).tracking(1.2).foregroundStyle(.secondary)
            content
        }
        .padding(14)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(Palette.panel, in: RoundedRectangle(cornerRadius: 12, style: .continuous))
    }
}

struct Bar: View {
    let value: Double      // 0..1
    var tint: Color = Palette.copper
    var height: CGFloat = 12
    var body: some View {
        GeometryReader { g in
            ZStack(alignment: .leading) {
                Capsule().fill(Color.primary.opacity(0.08))
                Capsule().fill(tint).frame(width: max(0, min(1, value)) * g.size.width)
            }
        }
        .frame(height: height)
        .animation(.linear(duration: 0.08), value: value)
    }
}

struct SpectrumBars: View {
    let values: [Double]
    static let labels = ["5", "10", "15", "20", "41"]
    var body: some View {
        HStack(alignment: .bottom, spacing: 6) {
            ForEach(Array(values.enumerated()), id: \.offset) { i, v in
                VStack(spacing: 4) {
                    ZStack(alignment: .bottom) {
                        RoundedRectangle(cornerRadius: 3).fill(Color.primary.opacity(0.06))
                        RoundedRectangle(cornerRadius: 3).fill(Palette.copper).frame(height: max(2, CGFloat(v) * 56))
                    }
                    .frame(height: 56)
                    Text(SpectrumBars.labels[i]).font(.caption2.monospacedDigit()).foregroundStyle(.secondary)
                }
            }
        }
        .animation(.linear(duration: 0.08), value: values)
    }
}

struct Stat: View {
    let label: String
    let value: String
    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(label).font(.caption2).foregroundStyle(.secondary)
            Text(value).font(.callout.monospacedDigit().weight(.medium))
        }
    }
}

struct BatteryGlyph: View {
    let pct: Int
    let charging: Bool
    var body: some View {
        HStack(spacing: 4) {
            ZStack(alignment: .leading) {
                RoundedRectangle(cornerRadius: 2).stroke(lineWidth: 1).frame(width: 22, height: 11)
                RoundedRectangle(cornerRadius: 1).fill(pct < 15 ? Palette.ferrous : Palette.accept).frame(width: max(1, 18 * CGFloat(pct) / 100), height: 7).padding(.leading, 2)
            }
            if charging { Image(systemName: "bolt.fill").font(.caption2) }
            Text("\(pct)%").font(.caption.monospacedDigit())
        }
    }
}

struct EventToast: View {
    let event: LinkEvent
    var body: some View {
        HStack(spacing: 8) {
            Image(systemName: icon).foregroundStyle(Palette.copper)
            Text(event.text).font(.footnote)
        }
        .padding(.horizontal, 14).padding(.vertical, 10)
        .background(.regularMaterial, in: Capsule())
        .shadow(radius: 4, y: 2)
    }
    var icon: String {
        switch event.kind {
        case .find: return "mappin.and.ellipse"
        case .noiseCancelDone: return "waveform.path"
        case .lowBattery: return "battery.25percent"
        case .wizard: return "wand.and.stars"
        case .coil: return "circle.circle"
        case .settings: return "slider.horizontal.3"
        }
    }
}
