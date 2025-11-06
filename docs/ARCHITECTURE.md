# CAN MQTT Architecture Diagrams

Visual reference for understanding the CAN MQTT protocol architecture.

## Network Topology

```
┌─────────────────────────────────────────────────────────────┐
│                     CAN Bus Network                         │
│                                                             │
│  ┌──────────────┐                                           │
│  │   Broker     │  • Assigns client IDs (0x10-0xFE)         │
│  │   Node       │  • Manages subscriptions                  │
│  │  (ID 0x00)   │  • Routes messages                        │
│  └──────┬───────┘  • Handles direct messages                │
│         │                                                   │
│    ─────┴─────────────────────────────────────────────      │
│         │           CAN Bus                                 │
│    ─────┬─────────────────────────────────────────────      │
│         │                                                   │
│    ┌────┴────┬────────────┬────────────┬──────────┐         │
│    │         │            │            │          │         │
│ ┌──▼───┐ ┌──▼───┐   ┌───▼────┐  ┌───▼────┐ ┌───▼────┐       │
│ │Client│ │Client│   │ Client │  │ Client │ │ Client │       │
│ │ 0x10 │ │ 0x11 │   │  0x12  │  │  0x13  │ │  0x14  │       │
│ │Sensor│ │Light │   │Display │  │Control │ │ Logger │       │
│ └──────┘ └──────┘   └────────┘  └────────┘ └────────┘       │
│                                                             │
│  Each client can:                                           │
│  • Subscribe to multiple topics                             │
│  • Publish to any topic                                     │
│  • Send direct messages to broker                           │
└─────────────────────────────────────────────────────────────┘
```

## Connection Sequence

```
Client                          Broker
  │                               │
  │                               │
  │────ID_REQUEST (0xFF)──────────▶│
  │                               │
  │                               │ Assign unique ID
  │                               │ (0x10, 0x11, ...)
  │                               │
  │◀───ID_RESPONSE (0xFE)─────────│
  │    [assigned_id]              │
  │                               │
  │  ✓ Connected                  │
  │  ✓ Ready to subscribe         │
  │                               │
```

## Subscription Flow

```
Client                          Broker
  │                               │
  │────SUBSCRIBE (0x01)───────────▶│
  │    [client_id]                │
  │    [topic_hash_h]             │
  │    [topic_hash_l]             │
  │                               │
  │                               │ Store subscription:
  │                               │ Topic 0xABCD → [0x10, ...]
  │                               │
  │  ✓ Subscribed                 │
  │                               │
```

## Publish-Subscribe Flow

```
Publisher (0x10)         Broker                 Subscriber (0x11)
     │                     │                          │
     │                     │                          │
     │──PUBLISH (0x03)─────▶│                          │
     │  [0x10]             │                          │
     │  [topic_hash]       │                          │
     │  [message]          │                          │
     │                     │                          │
     │                     │ Find subscribers         │
     │                     │ for topic hash           │
     │                     │                          │
     │                     │──TOPIC_DATA (0x04)──────▶│
     │                     │  [0x11]                  │
     │                     │  [topic_hash]            │
     │                     │  [message]               │
     │                     │                          │
     │                     │                          │
     │                     │  (Repeat for each        │
     │                     │   subscriber)            │
```

## Direct Messaging Flow

```
Client                          Broker
  │                               │
  │────DIRECT_MSG (0x05)──────────▶│
  │    [client_id]                │
  │    [message_data]             │
  │                               │
  │                               │ Process message
  │                               │
  │◀───ACK (0x08)─────────────────│
  │    [broker_id]                │
  │    [client_id]                │
  │    "ACK"                      │
  │                               │
```

## Ping-Pong Flow

```
Client                          Broker
  │                               │
  │────PING (0x06)────────────────▶│
  │    [client_id]                │
  │                               │
  │                               │
  │◀───PONG (0x07)────────────────│
  │    [broker_id]                │
  │    [client_id]                │
  │                               │
  │  Connection verified          │
  │                               │
```

