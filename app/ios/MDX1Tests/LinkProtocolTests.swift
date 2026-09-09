import XCTest
@testable import MDX1

final class LinkProtocolTests: XCTestCase {
    /// STATUS one-shot frame captured from the firmware bench (firmware/build/link_status_fixture.hex):
    /// cond_id 61.1, present, GPS 51.5074 / -0.1278, sensitivity 27, noise channel in 96..104.
    let fixture = "a537860e436302005a0111081d000a000166a800ff0198423c0f5ac8eafefff201d067b31ed07fecff090b000000ff01001b01000000000e00ff04961d"

    func testCRCMatchesFirmware() {
        // CRC-16/CCITT-FALSE check value for "123456789" is 0x29B1
        XCTAssertEqual(Link.crc16(Array("123456789".utf8)), 0x29B1)
    }

    func testFrameRoundTrip() {
        let f = Link.frame(type: Link.MsgType.set.rawValue, seq: 7, payload: [2, 27, 0])
        var p = LinkFrameParser()
        let frames = p.feed(f)
        XCTAssertEqual(frames.count, 1)
        XCTAssertEqual(frames.first?.type, 0x04)
        XCTAssertEqual(frames.first?.seq, 7)
        XCTAssertEqual(frames.first?.payload, [2, 27, 0])
        // split across two BLE notifications
        var q = LinkFrameParser()
        XCTAssertEqual(q.feed(f.prefix(4)).count, 0)
        XCTAssertEqual(q.feed(f.suffix(from: 4)).count, 1)
        // corrupted CRC is dropped
        var bad = f; bad[bad.count - 1] ^= 0xFF
        var r = LinkFrameParser()
        XCTAssertEqual(r.feed(bad).count, 0)
        XCTAssertEqual(r.badFrames, 1)
    }

    func testStatusFixtureFromFirmware() throws {
        let bytes = stride(from: 0, to: fixture.count, by: 2).map { i -> UInt8 in
            let s = fixture.index(fixture.startIndex, offsetBy: i); let e = fixture.index(s, offsetBy: 2)
            return UInt8(fixture[s..<e], radix: 16)!
        }
        var p = LinkFrameParser()
        let frames = p.feed(Data(bytes))
        XCTAssertEqual(frames.count, 1)
        let f = try XCTUnwrap(frames.first)
        XCTAssertEqual(f.type, Link.MsgType.status.response)
        XCTAssertEqual(f.seq, 14)
        let s = try XCTUnwrap(StatusPacket(f.payload))
        XCTAssertTrue(s.present)
        XCTAssertEqual(s.condID, 61.1, accuracy: 0.05)
        XCTAssertEqual(s.sensitivity, 27)
        XCTAssertTrue((96...104).contains(s.noiseM))
        XCTAssertTrue(s.gnssValid)
        XCTAssertEqual(s.lat, 51.5074, accuracy: 1e-6)
        XCTAssertEqual(s.lon, -0.1278, accuracy: 1e-6)
        XCTAssertEqual(s.spectrum.count, 5)
        XCTAssertEqual(s.wizardStep, 0xFF)
    }

    func testDemoDetectorAnswersHello() {
        let demo = DemoTransport()
        let exp = expectation(description: "hello")
        var parser = LinkFrameParser()
        demo.onReceive = { d in
            for f in parser.feed(d) where f.type == Link.MsgType.hello.response {
                let h = HelloInfo(f.payload)
                XCTAssertEqual(h?.authenticated, true)
                XCTAssertEqual(h?.itemCount, DemoDetector.items.count)
                exp.fulfill()
            }
        }
        demo.send(Link.frame(type: Link.MsgType.hello.rawValue, seq: 1, payload: [1, 0xD2, 0x04]))   // PIN 1234
        wait(for: [exp], timeout: 2)
    }
}
