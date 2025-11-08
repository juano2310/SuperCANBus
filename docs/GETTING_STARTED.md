# Getting Started with CAN Pub/Sub Protocol

This guide will help you get started with the publish/subscribe protocol in the Super CAN+ library.

## Table of Contents

1. [What You'll Need](#what-youll-need)
2. [Understanding the Architecture](#understanding-the-architecture)
3. [Your First Broker](#your-first-broker)
4. [Your First Client](#your-first-client)
5. [Testing Your Network](#testing-your-network)
6. [Common Patterns](#common-patterns)
7. [Troubleshooting](#troubleshooting)

---

## What You'll Need

### Hardware
- 2 or more Arduino-compatible boards (Arduino Uno, Mega, ESP32, etc.)
- MCP2515 CAN modules (one per board) OR ESP32 with built-in CAN
- CAN transceiver modules (if using MCP2515)
- 120Ω termination resistors (2 required, one at each end of the bus)
- Jumper wires

### Software
- Arduino IDE 1.8.0 or later
- Super CAN+ library installed

### CAN Bus Wiring

Connect all devices on the same CAN bus:

```
Device 1          Device 2          Device 3
[CAN Module]      [CAN Module]      [CAN Module]
  |                 |                 |
  CAN_H -----+------+------+--------- CAN_H
             |             |
            [120Ω]       [120Ω]  (Termination resistors)
             |             |
  CAN_L -----+------+------+--------- CAN_L
```

**Important:** Always use 120Ω termination resistors at both ends of the CAN bus!

---

## Understanding the Architecture

The pub/sub protocol uses a **broker-client** architecture:

```
     ┌─────────────┐
     │   Broker    │  ← Central message router
     │   (Node 1)  │     Manages subscriptions
     └──────┬──────┘     Forwards messages
            │
    ────────┴────────────────  CAN Bus
            │
    ┌───────┼───────┐
    │       │       │
┌───▼───┐ ┌─▼─────┐ ┌─▼──────┐
│Client1│ │Client2│ │Client3 │
│Sensor │ │Display│ │Control │
└───────┘ └───────┘ └────────┘
```

**Broker (1 per network):**
- Assigns unique IDs to clients
- Manages topic subscriptions
- Routes messages to subscribers
- Handles direct messages

**Clients (many per network):**
- Connect to broker
- Subscribe to topics of interest
- Publish messages to topics
- Send direct messages to broker

---

## Your First Broker

### Step 1: Create the Sketch

```cpp
#include <SUPER_CAN.h>

CANPubSubBroker broker(CAN);

void setup() {
  Serial.begin(115200);
  while (!Serial);
  
  Serial.println("=== CAN Pub/Sub Broker ===");
  
  // Start CAN at 500kbps
  if (!CAN.begin(500E3)) {
    Serial.println("CAN init failed!");
    while (1);
  }
  
  // Start broker
  if (!broker.begin()) {
    Serial.println("Broker start failed!");
    while (1);
  }
  
  // Setup callbacks to see what's happening
  broker.onClientConnect([](uint8_t id) {
    Serial.print("Client 0x");
    Serial.print(id, HEX);
    Serial.println(" connected");
  });
  
  broker.onPublish([](uint16_t hash, const String& topic, const String& msg) {
    Serial.print("[");
    Serial.print(topic);
    Serial.print("] ");
    Serial.println(msg);
  });
  
  Serial.println("Broker ready!");
}

void loop() {
  broker.loop();  // Process CAN messages
}
```

### Step 2: Hardware Setup

For **MCP2515**:
- Connect MCP2515 to SPI pins (CS=10, INT=2)
- Connect to CAN bus with termination

For **ESP32**:
- Connect CAN transceiver (TX=5, RX=4)
- Connect to CAN bus with termination

### Step 3: Upload and Test

1. Upload the sketch to your Arduino
2. Open Serial Monitor at 115200 baud
3. You should see "Broker ready!"

---

## Your First Client

### Step 1: Create the Sketch

```cpp
#include <SUPER_CAN.h>

CANPubSubClient client(CAN);

void setup() {
  Serial.begin(115200);
  while (!Serial);
  
  Serial.println("=== CAN Pub/Sub Client ===");
  
  // Start CAN at 500kbps
  if (!CAN.begin(500E3)) {
    Serial.println("CAN init failed!");
    while (1);
  }
  
  // Setup message callback
  client.onMessage([](uint16_t hash, const String& topic, const String& msg) {
    Serial.print("Received [");
    Serial.print(topic);
    Serial.print("]: ");
    Serial.println(msg);
  });
  
  // Connect to broker
  Serial.println("Connecting...");
  if (client.begin(5000)) {
    Serial.print("Connected! ID: 0x");
    Serial.println(client.getClientId(), HEX);
    
    // Subscribe to a topic
    client.subscribe("hello");
  } else {
    Serial.println("Connection failed!");
  }
}

void loop() {
  client.loop();  // Process CAN messages
  
  // Publish every 5 seconds
  static unsigned long lastPublish = 0;
  if (millis() - lastPublish >= 5000) {
    if (client.isConnected()) {
      client.publish("hello", "World!");
      Serial.println("Published!");
    }
    lastPublish = millis();
  }
}
```

### Step 2: Upload and Test

1. Upload to a second Arduino
2. Open Serial Monitor at 115200 baud
3. You should see:
   - "Connected! ID: 0x10" (or similar)
   - "Published!" every 5 seconds
   - "Received [hello]: World!" (from its own messages)

---

## Testing Your Network

### Test 1: Single Client

With one broker and one client:
1. Client should connect and get an ID
2. Client publishes to "hello" topic
3. Client receives its own messages (echo)

### Test 2: Multiple Clients

Add a second client with a different ID. Both clients should:
1. Connect with unique IDs (0x10, 0x11, etc.)
2. Receive messages from each other
3. See all messages on the "hello" topic

### Test 3: Different Topics

Modify clients to subscribe to different topics:

**Client 1:**
```cpp
client.subscribe("temperature");
client.publish("temperature", "25.5");
```

**Client 2:**
```cpp
client.subscribe("humidity");
client.publish("humidity", "60");
```

Each client should only receive messages for its subscribed topics.

---

## Common Patterns

### Pattern 1: Client with Persistent ID

Use serial numbers for persistent client IDs across reconnections:

```cpp
#include <SUPER_CAN.h>

CANPubSubClient client(CAN);
String SERIAL_NUMBER = "SENSOR_001";  // Or use MAC/chip ID

void setup() {
  Serial.begin(115200);
  CAN.begin(500E3);
  
  // Auto-generate serial from ESP32 chip ID
  #ifdef ESP32
    uint64_t chipid = ESP.getEfuseMac();
    SERIAL_NUMBER = "ESP32_" + String((uint32_t)chipid, HEX);
  #endif
  
  // Connect with serial - always gets same ID!
  if (client.begin(SERIAL_NUMBER)) {
    Serial.print("Connected with persistent ID: 0x");
    Serial.println(client.getClientId(), HEX);
  }
}
```

**Benefits:**
- ✅ Same ID every reconnection
- ✅ Survives power cycles
- ✅ No manual ID configuration
- ✅ Broker stores mapping in flash

### Pattern 2: Sensor Node

Publishes data periodically:

```cpp
void loop() {
  client.loop();
  
  static unsigned long lastRead = 0;
  if (millis() - lastRead >= 5000) {
    float temp = readSensor();
    String msg = String(temp, 2);
    client.publish("sensors/temperature", msg);
    lastRead = millis();
  }
}
```

### Pattern 3: Display Node

Subscribes to data and displays it:

```cpp
void setup() {
  // ...
  client.onMessage([](uint16_t hash, const String& topic, const String& msg) {
    if (topic == "sensors/temperature") {
      displayTemperature(msg.toFloat());
    }
  });
  
  client.subscribe("sensors/temperature");
}
```

### Pattern 3: Control Node

Subscribes to commands and executes them:

```cpp
void setup() {
  // ...
  client.onMessage([](uint16_t hash, const String& topic, const String& msg) {
    if (topic == "control/led") {
      if (msg == "on") digitalWrite(LED_PIN, HIGH);
      else if (msg == "off") digitalWrite(LED_PIN, LOW);
    }
  });
  
  client.subscribe("control/led");
}
```

### Pattern 4: Request-Response

Use direct messages for request-response:

```cpp
// Client requests status
client.sendDirectMessage("status");

// Broker responds
broker.onDirectMessage([](uint8_t senderId, const String& msg) {
  if (msg == "status") {
    broker.sendDirectMessage(senderId, "OK");
  }
});
```

---

## Troubleshooting

### Client Can't Connect

**Symptoms:** `begin()` returns false, "Connection failed" message

**Solutions:**
1. ✓ Check broker is running
2. ✓ Verify CAN bus wiring
3. ✓ Check baud rates match (both 500kbps)
4. ✓ Verify termination resistors are installed
5. ✓ Try increasing timeout: `client.begin(10000)`

### Messages Not Received

**Symptoms:** Client connected but not receiving messages

**Solutions:**
1. ✓ Verify subscription: `client.subscribe("topic")`
2. ✓ Check topic names match exactly (case-sensitive)
3. ✓ Ensure `client.loop()` is called frequently
4. ✓ Check message callback is registered

### Intermittent Communication

**Symptoms:** Messages lost occasionally

**Solutions:**
1. ✓ Add termination resistors if missing
2. ✓ Reduce publishing rate
3. ✓ Check wire connections
4. ✓ Reduce message size (max 5 bytes)
5. ✓ Add delays between messages: `delay(10)`

### No Serial Output

**Symptoms:** No messages in Serial Monitor

**Solutions:**
1. ✓ Check baud rate is 115200
2. ✓ Add `while (!Serial);` after `Serial.begin()`
3. ✓ Try different USB port/cable
4. ✓ Verify correct board is selected in Arduino IDE

---

## Next Steps

Now that you have a working CAN pub/sub network:

1. **Explore Examples** - Check the `examples` folder for more patterns
2. **Read Documentation** - See [PUBSUB_API.md](PUBSUB_API.md) for complete API reference
3. **Read Protocol Details** - See [PUBSUB_PROTOCOL.md](PUBSUB_PROTOCOL.md) for protocol specification
4. **Build Your Application** - Create sensor networks, control systems, etc.

### Example Projects

- Multi-sensor data logger
- Distributed control system
- Home automation over CAN
- Vehicle diagnostics network
- Industrial monitoring system

---

## Tips for Success

1. **Start Simple** - Begin with one broker and one client
2. **Use Serial Output** - Print debug messages to understand flow
3. **Test Incrementally** - Add features one at a time
4. **Monitor Traffic** - Use callbacks to see all messages
5. **Keep Messages Short** - CAN frames are limited to 8 bytes
6. **Handle Reconnection** - Check `isConnected()` periodically
7. **Name Topics Clearly** - Use hierarchical names like "sensors/temp"
8. **Test Error Cases** - Unplug cables to test reconnection logic

---

## Need Help?

If you encounter issues:

1. Check the troubleshooting section above
2. Review the example sketches
3. Verify your hardware connections
4. Check the protocol documentation
5. Open an issue on GitHub with:
   - Your hardware setup
   - Your sketch code
   - Serial Monitor output
   - What you expected vs. what happened

Happy CAN pub/sub networking! 🚀
