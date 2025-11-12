// PeerToPeer.ino - Example of peer-to-peer messaging between clients
// This example demonstrates how registered clients (with serial numbers) 
// can send messages directly to each other
//
// Features:
// - Peer-to-peer messaging between permanent clients
// - Persistent ID using serial numbers
// - 🔄 Automatic subscription restoration (if subscribed to topics)

#include <SuperCANBus.h>

// Pin definitions for MCP2515 (adjust for your setup)
#define CAN_CS_PIN    5
#define CAN_INT_PIN   4

CANControllerClass CAN(CAN_CS_PIN, CAN_INT_PIN, 8000000); // 8MHz crystal
CANPubSubClient client(CAN);

// Configuration
const String MY_SERIAL = "DEVICE_001";  // Change this for each device
const uint8_t TARGET_CLIENT_ID = 2;     // ID of the client to message

unsigned long lastPeerMessage = 0;
const unsigned long PEER_MESSAGE_INTERVAL = 5000; // 5 seconds

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  
  Serial.println("\n=== SuperCAN Peer-to-Peer Messaging Example ===");
  Serial.print("My Serial Number: ");
  Serial.println(MY_SERIAL);
  
  // Initialize CAN bus at 500kbps
  if (!CAN.begin(500E3)) {
    Serial.println("Failed to initialize CAN bus!");
    while (1) delay(100);
  }
  
  Serial.println("CAN bus initialized at 500kbps");
  
  // Set up message callback
  client.onDirectMessage(onPeerMessage);
  
  // Connect with serial number to get permanent ID
  Serial.println("Connecting to broker...");
  if (client.begin(MY_SERIAL, 5000)) {
    Serial.print("Connected! Client ID: ");
    Serial.println(client.getClientId());
    
    // Check if subscriptions were restored
    uint8_t subCount = client.getSubscriptionCount();
    if (subCount > 0) {
      Serial.print("\n🔄 Restored ");
      Serial.print(subCount);
      Serial.println(" subscription(s) from previous session");
    }
    
    Serial.println("\nPeer-to-peer messaging enabled!");
    Serial.println("Only clients with permanent IDs (1-100) can use this feature.");
    Serial.println("Temporary clients (101+) cannot send or receive peer messages.\n");
    Serial.println("Note: Same ID and subscriptions preserved across reconnections!");
  } else {
    Serial.println("Failed to connect to broker!");
    while (1) delay(100);
  }
}

void loop() {
  client.loop();
  
  // Send periodic peer message
  if (millis() - lastPeerMessage > PEER_MESSAGE_INTERVAL) {
    lastPeerMessage = millis();
    
    // Check if we have a permanent ID (< 101)
    if (client.getClientId() < 101) {
      String message = "Hello from " + MY_SERIAL + " at " + String(millis());
      
      Serial.print("Sending peer message to client ");
      Serial.print(TARGET_CLIENT_ID);
      Serial.print(": ");
      Serial.println(message);
      
      if (client.sendPeerMessage(TARGET_CLIENT_ID, message)) {
        Serial.println("✓ Message sent successfully");
      } else {
        Serial.println("✗ Failed to send message");
      }
    } else {
      Serial.println("⚠ Cannot send peer messages - temporary ID");
    }
  }
}

void onPeerMessage(uint8_t senderId, const String& message) {
  // Check if message is from self
  if (senderId == client.getClientId()) {
    Serial.println("\n--- Self Message Received ---");
    Serial.print("From: My own ID (");
    Serial.print(senderId);
    Serial.println(")");
  } else {
    Serial.println("\n--- Peer Message Received ---");
    Serial.print("From Client ID: ");
    Serial.println(senderId);
  }
  
  Serial.print("Message: ");
  Serial.println(message);
  Serial.println("-----------------------------\n");
  
  // Optional: Send a reply
  // String reply = "Thanks for your message!";
  // client.sendPeerMessage(senderId, reply);
}
