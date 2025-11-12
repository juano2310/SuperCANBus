# Extended CAN Frame Support

## Overview

The SuperCAN library now automatically uses **extended CAN frames** (29-bit identifiers) to support messages larger than 8 bytes. This enhancement eliminates the previous limitation where topic names, serial numbers, and messages were truncated.

## What Changed

### Before
- Maximum message size: **~5 bytes** (after protocol overhead)
- Serial numbers: **Truncated at 8 bytes**
- Topic names: **Limited to short abbreviations**
- Messages: **Severely restricted in length**

### After
- Maximum message size: **128 bytes** per message
- Serial numbers: **Full support up to 32 bytes** (no truncation)
- Topic names: **Full hierarchical paths** (e.g., "sensors/temperature/outdoor/north")
- Messages: **Publish rich data** without truncation
- **Automatic switching**: Library detects size and uses appropriate frame type

## How It Works

### Standard Frames (≤8 bytes)
For messages that fit in a single CAN frame, the library continues to use standard 11-bit CAN IDs for efficiency:

```
Standard CAN Frame:
┌────────────┬─────────────────┐
│  11-bit ID │   0-8 bytes     │
│  (MsgType) │   (Payload)     │
└────────────┴─────────────────┘
```

### Extended Frames (>8 bytes)
When a message exceeds 8 bytes, it's automatically split into multiple frames using extended 29-bit CAN IDs:

```
Extended CAN ID Format:
┌────────────┬────────────┬──────────────┐
│  MsgType   │  FrameSeq  │ TotalFrames  │
│  (8 bits)  │  (8 bits)  │  (13 bits)   │
└────────────┴────────────┴──────────────┘

Each frame carries up to 8 bytes of payload data.
```

### Frame Sequencing Example

Sending a 32-byte serial number `"ESP32-C3-MAC-AA:BB:CC:DD:EE:FF"`:

```
Frame 0: ExtID=0x00FF0004, Data="ESP32-C3" (8 bytes)
Frame 1: ExtID=0x01FF0004, Data="-MAC-AA:" (8 bytes)
Frame 2: ExtID=0x02FF0004, Data="BB:CC:DD" (8 bytes)
Frame 3: ExtID=0x03FF0004, Data=":EE:FF"   (6 bytes)
```

Where:
- `0x00` = Message type (ID_REQUEST)
- `0xFF` = Frame sequence (0, 1, 2, 3)
- `0x0004` = Total frames (4)

## Implementation Details

### Automatic Detection
The library automatically detects when to use extended frames:

```cpp
// In subscribe():
size_t totalSize = 1 + 2 + 1 + topic.length();
if (totalSize > CAN_FRAME_DATA_SIZE) {
    // Use extended frames
    sendExtendedMessage(...);
} else {
    // Use standard frame
    beginPacket(...);
}
```

### Message Fragmentation
The `sendExtendedMessage()` method:
1. Calculates number of frames needed
2. Builds extended CAN ID for each frame
3. Sends frames with small delays (5ms) between them
4. Returns success/failure status

### Message Reassembly
The `processExtendedFrame()` method:
1. Decodes extended CAN ID to extract frame info
2. Buffers incoming frames in sequence
3. Checks for timeouts (1 second)
4. Calls `onExtendedMessageComplete()` when all frames received
5. Handles out-of-order or missing frames

### Timeout Handling
If frames don't arrive within 1 second:
- Incomplete message is discarded
- Buffer is reset for next message
- No error callback (best-effort delivery)

## Use Cases Now Supported

### 1. Long Serial Numbers
```cpp
// Full MAC address as serial number (17 bytes)
String serial = "AA:BB:CC:DD:EE:FF";
client.begin(serial);  // ✓ No truncation

// Device ID with prefix (32 bytes)
String serial = "ESP32-C3-MAC-AA:BB:CC:DD:EE:FF";
client.begin(serial);  // ✓ Fully supported
```

### 2. Descriptive Topic Names
```cpp
// Full hierarchical topic paths
client.subscribe("sensors/temperature/outdoor/north");  // ✓ No abbreviation needed
client.subscribe("building/floor3/room302/hvac/setpoint");  // ✓ Works perfectly

// Publish to long topics
client.publish("devices/esp32/telemetry/voltage", "3.3V");  // ✓ Full topic name
```

