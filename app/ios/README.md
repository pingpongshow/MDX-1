# MDX-1 companion app (iOS)

SwiftUI app that talks to the detector over Bluetooth Low Energy (the BM83 module's Transparent
UART service) using the link protocol in `docs/MDX-1_link_protocol.md`.

* `MDX1/Link/LinkProtocol.swift` frame codec and message decoders (mirrors `firmware/core/link/link.h`)
* `MDX1/Link/Transport.swift`   BLE transport (CoreBluetooth) and a demo detector that answers the same protocol
* `MDX1/Link/DetectorSession.swift` connection, PIN handshake, menu (item table), live status, actions
* `MDX1/Views/`                 Detect, Settings (built from the detector's own menu), Finds (map), Diagnostics, Detector
* `MDX1Tests/`                  CRC/frame tests and a decode of a status frame captured from the firmware bench

Build: `xcodegen generate` (project.yml), open `MDX1.xcodeproj`, or

```
xcodebuild -project MDX1.xcodeproj -scheme MDX1 -destination 'platform=iOS Simulator,name=iPhone 17 Pro' build test
```

Turn on "Demo detector" on the Detector tab to try every screen without hardware. Factory PIN 1234.
Set a Development Team in Xcode (Signing) to run on a phone; BLE does not work in the simulator.
