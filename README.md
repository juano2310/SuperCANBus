````markdown
# SuperCAN Library

An enhanced Arduino library for CAN bus communication with built-in MQTT-like publish/subscribe protocol.

**Based on the excellent [arduino-CAN](https://github.com/sandeepmistry/arduino-CAN) library by Sandeep Mistry**

## Features

✨ **All original arduino-CAN features** - Full compatibility with existing CAN applications

🔥 **NEW: MQTT-like Protocol** - Complete publish/subscribe messaging system:
- **Broker-Client Architecture** - Central broker manages topic subscriptions and message routing
- **Topic-Based Messaging** - Publish and subscribe to named topics
- **Direct Messaging** - Send messages directly to the broker or specific clients
- **Automatic Client ID Assignment** - Plug-and-play client connection
- **Serial Number Registration** - Persistent client IDs based on unique identifiers
- **Flash Memory Storage** - Client mappings survive power cycles (ESP32 NVS / Arduino EEPROM)
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

`CTX` and `CRX` pins can be changed by using `CAN.setPins(rx, tx)`.

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

## Quick Start - MQTT Protocol

### Broker Node

```cpp
#include <SUPER_CAN.h>

CANMqttBroker broker(CAN);

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

CANMqttClient client(CAN);

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

## Documentation

- [API.md](API.md) - Original CAN bus API documentation
- [MQTT_PROTOCOL.md](MQTT_PROTOCOL.md) - Complete MQTT protocol documentation
- [MQTT_API.md](MQTT_API.md) - MQTT API reference
- [SERIAL_NUMBER_MANAGEMENT.md](SERIAL_NUMBER_MANAGEMENT.md) - Client ID persistence with serial numbers
- [FLASH_STORAGE.md](FLASH_STORAGE.md) - Flash memory storage guide
- [GETTING_STARTED.md](GETTING_STARTED.md) - Quick start tutorial
- [ARCHITECTURE.md](ARCHITECTURE.md) - System architecture
- [QUICK_REFERENCE.md](QUICK_REFERENCE.md) - Quick reference cheat sheet

## Examples

### Original CAN Examples
See the [arduino-CAN examples](https://github.com/sandeepmistry/arduino-CAN/tree/master/examples) for basic CAN usage.

### MQTT Protocol Examples
- **CANMqttBroker** - MQTT broker with serial interface
- **CANMqttClient** - Interactive MQTT client
- **CANMqttBrokerWithSerial** - Broker with serial number management and flash storage
- **CANMqttClientWithSerial** - Client with persistent ID using serial number
- **CANMqttStorageTest** - Flash storage testing and verification
- **CANMqttComplete** - Combined broker/client example (compile-time selectable)
- **CANMqttSensorNode** - Real-world sensor node with periodic publishing

## MQTT Protocol Features

### Broker Capabilities
- Automatic client ID assignment
- Serial number-based persistent client IDs
- Flash memory storage (survives power loss)
- Topic subscription management
- Message routing to subscribers
- Direct messaging support
- Client connection tracking
- Broadcast messaging
- Client registration/unregistration API

### Client Capabilities
- Automatic connection with ID request
- Persistent ID with serial number registration
- Subscribe/unsubscribe to topics
- Publish messages to topics
- Send direct messages to broker
- Ping/pong for connection monitoring
- Event callbacks for all message types

### Message Types
- `SUBSCRIBE` / `UNSUBSCRIBE` - Topic subscription management
- `PUBLISH` - Publish message to topic
- `TOPIC_DATA` - Receive subscribed topic data
- `DIRECT_MSG` - Direct node-to-broker communication
- `PING` / `PONG` - Connection monitoring
- `ACK` - Message acknowledgment

## Configuration

The MQTT protocol can be configured by modifying constants in `CANMqtt.h`:

```cpp
#define MAX_SUBSCRIPTIONS       20  // Max unique topics
#define MAX_SUBSCRIBERS_PER_TOPIC 10  // Max subscribers per topic
#define MAX_CLIENT_TOPICS       10  // Max topics per client
```

## How It Works

1. **Broker starts** and listens for client connections
2. **Client connects** by requesting an ID from the broker
3. **Broker assigns** a unique ID (0x10-0xFE)
4. **Client subscribes** to topics by sending topic hashes
5. **Any client publishes** a message to a topic
6. **Broker routes** the message to all subscribers
7. **Clients receive** messages via callbacks

Topics are hashed to 16-bit values for efficient CAN bus transmission.

## Comparison: Traditional CAN vs MQTT Protocol

| Feature | Traditional CAN | MQTT Protocol |
|---------|----------------|---------------|
| Message routing | Manual ID management | Automatic topic-based |
| Scalability | Fixed IDs, complex wiring | Dynamic subscription |
| Flexibility | Hard-coded destinations | Runtime topic subscription |
| Setup complexity | High | Low |
| Code reusability | Low | High |

## Migration from arduino-CAN

This library is fully backward compatible with arduino-CAN. To use the MQTT features:

1. Replace `#include <CAN.h>` with `#include <SUPER_CAN.h>`
2. Add MQTT broker or client code
3. All existing CAN code continues to work unchanged

## License

This library is [licensed](LICENSE) under the [MIT Licence](http://en.wikipedia.org/wiki/MIT_License).

Based on [arduino-CAN](https://github.com/sandeepmistry/arduino-CAN) by Sandeep Mistry.

MQTT protocol extensions by Juan Pablo Risso.

````
