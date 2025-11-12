# Super CAN Bus - Library

**A modern, robust communication protocol for the age of AI**

An enhanced Arduino library for CAN Bus communication with built-in publish/subscribe protocol.

**Based on the excellent [arduino-CAN](https://github.com/sandeepmistry/arduino-CAN) library by Sandeep Mistry**

## Features

✨ **All original arduino-CAN features** - Full compatibility with existing CAN applications

🔥 **NEW: Pub/Sub Protocol** - Complete publish/subscribe messaging system:
- **Broker-Client Architecture** - Central broker manages topic subscriptions and message routing
- **Topic-Based Messaging** - Publish and subscribe to named topics
- **Direct Messaging** - Send messages directly to the broker or specific clients
- **Automatic Client ID Assignment** - Plug-and-play client connection (sequential IDs: 1, 2, 3, ...)
- **⚡ Persistent ID Assignment** - Clients with serial numbers always get the same ID across reconnections
- **🔄 Automatic Subscription Restoration** - Clients automatically restore subscriptions after power cycles
- **Serial Number Registration** - Register clients using MAC addresses, chip IDs, or custom identifiers
- **💾 Flash Memory Storage** - Client ID mappings, subscriptions, and topic names survive power cycles (ESP32 NVS / Arduino EEPROM)
- **Extended Frame Support** - Handles messages longer than 8 bytes automatically
- **Callback-Based API** - Event-driven message handling
- **Multiple Examples** - Ready-to-use broker, client, and sensor node examples

## Compatible Hardware

* [Microchip MCP2515](http://www.microchip.com/wwwproducts/en/en010406) based boards/shields
  * [Arduino MKR CAN shield](https://store.arduino.cc/arduino-mkr-can-shield)
* [Espressif ESP32](http://espressif.com/en/products/hardware/esp32/overview)'s built-in [SJA1000](https://www.nxp.com/products/analog/interfaces/in-vehicle-network/can-transceiver-and-controllers/stand-alone-can-controller:SJA1000T) compatible CAN controller with an external 3.3V CAN transceiver

### Microchip MCP2515 wiring

| Microchip MCP2515 | Arduino |
| :---------------: | :-----: |
| VCC | 5V |
| GND | GND |
| SCK | SCK |
| SO | MISO |
| SI | MOSI |
| CS | 10 |
| INT | 2 |


`CS` and `INT` pins can be changed by using `CAN.setPins(cs, irq)`. `INT` pin is optional, it is only needed for receive callback mode. If `INT` pin is used, it **must** be interrupt capable via [`attachInterrupt(...)`](https://www.arduino.cc/en/Reference/AttachInterrupt).

**NOTE**: Logic level converters must be used for boards which operate at 3.3V.

### Espressif ESP32 wiring

Requires an external 3.3V CAN transceiver, such as a [TI SN65HVD230](http://www.ti.com/product/SN65HVD230).

| CAN transceiver | ESP32 |
| :-------------: | :---: |
| 3V3 | 3V3 |
| GND | GND |
| CTX | 5 |
| CRX | 4 |

`CTX` and `CRX` pins can be changed by using `CAN.setPins(rx, tx);` but make sure these are the CAN PINs on the board. Not all pins can be CAN.

## Installation

### Using the Arduino IDE Library Manager

1. Choose `Sketch` -> `Include Library` -> `Manage Libraries...`
2. Type `CAN` into the search box.
3. Click the row to select the library.
4. Click the `Install` button to install the library.

### Using Git

```sh
cd ~/Documents/Arduino/libraries/
git clone https://github.com/sandeepmistry/arduino-CAN CAN
```

## Quick Start - Pub/Sub Protocol

### Broker Node

```cpp
#include <SUPER_CAN.h>

CANPubSubBroker broker(CAN);

void setup() {
  Serial.begin(115200);
  CAN.begin(500E3);
  broker.begin();
  
  broker.onPublish([](uint16_t hash, const String& topic, const String& msg) {
    Serial.print("Message on ");
    Serial.print(topic);
    Serial.print(": ");
    Serial.println(msg);
  });
  
  Serial.println("Broker ready!");
}

void loop() {
  broker.loop();
}
```

### Client Node

```cpp
#include <SUPER_CAN.h>

CANPubSubClient client(CAN);

void setup() {
  Serial.begin(115200);
  CAN.begin(500E3);
  
  client.onMessage([](uint16_t hash, const String& topic, const String& msg) {
    Serial.print("Received [");
    Serial.print(topic);
    Serial.print("]: ");
    Serial.println(msg);
  });
  
  if (client.begin()) {
    client.subscribe("sensors/temperature");
    Serial.println("Connected!");
  }
}

void loop() {
  client.loop();
  
  // Publish every 5 seconds
  static unsigned long lastPublish = 0;
  if (millis() - lastPublish > 5000) {
    client.publish("sensors/temperature", "25.5");
    lastPublish = millis();
  }
}
```

### Client with Persistent ID (Serial Number)

```cpp
#include <SUPER_CAN.h>

CANPubSubClient client(CAN);
String SERIAL_NUMBER = "ESP32_ABC123";  // Or use MAC/chip ID

void setup() {
  Serial.begin(115200);
  CAN.begin(500E3);
  
  // Connect with serial number - same ID every time!
  if (client.begin(SERIAL_NUMBER)) {
    Serial.print("Connected with persistent ID: ");
    Serial.println(client.getClientId(), DEC);
    client.subscribe("sensors/temperature");
  }
}

void loop() {
  client.loop();
}
```

## Documentation

- [API.md](API.md) - Original CAN bus API documentation
- [PUBSUB_PROTOCOL.md](PUBSUB_PROTOCOL.md) - Complete pub/sub protocol documentation
- [PUBSUB_API.md](PUBSUB_API.md) - Pub/Sub API reference
- [SERIAL_NUMBER_MANAGEMENT.md](SERIAL_NUMBER_MANAGEMENT.md) - Client ID persistence with serial numbers
- [FLASH_STORAGE.md](FLASH_STORAGE.md) - Flash memory storage guide
- [GETTING_STARTED.md](GETTING_STARTED.md) - Quick start tutorial
- [ARCHITECTURE.md](ARCHITECTURE.md) - System architecture
- [QUICK_REFERENCE.md](QUICK_REFERENCE.md) - Quick reference cheat sheet

## Examples

### Original CAN Examples
See the [arduino-CAN examples](https://github.com/sandeepmistry/arduino-CAN/tree/master/examples) for basic CAN usage.

### Pub/Sub Protocol Examples
- **Broker** - Pub/Sub broker with serial interface
- **Client** - Interactive pub/sub client
- **BrokerWithSerial** - Broker with serial number management and flash storage
- **ClientWithSerial** - Client with persistent ID using serial number
- **SubscriptionRestore** - ⭐ **Demo of automatic subscription restoration** (recommended starting point!)
- **StorageTest** - Flash storage testing and verification
- **Complete** - Combined broker/client example (compile-time selectable)
- **SensorNode** - Real-world sensor node with periodic publishing

## Pub/Sub Protocol Features

### 🎯 Zero-Touch Reconnection

**The killer feature!** Clients with serial numbers get automatic subscription restoration:

```cpp
// First boot
client.begin("ESP32_ABC123");
client.subscribe("sensors/temp");
client.subscribe("alerts/critical");
// Broker stores everything in flash ✓

// After power cycle or reset...
client.begin("ESP32_ABC123");
// ✨ Subscriptions automatically restored!
// ✨ Same client ID! 
// ✨ Ready to receive messages immediately!
// NO manual subscribe() calls needed!
```

### Broker Capabilities
- **Automatic client ID assignment** (sequential: 1, 2, 3, ... displayed in decimal)
- **⚡ Persistent ID management** - Serial number-based client registration
- **💾 Flash memory storage** - Mappings, subscriptions, and topic names survive power loss and resets
- **🔄 Subscription restoration** - Automatically restores subscriptions when clients reconnect **WITHOUT ANY CODE CHANGES**
- **Topic name preservation** - Full topic names stored and restored (not just hashes)
- Topic subscription management with topic name storage
- Message routing to subscribers
- Direct messaging support (supports extended frames for long messages)
- Client connection tracking
- Broadcast messaging
- Client registration/unregistration API
- Query clients by serial number or ID

### Client Capabilities
- **Automatic connection** with ID request
- **⚡ Persistent ID registration** using serial numbers (MAC, chip ID, custom)
- **🔄 Automatic subscription restoration** - Subscriptions restored after power cycle **ZERO CODE CHANGES NEEDED**
- **Reconnection-friendly** - Same ID and subscriptions every time
- **Topic name preservation** - Full topic names maintained across reboots
- Subscribe/unsubscribe to topics
- Publish messages to topics (supports extended frames for long messages)
- Send direct messages to broker (supports extended frames for long messages)
- Automatic ping/pong for connection monitoring with configurable timeouts
- Event callbacks for all message types
- Topic name tracking for readable display

### Message Types
- `SUBSCRIBE` / `UNSUBSCRIBE` - Topic subscription management
- `PUBLISH` - Publish message to topic
- `TOPIC_DATA` - Receive subscribed topic data
- `DIRECT_MSG` - Direct node-to-broker communication
- `PING` / `PONG` - Connection monitoring
- `ACK` - Message acknowledgment

## Configuration

The pub/sub protocol can be configured by modifying constants in `CANPubSub.h`:

```cpp
#define MAX_SUBSCRIPTIONS       20  // Max unique topics
#define MAX_SUBSCRIBERS_PER_TOPIC 10  // Max subscribers per topic
#define MAX_CLIENT_TOPICS       10  // Max topics per client
```

## How It Works

### Basic Connection Flow
1. **Broker starts** and listens for client connections (broker is always ID 0)
2. **Client connects** by requesting an ID from the broker
3. **Broker assigns** a unique ID (starting from 1: first client gets 1, second gets 2, etc.)
4. **Client subscribes** to topics by sending topic hashes
5. **Any client publishes** a message to a topic
6. **Broker routes** the message to all subscribers
7. **Clients receive** messages via callbacks

### Persistent ID Flow (with Serial Number)
1. **Client sends** ID request with serial number (e.g., "ESP32_ABC123")
2. **Broker checks** if serial number is already registered
   - If YES → Returns existing ID from flash memory
   - If NO → Assigns new ID (starting from 1) and stores mapping to flash
3. **Client receives** same ID every time it reconnects
4. **Broker automatically restores** all previous subscriptions for that client
5. **Client receives** subscription notifications with topic names
6. **Client is fully restored** with same ID and all subscriptions intact

Topics are hashed to 16-bit values for efficient CAN bus transmission. Extended frames are automatically used for messages longer than 8 bytes.

## Documentation

- [Getting Started Guide](docs/GETTING_STARTED.md)
- [Quick Reference](docs/QUICK_REFERENCE.md)
- [Pub/Sub API](docs/PUBSUB_API.md)
- [Pub/Sub Protocol](docs/PUBSUB_PROTOCOL.md)
- [Serial Number Management](docs/SERIAL_NUMBER_MANAGEMENT.md)
- [Ping/Pong Connection Monitoring](docs/PING_MONITORING.md)
- [Peer-to-Peer Messaging](docs/PEER_TO_PEER.md)
- [Extended Frames](docs/EXTENDED_FRAMES.md)
- [Flash Storage](docs/FLASH_STORAGE.md)
- [Architecture](docs/ARCHITECTURE.md)

## Comparison: Traditional CAN vs Pub/Sub Protocol

| Feature | Traditional CAN | Pub/Sub Protocol |
|---------|----------------|---------------|
| Message routing | Manual ID management | Automatic topic-based |
| Client IDs | Hard-coded, manual assignment | Dynamic or persistent (serial-based) |
| Reconnection | May conflict with IDs | Same ID guaranteed with serial number |
| Storage | No persistence | Flash memory for ID mappings |
| Scalability | Fixed IDs, complex wiring | Dynamic subscription |
| Flexibility | Hard-coded destinations | Runtime topic subscription |
| Setup complexity | High | Low |
| Code reusability | Low | High |

## Migration from arduino-CAN

This library is fully backward compatible with arduino-CAN. To use the pub/sub features:

1. Replace `#include <CAN.h>` with `#include <SUPER_CAN.h>`
2. Add pub/sub broker or client code
3. All existing CAN code continues to work unchanged

## License

This library is [licensed](LICENSE) under the [MIT Licence](http://en.wikipedia.org/wiki/MIT_License).

Based on [arduino-CAN](https://github.com/sandeepmistry/arduino-CAN) by Sandeep Mistry.

Pub/Sub protocol extensions by Juan Pablo Risso.
