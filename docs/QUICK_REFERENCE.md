# CAN MQTT Quick Reference Card

Quick reference for common operations in the Super CAN+ MQTT protocol.

---

## Setup & Initialization

### Broker Setup
```cpp
#include <SUPER_CAN.h>
CANMqttBroker broker(CAN);

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
#include <SUPER_CAN.h>
CANMqttClient client(CAN);

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
client.sendDirectMessage("status request");
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
  Serial.print("From 0x");
  Serial.print(senderId, HEX);
  Serial.print(": ");
  Serial.println(msg);
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
uint16_t hash = CANMqttBase::hashTopic("alert");
broker.sendToClient(0x10, hash, "Warning!");
```

### Send Direct Message
```cpp
broker.sendDirectMessage(0x10, "Config updated");
```

### Broadcast to Topic
```cpp
uint16_t hash = CANMqttBase::hashTopic("broadcast");
broker.registerTopic("broadcast");
broker.broadcastMessage(hash, "System restart");
```

### Get Statistics
```cpp
uint8_t clients = broker.getClientCount();
uint8_t topics = broker.getSubscriptionCount();
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
| 0xFF | ID_REQUEST   | Client→Broker  | Request client ID          |
| 0xFE | ID_RESPONSE  | Broker→Client  | Assign client ID           |

---

## Constants

```cpp
CAN_MQTT_BROKER_ID       // 0x00 - Broker ID
CAN_MQTT_UNASSIGNED_ID   // 0xFF - Not connected

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
bool begin(timeout)           // Connect to broker
void end()                    // Disconnect
bool connect(timeout)         // Reconnect
bool isConnected()            // Check connection
uint8_t getClientId()         // Get assigned ID
void loop()                   // Process messages
bool subscribe(topic)         // Subscribe to topic
bool unsubscribe(topic)       // Unsubscribe
bool publish(topic, msg)      // Publish message
bool sendDirectMessage(msg)   // Send to broker
bool ping()                   // Ping broker
bool isSubscribed(topic)      // Check subscription
uint8_t getSubscriptionCount() // Get sub count
```

### Broker Methods
```cpp
bool begin()                  // Start broker
void end()                    // Stop broker
void loop()                   // Process messages
void sendToClient(id, h, msg) // Send to client
void sendDirectMessage(id, msg) // Direct to client
void broadcastMessage(h, msg) // Broadcast topic
uint8_t getClientCount()      // Get client count
uint8_t getSubscriptionCount() // Get topic count
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
#include <SUPER_CAN.h>

CANMqttClient client(CAN);

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
#include <SUPER_CAN.h>

CANMqttBroker broker(CAN);

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
- 📚 [MQTT_API.md](MQTT_API.md) - Complete API reference
- 🏗️ [ARCHITECTURE.md](ARCHITECTURE.md) - Architecture diagrams
- 📋 [MQTT_PROTOCOL.md](MQTT_PROTOCOL.md) - Protocol specification
- 💡 [examples/](examples/) - Working examples

---

**Happy CAN MQTT Coding! 🚀**
