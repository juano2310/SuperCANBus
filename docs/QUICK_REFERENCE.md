# CAN Pub/Sub Quick Reference Card

Quick reference for common operations in the Super CAN+ pub/sub protocol.

---

## Setup & Initialization

### Broker Setup
```cpp
#include <SuperCANBus.h>
CANPubSubBroker broker(CAN);

void setup() {
  CAN.begin(500E3);
  broker.begin();
}

void loop() {
  broker.loop();
}
```

### Client Setup
```cpp
#include <SuperCANBus.h>
CANPubSubClient client(CAN);

void setup() {
  CAN.begin(500E3);
  if (client.begin()) {
    // Connected!
  }
}

void loop() {
  client.loop();
}
```

### Client Setup with Persistent ID ⚡
```cpp
#include <SuperCANBus.h>
CANPubSubClient client(CAN);

void setup() {
  CAN.begin(500E3);
  
  // Always gets same ID with serial number
  String serial = "SENSOR_001";  // Or MAC/chip ID
  if (client.begin(serial)) {
    Serial.print("Persistent ID: 0x");
    Serial.println(client.getClientId(), HEX);
  }
}

void loop() {
  client.loop();
}
```

---

## Common Operations

### Subscribe to Topic
```cpp
client.subscribe("sensors/temperature");
client.subscribe("control/led");
```

### Unsubscribe from Topic
```cpp
client.unsubscribe("sensors/temperature");
```

### Publish Message
```cpp
client.publish("sensors/temperature", "25.5");
client.publish("control/led", "on");
```

### Send Direct Message
```cpp
// To broker (ID 0)
client.sendDirectMessage("status request");

// Peer-to-peer (ID 1-100) ⚡
// Only for clients with permanent IDs
client.sendPeerMessage(2, "Hello peer!");
```

### Unified Messaging Command ⚡
```cpp
// Using serial interface (ClientWithSerial example):
// msg:0:text     → Send to broker
// msg:2:hello    → Send to peer client ID 2
// msg:5:data     → Send to peer client ID 5
```

### Check Connection
```cpp
if (client.isConnected()) {
  // Do something
}
```

---

## Callbacks

### Client: Receive Messages
```cpp
client.onMessage([](uint16_t hash, const String& topic, const String& msg) {
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(msg);
});
```

### Client: Receive Direct Messages
```cpp
client.onDirectMessage([](uint8_t senderId, const String& msg) {
  if (senderId == client.getClientId()) {
    Serial.println("Self-message: " + msg);
  } else if (senderId == 0) {
    Serial.println("Broker: " + msg);
  } else {
    Serial.print("From peer 0x");
    Serial.print(senderId, HEX);
    Serial.print(": ");
    Serial.println(msg);
  }
});
```

### Client: Connection Event
```cpp
client.onConnect([]() {
  Serial.println("Connected!");
  client.subscribe("mytopic");
});
```

### Broker: Client Connect
```cpp
broker.onClientConnect([](uint8_t clientId) {
  Serial.print("Client 0x");
  Serial.print(clientId, HEX);
  Serial.println(" connected");
});
```

### Broker: Receive Publish
```cpp
broker.onPublish([](uint16_t hash, const String& topic, const String& msg) {
  Serial.print("Publish on ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(msg);
});
```

### Broker: Receive Direct Message
```cpp
broker.onDirectMessage([](uint8_t senderId, const String& msg) {
  Serial.print("Direct from 0x");
  Serial.print(senderId, HEX);
  Serial.print(": ");
  Serial.println(msg);
});
```

---

## Broker Operations

### Send to Specific Client
```cpp
uint16_t hash = CANPubSubBase::hashTopic("alert");
broker.sendToClient(0x10, hash, "Warning!");
```

### Send Direct Message
```cpp
broker.sendDirectMessage(0x10, "Config updated");
```

### Broadcast to Topic
```cpp
uint16_t hash = CANPubSubBase::hashTopic("broadcast");
broker.registerTopic("broadcast");
broker.broadcastMessage(hash, "System restart");
```