## Message Format

### Standard Message Structure

```
┌─────────────────────────────────────────┐
│         CAN Frame (11-bit ID)           │
├─────────────────────────────────────────┤
│ ID Field: Message Type (0x01-0xFF)     │
├─────────────────────────────────────────┤
│ Data[0]:   First payload byte           │
│ Data[1]:   Second payload byte          │
│ Data[2]:   Third payload byte           │
│ Data[3]:   Fourth payload byte          │
│ Data[4]:   Fifth payload byte           │
│ Data[5]:   Sixth payload byte           │
│ Data[6]:   Seventh payload byte         │
│ Data[7]:   Eighth payload byte          │
└─────────────────────────────────────────┘
```

### SUBSCRIBE Message (0x01)

```
┌──────────────────────────┐
│ CAN ID: 0x01             │
├──────────────────────────┤
│ Data[0]: Client ID       │
│ Data[1]: Topic Hash (H)  │
│ Data[2]: Topic Hash (L)  │
└──────────────────────────┘
```

### PUBLISH Message (0x03)

```
┌────────────────────────────┐
│ CAN ID: 0x03               │
├────────────────────────────┤
│ Data[0]: Publisher ID      │
│ Data[1]: Topic Hash (H)    │
│ Data[2]: Topic Hash (L)    │
│ Data[3-7]: Message Data    │
│            (up to 5 bytes) │
└────────────────────────────┘
```

### TOPIC_DATA Message (0x04)

```
┌────────────────────────────┐
│ CAN ID: 0x04               │
├────────────────────────────┤
│ Data[0]: Target Client ID  │
│ Data[1]: Topic Hash (H)    │
│ Data[2]: Topic Hash (L)    │
│ Data[3-7]: Message Data    │
│            (up to 5 bytes) │
└────────────────────────────┘
```

### DIRECT_MSG Message (0x05)

```
┌────────────────────────────┐
│ CAN ID: 0x05               │
├────────────────────────────┤
│ Data[0]: Sender ID         │
│ Data[1-7]: Message Data    │
│            (up to 7 bytes) │
└────────────────────────────┘
```

## Topic Hashing

```
Input: "sensors/temperature"
         ↓
    Hash Function:
    hash = 0
    for each char:
        hash = hash * 31 + char
         ↓
Output: 0xABCD (16-bit hash)
```

Example topic hashes:
- `"hello"` → 0x5E918D9F → 0x8D9F (truncated to 16-bit)
- `"sensors/temp"` → 0x1234
- `"control/led"` → 0x5678

## Broker Subscription Table

```
┌─────────────────────────────────────────────────┐
│         Broker Subscription Table               │
├─────────────────────────────────────────────────┤
│                                                 │
│  Topic Hash: 0x1234 ("sensors/temp")           │
│  Subscribers: [0x10, 0x11, 0x14]               │
│  Count: 3                                       │
│                                                 │
│  ─────────────────────────────────────────      │
│                                                 │
│  Topic Hash: 0x5678 ("control/led")            │
│  Subscribers: [0x12]                           │
│  Count: 1                                       │
│                                                 │
│  ─────────────────────────────────────────      │
│                                                 │
│  Topic Hash: 0xABCD ("status")                 │
│  Subscribers: [0x10, 0x11, 0x12, 0x13, 0x14]  │
│  Count: 5                                       │
│                                                 │
└─────────────────────────────────────────────────┘
```

## State Diagram - Client

```
        ┌─────────────┐
        │ DISCONNECTED│
        └──────┬──────┘
               │
               │ begin()
               ▼
        ┌─────────────┐
        │ CONNECTING  │ ← Send ID_REQUEST
        └──────┬──────┘   Wait for response
               │
               │ Receive ID_RESPONSE
               ▼
        ┌─────────────┐
    ┌──▶│  CONNECTED  │◀──┐
    │   └──────┬──────┘   │
    │          │           │
    │          │ Can:      │
    │          │ • subscribe()
    │          │ • publish()
    │          │ • sendDirectMessage()
    │          │ • ping()
    │          │           │
    │          ▼           │
    │   ┌─────────────┐   │
    │   │  OPERATING  │───┘
    │   └──────┬──────┘
    │          │
    │          │ Connection lost
    │          ▼
    └────┌─────────────┐
         │ RECONNECTING│
         └─────────────┘
```

