# Serial Number-Based Client ID Management

> **⚡ Persistent ID Assignment** - A game-changing feature that makes your CAN network deployment-ready!

## Overview

The Super CAN+ library includes advanced client ID management using **serial numbers**. This allows nodes to maintain **persistent IDs across reconnections** by registering with unique identifiers such as MAC addresses, chip IDs, or custom serial numbers.

**The Problem:** Traditional CAN networks require manual ID assignment. When a node restarts, it may get a different ID, breaking communication paths and requiring reconfiguration.

**The Solution:** With serial number registration, each node gets the **same ID every time**, stored in flash memory. Perfect for production deployments!

## Key Features

✅ **Persistent ID Assignment** - Same serial number always gets the same client ID (range: 1-100)  
✅ **Flash Memory Storage** - Mappings and subscriptions survive power outages and resets  
✅ **🔄 Automatic Subscription Restoration** - Subscriptions automatically restored on reconnection **NO RE-SUBSCRIBE NEEDED!**  
✅ **Topic Name Preservation** - Full topic names stored and restored (not just hashes)  
✅ **Peer-to-Peer Messaging** - Registered clients can send direct messages to each other  
✅ **Automatic Registration** - First connection automatically registers the client  
✅ **Reconnection Friendly** - Same ID and subscriptions assigned on reconnect  
✅ **Client Management** - Add, edit, remove, and query registered clients  
✅ **Serial Number Lookup** - Find client ID by serial or serial by ID  
✅ **Platform Agnostic** - Uses ESP32 Preferences or Arduino EEPROM  
✅ **Backward Compatible** - Old clients without serial numbers still work (IDs 101+)  
✅ **Extended Frame Support** - Handles long serial numbers (>8 bytes) automatically  

## How It Works

### Registration Flow

```
1. Client Sends:  ID_REQUEST with serial number "ESP32_ABC123"
                  ↓
2. Broker:        Checks if "ESP32_ABC123" is registered
                  ↓
                  YES → Returns existing ID (e.g., 1) + checks for stored subscriptions
                  NO  → Assigns new ID (starting from 1) and stores mapping
                  ↓
3. Broker Sends:  ID_RESPONSE with assigned ID + has_subscriptions flag
                  ↓
4. Client:        Receives ID and waits 200ms for restoration messages
                  ↓
5. Broker:        If has stored subscriptions, sends SUB_RESTORE messages
                  ↓
6. Broker Sends:  SUB_RESTORE messages with topic hash + full topic name
                  ↓
7. Client:        Receives each SUB_RESTORE, rebuilds subscription list
                  ↓
8. Client:        Fully operational - NO manual re-subscription needed!
```

### 🎯 Automatic Subscription Restoration

**This is the killer feature!** When a client with a known serial number reconnects after a power cycle or restart:

1. **Broker remembers everything** - All subscriptions stored in flash
2. **Client gets same ID** - Uses stored serial number mapping
3. **Subscriptions auto-restore** - Broker sends `SUB_RESTORE` messages with full topic names
4. **Client ready immediately** - No need to call `subscribe()` again!

**Example:**
```cpp
// First connection
client.begin("ESP32_ABC123");
client.subscribe("sensors/temperature");
client.subscribe("sensors/humidity");
// Subscriptions stored in broker's flash memory

// Device restarts or loses power...

// Reconnect after restart
client.begin("ESP32_ABC123");
// ✨ Magic! Client automatically has both subscriptions restored
// No need to call subscribe() again!
```

**What gets restored:**
- ✅ Topic hashes (for routing)
- ✅ Full topic names (for display/logging)
- ✅ Client's position in subscription table
- ✅ Ready to receive messages immediately

**Timing details:**
- Client waits 200ms after receiving ID for restoration messages
- Broker sends restoration messages with 15ms spacing
- Typical restoration: < 100ms for 5 subscriptions

### Mapping Table

The broker maintains a table of registered clients **stored in flash memory**:

| Client ID | Serial Number | Status   | Subscriptions  |
|-----------|---------------|----------|----------------|
| 1         | ESP32_ABC123  | Active   | temp, humidity |
| 2         | NODE_001      | Active   | status         |
| 3         | SENSOR_A      | Inactive | sensors/motion |
| 4         | MAC_00:1A:2B  | Active   | alerts         |

