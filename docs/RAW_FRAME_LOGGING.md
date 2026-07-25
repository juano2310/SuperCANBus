# Raw Frame Logging Hook

## Overview

The SuperCAN library includes a raw-frame logging hook that observes **every physical CAN frame** that crosses the bus on a broker or client node — both sent (TX) and received (RX) — regardless of message type, target ID, or pub/sub-level filtering. It's intended for diagnostics, bus sniffing, and forwarding 100% of traffic to an external log (for example, a gateway/brain firmware that streams raw CAN activity to the cloud).

This is a pure observer: registering a callback does not change any protocol behavior, message routing, or timing. If no callback is registered, the hook is a cheap branch-and-skip with no allocation.

## How It Works

### RX (Received Frames)

The hook fires at the **very top** of `handleMessage()`, before any target-ID filtering or `msgType` dispatch — the same call for both `CANPubSubBroker` and `CANPubSubClient`. This means:

- A **broker** sees every frame that arrives on the bus, including frames whose payload targets a different client than the one currently being processed, and frames the broker itself doesn't otherwise act on.
- A **client** sees every frame it received (i.e. that passed the controller's own hardware filters), not just the ones addressed to its client ID.
- The callback runs *before* the library decides whether the frame is an extended fragment, a known message type, or addressed to this node — so it reflects raw bus traffic, not the pub/sub protocol's interpretation of it.

The payload is copied out via `CANControllerClass::packetData()` (see [docs/API.md](API.md#packet-data-non-destructive)), a **non-destructive** read that doesn't advance the `Stream` read cursor. This means the raw-frame hook never interferes with a handler's own `available()`/`read()`/`peek()` calls later in `handleMessage()`.

Because RX has no "did it succeed" concept, `txOk` is always passed as `true` for RX events — a not-applicable placeholder, not a meaningful result. Only trust `txOk` when `isTx` is `true`.

### TX (Sent Frames)

Every physical frame transmission in the library goes through the internal `sendFrame()` helper, which:
1. Performs the `beginPacket()`/`beginExtendedPacket()` → `write()` → `endPacket()` sequence
2. Captures the real result of `endPacket()`
3. Fires the raw-frame hook with `isTx = true` and that real result as `txOk`

This means the hook fires **once per physical frame actually put on the wire**, not once per logical API call. For a message that fits in a single CAN frame (the common case), that's one callback invocation. For an [extended/multi-frame message](EXTENDED_FRAMES.md) (e.g. a long topic name, serial number, or publish payload split across several 8-byte fragments), the hook fires **once per fragment**, so you'll see several TX events for what was a single `publish()` or `subscribe()` call — this is intentional, since the goal is to observe the actual bus traffic, not the application-level call.

## API Reference

### `void onRawFrame(RawFrameCallback callback)`

Register the raw-frame hook. Available on both `CANPubSubBroker` and `CANPubSubClient` (inherited from `CANPubSubBase`).

- **Parameters**: `callback` - function matching `RawFrameCallback`, or `nullptr` to disable
- Cheap no-op when `callback` is `nullptr` (no buffer copy, no allocation)

### `typedef void (*RawFrameCallback)(bool isTx, bool extended, uint32_t id, const uint8_t* data, uint8_t len, bool txOk)`

| Parameter  | Meaning |
|------------|---------|
| `isTx`     | `true` for a transmitted frame, `false` for a received frame |
| `extended` | `true` if the frame used a 29-bit extended CAN ID, `false` for an 11-bit standard ID |
| `id`       | The frame's CAN identifier (message type for standard frames, encoded `[msgType][frameSeq][totalFrames]` for extended — see [EXTENDED_FRAMES.md](EXTENDED_FRAMES.md)) |
| `data`     | Pointer to the frame's payload bytes, valid only for the duration of the callback |
| `len`      | Number of payload bytes (0-8) |
| `txOk`     | Real TX success/failure when `isTx` is `true`; always `true` (not-applicable placeholder) when `isTx` is `false` |

### `bool sendFrame(uint32_t id, const uint8_t* data, uint8_t len, bool extended = false)`

The low-level send primitive that every TX path in the library (and the raw-frame hook) is built on. Applications can also call it directly to send a custom single-frame message with the same TX-failure accounting as the rest of the library. See [PUBSUB_API.md](PUBSUB_API.md#sendframe) for full details.

### `uint32_t getTxFailCount() const`

Running total of `sendFrame()` calls whose `endPacket()` reported failure, tracked automatically for every send the library performs. A simple, always-on companion metric to the raw-frame hook — useful even without a callback registered, e.g. as a periodic bus-health check. See [PUBSUB_API.md](PUBSUB_API.md#gettxfailcount) and [PING_MONITORING.md](PING_MONITORING.md) for connection-level monitoring.

## Usage Examples

### Bus Sniffer (Serial Logging)

```cpp
#include <SuperCANBus.h>

CANPubSubBroker broker(CAN);

void setup() {
  Serial.begin(115200);
  CAN.begin(500E3);
  broker.begin();

  broker.onRawFrame([](bool isTx, bool extended, uint32_t id, const uint8_t* data, uint8_t len, bool txOk) {
    Serial.print(isTx ? "TX" : "RX");
    Serial.print(extended ? " EXT" : " STD");
    Serial.print(" id=0x");
    Serial.print(id, HEX);
    Serial.print(" len=");
    Serial.print(len);
    Serial.print(" data=");
    for (uint8_t i = 0; i < len; i++) {
      if (data[i] < 0x10) Serial.print('0');
      Serial.print(data[i], HEX);
      Serial.print(' ');
    }
    if (isTx) {
      Serial.print(" ok=");
      Serial.print(txOk);
    }
    Serial.println();
  });
}

void loop() {
  broker.loop();
}
```

### Forwarding 100% of Traffic to the Cloud (Gateway Pattern)

```cpp
#include <SuperCANBus.h>

CANPubSubBroker broker(CAN);

void logFrameToCloud(bool isTx, bool extended, uint32_t id, const uint8_t* data, uint8_t len, bool txOk) {
  // Buffer/queue the frame and flush it asynchronously to your cloud
  // logging pipeline (e.g. MQTT, HTTP batch upload, etc.). Keep this
  // callback fast — it runs inline with parsePacket()/sendFrame().
  cloudLogQueue.push(isTx, extended, id, data, len, txOk);
}

void setup() {
  Serial.begin(115200);
  CAN.begin(500E3);
  broker.begin();

  broker.onRawFrame(logFrameToCloud);
}

void loop() {
  broker.loop();
  cloudLogQueue.flushIfDue();
}
```

### Periodic TX Health Check

```cpp
void loop() {
  client.loop();

  static unsigned long lastCheck = 0;
  if (millis() - lastCheck >= 60000) {
    uint32_t fails = client.getTxFailCount();
    if (fails > 0) {
      Serial.print("TX failures in the last minute check: ");
      Serial.println(fails);
    }
    lastCheck = millis();
  }
}
```

## Notes

- The hook is purely observational — it never suppresses, modifies, or delays a frame.
- Keep the callback fast: it runs synchronously inside `loop()`/`handleMessage()` (RX) or inside `sendFrame()` (TX). Heavy work (network I/O, flash writes) should be queued and processed elsewhere.
- `data`/`len` describe only the payload of **one physical frame** (max 8 bytes) — reassembled extended-message payloads are not exposed here; use [`onExtendedMessageComplete`-level callbacks](PUBSUB_API.md) (`onMessage`, `onDirectMessage`, etc.) for the reassembled, logical message.
- `getTxFailCount()` counts failures across the node's lifetime and is not reset by `begin()`/`end()`/reconnects.
- Registering `onRawFrame(nullptr)` disables the hook again at any time.

## See Also

- [PUBSUB_API.md](PUBSUB_API.md) - Full API reference, including `sendFrame()`, `onRawFrame()`, `getTxFailCount()`, and `RawFrameCallback`
- [EXTENDED_FRAMES.md](EXTENDED_FRAMES.md) - How multi-frame/extended messages are fragmented into physical frames
- [PING_MONITORING.md](PING_MONITORING.md) - Connection-level (logical) health monitoring
- [API.md](API.md) - Low-level `CANControllerClass` API, including `packetData()`
