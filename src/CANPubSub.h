// CANPubSub.h - Publish/Subscribe protocol for CAN bus
// Part of Super CAN+ Library

#ifndef CAN_PS_H
#define CAN_PS_H

#include <Arduino.h>
#include <functional>
#include "CANController.h"

// Platform-specific storage includes
#ifdef ESP32
  #include <Preferences.h>
#else
  #include <EEPROM.h>
#endif

// Message types
#define CAN_PS_SUBSCRIBE      0x01
#define CAN_PS_UNSUBSCRIBE    0x02
#define CAN_PS_PUBLISH        0x03
#define CAN_PS_TOPIC_DATA     0x04
#define CAN_PS_DIRECT_MSG     0x05
#define CAN_PS_PEER_MSG       0x09  // Peer-to-peer message (client to client)
#define CAN_PS_SUB_RESTORE    0x0A  // Broker restores subscription with topic name to client
#define CAN_PS_ID_REQUEST     0xFF
#define CAN_PS_ID_RESPONSE    0xFE
#define CAN_PS_PING           0x06
#define CAN_PS_PONG           0x07
#define CAN_PS_ACK            0x08

#define CAN_PS_BROKER_ID      0x00
#define CAN_PS_UNASSIGNED_ID  0xFF

#define MAX_SUBSCRIPTIONS       20
#define MAX_SUBSCRIBERS_PER_TOPIC 10
#define MAX_CLIENT_TOPICS       10
#define MAX_MESSAGE_CALLBACKS   5
#define MAX_CLIENT_MAPPINGS     50  // Maximum number of registered clients
#define MAX_SERIAL_LENGTH       32  // Maximum length for serial numbers
#define MAX_CONNECTED_CLIENTS   256 // Size of the connected-clients tracking array (client IDs are uint8_t)

// Extended message support (for messages > 8 bytes)
#define CAN_FRAME_DATA_SIZE     8   // Standard CAN frame data size
#define MAX_EXTENDED_MSG_SIZE   128 // Maximum size for extended messages
#define EXTENDED_MSG_TIMEOUT    1000 // Timeout for multi-frame messages (ms)

// Message validation
#define CAN_PS_USE_CHECKSUM     1   // Enable checksum validation for ID_REQUEST messages

// Forward declarations
class CANPubSubBroker;
class CANPubSubClient;

// Extended message buffer structure
struct ExtendedMessageBuffer {
  uint8_t msgType;
  uint8_t senderId;
  uint8_t buffer[MAX_EXTENDED_MSG_SIZE];
  uint16_t receivedSize;
  uint16_t totalSize;
  unsigned long lastFrameTime;
  bool active;
  // Reassembly-session guard: totalFrames and the next expected frame
  // sequence are captured from frame 0 and re-validated on every subsequent
  // frame. Extended CAN IDs only encode [msgType][frameSeq][totalFrames] -
  // they carry no sender/session discriminator - so without this, a second
  // concurrent multi-frame message sharing the same msgType (e.g. two
  // clients both mid-transmission after a shared power-on/reconnect event)
  // can interleave with an in-progress reassembly and silently splice
  // together corrupted-but-plausible-looking output.
  uint8_t totalFrames;
  uint8_t nextFrameSeq;
};

// Callback types
typedef void (*MessageCallback)(uint16_t topicHash, const String& topic, const String& message);
typedef void (*DirectMessageCallback)(uint8_t senderId, const String& message);
typedef void (*ConnectionCallback)(uint8_t clientId);
typedef bool (*RegistrationValidationCallback)(const String& serialNumber); // Returns false to reject registration

// Raw-frame logging hook: fired for every physical CAN frame sent or received,
// before any protocol-level filtering/dispatch. txOk is only meaningful when
// isTx is true (RX calls always pass txOk=true as a not-applicable placeholder).
typedef void (*RawFrameCallback)(bool isTx, bool extended, uint32_t id, const uint8_t* data, uint8_t len, bool txOk);

// Subscription structure for broker
struct Subscription {
  uint16_t topicHash;
  uint8_t subscribers[MAX_SUBSCRIBERS_PER_TOPIC];
  uint8_t subCount;
};

// Topic mapping structure
struct TopicMapping {
  uint16_t hash;
  String name;
};