- **Client ID**: Unique 8-bit identifier (sequential: 1, 2, 3, ... broker is 0)
- **Serial Number**: String identifier (MAC, UUID, custom) - max 32 chars
- **Status**: Active (currently connected) or Inactive
- **Subscriptions**: Stored topic subscriptions, automatically restored on reconnect
- **Storage**: Automatically saved to flash memory on each change
- **Display**: IDs shown in decimal format (1, 2, 3) not hex (0x01, 0x02, 0x03)

### ID Assignment Ranges

The library uses different ID ranges for different purposes:

| ID Range | Type          | Features                  |
|----------|---------------|---------------------------|
| 0        | Broker        | Special ID for the broker |
| 1-100    | Permanent IDs | Registered clients with serial numbers - support peer-to-peer messaging |
| 101-254  | Temporary IDs | Guest clients without serial numbers - pub/sub only |
| 255      | Unassigned    | Special ID indicating no assignment |

**Permanent IDs (1-100):**
- Assigned to clients registered with serial numbers
- Stored in flash memory and persist across power cycles
- Subscriptions automatically restored on reconnection
- **Can send and receive peer-to-peer messages**
- Same ID assigned every time for the same serial number

**Temporary IDs (101+):**
- Assigned to clients without serial numbers
- Not stored in flash memory
- Different ID on each connection
- Can publish and subscribe to topics
- **Cannot participate in peer-to-peer messaging**
- Subscriptions not restored on reconnection

## API Reference

### Broker Methods

#### registerClient()
```cpp
uint8_t registerClient(const String& serialNumber)
```
Manually register a client with a serial number. Returns assigned client ID.

**Example:**
```cpp
uint8_t id = broker.registerClient("ESP32_ABC123");
Serial.print("Registered client ID: ");
Serial.println(id, DEC);
```

---

#### unregisterClient()
```cpp
bool unregisterClient(uint8_t clientId)
```
Unregister a client by ID. Marks as inactive and removes subscriptions.

**Example:**
```cpp
if (broker.unregisterClient(1)) {
  Serial.println("Client unregistered");
}
```

---

#### unregisterClientBySerial()
```cpp
bool unregisterClientBySerial(const String& serialNumber)
```
Unregister a client by serial number.

**Example:**
```cpp
if (broker.unregisterClientBySerial("ESP32_ABC123")) {
  Serial.println("Client unregistered");
}
```

---

#### getClientIdBySerial()
```cpp
uint8_t getClientIdBySerial(const String& serialNumber)
```
Get client ID for a given serial number. Returns `CAN_PS_UNASSIGNED_ID` (0xFF) if not found.

**Example:**
```cpp
uint8_t id = broker.getClientIdBySerial("ESP32_ABC123");
if (id != CAN_PS_UNASSIGNED_ID) {
  Serial.print("Client ID: ");
  Serial.println(id, DEC);
}
```

---

#### getSerialByClientId()
```cpp
String getSerialByClientId(uint8_t clientId)
```
Get serial number for a given client ID. Returns empty string if not found.

**Example:**
```cpp
String serial = broker.getSerialByClientId(1);
if (serial.length() > 0) {
  Serial.print("Serial: ");
  Serial.println(serial);
}
```

---

#### updateClientSerial()
```cpp
bool updateClientSerial(uint8_t clientId, const String& newSerial)
```
Update the serial number for a client ID. Returns false if new serial already exists.

**Example:**
```cpp
if (broker.updateClientSerial(1, "NEW_SERIAL")) {
  Serial.println("Serial updated");
}
```

---

#### getRegisteredClientCount()
```cpp
uint8_t getRegisteredClientCount()
```
Get total number of registered clients (active + inactive).

**Example:**
```cpp
Serial.print("Total registered: ");
Serial.println(broker.getRegisteredClientCount());
```

---

#### listRegisteredClients()
```cpp
void listRegisteredClients(void (*callback)(uint8_t id, const String& serial, bool active))
```
Iterate through all registered clients with a callback. The `active` parameter indicates if the client is registered (not unregistered). To check if a client is currently online, use `isClientOnline()`.