### Get Statistics
```cpp
uint8_t registered = broker.getRegisteredClientCount(); // Total registered
uint8_t online = broker.getClientCount();               // Currently online
uint8_t topics = broker.getSubscriptionCount();         // Active topics

// Check specific client
if (broker.isClientOnline(5)) {
  Serial.println("Client 5 is online");
}

// Get client's subscription count
uint8_t subs = broker.getClientSubscriptionCount(5);
Serial.print("Client has " + String(subs) + " subscriptions");
```

---

## Message Types Reference

| ID   | Type         | Direction      | Purpose                    |
|------|--------------|----------------|----------------------------|
| 0x01 | SUBSCRIBE    | Client→Broker  | Subscribe to topic         |
| 0x02 | UNSUBSCRIBE  | Client→Broker  | Unsubscribe from topic     |
| 0x03 | PUBLISH      | Client→Broker  | Publish to topic           |
| 0x04 | TOPIC_DATA   | Broker→Client  | Deliver subscribed data    |
| 0x05 | DIRECT_MSG   | Bidirectional  | Direct message             |
| 0x06 | PING         | Client→Broker  | Connection check           |
| 0x07 | PONG         | Broker→Client  | Ping response              |
| 0x08 | ACK          | Bidirectional  | Acknowledgment             |
| 0x09 | PEER_MSG     | Client↔Client  | Peer-to-peer message       |
| 0xFF | ID_REQUEST   | Client→Broker  | Request client ID          |
| 0xFE | ID_RESPONSE  | Broker→Client  | Assign client ID           |

---

## Constants

```cpp
CAN_PS_BROKER_ID       // 0x00 - Broker ID
CAN_PS_UNASSIGNED_ID   // 0xFF - Not connected

MAX_SUBSCRIPTIONS        // 20 - Max topics
MAX_SUBSCRIBERS_PER_TOPIC // 10 - Max subs per topic
MAX_CLIENT_TOPICS        // 10 - Max client subscriptions
```

---

## Common Patterns

### Periodic Publishing
```cpp
void loop() {
  client.loop();
  
  static unsigned long lastPublish = 0;
  if (millis() - lastPublish >= 5000) {
    client.publish("sensors/temp", String(readTemp()));
    lastPublish = millis();
  }
}
```

### Topic-Based Control
```cpp
client.onMessage([](uint16_t h, const String& t, const String& m) {
  if (t == "control/led") {
    digitalWrite(LED_PIN, m == "on" ? HIGH : LOW);
  }
});
client.subscribe("control/led");
```

### Request-Response
```cpp
// Request
client.sendDirectMessage("get_status");

// Response
client.onDirectMessage([](uint8_t id, const String& msg) {
  if (id == 0x00 && msg.startsWith("status:")) {
    // Process status
  }
});
```

### Connection Monitoring
```cpp
void loop() {
  client.loop();
  
  if (!client.isConnected()) {
    Serial.println("Reconnecting...");
    client.connect(5000);
  }
  
  static unsigned long lastPing = 0;
  if (millis() - lastPing >= 10000) {
    client.ping();
    lastPing = millis();
  }
}
```

---

## Troubleshooting Quick Checks

### Connection Failed
```cpp
// ✓ Broker running?
// ✓ CAN bus connected?
// ✓ Baud rates match?
// ✓ Termination resistors?
if (!client.begin(10000)) {  // Try longer timeout
  Serial.println("Failed!");
}
```

### Messages Not Received
```cpp
// ✓ Subscribed to topic?
// ✓ Topic name exact match?
// ✓ Callback registered?
// ✓ loop() called?
client.onMessage(myCallback);
client.subscribe("exact/topic/name");
```

### Message Too Long
```cpp
// Max message size: ~5 bytes
// Use short messages:
client.publish("temp", "25.5");     // ✓ Good
client.publish("temp", "25.5°C");   // ✓ OK
// Not: "Temperature: 25.5 degrees"  // ✗ Too long
```

---

## Wiring Quick Reference

### MCP2515 CAN Module
```
MCP2515    Arduino
VCC    →   5V
GND    →   GND
SCK    →   SCK (13)
MISO   →   MISO (12)
MOSI   →   MOSI (11)
CS     →   10
INT    →   2

CAN_H  →   CAN Bus H
CAN_L  →   CAN Bus L
```