// Client ID mapping structure (ID <-> Serial Number)
// This structure is stored in flash memory
struct ClientMapping {
  uint8_t clientId;
  char serialNumber[MAX_SERIAL_LENGTH];
  bool registered;  // true = registered and stored in flash, false = unregistered
  
  // Helper methods for String compatibility
  void setSerial(const String& serial) {
    strncpy(serialNumber, serial.c_str(), MAX_SERIAL_LENGTH - 1);
    serialNumber[MAX_SERIAL_LENGTH - 1] = '\0';
  }
  
  String getSerial() const {
    return String(serialNumber);
  }
};

// Runtime ping state (NOT stored in flash - resets on power cycle)
struct ClientPingState {
  uint8_t clientId;
  unsigned long lastPongTime;
  uint8_t missedPings;
};

// Client subscription storage structure
// Stores which topics each client is subscribed to (persists across power cycles)
#define MAX_STORED_SUBS_PER_CLIENT 10
struct ClientSubscriptions {
  uint8_t clientId;
  uint16_t topics[MAX_STORED_SUBS_PER_CLIENT];
  uint8_t topicCount;
};

// Topic name storage structure for persistence
// Stores topic hash -> name mappings in flash memory
#define MAX_TOPIC_NAME_LENGTH 32
#define MAX_STORED_TOPIC_NAMES 20
struct StoredTopicName {
  uint16_t hash;
  char name[MAX_TOPIC_NAME_LENGTH];
  bool active;
  
  void setName(const String& topicName) {
    strncpy(name, topicName.c_str(), MAX_TOPIC_NAME_LENGTH - 1);
    name[MAX_TOPIC_NAME_LENGTH - 1] = '\0';
  }
  
  String getName() const {
    return String(name);
  }
};

// Storage configuration
#define STORAGE_NAMESPACE "CANPubSub"
#define STORAGE_MAGIC 0xCABE        // Magic number to verify valid data
#define STORAGE_SUB_MAGIC 0xCAFF    // Magic number for subscription data
#define STORAGE_TOPIC_MAGIC 0xFEED  // Magic number for topic name data
#define EEPROM_SIZE 8192            // EEPROM size for non-ESP32 platforms (increased for topic names)

// Base pub/sub class
class CANPubSubBase {
public:
  CANPubSubBase(CANControllerClass& can);
  
  // Topic hashing
  static uint16_t hashTopic(const String& topic);
  
  // Topic name management
  void registerTopic(const String& topic);
  String getTopicName(uint16_t hash);

  // Low-level frame send: replicates beginPacket()/write()/endPacket(), but
  // captures the real TX result, tracks failures, and fires the raw-frame hook.
  bool sendFrame(uint32_t id, const uint8_t* data, uint8_t len, bool extended = false);

  // Raw-frame logging hook (sees every physical TX and RX frame). Pass nullptr to disable.
  void onRawFrame(RawFrameCallback callback);

  // Number of sendFrame() calls whose endPacket() reported failure.
  uint32_t getTxFailCount() const;

protected:
  CANControllerClass* _can;
  TopicMapping _topicMappings[MAX_SUBSCRIPTIONS];
  uint8_t _topicMappingCount;

  // Extended message support
  bool sendExtendedMessage(uint8_t msgType, const uint8_t* data, size_t length);
  void processExtendedFrame(int packetSize);
  virtual void onExtendedMessageComplete(uint8_t msgType, uint8_t senderId, const uint8_t* data, size_t length) = 0;

  // Invokes the raw-frame callback if one is registered; cheap no-op otherwise.
  void emitRawFrame(bool isTx, bool extended, uint32_t id, const uint8_t* data, uint8_t len, bool txOk);

  ExtendedMessageBuffer _extBuffer;
  RawFrameCallback _onRawFrame;
  uint32_t _txFailCount;
};

// Pub/Sub Broker class
class CANPubSubBroker : public CANPubSubBase {
public:
  CANPubSubBroker(CANControllerClass& can);
  
  // Initialization
  bool begin();
  void end();
  
  // Main loop processing
  void loop();
  
  // Message handling
  void handleMessage(int packetSize);
  