**Example:**
```cpp
broker.listRegisteredClients([](uint8_t id, const String& serial, bool active) {
  Serial.print("ID: ");
  Serial.print(id, DEC);
  Serial.print(" Serial: ");
  Serial.print(serial);
  Serial.print(" Registered: ");
  Serial.print(active ? "Yes" : "No");
  Serial.print(" Online: ");
  Serial.println(broker.isClientOnline(id) ? "Yes" : "No");
});
```

---

#### isClientOnline()
```cpp
bool isClientOnline(uint8_t clientId)
```
Check if a client is currently online (has recently sent a message to the broker).

**Returns:** `true` if online, `false` if offline

**Example:**
```cpp
if (broker.isClientOnline(5)) {
  Serial.println("Client 5 is online");
}
```

---

#### getClientSubscriptionCount()
```cpp
uint8_t getClientSubscriptionCount(uint8_t clientId)
```
Get the number of topics a client is subscribed to.

**Returns:** Number of subscriptions

**Example:**
```cpp
uint8_t subs = broker.getClientSubscriptionCount(5);
Serial.print("Client 5 has ");
Serial.print(subs);
Serial.println(" subscriptions");
```

---

#### saveMappingsToStorage()
```cpp
bool saveMappingsToStorage()
```
Manually save all client mappings to flash memory. Called automatically on changes.

**Example:**
```cpp
if (broker.saveMappingsToStorage()) {
  Serial.println("Mappings saved to flash");
}
```

---

#### loadMappingsFromStorage()
```cpp
bool loadMappingsFromStorage()
```
Load client mappings from flash memory. Called automatically in `begin()`.

**Example:**
```cpp
if (broker.loadMappingsFromStorage()) {
  Serial.println("Mappings loaded from flash");
}
```

---

#### clearStoredMappings()
```cpp
bool clearStoredMappings()
```
Clear all stored mappings from flash memory and reset the broker.

**Example:**
```cpp
if (broker.clearStoredMappings()) {
  Serial.println("All mappings cleared");
}
```

---

### Client Methods

#### begin() with Serial Number
```cpp
bool begin(const String& serialNumber, unsigned long timeout = 5000)
```
Connect to broker with serial number registration.

**Example:**
```cpp
if (client.begin("ESP32_ABC123", 5000)) {
  Serial.println("Connected with serial number");
}
```

---

#### connect() with Serial Number
```cpp
bool connect(const String& serialNumber, unsigned long timeout = 5000)
```
Connect or reconnect with serial number.

**Example:**
```cpp
if (client.connect("ESP32_ABC123")) {
  Serial.println("Reconnected");
}
```

---

#### getSerialNumber()
```cpp
String getSerialNumber()
```
Get the serial number used for registration.

**Example:**
```cpp
String serial = client.getSerialNumber();
Serial.print("My serial: ");
Serial.println(serial);
```

---

## Usage Examples

### Broker with Serial Number Management

```cpp
#include <SuperCANBus.h>

CANPubSubBroker broker(CAN);

void setup() {
  Serial.begin(115200);
  CAN.begin(500E3);
  broker.begin();
  
  // Callback shows serial number
  broker.onClientConnect([](uint8_t id) {
    String serial = broker.getSerialByClientId(id);
    Serial.print("Client 0x");
    Serial.print(id, HEX);
    Serial.print(" connected (SN: ");
    Serial.print(serial);
    Serial.println(")");
  });
}

void loop() {
  broker.loop();
  
  // List all clients
  if (Serial.available() && Serial.read() == 'l') {
    broker.listRegisteredClients([](uint8_t id, const String& serial, bool active) {
      Serial.print("0x");
      Serial.print(id, HEX);
      Serial.print(" - ");
      Serial.print(serial);
      Serial.print(" [");
      Serial.print(active ? "Active" : "Inactive");
      Serial.println("]");
    });
  }
}
```

### Client with ESP32 Chip ID

