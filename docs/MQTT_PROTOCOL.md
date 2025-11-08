# CAN Pub/Sub Protocol Documentation

## Overview

The Super CAN+ library includes a complete publish/subscribe protocol implementation for CAN bus networks. This protocol enables efficient topic-based messaging between multiple nodes with a central broker managing subscriptions and message routing.

## Architecture

The protocol follows a broker-client architecture:

- **Broker**: Central node that manages client connections, topic subscriptions, and routes messages
- **Clients**: Nodes that can publish messages, subscribe to topics, and communicate with the broker

## Message Types

The protocol defines the following message types (sent as CAN packet IDs):

| Type | ID | Description |
|------|------|-------------|
| SUBSCRIBE | 0x01 | Client subscribes to a topic |
| UNSUBSCRIBE | 0x02 | Client unsubscribes from a topic |
| PUBLISH | 0x03 | Client publishes message to topic |
| TOPIC_DATA | 0x04 | Broker forwards message to subscriber |
| DIRECT_MSG | 0x05 | Direct message between nodes |
| PING | 0x06 | Client pings broker |
| PONG | 0x07 | Broker responds to ping |
| ACK | 0x08 | Acknowledgment message |
| ID_REQUEST | 0xFF | Client requests ID assignment |
| ID_RESPONSE | 0xFE | Broker assigns client ID |

## Protocol Flow

### 1. Client Connection

#### Standard Connection (Auto-assigned ID)

```
Client                  Broker
  |                       |
  |---ID_REQUEST (0xFF)-->|
  |                       |
  |<--ID_RESPONSE (0xFE)--|
  |    [assigned_id]      |
  |                       |
```

#### ⚡ Persistent ID Connection (Serial Number)

When connecting with a serial number, the client sends the serial number in the ID_REQUEST:

```
Client                  Broker
  |                       |
  |---ID_REQUEST (0xFF)-->|
  | [serial_number]       |  Broker checks flash storage 💾
  |                       |  - Found: Returns saved ID
  |<--ID_RESPONSE (0xFE)--|  - New: Assigns and saves ID
  |    [persistent_id]    |
  |                       |
```

**Benefits**:
- Same client ID across reboots and reconnections
- Stored in flash memory (ESP32 NVS / Arduino EEPROM)
- Enables stateful applications and predictable routing

**Example**:
```cpp
// Use MAC address as serial number
String serial = WiFi.macAddress();
if (client.begin(serial)) {
  // Same ID every time this device connects
  Serial.print("Connected with ID: ");
  Serial.println(client.getClientId());
}
```

### 2. Topic Subscription

```
Client                  Broker
  |                       |
  |---SUBSCRIBE (0x01)--->|
  | [client_id]           |
  | [topic_hash_h]        |
  | [topic_hash_l]        |
  |                       |
```

### 3. Message Publishing

```
Publisher              Broker              Subscriber
    |                    |                      |
    |--PUBLISH (0x03)--->|                      |
    | [publisher_id]     |                      |
    | [topic_hash]       |                      |
    | [message_data]     |                      |
    |                    |---TOPIC_DATA (0x04)->|
    |                    |  [subscriber_id]     |
    |                    |  [topic_hash]        |
    |                    |  [message_data]      |
    |                    |                      |
```

### 4. Direct Messaging

```
Client                  Broker
  |                       |
  |--DIRECT_MSG (0x05)--->|
  | [sender_id]           |
  | [message_data]        |
  |                       |
  |<------ACK (0x08)------|
  | [broker_id]           |
  | [sender_id]           |
  | "ACK"                 |
  |                       |
```

## Topic Hashing

Topics are converted to 16-bit hashes using a simple hash function:

```cpp
uint16_t hash = 0;
for (each character in topic) {
    hash = hash * 31 + character;
}
```

This allows topic names to be efficiently transmitted over CAN bus while maintaining reasonable uniqueness.

## API Reference

### Broker API

#### CANMqttBroker(CANControllerClass& can)
Constructor - Creates a broker instance.

#### bool begin()
Initializes the broker. Returns true on success.

#### void loop()
Processes incoming CAN messages. Call this in your main loop.

#### void onClientConnect(ConnectionCallback callback)
Register callback for client connection events.

#### void onPublish(MessageCallback callback)
Register callback for published messages.

#### void onDirectMessage(DirectMessageCallback callback)
Register callback for direct messages.

#### void sendToClient(uint8_t clientId, uint16_t topicHash, const String& message)
Send a message to a specific client on a topic.

#### void sendDirectMessage(uint8_t clientId, const String& message)
Send a direct message to a client.

#### void broadcastMessage(uint16_t topicHash, const String& message)
Broadcast a message to all subscribers of a topic.

### Client API

#### CANMqttClient(CANControllerClass& can)
Constructor - Creates a client instance.

#### bool begin(unsigned long timeout = 5000)
Connects to broker with specified timeout. Returns true on success.

#### bool isConnected()
Returns true if connected to broker.

#### uint8_t getClientId()
Returns the assigned client ID.

#### void loop()
Processes incoming CAN messages. Call this in your main loop.