  // Callbacks
  void onClientConnect(ConnectionCallback callback);
  void onClientDisconnect(ConnectionCallback callback);
  void onPublish(MessageCallback callback);
  void onDirectMessage(DirectMessageCallback callback);
  void onValidateRegistration(RegistrationValidationCallback callback); // Validate serial before registration
  
  // Connection monitoring
  void setPingInterval(unsigned long intervalMs);
  unsigned long getPingInterval();
  void enableAutoPing(bool enable);
  bool isAutoPingEnabled();
  void setMaxMissedPings(uint8_t maxMissed);
  uint8_t getMaxMissedPings();
  
  // Broker operations
  void sendToClient(uint8_t clientId, uint16_t topicHash, const String& message);
  void sendDirectMessage(uint8_t clientId, const String& message);
  void broadcastMessage(uint16_t topicHash, const String& message);
  
  // Statistics
  uint8_t getClientCount();
  uint8_t getSubscriptionCount();
  void getSubscribers(uint16_t topicHash, uint8_t* subscribers, uint8_t* count);
  void listSubscribedTopics(std::function<void(uint16_t hash, const String& name, uint8_t subscriberCount)> callback);
  
  // Client ID mapping management
  uint8_t registerClient(const String& serialNumber);
  bool unregisterClient(uint8_t clientId);
  bool unregisterClientBySerial(const String& serialNumber);
  uint8_t getClientIdBySerial(const String& serialNumber);
  String getSerialByClientId(uint8_t clientId);
  bool updateClientSerial(uint8_t clientId, const String& newSerial);
  uint8_t getRegisteredClientCount();
  void listRegisteredClients(std::function<void(uint8_t id, const String& serial, bool registered)> callback);
  bool isClientOnline(uint8_t clientId);
  uint8_t getClientSubscriptionCount(uint8_t clientId);
  
  // Extended message handling override
  void onExtendedMessageComplete(uint8_t msgType, uint8_t senderId, const uint8_t* data, size_t length) override;
  
  // Persistent storage management
  bool loadMappingsFromStorage();
  bool saveMappingsToStorage();
  bool clearStoredMappings();
  
  // Subscription persistence
  bool loadSubscriptionsFromStorage();
  bool saveSubscriptionsToStorage();
  bool clearStoredSubscriptions();
  void restoreAllSubscriptionsToActiveTable();
  
  // Ping configuration persistence
  bool loadPingConfigFromStorage();
  bool savePingConfigToStorage();
  bool clearStoredPingConfig();
  
  // Topic name persistence
  bool loadTopicNamesFromStorage();
  bool saveTopicNamesToStorage();
  bool clearStoredTopicNames();
  
private:
  // Subscription management
  void addSubscription(uint8_t clientId, uint16_t topicHash);
  void removeSubscription(uint8_t clientId, uint16_t topicHash);
  void removeAllSubscriptions(uint8_t clientId);
  void forwardToSubscribers(uint16_t topicHash, const String& message);
  
  // Client ID management (old method for backward compatibility)
  void assignClientID();
  
  // Client ID management with serial number
  void handleIdRequestWithSerial();
  uint8_t findOrCreateClientId(const String& serialNumber);
  int findClientMapping(const String& serialNumber);
  int findClientMappingById(uint8_t clientId);
  
  // Message handlers
  void handleSubscribe();
  void handleUnsubscribe();
  void handlePublish();
  void handleDirectMessage();
  void handlePeerMessage();
  void handlePing();
  void handlePong();
  
  // Connection monitoring
  void pingAllClients();
  void checkClientTimeouts();
  int findPingState(uint8_t clientId);
  void initPingState(uint8_t clientId);
  void trackClientActivity(uint8_t clientId);
  // Removes clientId from the online/connected tracking (if present) and fires
  // the disconnect callback. Used whenever a mapping is invalidated so a client
  // can never linger in getClientCount() after its registration is gone.
  void removeFromConnectedClients(uint8_t clientId);
  
  // Data members
  Subscription _subscriptions[MAX_SUBSCRIPTIONS];
  uint8_t _subTableSize;
  uint8_t _nextClientID;
  uint8_t _nextTempID;
  uint8_t _connectedClients[MAX_CONNECTED_CLIENTS]; // Track connected clients
  uint8_t _clientCount;
  