```cpp
#include <SuperCANBus.h>

CANPubSubClient client(CAN);
String serialNumber;

void setup() {
  Serial.begin(115200);
  CAN.begin(500E3);
  
  // Generate serial from ESP32 chip ID
  #ifdef ESP32
    uint64_t chipid = ESP.getEfuseMac();
    serialNumber = "ESP32_" + String((uint32_t)(chipid >> 32), HEX) + 
                               String((uint32_t)chipid, HEX);
  #else
    serialNumber = "ARDUINO_001";
  #endif
  
  Serial.print("Serial Number: ");
  Serial.println(serialNumber);
  
  // Connect with serial number
  if (client.begin(serialNumber)) {
    Serial.print("Connected! ID: 0x");
    Serial.println(client.getClientId(), HEX);
  }
}

void loop() {
  client.loop();
}
```

### Client with MAC Address

```cpp
#include <SuperCANBus.h>
#include <WiFi.h>

CANPubSubClient client(CAN);
String serialNumber;

void setup() {
  Serial.begin(115200);
  CAN.begin(500E3);
  
  // Use MAC address as serial number
  uint8_t mac[6];
  WiFi.macAddress(mac);
  serialNumber = "";
  for (int i = 0; i < 6; i++) {
    if (i > 0) serialNumber += ":";
    if (mac[i] < 0x10) serialNumber += "0";
    serialNumber += String(mac[i], HEX);
  }
  serialNumber.toUpperCase();
  
  Serial.print("MAC Address: ");
  Serial.println(serialNumber);
  
  if (client.begin(serialNumber)) {
    Serial.print("Registered with ID: 0x");
    Serial.println(client.getClientId(), HEX);
  }
}

void loop() {
  client.loop();
}
```

## Configuration

```cpp
#define MAX_CLIENT_MAPPINGS 50   // Maximum registered clients
#define MAX_SERIAL_LENGTH 32     // Maximum serial number length
#define STORAGE_NAMESPACE "CANPubSub"  // ESP32 Preferences namespace
#define EEPROM_SIZE 2048         // EEPROM size for Arduino
```

Modify these in `CANPubSub.h` to change storage parameters.

## Storage Implementation

### ESP32 (Preferences/NVS)
The ESP32 version uses the **Preferences library** which stores data in Non-Volatile Storage (NVS):
- Namespace: `"CANPubSub"`
- Survives: Power loss, resets, firmware updates (unless flash is erased)
- Wear leveling: Built-in by ESP32 NVS
- Max writes: ~100,000 cycles per sector

### Arduino (EEPROM)
Arduino boards use **EEPROM** for persistent storage:
- Size: 2048 bytes (configurable)
- Survives: Power loss, resets
- Wear leveling: None (manual management recommended)
- Max writes: ~100,000 cycles for AVR, ~10,000 for ESP8266

### Storage Format
```
Offset | Size | Description
-------|------|------------------
0      | 2    | Magic number (0xCABE)
2      | 1    | Mapping count
3      | 1    | Next client ID
4+     | var  | ClientMapping array
```

Each `ClientMapping` struct:
```cpp
struct ClientMapping {
  uint8_t clientId;           // 1 byte
  char serialNumber[32];      // 32 bytes
  bool active;                // 1 byte
} // Total: 34 bytes per client
```

**Total storage required**: 4 + (34 × MAX_CLIENT_MAPPINGS) bytes  
**Default**: 4 + (34 × 50) = **1704 bytes**

## Serial Number Guidelines

### Good Serial Numbers
- ✅ Unique per device
- ✅ Persistent across resets
- ✅ Human-readable (optional)
- ✅ Reasonable length (< 20 chars)

### Examples
```cpp
"ESP32_AABBCCDD1122"        // ESP32 chip ID
"00:1A:2B:3C:4D:5E"         // MAC address
"SENSOR_TEMP_001"           // Custom naming
"550e8400-e29b-41d4"        // UUID (truncated)
"NODE_BUILDING_A_FLOOR_2"   // Descriptive
```

### Avoid
- ❌ Non-unique IDs (e.g., all devices with "NODE_01")
- ❌ Very long strings (> 30 chars)
- ❌ Random values that change on reset
- ❌ Empty strings