#### bool subscribe(const String& topic)
Subscribe to a topic. Returns true on success.

#### bool unsubscribe(const String& topic)
Unsubscribe from a topic. Returns true on success.

#### bool publish(const String& topic, const String& message)
Publish a message to a topic. Returns true on success.

#### bool sendDirectMessage(const String& message)
Send a direct message to the broker. Returns true on success.

#### bool ping()
Send a ping to the broker. Returns true on success.

#### void onMessage(MessageCallback callback)
Register callback for received messages.

#### void onDirectMessage(DirectMessageCallback callback)
Register callback for direct messages.

#### void onConnect(void (*callback)())
Register callback for connection events.

## Usage Examples

### Basic Broker

```cpp
#include <SUPER_CAN.h>

CANMqttBroker broker(CAN);

void setup() {
  CAN.begin(500E3);
  broker.begin();
  
  broker.onPublish([](uint16_t hash, const String& topic, const String& msg) {
    Serial.print("Publish on ");
    Serial.print(topic);
    Serial.print(": ");
    Serial.println(msg);
  });
}

void loop() {
  broker.loop();
}
```

### Basic Client

```cpp
#include <SUPER_CAN.h>

CANMqttClient client(CAN);

void setup() {
  CAN.begin(500E3);
  
  client.onMessage([](uint16_t hash, const String& topic, const String& msg) {
    Serial.print("Received on ");
    Serial.print(topic);
    Serial.print(": ");
    Serial.println(msg);
  });
  
  if (client.begin()) {
    client.subscribe("sensors/temp");
  }
}

void loop() {
  client.loop();
  
  // Publish periodically
  static unsigned long lastPublish = 0;
  if (millis() - lastPublish > 5000) {
    client.publish("sensors/temp", "25.5");
    lastPublish = millis();
  }
}
```

### ⚡ Client with Persistent ID

```cpp
#include <SUPER_CAN.h>
#ifdef ESP32
  #include <WiFi.h>
#endif

CANMqttClient client(CAN);

void setup() {
  CAN.begin(500E3);
  
  client.onMessage([](uint16_t hash, const String& topic, const String& msg) {
    Serial.print("Received on ");
    Serial.print(topic);
    Serial.print(": ");
    Serial.println(msg);
  });
  
  // Connect with MAC address as serial number
  #ifdef ESP32
    String serial = WiFi.macAddress();
  #else
    String serial = "ARDUINO_001";  // Custom serial
  #endif
  
  if (client.begin(serial)) {  // 💾 Same ID every reconnection
    Serial.print("Connected with persistent ID: ");
    Serial.println(client.getClientId());
    client.subscribe("sensors/temp");
  }
}

void loop() {
  client.loop();
  
  // Publish periodically
  static unsigned long lastPublish = 0;
  if (millis() - lastPublish > 5000) {
    client.publish("sensors/temp", "25.5");
    lastPublish = millis();
  }
}
```

## Configuration Limits

The following limits are defined and can be modified in `CANMqtt.h`:

- `MAX_SUBSCRIPTIONS` - Maximum number of unique topics (default: 20)
- `MAX_SUBSCRIBERS_PER_TOPIC` - Maximum subscribers per topic (default: 10)
- `MAX_CLIENT_TOPICS` - Maximum topics a client can subscribe to (default: 10)

## Best Practices

1. **Keep messages short**: CAN frames are limited to 8 bytes of data
2. **Use topic hashing**: The library automatically handles topic hashing
3. **Handle reconnection**: Clients should monitor connection status and reconnect if needed
4. **Avoid message storms**: Add delays between rapid message publishing
5. **Use callbacks**: Register callbacks for efficient event handling
6. **Topic naming**: Use hierarchical naming like "sensors/temperature"
7. **⚡ Use persistent IDs**: Connect with serial numbers for production deployments
   - Maintains same client ID across reboots
   - Enables predictable message routing
   - Required for stateful applications
   - Use device-unique identifiers (MAC address, chip ID, etc.)

## Limitations

1. **Message size**: Limited to ~5 bytes per message (after protocol overhead)
2. **Topic collisions**: Hash collisions are possible but rare with the 16-bit hash
3. **No QoS levels**: Messages are best-effort delivery only
4. **No message persistence**: Messages are not stored by the broker
5. **Single broker**: The protocol supports one broker per CAN bus

## Troubleshooting

### Client can't connect
- Ensure broker is running
- Check CAN bus wiring and termination
- Verify baud rates match (both should use same rate, e.g., 500kbps)

### Messages not received
- Verify subscription was successful
- Check topic names match exactly (case-sensitive)
- Ensure both nodes are on the same CAN bus

### Lost messages
- Reduce publishing rate
- Check for CAN bus errors
- Verify proper termination resistors

## Future Enhancements

Possible future additions to the protocol:
- Quality of Service (QoS) levels
- Message persistence
- Last Will and Testament (LWT)
- Retained messages
- Multi-broker support with bridging
- Encryption and authentication

## License

This pub/sub protocol implementation is part of the Super CAN+ library and inherits the same MIT license as the original arduino-CAN library.