  // Ping monitoring
  unsigned long _pingInterval;
  bool _autoPingEnabled;
  uint8_t _maxMissedPings;
  unsigned long _lastPingTime;
  
  // Client ID to Serial Number mapping
  ClientMapping _clientMappings[MAX_CLIENT_MAPPINGS];
  uint8_t _mappingCount;
  
  // Client subscription persistence
  ClientSubscriptions _storedSubscriptions[MAX_CLIENT_MAPPINGS];
  uint8_t _storedSubCount;
  
  // Runtime ping state (separate from persistent storage)
  ClientPingState _pingStates[MAX_CLIENT_MAPPINGS];
  uint8_t _pingStateCount;
  
  // Topic name persistence
  StoredTopicName _storedTopicNames[MAX_STORED_TOPIC_NAMES];
  uint8_t _storedTopicCount;
  
  // Subscription storage helpers
  void storeClientSubscriptions(uint8_t clientId);
  void restoreClientSubscriptions(uint8_t clientId);
  int findStoredSubscription(uint8_t clientId);
  
  // Topic name storage helpers
  void storeTopicName(uint16_t hash, const String& name);
  String getStoredTopicName(uint16_t hash);
  int findStoredTopicName(uint16_t hash);
  
  // Storage helpers
  #ifdef ESP32
  Preferences _preferences;
  #endif
  void initStorage();
  
  // Callbacks
  ConnectionCallback _onClientConnect;
  ConnectionCallback _onClientDisconnect;
  MessageCallback _onPublish;
  DirectMessageCallback _onDirectMessage;
  RegistrationValidationCallback _onValidateRegistration;
};

// Pub/Sub Client class
class CANPubSubClient : public CANPubSubBase {
public:
  CANPubSubClient(CANControllerClass& can);
  
  // Initialization
  bool begin(unsigned long timeout = 5000);
  bool begin(const String& serialNumber, unsigned long timeout = 5000);
  void end();
  
  // Connection
  bool connect(unsigned long timeout = 5000);
  bool connect(const String& serialNumber, unsigned long timeout = 5000);
  bool isConnected();
  uint8_t getClientId();
  String getSerialNumber();
  
  // Main loop processing
  void loop();
  
  // Message handling
  void handleMessage(int packetSize);
  
  // Pub/Sub operations
  bool subscribe(const String& topic);
  bool unsubscribe(const String& topic);
  bool publish(const String& topic, const String& message);
  bool sendDirectMessage(const String& message);
  bool sendPeerMessage(uint8_t targetClientId, const String& message);
  bool ping();
  
  // Callbacks
  void onMessage(MessageCallback callback);
  void onDirectMessage(DirectMessageCallback callback);
  void onConnect(void (*callback)());
  void onDisconnect(void (*callback)());
  
  // Topic management
  bool isSubscribed(const String& topic);
  uint8_t getSubscriptionCount();
  void listSubscribedTopics(std::function<void(uint16_t hash, const String& name)> callback);
  
  // Ping response callback
  void onPong(void (*callback)());
  
  // Get last ping round-trip time in milliseconds (0 if no pong received yet)
  unsigned long getLastPingTime();
  
  // Extended message handling override
  void onExtendedMessageComplete(uint8_t msgType, uint8_t senderId, const uint8_t* data, size_t length) override;
  
private:
  // ID management
  void requestClientID();
  void requestClientIDWithSerial(const String& serialNumber);
  
  // Message handlers
  void handleIdAssignment();
  void handleSubscribeNotification();
  void handleTopicData();
  void handleDirectMessageReceived();
  void handlePong();
  void handleSubscriptionRestore();
  
  // Data members
  uint8_t _clientId;
  bool _connected;
  String _serialNumber;
  uint16_t _subscribedTopics[MAX_CLIENT_TOPICS];
  uint8_t _subscribedTopicCount;
  unsigned long _lastPing;
  unsigned long _lastPong;
  
  // Peer message deduplication
  uint8_t _lastPeerSenderId;
  unsigned long _lastPeerMsgTime;
  char _lastPeerMessage[32];
  
  // Callbacks
  MessageCallback _onMessage;
  DirectMessageCallback _onDirectMessage;
  void (*_onConnect)();
  void (*_onDisconnect)();
  void (*_onPong)();
};

#endif // CAN_PS_H
