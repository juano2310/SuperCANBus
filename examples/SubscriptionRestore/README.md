# Subscription Restoration Demo

This example demonstrates the **automatic subscription restoration** feature of the SuperCAN library. When a client with a persistent serial number reconnects after a power cycle or reset, all its previous subscriptions are automatically restored - no manual re-subscription needed!

## Features Demonstrated

- ✅ **Automatic subscription restoration** on reconnect
- ✅ **Topic name preservation** across power cycles
- ✅ **Zero-code reconnection** - works transparently
- ✅ **Flash memory persistence** - survives power loss

## How It Works

### First Boot
1. Client connects with unique serial number
2. Client subscribes to topics (e.g., "sensors/temperature")
3. Broker stores subscriptions in flash memory
4. Client ready to receive messages

### After Reset/Power Cycle
1. Client reconnects with same serial number
2. Broker recognizes client and sends **SUB_RESTORE** messages
3. Client automatically rebuilds subscription list
4. Client immediately ready - **NO manual subscribe() calls needed!**

## Testing Steps

### 1. Setup
- Upload `BrokerWithSerial.ino` to broker node
- Upload this sketch to client node
- Both nodes on same CAN bus (500 kbps)

### 2. First Run
```
Open serial monitor - you'll see:
✓ Connected! Client ID: 1
First connection detected.
Subscribing to demo topics...
✓ Subscribed to: sensors/temperature
✓ Subscribed to: sensors/humidity
✓ Subscribed to: status/system
✓ Subscribed to: alerts/critical
```

### 3. Test Restoration
Press **RESET** button on the client Arduino

Watch the magic happen:
```
✓ Connected! Client ID: 1

╔═══════════════════════════════════════════════╗
║  🎉 SUBSCRIPTION RESTORATION SUCCESSFUL!       ║
╚═══════════════════════════════════════════════╝

Restored 4 subscription(s) from broker:
  ✓ sensors/temperature (0x1234)
  ✓ sensors/humidity (0x5678)
  ✓ status/system (0x9abc)
  ✓ alerts/critical (0xdef0)

Client is ready to receive messages!
No manual re-subscription needed! 🚀
```

## Serial Commands

Type these in the serial monitor:

- `status` - Show connection and subscription status
- `reset` - Instructions for testing restoration
- `help` - Show command list

## Expected Behavior

### First Boot
- Client subscribes to 4 topics
- Broker saves subscriptions to flash
- Client publishes test messages every 10 seconds

### After Reset
- Client connects instantly
- Subscriptions automatically restored
- Messages received immediately
- **No subscribe() calls in code!**

## Key Points

1. **Serial Number Required**: Client must use `client.begin(serialNumber)` not `client.begin()`
2. **Flash Storage**: Broker stores mappings and subscriptions in flash memory (ESP32 NVS or Arduino EEPROM)
3. **Timing**: Client waits 200ms after connection for restoration messages
4. **Topic Names**: Full topic names preserved, not just hashes

## Use Cases

This feature is essential for:
- **IoT Sensors** - Survive power glitches
- **Industrial Systems** - Hot-swap components
- **Remote Nodes** - Reduce configuration overhead
- **Battery-Powered** - Fast reconnect, less power
- **Production Deployments** - Zero-touch operation

## Troubleshooting

**Subscriptions not restored?**
- Check serial number is persistent (not random each boot)
- Verify broker has flash storage working
- Use broker's `clients` command to check stored mappings
- Ensure client ID is < 101 (permanent ID range)

**Client gets different ID each time?**
- Serial number might be changing
- Check `SERIAL_NUMBER` variable value
- For ESP32, use chip ID (built-in)
- For Arduino, use fixed string

## Learn More

See documentation:
- [SERIAL_NUMBER_MANAGEMENT.md](../../docs/SERIAL_NUMBER_MANAGEMENT.md) - Full serial number feature guide
- [PUBSUB_PROTOCOL.md](../../docs/PUBSUB_PROTOCOL.md) - Protocol details including SUB_RESTORE
- [FLASH_STORAGE.md](../../docs/FLASH_STORAGE.md) - Storage implementation details

## License

MIT License - See library LICENSE file
