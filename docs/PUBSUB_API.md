# Pub/Sub API Reference

Complete API reference for the publish/subscribe protocol in Super CAN+ library.

## Table of Contents

1. [Broker API](#broker-api)
2. [Client API](#client-api)
3. [Base Functions](#base-functions)
4. [Callback Types](#callback-types)
5. [Constants](#constants)

---

## Broker API

### CANPubSubBroker

Class for creating a pub/sub broker on the CAN bus.

#### Constructor

```cpp
CANPubSubBroker(CANControllerClass& can)
```

**Parameters:**
- `can` - Reference to a CAN controller instance (e.g., `CAN`)

**Example:**
```cpp
CANPubSubBroker broker(CAN);
```

---

#### begin()

```cpp
bool begin()
```

Initializes the broker.

**Returns:** `true` on success, `false` on failure

**Example:**
```cpp
if (broker.begin()) {
  Serial.println("Broker started");
}
```

---

#### end()

```cpp
void end()
```

Stops the broker and clears all subscriptions.

---

#### loop()

```cpp
void loop()
```

Processes incoming CAN messages. Must be called frequently in the main loop.

**Example:**
```cpp
void loop() {
  broker.loop();
}
```

---

#### handleMessage()

```cpp
void handleMessage(int packetSize)
```

Handles a received CAN message. Usually called internally by `loop()`.

**Parameters:**
- `packetSize` - Size of the received packet

---

#### onClientConnect()

```cpp
void onClientConnect(ConnectionCallback callback)
```

Register a callback function to be called when a client connects.

**Parameters:**
- `callback` - Function to call with signature `void callback(uint8_t clientId)`

**Example:**
```cpp
broker.onClientConnect([](uint8_t clientId) {
  Serial.print("Client connected: 0x");
  Serial.println(clientId, HEX);
});
```

---

#### onClientDisconnect()

```cpp
void onClientDisconnect(ConnectionCallback callback)
```

Register a callback function to be called when a client disconnects.

**Parameters:**
- `callback` - Function to call with signature `void callback(uint8_t clientId)`

---

#### onPublish()

```cpp
void onPublish(MessageCallback callback)
```

Register a callback function to be called when a message is published.

**Parameters:**
- `callback` - Function with signature `void callback(uint16_t topicHash, const String& topic, const String& message)`

**Example:**
```cpp
broker.onPublish([](uint16_t hash, const String& topic, const String& msg) {
  Serial.print("Published to ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(msg);
});
```

---

#### onDirectMessage()

```cpp
void onDirectMessage(DirectMessageCallback callback)
```

Register a callback function to be called when a direct message is received.

**Parameters:**
- `callback` - Function with signature `void callback(uint8_t senderId, const String& message)`

**Example:**
```cpp
broker.onDirectMessage([](uint8_t senderId, const String& msg) {
  Serial.print("Direct from 0x");
  Serial.print(senderId, HEX);
  Serial.print(": ");
  Serial.println(msg);
});
```

---

#### sendToClient()

```cpp
void sendToClient(uint8_t clientId, uint16_t topicHash, const String& message)
```

Send a message to a specific client on a topic.

**Parameters:**
- `clientId` - Target client ID
- `topicHash` - Topic hash (use `hashTopic()` to generate)
- `message` - Message to send (max ~5 bytes)

**Example:**
```cpp
uint16_t hash = CANPubSubBase::hashTopic("alert");
broker.sendToClient(0x10, hash, "Warning!");
```

---

#### sendDirectMessage()

```cpp
void sendDirectMessage(uint8_t clientId, const String& message)
```

Send a direct message to a specific client.

**Parameters:**
- `clientId` - Target client ID
- `message` - Message to send

**Example:**
```cpp
broker.sendDirectMessage(0x10, "Config updated");
```

---

#### broadcastMessage()

```cpp
void broadcastMessage(uint16_t topicHash, const String& message)
```

Broadcast a message to all subscribers of a topic.

**Parameters:**
- `topicHash` - Topic hash
- `message` - Message to send

**Example:**
```cpp
uint16_t hash = CANPubSubBase::hashTopic("broadcast");
broker.registerTopic("broadcast");
broker.broadcastMessage(hash, "System restart");
```

---

#### getClientCount()

```cpp
uint8_t getClientCount()
```

Get the number of connected clients.

**Returns:** Number of connected clients

---

#### getSubscriptionCount()

```cpp
uint8_t getSubscriptionCount()
```

Get the number of active topic subscriptions.

**Returns:** Number of subscribed topics

---

#### getSubscribers()

```cpp
void getSubscribers(uint16_t topicHash, uint8_t* subscribers, uint8_t* count)
```

Get the list of subscribers for a topic.

**Parameters:**
- `topicHash` - Topic hash to query
- `subscribers` - Array to fill with subscriber IDs
- `count` - Pointer to store the number of subscribers

**Example:**
```cpp
uint8_t subs[10];
uint8_t count;
broker.getSubscribers(hash, subs, &count);
```

---

## Client API

### CANPubSubClient

Class for creating a pub/sub client on the CAN bus.

#### Constructor

```cpp
CANPubSubClient(CANControllerClass& can)
```

**Parameters:**
- `can` - Reference to a CAN controller instance (e.g., `CAN`)

**Example:**
```cpp
CANPubSubClient client(CAN);
```

---

#### begin()

```cpp
bool begin(unsigned long timeout = 5000)
bool begin(const String& serialNumber, unsigned long timeout = 5000)
```

Connect to the broker and request a client ID. If a serial number is provided, the client will receive a persistent ID and automatically restore previous subscriptions.

**Parameters:**
- `serialNumber` - Optional unique identifier for persistent ID assignment
- `timeout` - Connection timeout in milliseconds (default: 5000)

**Returns:** `true` if connected successfully, `false` otherwise

**Example:**
```cpp
// Simple connection (ID assigned sequentially: 1, 2, 3, ...)
if (client.begin(5000)) {
  Serial.println("Connected to broker");
}

// Persistent connection with automatic subscription restoration
String serial = "ESP32_" + String((uint32_t)ESP.getEfuseMac(), HEX);
if (client.begin(serial, 5000)) {
  Serial.print("Connected with persistent ID: ");
  Serial.println(client.getClientId(), DEC);
  // Previous subscriptions automatically restored!
}
```

---

#### end()

```cpp
void end()
```

Disconnect from the broker.

---

#### connect()

```cpp
bool connect(unsigned long timeout = 5000)
```

Connect or reconnect to the broker.

**Parameters:**
- `timeout` - Connection timeout in milliseconds

**Returns:** `true` if connected successfully

---

#### isConnected()

```cpp
bool isConnected()
```

Check if the client is connected to the broker.

**Returns:** `true` if connected, `false` otherwise

---

#### getClientId()

```cpp
uint8_t getClientId()
```

Get the assigned client ID.

**Returns:** Client ID (0xFF if not connected)

---

#### loop()

```cpp
void loop()
```

Process incoming CAN messages. Must be called frequently in the main loop.

**Example:**
```cpp
void loop() {
  client.loop();
}
```

---

#### subscribe()

```cpp
bool subscribe(const String& topic)
```

Subscribe to a topic.

**Parameters:**
- `topic` - Topic name to subscribe to

**Returns:** `true` on success, `false` on failure

**Example:**
```cpp
if (client.subscribe("sensors/temperature")) {
  Serial.println("Subscribed");
}
```

---

#### unsubscribe()

```cpp
bool unsubscribe(const String& topic)
```

Unsubscribe from a topic.

**Parameters:**
- `topic` - Topic name to unsubscribe from

**Returns:** `true` on success, `false` on failure

---

#### publish()

```cpp
bool publish(const String& topic, const String& message)
```

Publish a message to a topic.

**Parameters:**
- `topic` - Topic name
- `message` - Message to publish (max ~5 bytes)

**Returns:** `true` on success, `false` on failure

**Example:**
```cpp
client.publish("sensors/temp", "25.5");
```

---

#### sendDirectMessage()

```cpp
bool sendDirectMessage(const String& message)
```

Send a direct message to the broker.

**Parameters:**
- `message` - Message to send

**Returns:** `true` on success, `false` on failure

**Example:**
```cpp
client.sendDirectMessage("Status request");
```

---

#### ping()

```cpp
bool ping()
```

Send a ping to the broker.

**Returns:** `true` if ping was sent, `false` otherwise

---

#### onMessage()

```cpp
void onMessage(MessageCallback callback)
```

Register a callback for received topic messages.

**Parameters:**
- `callback` - Function with signature `void callback(uint16_t topicHash, const String& topic, const String& message)`

**Example:**
```cpp
client.onMessage([](uint16_t hash, const String& topic, const String& msg) {
  Serial.print("Received [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(msg);
});
```

---

#### onDirectMessage()

```cpp
void onDirectMessage(DirectMessageCallback callback)
```

Register a callback for direct messages.

**Parameters:**
- `callback` - Function with signature `void callback(uint8_t senderId, const String& message)`

---

#### onConnect()

```cpp
void onConnect(void (*callback)())
```

Register a callback for connection events.

**Parameters:**
- `callback` - Function with signature `void callback()`

**Example:**
```cpp
client.onConnect([]() {
  Serial.println("Connected!");
});
```

---

#### onDisconnect()

```cpp
void onDisconnect(void (*callback)())
```

Register a callback for disconnection events.

**Parameters:**
- `callback` - Function with signature `void callback()`

---

#### isSubscribed()

```cpp
bool isSubscribed(const String& topic)
```

Check if subscribed to a topic.

**Parameters:**
- `topic` - Topic name to check

**Returns:** `true` if subscribed, `false` otherwise

---

#### getSubscriptionCount()

```cpp
uint8_t getSubscriptionCount()
```

Get the number of subscribed topics.

**Returns:** Number of subscriptions

---

## Base Functions

### hashTopic()

```cpp
static uint16_t hashTopic(const String& topic)
```

Calculate a 16-bit hash for a topic name.

**Parameters:**
- `topic` - Topic name to hash

**Returns:** 16-bit hash value

**Example:**
```cpp
uint16_t hash = CANPubSubBase::hashTopic("sensors/temp");
```

---

### registerTopic()

```cpp
void registerTopic(const String& topic)
```

Register a topic name with its hash for reverse lookup.

**Parameters:**
- `topic` - Topic name to register

---

### getTopicName()

```cpp
String getTopicName(uint16_t hash)
```

Get the topic name for a hash (if previously registered).

**Parameters:**
- `hash` - Topic hash

**Returns:** Topic name or hex representation if not found

---

## Callback Types

### MessageCallback

```cpp
typedef void (*MessageCallback)(uint16_t topicHash, const String& topic, const String& message)
```

Callback for topic messages.

**Parameters:**
- `topicHash` - Hash of the topic
- `topic` - Topic name (if registered)
- `message` - Message content

---

### DirectMessageCallback

```cpp
typedef void (*DirectMessageCallback)(uint8_t senderId, const String& message)
```

Callback for direct messages.

**Parameters:**
- `senderId` - ID of the sender
- `message` - Message content

---

### ConnectionCallback

```cpp
typedef void (*ConnectionCallback)(uint8_t clientId)
```

Callback for connection events.

**Parameters:**
- `clientId` - ID of the client

---

## Constants

### Message Types

```cpp
#define CAN_PS_SUBSCRIBE      0x01
#define CAN_PS_UNSUBSCRIBE    0x02
#define CAN_PS_PUBLISH        0x03
#define CAN_PS_TOPIC_DATA     0x04
#define CAN_PS_DIRECT_MSG     0x05
#define CAN_PS_PING           0x06
#define CAN_PS_PONG           0x07
#define CAN_PS_ACK            0x08
#define CAN_PS_ID_REQUEST     0xFF
#define CAN_PS_ID_RESPONSE    0xFE
```

### Special IDs

```cpp
#define CAN_PS_BROKER_ID      0x00
#define CAN_PS_UNASSIGNED_ID  0xFF
```

### Configuration

```cpp
#define MAX_SUBSCRIPTIONS       20
#define MAX_SUBSCRIBERS_PER_TOPIC 10
#define MAX_CLIENT_TOPICS       10
```

---

## Complete Example

```cpp
#include <SUPER_CAN.h>

CANPubSubClient client(CAN);

void setup() {
  Serial.begin(115200);
  CAN.begin(500E3);
  
  // Setup callbacks
  client.onConnect([]() {
    Serial.println("Connected!");
    client.subscribe("sensors/temperature");
  });
  
  client.onMessage([](uint16_t hash, const String& topic, const String& msg) {
    Serial.print("Received on ");
    Serial.print(topic);
    Serial.print(": ");
    Serial.println(msg);
  });
  
  // Connect to broker
  if (!client.begin()) {
    Serial.println("Connection failed!");
  }
}

void loop() {
  client.loop();
  
  // Publish periodically
  static unsigned long lastPublish = 0;
  if (millis() - lastPublish > 5000) {
    float temp = readTemperature();
    client.publish("sensors/temperature", String(temp));
    lastPublish = millis();
  }
}

float readTemperature() {
  // Your sensor reading code
  return 25.5;
}
```