### 3. Rich Message Payloads
```cpp
// JSON data
client.publish("sensor/data", "{\"temp\":25.5,\"humidity\":60,\"pressure\":1013}");  // ✓ 50+ bytes

// Comma-separated values
client.publish("readings", "25.5,60.2,1013.25,3.3,12.5,18.2,42.1");  // ✓ Long CSV

// Text messages
client.publish("alerts", "Temperature exceeded threshold in Building A, Floor 3, Room 302");  // ✓ 70+ bytes
```

### 4. Registration with Full Device Info
```cpp
// During client registration with serial number
// Old: Serial truncated at 8 bytes → "ESP32-C3" only
// New: Full serial preserved → "ESP32-C3-MAC-AA:BB:CC:DD:EE:FF"

String macAddr = WiFi.macAddress();
client.begin("ESP32-" + macAddr);  // ✓ Full serial number stored
```

## Performance Considerations

### Latency
- **Standard frame**: ~1ms send time
- **Extended frame**: ~1ms + (5ms × extra frames)
- **Example**: 32-byte message = 4 frames = ~16ms total

### Throughput
- **500 kbps CAN**: ~1.6 KB/s for extended messages
- **250 kbps CAN**: ~0.8 KB/s for extended messages

### Bus Load
- Extended messages use more bus bandwidth
- Consider message frequency when using large payloads
- Multiple small messages may be more efficient than one large message

## Configuration

Adjust these constants in `CANPubSub.h` if needed:

```cpp
#define CAN_FRAME_DATA_SIZE     8   // Standard CAN frame size
#define MAX_EXTENDED_MSG_SIZE   128 // Maximum extended message size
#define EXTENDED_MSG_TIMEOUT    1000 // Timeout in milliseconds
```

## Backward Compatibility

✅ **Fully backward compatible**

- Small messages (<8 bytes) still use standard frames
- No changes needed to existing code
- Broker and clients auto-detect frame type
- Mixed networks with old/new nodes work together

## Examples

### Client Registration with Long Serial
```cpp
#include <SuperCANBus.h>
#include <WiFi.h>

CANPubSubClient client(CAN);

void setup() {
  Serial.begin(115200);
  CAN.begin(500E3);
  
  // Use full MAC address as serial (17 bytes)
  String serial = WiFi.macAddress();
  
  if (client.begin(serial)) {
    Serial.print("Connected with ID: ");
    Serial.println(client.getClientId());
    Serial.print("Serial: ");
    Serial.println(client.getSerialNumber());
    
    // Subscribe to descriptive topic (40+ bytes)
    client.subscribe("building/floor2/room201/temperature/sensor1");
  }
}

void loop() {
  client.loop();
}
```

### Publishing Rich Data
```cpp
void loop() {
  client.loop();
  
  // Publish JSON telemetry (60+ bytes)
  static unsigned long lastPublish = 0;
  if (millis() - lastPublish > 5000) {
    String json = "{\"temp\":25.5,\"hum\":60.2,\"press\":1013,\"volt\":3.3}";
    client.publish("device/telemetry/realtime", json);
    lastPublish = millis();
  }
}
```

## Technical Notes

### Extended ID Encoding
```
Bits 28-21: Message Type (8 bits)
Bits 20-13: Frame Sequence (8 bits, 0-255)
Bits 12-0:  Total Frames (13 bits, up to 8191)
```

### Buffer Management
- Single buffer per node (broker/client)
- Buffer reused for each multi-frame message
- Timeout protection prevents memory leaks
- Maximum buffer size: 128 bytes (configurable)

### Error Handling
- Incomplete messages silently discarded after timeout
- No retransmission (best-effort delivery)
- Applications should implement their own acknowledgment if needed

## Testing

Test extended frame support with:

```cpp
// Test long serial number
String longSerial = "DEVICE-1234567890-ABCDEF-XYZ";
client.begin(longSerial);

// Test long topic subscription
client.subscribe("this/is/a/very/long/topic/path/with/many/levels");

// Test large message publishing
String largeMsg = "This is a test message that exceeds 8 bytes and will use extended frames automatically";
client.publish("test/topic", largeMsg);
```

## Summary

The extended frame support provides:

✅ **Transparent**: Automatic switching, no code changes required  
✅ **Efficient**: Standard frames for small messages, extended only when needed  
✅ **Robust**: Timeout protection and proper reassembly  
✅ **Backward Compatible**: Works with existing code  
✅ **Flexible**: Configurable limits and timeouts  