## Example Network Scenarios

### Scenario 1: Temperature Monitoring

```
┌────────────┐     ┌──────────┐     ┌────────────┐
│  Sensor    │────▶│  Broker  │────▶│  Display   │
│  (0x10)    │     │  (0x00)  │     │  (0x11)    │
└────────────┘     └──────────┘     └────────────┘

Sensor publishes:  "sensors/temp" → "25.5"
Display receives:  "sensors/temp" ← "25.5"
```

### Scenario 2: Multi-Sensor Dashboard

```
┌──────────┐
│  Temp    │─┐
│ (0x10)   │ │
└──────────┘ │
             │    ┌──────────┐    ┌────────────┐
┌──────────┐ ├───▶│  Broker  │───▶│ Dashboard  │
│ Humidity │ │    │  (0x00)  │    │  (0x14)    │
│ (0x11)   │ │    └──────────┘    └────────────┘
└──────────┘ │
             │
┌──────────┐ │
│ Pressure │─┘
│ (0x12)   │
└──────────┘

Dashboard subscribes to:
• "sensors/temperature"
• "sensors/humidity"
• "sensors/pressure"
```

### Scenario 3: Control System

```
┌────────────┐     ┌──────────┐     ┌────────────┐
│  Control   │────▶│  Broker  │────▶│   Light    │
│  Panel     │     │  (0x00)  │     │   (0x11)   │
│  (0x10)    │     └────┬─────┘     └────────────┘
└────────────┘          │
                        │           ┌────────────┐
                        └──────────▶│   Heater   │
                                    │   (0x12)   │
                                    └────────────┘

Control publishes: "control/light" → "on"
                   "control/heater" → "75"
```

## Performance Characteristics

```
┌────────────────────────────────────────────────┐
│  Network Capacity @ 500 kbps                   │
├────────────────────────────────────────────────┤
│                                                │
│  Message size:    3-8 bytes (typical 5-6)     │
│  Frame overhead:  ~50 bits per message        │
│  Effective rate:  ~4000 messages/second       │
│  Practical rate:  ~1000 messages/second       │
│                   (with bus arbitration)      │
│                                                │
│  Latency:        < 1 ms (typical)             │
│  Max nodes:      255 (0x10-0xFF)              │
│  Max topics:     20 (configurable)            │
│  Max subs/topic: 10 (configurable)            │
│                                                │
└────────────────────────────────────────────────┘
```

## Best Practices Visualization

```
✅ DO:                          ❌ DON'T:

┌─────────────────┐            ┌─────────────────┐
│ Short Messages  │            │ Long Messages   │
│ "25.5"          │            │ "Temperature is │
│ "ON"            │            │  currently at   │
│ "75"            │            │  25.5 degrees"  │
└─────────────────┘            └─────────────────┘

┌─────────────────┐            ┌─────────────────┐
│ Clear Topics    │            │ Vague Topics    │
│ sensors/temp    │            │ data            │
│ control/led     │            │ topic1          │
│ status/system   │            │ x               │
└─────────────────┘            └─────────────────┘

┌─────────────────┐            ┌─────────────────┐
│ Regular Pings   │            │ No Monitoring   │
│ Every 10s       │            │ Assume OK       │
│ Check response  │            │ Hope it works   │
└─────────────────┘            └─────────────────┘
```

---

## Summary

This architecture provides:
- ✅ Scalable pub/sub messaging
- ✅ Automatic client management
- ✅ Efficient topic-based routing
- ✅ Direct communication channel
- ✅ Connection monitoring
- ✅ Easy to understand and use

Perfect for building distributed CAN bus systems!