### ESP32 Built-in CAN
```
ESP32      Transceiver
GPIO5  →   TX
GPIO4  →   RX
3V3    →   VCC
GND    →   GND

Transceiver CAN
CAN_H  →   CAN Bus H
CAN_L  →   CAN Bus L
```

### Termination
```
120Ω resistor between CAN_H and CAN_L at each end of bus
```

---

## API Method Summary

### Client Methods
```cpp
bool begin(timeout)              // Connect to broker
bool begin(serial, timeout)      // Connect with persistent ID
void end()                       // Disconnect
bool connect(timeout)            // Reconnect
bool isConnected()               // Check connection
uint8_t getClientId()            // Get assigned ID
void loop()                      // Process messages
bool subscribe(topic)            // Subscribe to topic
bool unsubscribe(topic)          // Unsubscribe
bool publish(topic, msg)         // Publish message
bool sendDirectMessage(msg)      // Send to broker
bool sendPeerMessage(id, msg)    // Send to peer client
bool ping()                      // Ping broker
unsigned long getLastPingTime()  // Get last RTT in ms
bool isSubscribed(topic)         // Check subscription
uint8_t getSubscriptionCount()   // Get sub count
```

### Broker Methods
```cpp
bool begin()                     // Start broker
void end()                       // Stop broker
void loop()                      // Process messages
void sendToClient(id, h, msg)    // Send to client
void sendDirectMessage(id, msg)  // Direct to client
void broadcastMessage(h, msg)    // Broadcast topic
uint8_t getClientCount()         // Get online client count
uint8_t getRegisteredClientCount() // Get registered count
bool isClientOnline(id)          // Check if client online
uint8_t getClientSubscriptionCount(id) // Get client's sub count
uint8_t getSubscriptionCount()   // Get topic count
```

### Utility Functions
```cpp
uint16_t hashTopic(topic)     // Calculate hash
void registerTopic(topic)     // Register name
String getTopicName(hash)     // Get topic name
```

---

## Example: Complete Client

```cpp
#include <SuperCANBus.h>

CANPubSubClient client(CAN);

void setup() {
  Serial.begin(115200);
  CAN.begin(500E3);
  
  // Setup callbacks
  client.onMessage([](uint16_t h, const String& t, const String& m) {
    Serial.print("["); Serial.print(t); Serial.print("]: ");
    Serial.println(m);
  });
  
  client.onConnect([]() {
    Serial.println("Connected!");
    client.subscribe("sensors/temperature");
  });
  
  // Connect
  if (client.begin()) {
    Serial.print("ID: 0x");
    Serial.println(client.getClientId(), HEX);
  }
}

void loop() {
  client.loop();
  
  static unsigned long lastPub = 0;
  if (millis() - lastPub >= 5000) {
    float temp = analogRead(A0) * 0.1;
    client.publish("sensors/temperature", String(temp, 1));
    lastPub = millis();
  }
}
```

---

## Example: Complete Broker

```cpp
#include <SuperCANBus.h>

CANPubSubBroker broker(CAN);

void setup() {
  Serial.begin(115200);
  CAN.begin(500E3);
  broker.begin();
  
  broker.onClientConnect([](uint8_t id) {
    Serial.print("Client 0x");
    Serial.print(id, HEX);
    Serial.println(" connected");
  });
  
  broker.onPublish([](uint16_t h, const String& t, const String& m) {
    Serial.print("["); Serial.print(t); Serial.print("]: ");
    Serial.println(m);
  });
  
  Serial.println("Broker ready");
}

void loop() {
  broker.loop();
}
```

---

## Need More Help?

- 📖 [GETTING_STARTED.md](GETTING_STARTED.md) - Step-by-step tutorial
- 📚 [PUBSUB_API.md](PUBSUB_API.md) - Complete API reference
- 🏗️ [ARCHITECTURE.md](ARCHITECTURE.md) - Architecture diagrams
- 📋 [PUBSUB_PROTOCOL.md](PUBSUB_PROTOCOL.md) - Protocol specification
- 💡 [examples/](examples/) - Working examples

---

**Happy CAN Pub/Sub Coding! 🚀**
