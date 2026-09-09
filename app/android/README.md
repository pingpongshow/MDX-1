# MDX-1 companion app (Android)

Kotlin / Jetpack Compose app that talks to the detector over Bluetooth Low Energy (the BM83
module's Transparent UART service) using the link protocol in `docs/MDX-1_link_protocol.md`.
It is a screen-for-screen port of the iOS app in `app/ios`.

* `link/LinkProtocol.kt`   frame codec and message decoders (mirrors `firmware/core/link/link.h`)
* `link/Transport.kt`      BLE transport (BluetoothGatt) and a demo detector answering the same protocol
* `link/DetectorSession.kt` ViewModel: connection, PIN handshake, item table, live status, actions
* `ui/Screens.kt`          Detect, Settings (built from the detector's menu), Finds, Diagnostics, Detector

## Build

Open `app/android` in Android Studio (Ladybug or newer, AGP 8.7, Kotlin 2.0) and run, or:

```
cd app/android && gradle wrapper --gradle-version 8.10 && ./gradlew assembleDebug
```

Requires the Android SDK (compileSdk 35) and JDK 17. This project was written on a machine
without the Android SDK, so it has not been compiled here; expect to fix small compile issues
on first build. The protocol code is a direct port of the Swift version whose unit tests pass.

Finds open in whatever maps app is installed (`geo:` intent) rather than an embedded map, so
no Maps API key is needed. Turn on "Demo detector" on the Detector screen to try every screen
without hardware. Factory PIN 1234.