## Backward Compatibility

Old clients without serial numbers still work:

```cpp
// Old method (no serial number)
client.begin();  // Works, gets sequential ID

// New method (with serial number)
client.begin("ESP32_ABC123");  // Gets persistent ID
```

Both can coexist on the same network!

## Benefits

| Feature | Without Serial Numbers | With Serial Numbers |
|---------|------------------------|---------------------|
| ID Range | 101-254 (Temporary) | 1-100 (Permanent) |
| ID Persistence | ❌ New ID on reconnect | ✅ Same ID always |
| Power Cycle Safety | ❌ Lost on power loss | ✅ Survives power loss |
| Client Identification | ❌ Only by ID | ✅ By serial + ID |
| Configuration Management | ❌ Manual tracking | ✅ Automatic |
| **Subscription Restoration** | **❌ Must resubscribe manually** | **✅ Automatic restore - NO CODE CHANGES!** |
| Topic Names | ❌ Lost on restart | ✅ Preserved in flash |
| Peer-to-Peer Messaging | ❌ Not available | ✅ Fully supported |
| Network Administration | ❌ Difficult | ✅ Easy |
| Debugging | ❌ Hard to identify | ✅ Clear identification |
| Hot Swap Support | ❌ Manual reconfiguration | ✅ Automatic recognition |
| Zero-Touch Reconnection | ❌ App must track state | ✅ Broker handles everything |

## Use Cases

### 1. Sensor Network
Each sensor has a unique serial number printed on it. The network administrator knows which ID corresponds to which physical sensor.

### 2. Vehicle CAN Bus
Multiple ECUs with persistent IDs based on hardware serial numbers. Replacement ECUs automatically get new IDs.

### 3. Building Automation
Controllers identified by location ("HVAC_FLOOR_3_ZONE_A"). Easy to identify and manage.

### 4. Industrial Equipment
Equipment with serial plates. The CAN network maps serial numbers to IDs for asset tracking.

## Troubleshooting

### Client gets different ID each time
- Ensure serial number is persistent (not random)
- Check that serial number is passed to `begin()`
- Verify serial number doesn't change on reset
- Check if storage is working: `broker.loadMappingsFromStorage()`

### Mapping table full (50 clients)
- Increase `MAX_CLIENT_MAPPINGS` in `CANPubSub.h`
- Remember to increase `EEPROM_SIZE` accordingly
- Unregister inactive clients

### Serial number not found
- Check exact spelling (case-sensitive)
- Verify client has connected at least once
- Use `broker.listRegisteredClients()` to see all
- Check if mappings were loaded: `broker.getRegisteredClientCount()`

### Mappings lost after power cycle
- ESP32: Check that Preferences library is available
- Arduino: Verify EEPROM library is included
- Check magic number: Should be 0xCABE at address 0
- Try `broker.saveMappingsToStorage()` manually

### Storage corruption
- Call `broker.clearStoredMappings()` to reset
- Power cycle the broker after clearing
- Check EEPROM/flash health (excessive writes)

### Serial number too long
- Maximum 31 characters (32 with null terminator)
- Truncate or hash long identifiers
- Example: Use last 12 chars of MAC address

### Subscriptions not restored on reconnect
- Verify client is using serial number: `client.begin(serialNumber)` not `client.begin()`
- Check broker has subscriptions stored: Use `clients` command in BrokerWithSerial
- Ensure client waits after connection (automatic, but check no early `return`)
- Verify broker called `saveSubscriptionsToStorage()` (automatic on subscribe)
- Check flash memory is working: Use `broker.getRegisteredClientCount()` to verify persistence

### Subscriptions restored but messages not received
- This is normal! Broker table is updated but you need to publish to see messages
- Check broker is forwarding: Use broker's `topics` command to verify subscribers
- Test with: `pub:your_topic:test_message` from broker's serial monitor
- Client should receive message via `onMessage()` callback

## Complete Example

See the following examples for full implementations:
- `BrokerWithSerial.ino` - Broker with client management
- `ClientWithSerial.ino` - Client with serial number

---

This feature makes CAN pub/sub networks easier to manage and more suitable for production deployments!
