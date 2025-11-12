// CANPubSub.cpp - Publish/Subscribe protocol for CAN bus
// Part of Super CAN+ Library

#include "CANPubSub.h"

// ===== CANPubSubBase Implementation =====

CANPubSubBase::CANPubSubBase(CANControllerClass& can) : _can(&can), _topicMappingCount(0) {
  memset(&_extBuffer, 0, sizeof(_extBuffer));
}

uint16_t CANPubSubBase::hashTopic(const String& topic) {
  uint16_t hash = 0;
  for (unsigned int i = 0; i < topic.length(); i++) {
    hash = hash * 31 + topic.charAt(i);
  }
  return hash;
}

void CANPubSubBase::registerTopic(const String& topic) {
  uint16_t hash = hashTopic(topic);
  
  // Check if already registered in runtime mapping
  for (uint8_t i = 0; i < _topicMappingCount; i++) {
    if (_topicMappings[i].hash == hash) {
      return; // Already registered
    }
  }
  
  // Add new mapping to runtime table
  if (_topicMappingCount < MAX_SUBSCRIPTIONS) {
    _topicMappings[_topicMappingCount].hash = hash;
    _topicMappings[_topicMappingCount].name = topic;
    _topicMappingCount++;
  }
}

String CANPubSubBase::getTopicName(uint16_t hash) {
  for (uint8_t i = 0; i < _topicMappingCount; i++) {
    if (_topicMappings[i].hash == hash) {
      return _topicMappings[i].name;
    }
  }
  return String("0x") + String(hash, HEX);
}

bool CANPubSubBase::sendExtendedMessage(uint8_t msgType, const uint8_t* data, size_t length) {
  if (length <= CAN_FRAME_DATA_SIZE) {
    // Single frame - use standard packet
    _can->beginPacket(msgType);
    _can->write(data, length);
    return _can->endPacket() == 1;
  }
  
  // Multi-frame message using extended CAN IDs
  // Extended ID format: [8-bit msgType][8-bit frameSeq][13-bit reserved/totalFrames]
  uint8_t totalFrames = (length + CAN_FRAME_DATA_SIZE - 1) / CAN_FRAME_DATA_SIZE;
  
  for (uint8_t frame = 0; frame < totalFrames; frame++) {
    uint8_t frameSize = min((size_t)CAN_FRAME_DATA_SIZE, length - (frame * CAN_FRAME_DATA_SIZE));
    
    // Build extended ID: [msgType][frameSeq][totalFrames]
    long extId = ((long)msgType << 21) | ((long)frame << 13) | totalFrames;
    
    _can->beginExtendedPacket(extId);
    _can->write(data + (frame * CAN_FRAME_DATA_SIZE), frameSize);
    
    if (_can->endPacket() != 1) {
      return false;
    }
    
    delay(5); // Small delay between frames to prevent bus congestion
  }
  
  return true;
}

void CANPubSubBase::processExtendedFrame(int packetSize) {
  if (!_can->packetExtended()) {
    return; // Not an extended frame
  }
  
  long extId = _can->packetId();
  
  // Decode extended ID: [msgType][frameSeq][totalFrames]
  uint8_t msgType = (extId >> 21) & 0xFF;
  uint8_t frameSeq = (extId >> 13) & 0xFF;
  uint8_t totalFrames = extId & 0x1FFF;
  
  // Check for timeout on existing buffer
  if (_extBuffer.active && (millis() - _extBuffer.lastFrameTime > EXTENDED_MSG_TIMEOUT)) {
    // Timeout - discard incomplete message
    memset(&_extBuffer, 0, sizeof(_extBuffer));
  }
  
  // First frame - initialize buffer
  if (frameSeq == 0) {
    memset(&_extBuffer, 0, sizeof(_extBuffer));
    _extBuffer.msgType = msgType;
    _extBuffer.totalSize = totalFrames * CAN_FRAME_DATA_SIZE; // Approximate
    _extBuffer.active = true;
    
    // Read sender ID if available (first byte)
    if (_can->available() > 0) {
      _extBuffer.senderId = _can->read();
      packetSize--;
    }
  }
  
  // Verify this frame belongs to current message
  if (!_extBuffer.active || _extBuffer.msgType != msgType) {
    return; // Frame doesn't match current message
  }
  
  // Read frame data
  while (_can->available() && _extBuffer.receivedSize < MAX_EXTENDED_MSG_SIZE) {
    _extBuffer.buffer[_extBuffer.receivedSize++] = _can->read();
  }
  
  _extBuffer.lastFrameTime = millis();
  
  // Check if message is complete
  if (frameSeq == totalFrames - 1) {
    // Message complete - notify subclass
    onExtendedMessageComplete(_extBuffer.msgType, _extBuffer.senderId, 
                              _extBuffer.buffer, _extBuffer.receivedSize);
    
    // Reset buffer
    memset(&_extBuffer, 0, sizeof(_extBuffer));
  }
}

// ===== CANPubSubBroker Implementation =====

CANPubSubBroker::CANPubSubBroker(CANControllerClass& can) 
  : CANPubSubBase(can),
    _subTableSize(0),
    _nextClientID(0x01),
    _nextTempID(101),
    _clientCount(0),
    _mappingCount(0),
    _storedSubCount(0),
    _storedTopicCount(0),
    _pingStateCount(0),
    _pingInterval(5000),
    _autoPingEnabled(false),
    _maxMissedPings(2),
    _lastPingTime(0),
    _onClientConnect(nullptr),
    _onClientDisconnect(nullptr),
    _onPublish(nullptr),
    _onDirectMessage(nullptr) {
  memset(_subscriptions, 0, sizeof(_subscriptions));
  memset(_connectedClients, 0, sizeof(_connectedClients));
  memset(_clientMappings, 0, sizeof(_clientMappings));
  memset(_storedSubscriptions, 0, sizeof(_storedSubscriptions));
  memset(_storedTopicNames, 0, sizeof(_storedTopicNames));
  memset(_pingStates, 0, sizeof(_pingStates));
}

bool CANPubSubBroker::begin() {
  _subTableSize = 0;
  _nextClientID = 0x01;
  _nextTempID = 101;
  _clientCount = 0;  // All clients start as offline after power cycle
  _mappingCount = 0;
  _storedSubCount = 0;
  _storedTopicCount = 0;
  _pingStateCount = 0;  // Clear ping states - will be reinitialized when clients connect
  _topicMappingCount = 0;  // Clear runtime topic name mappings - will be repopulated from storage
  
  // Initialize storage and load saved mappings
  initStorage();
  loadMappingsFromStorage();
  loadSubscriptionsFromStorage();
  loadTopicNamesFromStorage();  // Load topic names from flash
  loadPingConfigFromStorage();
  
  // Restore all stored subscriptions to active table
  restoreAllSubscriptionsToActiveTable();
  
  // If auto-ping was enabled, initialize ping states for registered clients
  // Note: This only sets up ping tracking, clients are NOT marked as online
  // Clients will be marked online when they send their first message
  if (_autoPingEnabled) {
    for (uint8_t i = 0; i < _mappingCount; i++) {
      if (_clientMappings[i].registered) {
        initPingState(_clientMappings[i].clientId);
      }
    }
    
    // Immediately ping all registered clients after power-up to discover who's online
    // Give a small delay for CAN bus to stabilize
    delay(100);
    pingAllClients();
    _lastPingTime = millis();
  }
  
  return true;
}

void CANPubSubBroker::end() {
  _subTableSize = 0;
  _clientCount = 0;
}

void CANPubSubBroker::loop() {
  int packetSize = _can->parsePacket();
  if (packetSize > 0) {
    handleMessage(packetSize);
  }
  
  // Auto-ping clients if enabled
  if (_autoPingEnabled && (millis() - _lastPingTime >= _pingInterval)) {
    pingAllClients();
    checkClientTimeouts();
    _lastPingTime = millis();
  }
}

void CANPubSubBroker::handleMessage(int packetSize) {
  // Check for extended frames first
  if (_can->packetExtended()) {
    processExtendedFrame(packetSize);
    return;
  }
  
  uint8_t msgType = _can->packetId();
  
  switch (msgType) {
    case CAN_PS_SUBSCRIBE:
      handleSubscribe();
      break;
    case CAN_PS_UNSUBSCRIBE:
      handleUnsubscribe();
      break;
    case CAN_PS_PUBLISH:
      handlePublish();
      break;
    case CAN_PS_DIRECT_MSG:
      handleDirectMessage();
      break;
    case CAN_PS_PEER_MSG:
      handlePeerMessage();
      break;
    case CAN_PS_PING:
      handlePing();
      break;
    case CAN_PS_PONG:
      handlePong();
      break;
    case CAN_PS_ID_REQUEST:
      // Check if this is a request with serial number (has data)
      if (_can->available() > 0) {
        handleIdRequestWithSerial();
      } else {
        // Assign temporary ID for clients without serial numbers
        // These IDs are not persistent and won't be saved to storage
        assignClientID();
      }
      break;
  }
}

void CANPubSubBroker::handleSubscribe() {
  if (_can->available() < 3) return;
  
  uint8_t clientId = _can->read();
  uint16_t topicHash = (_can->read() << 8) | _can->read();
  
  // Track client activity (marks as online)
  trackClientActivity(clientId);
  
  // Read topic name if available (for broker-side topic mapping)
  String topicName = "";
  if (_can->available() > 0) {
    uint8_t topicLen = _can->read();
    for (uint8_t i = 0; i < topicLen && _can->available(); i++) {
      topicName += (char)_can->read();
    }
  }
  
  // Register topic name if provided
  if (topicName.length() > 0) {
    registerTopic(topicName);
    // Also persist topic name to flash storage
    storeTopicName(topicHash, topicName);
  }
  
  addSubscription(clientId, topicHash);
}

void CANPubSubBroker::handleUnsubscribe() {
  if (_can->available() < 3) return;
  
  uint8_t clientId = _can->read();
  uint16_t topicHash = (_can->read() << 8) | _can->read();
  
  // Track client activity (marks as online)
  trackClientActivity(clientId);
  
  removeSubscription(clientId, topicHash);
}

void CANPubSubBroker::handlePublish() {
  if (_can->available() < 3) return;
  
  uint8_t publisherId = _can->read();
  uint16_t topicHash = (_can->read() << 8) | _can->read();
  
  // Track client activity (marks as online)
  trackClientActivity(publisherId);
  
  // Get topic name from stored mapping (learned from SUBSCRIBE)
  String topicName = getTopicName(topicHash);
  
  // Read message (all remaining data)
  String message = "";
  while (_can->available()) {
    message += (char)_can->read();
  }
  
  // Call callback if registered
  if (_onPublish) {
    _onPublish(topicHash, topicName, message);
  }
  
  // Forward to subscribers
  forwardToSubscribers(topicHash, message);
}

void CANPubSubBroker::handleDirectMessage() {
  if (_can->available() < 1) return;
  
  uint8_t senderId = _can->read();
  
  // Track client activity (marks as online)
  trackClientActivity(senderId);
  
  String message = "";
  while (_can->available()) {
    message += (char)_can->read();
  }
  
  // Call callback if registered
  if (_onDirectMessage) {
    _onDirectMessage(senderId, message);
  }
  
  // Send acknowledgment
  _can->beginPacket(CAN_PS_ACK);
  _can->write(CAN_PS_BROKER_ID);
  _can->write(senderId);
  _can->print("ACK");
  _can->endPacket();
}

void CANPubSubBroker::handlePing() {
  if (_can->available() < 1) return;
  
  uint8_t clientId = _can->read();
  
  // Track client activity (marks as online)
  trackClientActivity(clientId);
  
  // Send pong response
  _can->beginPacket(CAN_PS_PONG);
  _can->write(CAN_PS_BROKER_ID);
  _can->write(clientId);
  _can->endPacket();
}

void CANPubSubBroker::handlePong() {
  if (_can->available() < 2) return;
  
  uint8_t senderId = _can->read();  // Should be client ID
  uint8_t targetId = _can->read();  // Should be broker ID (0x00)
  
  if (targetId != CAN_PS_BROKER_ID) return;
  
  // Track client activity (marks as online and updates ping state)
  trackClientActivity(senderId);
}

void CANPubSubBroker::pingAllClients() {
  // Ping all registered clients
  for (uint8_t i = 0; i < _mappingCount; i++) {
    if (_clientMappings[i].registered) {
      uint8_t clientId = _clientMappings[i].clientId;
      
      _can->beginPacket(CAN_PS_PING);
      _can->write(CAN_PS_BROKER_ID);
      _can->write(clientId);
      _can->endPacket();
      
      // Increment missed pings counter in ping state
      int stateIdx = findPingState(clientId);
      if (stateIdx >= 0) {
        _pingStates[stateIdx].missedPings++;
      }
      
      delay(5); // Small delay between pings
    }
  }
}

void CANPubSubBroker::checkClientTimeouts() {
  // Check for clients that haven't responded
  for (uint8_t i = 0; i < _pingStateCount; i++) {
    uint8_t clientId = _pingStates[i].clientId;
    
    if (_pingStates[i].missedPings >= _maxMissedPings) {
      // Check if client is currently online
      bool wasOnline = false;
      for (uint8_t j = 0; j < _clientCount; j++) {
        if (_connectedClients[j] == clientId) {
          wasOnline = true;
          break;
        }
      }
      
      // Only process if client was online (avoid duplicate disconnect callbacks)
      if (wasOnline) {
        // Remove from connected clients list (mark offline)
        for (uint8_t j = 0; j < _clientCount; j++) {
          if (_connectedClients[j] == clientId) {
            // Shift remaining clients
            for (uint8_t k = j; k < _clientCount - 1; k++) {
              _connectedClients[k] = _connectedClients[k + 1];
            }
            _clientCount--;
            break;
          }
        }
        
        // Call disconnect callback
        if (_onClientDisconnect) {
          _onClientDisconnect(clientId);
        }
      }
      
      // Note: Client remains registered (active=true) in mappings
      // and will be marked online again when it reconnects
    }
  }
}

void CANPubSubBroker::handlePeerMessage() {
  // Peer-to-peer message forwarding (only for clients with permanent IDs)
  // Format: [senderId][targetId][message...]
  if (_can->available() < 2) return;
  
  uint8_t senderId = _can->read();
  uint8_t targetId = _can->read();
  
  // Track client activity (marks as online)
  trackClientActivity(senderId);
  
  // Verify sender has a permanent ID (registered with serial number)
  int senderIndex = findClientMappingById(senderId);
  if (senderIndex < 0) {
    // Sender doesn't have permanent ID, reject
    return;
  }
  
  // Verify target has a permanent ID
  int targetIndex = findClientMappingById(targetId);
  if (targetIndex < 0) {
    // Target doesn't have permanent ID, reject
    return;
  }
  
  // Read message
  String message = "";
  while (_can->available()) {
    message += (char)_can->read();
  }
  
  // Forward message to target client
  // Calculate total message size: senderId + targetId + message
  size_t totalSize = 1 + 1 + message.length();
  
  if (totalSize > CAN_FRAME_DATA_SIZE) {
    // Use extended message for long messages
    uint8_t buffer[MAX_EXTENDED_MSG_SIZE];
    buffer[0] = senderId;
    buffer[1] = targetId;
    memcpy(buffer + 2, message.c_str(), min(message.length(), (size_t)(MAX_EXTENDED_MSG_SIZE - 2)));
    
    sendExtendedMessage(CAN_PS_PEER_MSG, buffer, min(2 + message.length(), (size_t)MAX_EXTENDED_MSG_SIZE));
  } else {
    _can->beginPacket(CAN_PS_PEER_MSG);
    _can->write(senderId);
    _can->write(targetId);
    _can->print(message);
    _can->endPacket();
  }
}

void CANPubSubBroker::addSubscription(uint8_t clientId, uint16_t topicHash) {
  // Find or create topic entry
  for (uint8_t i = 0; i < _subTableSize; i++) {
    if (_subscriptions[i].topicHash == topicHash) {
      // Add client if not already subscribed
      for (uint8_t j = 0; j < _subscriptions[i].subCount; j++) {
        if (_subscriptions[i].subscribers[j] == clientId) return;
      }
      if (_subscriptions[i].subCount < MAX_SUBSCRIBERS_PER_TOPIC) {
        _subscriptions[i].subscribers[_subscriptions[i].subCount++] = clientId;
      }
      // Store subscription persistently
      storeClientSubscriptions(clientId);
      return;
    }
  }
  
  // Create new topic entry
  if (_subTableSize < MAX_SUBSCRIPTIONS) {
    _subscriptions[_subTableSize].topicHash = topicHash;
    _subscriptions[_subTableSize].subscribers[0] = clientId;
    _subscriptions[_subTableSize].subCount = 1;
    _subTableSize++;
    // Store subscription persistently
    storeClientSubscriptions(clientId);
  }
}

void CANPubSubBroker::removeSubscription(uint8_t clientId, uint16_t topicHash) {
  for (uint8_t i = 0; i < _subTableSize; i++) {
    if (_subscriptions[i].topicHash == topicHash) {
      for (uint8_t j = 0; j < _subscriptions[i].subCount; j++) {
        if (_subscriptions[i].subscribers[j] == clientId) {
          // Shift remaining subscribers
          for (uint8_t k = j; k < _subscriptions[i].subCount - 1; k++) {
            _subscriptions[i].subscribers[k] = _subscriptions[i].subscribers[k + 1];
          }
          _subscriptions[i].subCount--;
          
          // If no subscribers left, remove the topic entry
          if (_subscriptions[i].subCount == 0) {
            // Shift remaining topics
            for (uint8_t k = i; k < _subTableSize - 1; k++) {
              _subscriptions[k] = _subscriptions[k + 1];
            }
            _subTableSize--;
          }
          
          // Update stored subscriptions
          storeClientSubscriptions(clientId);
          return;
        }
      }
    }
  }
}

void CANPubSubBroker::removeAllSubscriptions(uint8_t clientId) {
  // Iterate through all topics (backwards to handle removal safely)
  for (int i = _subTableSize - 1; i >= 0; i--) {
    for (uint8_t j = 0; j < _subscriptions[i].subCount; j++) {
      if (_subscriptions[i].subscribers[j] == clientId) {
        // Shift remaining subscribers
        for (uint8_t k = j; k < _subscriptions[i].subCount - 1; k++) {
          _subscriptions[i].subscribers[k] = _subscriptions[i].subscribers[k + 1];
        }
        _subscriptions[i].subCount--;
        j--; // Check the same index again
      }
    }
    
    // If no subscribers left after removing this client, remove the topic entry
    if (_subscriptions[i].subCount == 0) {
      // Shift remaining topics
      for (uint8_t k = i; k < _subTableSize - 1; k++) {
        _subscriptions[k] = _subscriptions[k + 1];
      }
      _subTableSize--;
    }
  }
  // Update stored subscriptions
  storeClientSubscriptions(clientId);
}

void CANPubSubBroker::forwardToSubscribers(uint16_t topicHash, const String& message) {
  for (uint8_t i = 0; i < _subTableSize; i++) {
    if (_subscriptions[i].topicHash == topicHash) {
      for (uint8_t j = 0; j < _subscriptions[i].subCount; j++) {
        uint8_t subId = _subscriptions[i].subscribers[j];
        
        // Calculate total message size: subId + topicHash + message
        size_t totalSize = 1 + 2 + message.length();
        
        if (totalSize > CAN_FRAME_DATA_SIZE) {
          // Use extended message for long messages
          uint8_t buffer[MAX_EXTENDED_MSG_SIZE];
          buffer[0] = subId;
          buffer[1] = topicHash >> 8;
          buffer[2] = topicHash & 0xFF;
          memcpy(buffer + 3, message.c_str(), min(message.length(), (size_t)(MAX_EXTENDED_MSG_SIZE - 3)));
          
          sendExtendedMessage(CAN_PS_TOPIC_DATA, buffer, min(3 + message.length(), (size_t)MAX_EXTENDED_MSG_SIZE));
        } else {
          _can->beginPacket(CAN_PS_TOPIC_DATA);
          _can->write(subId);
          _can->write(topicHash >> 8);
          _can->write(topicHash & 0xFF);
          _can->print(message);
          _can->endPacket();
        }
        
        delay(10); // Small delay between sends
      }
      return;
    }
  }
}

void CANPubSubBroker::assignClientID() {
  _can->beginPacket(CAN_PS_ID_RESPONSE);
  _can->write(_nextTempID);
  _can->endPacket();
  
  _nextTempID++;
  if (_nextTempID == 0xFF) {
    _nextTempID = 101; // Wrap around to 101 for temporary IDs
  }
}

void CANPubSubBroker::onClientConnect(ConnectionCallback callback) {
  _onClientConnect = callback;
}

void CANPubSubBroker::onClientDisconnect(ConnectionCallback callback) {
  _onClientDisconnect = callback;
}

void CANPubSubBroker::onPublish(MessageCallback callback) {
  _onPublish = callback;
}

void CANPubSubBroker::onDirectMessage(DirectMessageCallback callback) {
  _onDirectMessage = callback;
}

void CANPubSubBroker::setPingInterval(unsigned long intervalMs) {
  _pingInterval = intervalMs;
  savePingConfigToStorage();
}

unsigned long CANPubSubBroker::getPingInterval() {
  return _pingInterval;
}

void CANPubSubBroker::enableAutoPing(bool enable) {
  _autoPingEnabled = enable;
  if (enable) {
    _lastPingTime = millis();
    
    // Initialize ping state for all registered clients
    for (uint8_t i = 0; i < _mappingCount; i++) {
      if (_clientMappings[i].registered) {
        initPingState(_clientMappings[i].clientId);
      }
    }
  }
  savePingConfigToStorage();
}

bool CANPubSubBroker::isAutoPingEnabled() {
  return _autoPingEnabled;
}

void CANPubSubBroker::setMaxMissedPings(uint8_t maxMissed) {
  _maxMissedPings = maxMissed;
  savePingConfigToStorage();
}

uint8_t CANPubSubBroker::getMaxMissedPings() {
  return _maxMissedPings;
}

int CANPubSubBroker::findPingState(uint8_t clientId) {
  for (uint8_t i = 0; i < _pingStateCount; i++) {
    if (_pingStates[i].clientId == clientId) {
      return i;
    }
  }
  return -1;
}

void CANPubSubBroker::initPingState(uint8_t clientId) {
  // Check if already exists
  int index = findPingState(clientId);
  
  if (index >= 0) {
    // Reset existing state
    _pingStates[index].lastPongTime = millis();
    _pingStates[index].missedPings = 0;
  } else if (_pingStateCount < MAX_CLIENT_MAPPINGS) {
    // Create new state
    _pingStates[_pingStateCount].clientId = clientId;
    _pingStates[_pingStateCount].lastPongTime = millis();
    _pingStates[_pingStateCount].missedPings = 0;
    _pingStateCount++;
  }
}

void CANPubSubBroker::trackClientActivity(uint8_t clientId) {
  // Add client to connected clients list if not already present
  bool found = false;
  for (uint8_t i = 0; i < _clientCount; i++) {
    if (_connectedClients[i] == clientId) {
      found = true;
      break;
    }
  }
  
  if (!found && _clientCount < 256) {
    _connectedClients[_clientCount++] = clientId;
    
    // Call connect callback if this is a new connection
    if (_onClientConnect) {
      _onClientConnect(clientId);
    }
  }
  
  // Update ping state if auto-ping is enabled
  if (_autoPingEnabled) {
    int index = findPingState(clientId);
    if (index >= 0) {
      _pingStates[index].lastPongTime = millis();
      _pingStates[index].missedPings = 0;
    }
  }
}

void CANPubSubBroker::sendToClient(uint8_t clientId, uint16_t topicHash, const String& message) {
  // Calculate total message size: clientId + topicHash + message
  size_t totalSize = 1 + 2 + message.length();
  
  if (totalSize > CAN_FRAME_DATA_SIZE) {
    // Use extended message for long messages
    uint8_t buffer[MAX_EXTENDED_MSG_SIZE];
    buffer[0] = clientId;
    buffer[1] = topicHash >> 8;
    buffer[2] = topicHash & 0xFF;
    memcpy(buffer + 3, message.c_str(), min(message.length(), (size_t)(MAX_EXTENDED_MSG_SIZE - 3)));
    
    sendExtendedMessage(CAN_PS_TOPIC_DATA, buffer, min(3 + message.length(), (size_t)MAX_EXTENDED_MSG_SIZE));
  } else {
    _can->beginPacket(CAN_PS_TOPIC_DATA);
    _can->write(clientId);
    _can->write(topicHash >> 8);
    _can->write(topicHash & 0xFF);
    _can->print(message);
    _can->endPacket();
  }
}

void CANPubSubBroker::sendDirectMessage(uint8_t clientId, const String& message) {
  // Calculate total message size: brokerId + clientId + message
  size_t totalSize = 1 + 1 + message.length();
  
  if (totalSize > CAN_FRAME_DATA_SIZE) {
    // Use extended message for long messages
    // Format: [brokerID][clientID][message...]
    // processExtendedFrame will extract brokerID as senderId from first byte
    uint8_t buffer[MAX_EXTENDED_MSG_SIZE];
    buffer[0] = CAN_PS_BROKER_ID;
    buffer[1] = clientId;
    memcpy(buffer + 2, message.c_str(), min(message.length(), (size_t)(MAX_EXTENDED_MSG_SIZE - 2)));
    
    sendExtendedMessage(CAN_PS_DIRECT_MSG, buffer, min(2 + message.length(), (size_t)MAX_EXTENDED_MSG_SIZE));
  } else {
    _can->beginPacket(CAN_PS_DIRECT_MSG);
    _can->write(CAN_PS_BROKER_ID);
    _can->write(clientId);
    _can->print(message);
    _can->endPacket();
  }
}

void CANPubSubBroker::broadcastMessage(uint16_t topicHash, const String& message) {
  forwardToSubscribers(topicHash, message);
}

uint8_t CANPubSubBroker::getClientCount() {
  return _clientCount;
}

uint8_t CANPubSubBroker::getSubscriptionCount() {
  return _subTableSize;
}

void CANPubSubBroker::getSubscribers(uint16_t topicHash, uint8_t* subscribers, uint8_t* count) {
  for (uint8_t i = 0; i < _subTableSize; i++) {
    if (_subscriptions[i].topicHash == topicHash) {
      *count = _subscriptions[i].subCount;
      memcpy(subscribers, _subscriptions[i].subscribers, _subscriptions[i].subCount);
      return;
    }
  }
  *count = 0;
}

void CANPubSubBroker::listSubscribedTopics(std::function<void(uint16_t hash, const String& name, uint8_t subscriberCount)> callback) {
  if (!callback) return;
  
  // Show active subscriptions (includes restored subscriptions from storage)
  for (uint8_t i = 0; i < _subTableSize; i++) {
    uint16_t hash = _subscriptions[i].topicHash;
    String name = getTopicName(hash);
    uint8_t count = _subscriptions[i].subCount;
    callback(hash, name, count);
  }
  
  // Also show stored topics that don't have active subscribers (after power cycle)
  // This helps show what topics are in storage even if clients haven't reconnected
  for (uint8_t i = 0; i < _storedTopicCount; i++) {
    if (!_storedTopicNames[i].active) continue;
    
    uint16_t hash = _storedTopicNames[i].hash;
    
    // Check if this topic is already in the active subscriptions
    bool alreadyListed = false;
    for (uint8_t j = 0; j < _subTableSize; j++) {
      if (_subscriptions[j].topicHash == hash) {
        alreadyListed = true;
        break;
      }
    }
    
    // If not already listed, show it with 0 subscribers
    if (!alreadyListed) {
      String name = _storedTopicNames[i].getName();
      callback(hash, name, 0);
    }
  }
}

// ===== Client ID Mapping Methods =====

void CANPubSubBroker::handleIdRequestWithSerial() {
  // Read serial number from CAN message
  String serialNumber = "";
  while (_can->available()) {
    serialNumber += (char)_can->read();
  }
  
  if (serialNumber.length() == 0) {
    // No serial number provided - assign temporary ID (not saved to storage)
    assignClientID();
    return;
  }
  
  // Find or create client ID for this serial number (will be saved to storage)
  uint8_t assignedId = findOrCreateClientId(serialNumber);
  
  // Check if this is a returning client (has stored subscriptions)
  int subIndex = findStoredSubscription(assignedId);
  bool hasStoredSubs = (subIndex >= 0 && _storedSubscriptions[subIndex].topicCount > 0);
  
  // Send response with serial number included so client can verify it's for them
  // Format: [assignedId][hasStoredSubs][serialNumberLength][serialNumber...]
  size_t totalSize = 1 + 1 + 1 + serialNumber.length();
  
  if (totalSize > CAN_FRAME_DATA_SIZE) {
    // Use extended message for long serial numbers
    uint8_t buffer[MAX_EXTENDED_MSG_SIZE];
    buffer[0] = assignedId;
    buffer[1] = hasStoredSubs ? 0x01 : 0x00;
    buffer[2] = (uint8_t)serialNumber.length();
    memcpy(buffer + 3, serialNumber.c_str(), min(serialNumber.length(), (size_t)(MAX_EXTENDED_MSG_SIZE - 3)));
    
    sendExtendedMessage(CAN_PS_ID_RESPONSE, buffer, min(3 + serialNumber.length(), (size_t)MAX_EXTENDED_MSG_SIZE));
  } else {
    _can->beginPacket(CAN_PS_ID_RESPONSE);
    _can->write(assignedId);
    _can->write(hasStoredSubs ? 0x01 : 0x00); // Flag: has stored subscriptions
    _can->write((uint8_t)serialNumber.length());
    _can->print(serialNumber);
    _can->endPacket();
  }
  
  // Track connected client (marks as online)
  trackClientActivity(assignedId);
  
  // Restore stored subscriptions for this client (if any)
  if (hasStoredSubs) {
    delay(100); // Delay to let client process ID response and prepare to receive subscriptions
    restoreClientSubscriptions(assignedId);
  }
}

uint8_t CANPubSubBroker::findOrCreateClientId(const String& serialNumber) {
  // Check if this serial number already has an ID
  int index = findClientMapping(serialNumber);
  
  if (index >= 0) {
    // Found existing mapping, mark as registered and return the same ID
    _clientMappings[index].registered = true;
    saveMappingsToStorage(); // Save state change
    
    // Initialize ping tracking if auto-ping is enabled
    if (_autoPingEnabled) {
      initPingState(_clientMappings[index].clientId);
    }
    
    return _clientMappings[index].clientId;
  }
  
  // No existing mapping, create a new one
  if (_mappingCount < MAX_CLIENT_MAPPINGS) {
    _clientMappings[_mappingCount].clientId = _nextClientID;
    _clientMappings[_mappingCount].setSerial(serialNumber);
    _clientMappings[_mappingCount].registered = true;
    _mappingCount++;
    
    uint8_t assignedId = _nextClientID;
    
    // Increment for next client
    _nextClientID++;
    if (_nextClientID == 0xFF) {
      _nextClientID = 0x01; // Wrap around, skip special IDs
    }
    
    // Initialize ping tracking if auto-ping is enabled
    if (_autoPingEnabled) {
      initPingState(assignedId);
    }
    
    saveMappingsToStorage(); // Save new mapping
    return assignedId;
  }
  
  // Mapping table full, return error (use 0xFF)
  return CAN_PS_UNASSIGNED_ID;
}

int CANPubSubBroker::findClientMapping(const String& serialNumber) {
  for (uint8_t i = 0; i < _mappingCount; i++) {
    if (_clientMappings[i].getSerial() == serialNumber) {
      return i;
    }
  }
  return -1;
}

int CANPubSubBroker::findClientMappingById(uint8_t clientId) {
  for (uint8_t i = 0; i < _mappingCount; i++) {
    if (_clientMappings[i].clientId == clientId) {
      return i;
    }
  }
  return -1;
}

uint8_t CANPubSubBroker::registerClient(const String& serialNumber) {
  return findOrCreateClientId(serialNumber);
}

bool CANPubSubBroker::unregisterClient(uint8_t clientId) {
  int index = findClientMappingById(clientId);
  if (index >= 0) {
    _clientMappings[index].registered = false;
    // Remove all subscriptions for this client
    removeAllSubscriptions(clientId);
    saveMappingsToStorage(); // Save state change
    return true;
  }
  return false;
}

bool CANPubSubBroker::unregisterClientBySerial(const String& serialNumber) {
  int index = findClientMapping(serialNumber);
  if (index >= 0) {
    _clientMappings[index].registered = false;
    // Remove all subscriptions for this client
    removeAllSubscriptions(_clientMappings[index].clientId);
    saveMappingsToStorage(); // Save state change
    return true;
  }
  return false;
}

uint8_t CANPubSubBroker::getClientIdBySerial(const String& serialNumber) {
  int index = findClientMapping(serialNumber);
  if (index >= 0) {
    return _clientMappings[index].clientId;
  }
  return CAN_PS_UNASSIGNED_ID;
}

String CANPubSubBroker::getSerialByClientId(uint8_t clientId) {
  int index = findClientMappingById(clientId);
  if (index >= 0) {
    return _clientMappings[index].getSerial();
  }
  return "";
}

bool CANPubSubBroker::updateClientSerial(uint8_t clientId, const String& newSerial) {
  int index = findClientMappingById(clientId);
  if (index >= 0) {
    // Check if new serial already exists
    if (findClientMapping(newSerial) >= 0) {
      return false; // Serial already in use
    }
    _clientMappings[index].setSerial(newSerial);
    saveMappingsToStorage(); // Save change
    return true;
  }
  return false;
}

uint8_t CANPubSubBroker::getRegisteredClientCount() {
  // Count only registered clients (registered=true)
  uint8_t count = 0;
  for (uint8_t i = 0; i < _mappingCount; i++) {
    if (_clientMappings[i].registered) {
      count++;
    }
  }
  return count;
}

void CANPubSubBroker::listRegisteredClients(std::function<void(uint8_t id, const String& serial, bool registered)> callback) {
  if (!callback) return;
  
  for (uint8_t i = 0; i < _mappingCount; i++) {
    callback(_clientMappings[i].clientId, 
             _clientMappings[i].getSerial(), 
             _clientMappings[i].registered);
  }
}

bool CANPubSubBroker::isClientOnline(uint8_t clientId) {
  // Check if client is in the connected clients list
  for (uint8_t i = 0; i < _clientCount; i++) {
    if (_connectedClients[i] == clientId) {
      return true;
    }
  }
  return false;
}

uint8_t CANPubSubBroker::getClientSubscriptionCount(uint8_t clientId) {
  uint8_t count = 0;
  // Count how many topics this client is subscribed to
  for (uint8_t i = 0; i < _subTableSize; i++) {
    for (uint8_t j = 0; j < _subscriptions[i].subCount; j++) {
      if (_subscriptions[i].subscribers[j] == clientId) {
        count++;
        break; // Client found in this topic, move to next topic
      }
    }
  }
  return count;
}

void CANPubSubBroker::onExtendedMessageComplete(uint8_t msgType, uint8_t senderId, const uint8_t* data, size_t length) {
  // Handle extended messages based on type
  switch (msgType) {
    case CAN_PS_ID_REQUEST: {
      // Extended ID request with serial number
      // Note: A placeholder byte (0x00) was extracted by processExtendedFrame as "senderId"
      // The actual serial number is in the data buffer
      String serialNumber = "";
      for (size_t i = 0; i < length; i++) {
        serialNumber += (char)data[i];
      }
      
      if (serialNumber.length() == 0) {
        // No serial number provided - reject by not sending a response
        return;
      }
      
      uint8_t assignedId = findOrCreateClientId(serialNumber);
      
      // Check if this is a returning client (has stored subscriptions)
      int subIndex = findStoredSubscription(assignedId);
      bool hasStoredSubs = (subIndex >= 0 && _storedSubscriptions[subIndex].topicCount > 0);
      
      // Send response with serial number included so client can verify it's for them
      // Format: [assignedId][hasStoredSubs][serialNumberLength][serialNumber...]
      size_t totalSize = 1 + 1 + 1 + serialNumber.length();
      
      if (totalSize > CAN_FRAME_DATA_SIZE) {
        // Use extended message for long serial numbers
        uint8_t buffer[MAX_EXTENDED_MSG_SIZE];
        buffer[0] = assignedId;
        buffer[1] = hasStoredSubs ? 0x01 : 0x00;
        buffer[2] = (uint8_t)serialNumber.length();
        memcpy(buffer + 3, serialNumber.c_str(), min(serialNumber.length(), (size_t)(MAX_EXTENDED_MSG_SIZE - 3)));
        
        sendExtendedMessage(CAN_PS_ID_RESPONSE, buffer, min(3 + serialNumber.length(), (size_t)MAX_EXTENDED_MSG_SIZE));
      } else {
        _can->beginPacket(CAN_PS_ID_RESPONSE);
        _can->write(assignedId);
        _can->write(hasStoredSubs ? 0x01 : 0x00);
        _can->write((uint8_t)serialNumber.length());
        _can->print(serialNumber);
        _can->endPacket();
      }
      
      // Track connected client if not already tracked
      bool found = false;
      for (uint8_t i = 0; i < _clientCount; i++) {
        if (_connectedClients[i] == assignedId) {
          found = true;
          break;
        }
      }
      if (!found && _clientCount < 256) {
        _connectedClients[_clientCount++] = assignedId;
        
        if (_onClientConnect) {
          _onClientConnect(assignedId);
        }
      }
      
      // Restore stored subscriptions for this client (if any)
      if (hasStoredSubs) {
        delay(100); // Delay to let client process ID response and prepare to receive subscriptions
        restoreClientSubscriptions(assignedId);
      }
      break;
    }
    
    case CAN_PS_SUBSCRIBE: {
      // Extended subscribe with full topic name
      // Format (in buffer): [topicHash_h][topicHash_l][topic_name...]
      // Note: clientId was already extracted by processExtendedFrame from first byte
      if (length < 2) return;
      
      uint8_t clientId = senderId; // Use the extracted sender ID
      uint16_t topicHash = (data[0] << 8) | data[1];
      String topicName = "";
      
      for (size_t i = 2; i < length; i++) {
        topicName += (char)data[i];
      }
      
      if (topicName.length() > 0) {
        registerTopic(topicName);
        // Also persist topic name to flash storage
        storeTopicName(topicHash, topicName);
      }
      
      addSubscription(clientId, topicHash);
      
      // Track client activity (marks as online)
      trackClientActivity(clientId);
      
      break;
    }
    
    case CAN_PS_PUBLISH: {
      // Extended publish with long message
      // Format (in buffer): [topicHash_h][topicHash_l][message...]
      // Note: publisherId was already extracted by processExtendedFrame from first byte
      if (length < 2) return;
      
      uint8_t publisherId = senderId; // Use the extracted sender ID
      uint16_t topicHash = (data[0] << 8) | data[1];
      String message = "";
      
      for (size_t i = 2; i < length; i++) {
        message += (char)data[i];
      }
      
      // Track client activity (marks as online)
      trackClientActivity(publisherId);
      
      String topicName = getTopicName(topicHash);
      
      if (_onPublish) {
        _onPublish(topicHash, topicName, message);
      }
      
      forwardToSubscribers(topicHash, message);
      break;
    }
    
    case CAN_PS_DIRECT_MSG: {
      // Extended direct message from client to broker
      // Format (in buffer): [message...]
      // Note: senderId (client ID) was already extracted by processExtendedFrame from first byte
      
      // Track client activity (marks as online)
      trackClientActivity(senderId);
      
      String message = "";
      for (size_t i = 0; i < length; i++) {
        message += (char)data[i];
      }
      
      if (_onDirectMessage) {
        _onDirectMessage(senderId, message);
      }
      
      // Send acknowledgment
      _can->beginPacket(CAN_PS_ACK);
      _can->write(CAN_PS_BROKER_ID);
      _can->write(senderId);
      _can->print("ACK");
      _can->endPacket();
      break;
    }
    
    case CAN_PS_PEER_MSG: {
      // Extended peer message from client to client (forwarded by broker)
      // Format (in buffer): [targetId][message...]
      // Note: senderId was already extracted by processExtendedFrame from first byte
      if (length < 1) return;
      
      uint8_t targetId = data[0];
      
      // Track client activity (marks as online)
      trackClientActivity(senderId);
      
      // Verify sender has a permanent ID (registered with serial number)
      int senderIndex = findClientMappingById(senderId);
      if (senderIndex < 0) {
        // Sender doesn't have permanent ID, reject
        return;
      }
      
      // Verify target has a permanent ID
      int targetIndex = findClientMappingById(targetId);
      if (targetIndex < 0) {
        // Target doesn't have permanent ID, reject
        return;
      }
      
      // Extract message
      String message = "";
      for (size_t i = 1; i < length; i++) {
        message += (char)data[i];
      }
      
      // Forward message to target client
      size_t totalSize = 1 + 1 + message.length();
      
      if (totalSize > CAN_FRAME_DATA_SIZE) {
        // Use extended message for long messages
        uint8_t buffer[MAX_EXTENDED_MSG_SIZE];
        buffer[0] = senderId;
        buffer[1] = targetId;
        memcpy(buffer + 2, message.c_str(), min(message.length(), (size_t)(MAX_EXTENDED_MSG_SIZE - 2)));
        
        sendExtendedMessage(CAN_PS_PEER_MSG, buffer, min(2 + message.length(), (size_t)MAX_EXTENDED_MSG_SIZE));
      } else {
        _can->beginPacket(CAN_PS_PEER_MSG);
        _can->write(senderId);
        _can->write(targetId);
        _can->print(message);
        _can->endPacket();
      }
      break;
    }
  }
}

// ===== CANPubSubClient Implementation =====

CANPubSubClient::CANPubSubClient(CANControllerClass& can)
  : CANPubSubBase(can),
    _clientId(CAN_PS_UNASSIGNED_ID),
    _connected(false),
    _subscribedTopicCount(0),
    _lastPing(0),
    _lastPong(0),
    _lastPeerSenderId(0),
    _lastPeerMsgTime(0),
    _onMessage(nullptr),
    _onDirectMessage(nullptr),
    _onConnect(nullptr),
    _onDisconnect(nullptr),
    _onPong(nullptr) {
  memset(_subscribedTopics, 0, sizeof(_subscribedTopics));
  memset(_lastPeerMessage, 0, sizeof(_lastPeerMessage));
}

bool CANPubSubClient::begin(unsigned long timeout) {
  return connect(timeout);
}

bool CANPubSubClient::begin(const String& serialNumber, unsigned long timeout) {
  return connect(serialNumber, timeout);
}

void CANPubSubClient::end() {
  _connected = false;
  _clientId = CAN_PS_UNASSIGNED_ID;
  _subscribedTopicCount = 0;
  _serialNumber = "";
}

bool CANPubSubClient::connect(unsigned long timeout) {
  // Clear subscriptions on (re)connect - they will be restored by broker if persistent
  _subscribedTopicCount = 0;
  memset(_subscribedTopics, 0, sizeof(_subscribedTopics));
  
  requestClientID();
  
  unsigned long startTime = millis();
  while (_clientId == CAN_PS_UNASSIGNED_ID && (millis() - startTime) < timeout) {
    int packetSize = _can->parsePacket();
    if (packetSize > 0) {
      handleMessage(packetSize);
    }
    delay(10);
  }
  
  if (_clientId != CAN_PS_UNASSIGNED_ID) {
    _connected = true;
    if (_onConnect) {
      _onConnect();
    }
    return true;
  }
  
  return false;
}

bool CANPubSubClient::connect(const String& serialNumber, unsigned long timeout) {
  // Clear subscriptions on (re)connect - they will be restored by broker if persistent
  _subscribedTopicCount = 0;
  memset(_subscribedTopics, 0, sizeof(_subscribedTopics));
  
  _serialNumber = serialNumber;
  requestClientIDWithSerial(serialNumber);
  
  unsigned long startTime = millis();
  unsigned long idReceivedTime = 0;
  bool idReceived = false;
  
  // Wait for ID assignment and subscription restoration
  while ((millis() - startTime) < timeout) {
    int packetSize = _can->parsePacket();
    if (packetSize > 0) {
      handleMessage(packetSize);
      
      // Check if we just received our ID
      if (!idReceived && _clientId != CAN_PS_UNASSIGNED_ID) {
        idReceived = true;
        idReceivedTime = millis();
      }
    }
    
    // If we have an ID and enough time has passed for subscription restoration, we're done
    if (idReceived && (millis() - idReceivedTime) >= 200) {
      break;
    }
    
    delay(10);
  }
  
  if (_clientId != CAN_PS_UNASSIGNED_ID) {
    _connected = true;
    if (_onConnect) {
      _onConnect();
    }
    return true;
  }
  
  return false;
}

bool CANPubSubClient::isConnected() {
  return _connected;
}

uint8_t CANPubSubClient::getClientId() {
  return _clientId;
}

String CANPubSubClient::getSerialNumber() {
  return _serialNumber;
}

void CANPubSubClient::loop() {
  int packetSize = _can->parsePacket();
  if (packetSize > 0) {
    handleMessage(packetSize);
  }
}

void CANPubSubClient::handleMessage(int packetSize) {
  // Check for extended frames first
  if (_can->packetExtended()) {
    processExtendedFrame(packetSize);
    return;
  }
  
  uint8_t msgType = _can->packetId();
  
  switch (msgType) {
    case CAN_PS_ID_RESPONSE:
      handleIdAssignment();
      break;
    case CAN_PS_SUBSCRIBE:
      handleSubscribeNotification();
      break;
    case CAN_PS_SUB_RESTORE:
      handleSubscriptionRestore();
      break;
    case CAN_PS_TOPIC_DATA:
      handleTopicData();
      break;
    case CAN_PS_DIRECT_MSG:
      handleDirectMessageReceived();
      break;
    case CAN_PS_PEER_MSG:
      // Handle standard peer messages (extended peer messages go through onExtendedMessageComplete)
      if (_can->available() >= 2) {
        uint8_t senderId = _can->read();
        uint8_t targetId = _can->read();
        
        if (targetId == _clientId) {
          // This message is for us
          String message = "";
          while (_can->available()) {
            message += (char)_can->read();
          }
          
          // Deduplicate: skip if same sender and message within 50ms
          unsigned long now = millis();
          bool isDuplicate = (senderId == _lastPeerSenderId && 
                              message == String(_lastPeerMessage) &&
                              (now - _lastPeerMsgTime) < 50);
          
          if (!isDuplicate && _onDirectMessage) {
            _onDirectMessage(senderId, message);
            
            // Track this message to detect duplicates
            _lastPeerSenderId = senderId;
            _lastPeerMsgTime = now;
            strncpy(_lastPeerMessage, message.c_str(), 31);
            _lastPeerMessage[31] = '\0';
          }
        }
        // Note: Messages not for us are silently discarded to avoid processing
        // forwarded peer messages intended for other clients
      }
      break;
    case CAN_PS_PING:
      // Broker is pinging us, respond with pong
      if (_can->available() >= 2) {
        uint8_t senderId = _can->read();  // Broker ID
        uint8_t targetId = _can->read();  // Our ID
        if (targetId == _clientId) {
          // Send pong response
          _can->beginPacket(CAN_PS_PONG);
          _can->write(_clientId);
          _can->write(senderId);
          _can->endPacket();
        }
      }
      break;
    case CAN_PS_PONG:
      handlePong();
      break;
    case CAN_PS_ACK:
      // Acknowledgment received
      break;
  }
}

void CANPubSubClient::handleIdAssignment() {
  if (_can->available() < 1) return;
  
  uint8_t assignedId = _can->read();
  
  // Check if there's a flag indicating stored subscriptions will be restored
  bool hasStoredSubs = false;
  if (_can->available() > 0) {
    hasStoredSubs = (_can->read() == 0x01);
  }
  
  // If serial number is provided, verify it matches ours
  if (_can->available() > 0 && _serialNumber.length() > 0) {
    uint8_t serialLen = _can->read();
    String receivedSerial = "";
    
    for (uint8_t i = 0; i < serialLen && _can->available(); i++) {
      receivedSerial += (char)_can->read();
    }
    
    // Only accept this ID if the serial number matches
    if (receivedSerial != _serialNumber) {
      // This ID response is not for us, ignore it
      return;
    }
  }
  
  _clientId = assignedId;
  _connected = true;
  
  // If subscriptions will be restored, broker will send them shortly
  // Client just needs to wait and handle incoming SUBSCRIBE messages
}

void CANPubSubClient::handleSubscribeNotification() {
  // Broker is notifying us about a subscription (restored from storage)
  // Format: [clientId][topicHash_h][topicHash_l][topicNameLen][topicName]
  if (_can->available() < 4) return;
  
  uint8_t clientId = _can->read();
  if (clientId != _clientId) return;
  
  uint16_t topicHash = (_can->read() << 8) | _can->read();
  uint8_t topicLen = _can->read();
  
  String topic = "";
  for (uint8_t i = 0; i < topicLen && _can->available(); i++) {
    topic += (char)_can->read();
  }
  
  // Register the topic mapping locally
  if (topic.length() > 0) {
    registerTopic(topic);
  }
  
  // Add to local subscription list
  if (_subscribedTopicCount < MAX_CLIENT_TOPICS) {
    // Check if already subscribed
    bool alreadySubscribed = false;
    for (uint8_t i = 0; i < _subscribedTopicCount; i++) {
      if (_subscribedTopics[i] == topicHash) {
        alreadySubscribed = true;
        break;
      }
    }
    
    if (!alreadySubscribed) {
      _subscribedTopics[_subscribedTopicCount++] = topicHash;
    }
  }
}

void CANPubSubClient::handleTopicData() {
  if (_can->available() < 3) return;
  
  uint8_t targetId = _can->read();
  if (targetId != _clientId) return;
  
  uint16_t topicHash = (_can->read() << 8) | _can->read();
  
  String message = "";
  while (_can->available()) {
    message += (char)_can->read();
  }
  
  // Call callback if registered
  if (_onMessage) {
    String topicName = getTopicName(topicHash);
    _onMessage(topicHash, topicName, message);
  }
}

void CANPubSubClient::handleDirectMessageReceived() {
  if (_can->available() < 2) return;
  
  uint8_t senderId = _can->read();
  uint8_t targetId = _can->read();
  
  if (targetId != _clientId) return;
  
  String message = "";
  while (_can->available()) {
    message += (char)_can->read();
  }
  
  // Call callback if registered
  if (_onDirectMessage) {
    _onDirectMessage(senderId, message);
  }
}

void CANPubSubClient::handlePong() {
  if (_can->available() < 2) return;
  
  uint8_t brokerId = _can->read();
  uint8_t targetId = _can->read();
  
  if (targetId == _clientId) {
    _lastPong = millis();
    
    // Call callback if registered
    if (_onPong) {
      _onPong();
    }
  }
}

void CANPubSubClient::handleSubscriptionRestore() {
  // Broker is sending us a stored subscription with topic name
  // Format: [clientId][topicHash][topicNameLength][topicName]
  if (_can->available() < 4) return;
  
  uint8_t clientId = _can->read();
  if (clientId != _clientId) return; // Not for us
  
  uint16_t topicHash = (_can->read() << 8) | _can->read();
  uint8_t topicNameLen = _can->read();
  
  // Read topic name
  String topicName = "";
  for (uint8_t i = 0; i < topicNameLen && _can->available(); i++) {
    topicName += (char)_can->read();
  }
  
  // Register topic name in our local mapping
  registerTopic(topicName);
  
  // Add to our subscribed topics list if not already present
  bool alreadySubscribed = false;
  for (uint8_t i = 0; i < _subscribedTopicCount; i++) {
    if (_subscribedTopics[i] == topicHash) {
      alreadySubscribed = true;
      break;
    }
  }
  
  if (!alreadySubscribed && _subscribedTopicCount < MAX_CLIENT_TOPICS) {
    _subscribedTopics[_subscribedTopicCount++] = topicHash;
  }
}

void CANPubSubClient::requestClientID() {
  _can->beginPacket(CAN_PS_ID_REQUEST);
  _can->endPacket();
}

void CANPubSubClient::requestClientIDWithSerial(const String& serialNumber) {
  // Use extended message for serial numbers > 8 bytes
  if (serialNumber.length() > CAN_FRAME_DATA_SIZE) {
    // Prepend a dummy byte (0x00) since processExtendedFrame will extract first byte as "senderId"
    uint8_t buffer[MAX_EXTENDED_MSG_SIZE];
    buffer[0] = 0x00; // Placeholder for "senderId" field
    memcpy(buffer + 1, serialNumber.c_str(), min(serialNumber.length(), (size_t)(MAX_EXTENDED_MSG_SIZE - 1)));
    sendExtendedMessage(CAN_PS_ID_REQUEST, buffer, min(1 + serialNumber.length(), (size_t)MAX_EXTENDED_MSG_SIZE));
  } else {
    _can->beginPacket(CAN_PS_ID_REQUEST);
    _can->print(serialNumber);
    _can->endPacket();
  }
}

bool CANPubSubClient::subscribe(const String& topic) {
  if (!_connected) return false;
  
  uint16_t topicHash = hashTopic(topic);
  registerTopic(topic);
  
  // Calculate total message size: clientId + topicHash + topicLength + topic
  size_t totalSize = 1 + 2 + 1 + topic.length();
  
  if (totalSize > CAN_FRAME_DATA_SIZE) {
    // Use extended message for long topics
    uint8_t buffer[MAX_EXTENDED_MSG_SIZE];
    buffer[0] = _clientId;
    buffer[1] = topicHash >> 8;
    buffer[2] = topicHash & 0xFF;
    memcpy(buffer + 3, topic.c_str(), topic.length());
    
    sendExtendedMessage(CAN_PS_SUBSCRIBE, buffer, 3 + topic.length());
  } else {
    _can->beginPacket(CAN_PS_SUBSCRIBE);
    _can->write(_clientId);
    _can->write(topicHash >> 8);
    _can->write(topicHash & 0xFF);
    _can->write((uint8_t)topic.length());  // Send topic name length
    _can->print(topic);  // Send topic name to broker for mapping
    _can->endPacket();
  }
  
  // Store locally
  if (_subscribedTopicCount < MAX_CLIENT_TOPICS) {
    _subscribedTopics[_subscribedTopicCount++] = topicHash;
  }
  
  return true;
}

bool CANPubSubClient::unsubscribe(const String& topic) {
  if (!_connected) return false;
  
  uint16_t topicHash = hashTopic(topic);
  
  _can->beginPacket(CAN_PS_UNSUBSCRIBE);
  _can->write(_clientId);
  _can->write(topicHash >> 8);
  _can->write(topicHash & 0xFF);
  _can->endPacket();
  
  // Remove from local list
  for (uint8_t i = 0; i < _subscribedTopicCount; i++) {
    if (_subscribedTopics[i] == topicHash) {
      for (uint8_t j = i; j < _subscribedTopicCount - 1; j++) {
        _subscribedTopics[j] = _subscribedTopics[j + 1];
      }
      _subscribedTopicCount--;
      break;
    }
  }
  
  return true;
}

bool CANPubSubClient::publish(const String& topic, const String& message) {
  if (!_connected) return false;
  
  uint16_t topicHash = hashTopic(topic);
  registerTopic(topic);
  
  // Calculate total message size: clientId + topicHash + message
  size_t totalSize = 1 + 2 + message.length();
  
  if (totalSize > CAN_FRAME_DATA_SIZE) {
    // Use extended message for long messages
    uint8_t buffer[MAX_EXTENDED_MSG_SIZE];
    buffer[0] = _clientId;
    buffer[1] = topicHash >> 8;
    buffer[2] = topicHash & 0xFF;
    memcpy(buffer + 3, message.c_str(), min(message.length(), (size_t)(MAX_EXTENDED_MSG_SIZE - 3)));
    
    return sendExtendedMessage(CAN_PS_PUBLISH, buffer, min(3 + message.length(), (size_t)MAX_EXTENDED_MSG_SIZE));
  } else {
    _can->beginPacket(CAN_PS_PUBLISH);
    _can->write(_clientId);
    _can->write(topicHash >> 8);
    _can->write(topicHash & 0xFF);
    _can->print(message);
    _can->endPacket();
    
    return true;
  }
}

bool CANPubSubClient::sendDirectMessage(const String& message) {
  if (!_connected) return false;
  
  // Calculate total message size: clientId + message
  size_t totalSize = 1 + message.length();
  
  if (totalSize > CAN_FRAME_DATA_SIZE) {
    // Use extended message for long messages
    uint8_t buffer[MAX_EXTENDED_MSG_SIZE];
    buffer[0] = _clientId;
    memcpy(buffer + 1, message.c_str(), min(message.length(), (size_t)(MAX_EXTENDED_MSG_SIZE - 1)));
    
    return sendExtendedMessage(CAN_PS_DIRECT_MSG, buffer, min(1 + message.length(), (size_t)MAX_EXTENDED_MSG_SIZE));
  } else {
    _can->beginPacket(CAN_PS_DIRECT_MSG);
    _can->write(_clientId);
    _can->print(message);
    _can->endPacket();
    
    return true;
  }
}

bool CANPubSubClient::sendPeerMessage(uint8_t targetClientId, const String& message) {
  if (!_connected) return false;
  
  // Only clients with permanent IDs (registered with serial numbers) can send peer messages
  if (_serialNumber.length() == 0) {
    return false; // No serial number = temporary ID, peer messaging not allowed
  }
  
  // Calculate total message size: senderId + targetId + message
  size_t totalSize = 1 + 1 + message.length();
  
  if (totalSize > CAN_FRAME_DATA_SIZE) {
    // Use extended message for long messages
    uint8_t buffer[MAX_EXTENDED_MSG_SIZE];
    buffer[0] = _clientId;
    buffer[1] = targetClientId;
    memcpy(buffer + 2, message.c_str(), min(message.length(), (size_t)(MAX_EXTENDED_MSG_SIZE - 2)));
    
    return sendExtendedMessage(CAN_PS_PEER_MSG, buffer, min(2 + message.length(), (size_t)MAX_EXTENDED_MSG_SIZE));
  } else {
    _can->beginPacket(CAN_PS_PEER_MSG);
    _can->write(_clientId);
    _can->write(targetClientId);
    _can->print(message);
    _can->endPacket();
    
    return true;
  }
}

bool CANPubSubClient::ping() {
  if (!_connected) return false;
  
  _can->beginPacket(CAN_PS_PING);
  _can->write(_clientId);
  _can->endPacket();
  
  _lastPing = millis();
  
  return true;
}

void CANPubSubClient::onMessage(MessageCallback callback) {
  _onMessage = callback;
}

void CANPubSubClient::onDirectMessage(DirectMessageCallback callback) {
  _onDirectMessage = callback;
}

void CANPubSubClient::onConnect(void (*callback)()) {
  _onConnect = callback;
}

void CANPubSubClient::onDisconnect(void (*callback)()) {
  _onDisconnect = callback;
}

void CANPubSubClient::onPong(void (*callback)()) {
  _onPong = callback;
}

unsigned long CANPubSubClient::getLastPingTime() {
  if (_lastPong == 0 || _lastPing == 0 || _lastPong < _lastPing) {
    return 0; // No valid pong received yet
  }
  return _lastPong - _lastPing;
}

bool CANPubSubClient::isSubscribed(const String& topic) {
  uint16_t topicHash = hashTopic(topic);
  for (uint8_t i = 0; i < _subscribedTopicCount; i++) {
    if (_subscribedTopics[i] == topicHash) {
      return true;
    }
  }
  return false;
}

uint8_t CANPubSubClient::getSubscriptionCount() {
  return _subscribedTopicCount;
}

void CANPubSubClient::listSubscribedTopics(std::function<void(uint16_t hash, const String& name)> callback) {
  if (!callback) return;
  
  for (uint8_t i = 0; i < _subscribedTopicCount; i++) {
    uint16_t hash = _subscribedTopics[i];
    String name = getTopicName(hash);
    callback(hash, name);
  }
}

void CANPubSubClient::onExtendedMessageComplete(uint8_t msgType, uint8_t senderId, const uint8_t* data, size_t length) {
  // Handle extended messages based on type
  switch (msgType) {
    case CAN_PS_ID_RESPONSE: {
      // Extended ID response with serial number verification
      // Format (in buffer): [hasStoredSubs][serialNumberLength][serialNumber...]
      // Note: assignedId was already extracted by processExtendedFrame from first byte
      if (length < 2) return;
      
      uint8_t assignedId = senderId; // The extracted "senderId" is actually the assignedId
      bool hasStoredSubs = (data[0] == 0x01);
      uint8_t serialLen = data[1];
      
      String receivedSerial = "";
      for (uint8_t i = 0; i < serialLen && (2 + i) < length; i++) {
        receivedSerial += (char)data[2 + i];
      }
      
      // Only accept this ID if the serial number matches ours
      if (_serialNumber.length() > 0 && receivedSerial != _serialNumber) {
        // This ID response is not for us, ignore it
        return;
      }
      
      _clientId = assignedId;
      _connected = true;
      
      // If subscriptions will be restored, broker will send them shortly
      // Client just needs to wait and handle incoming SUBSCRIBE messages
      break;
    }
    
    case CAN_PS_SUBSCRIBE: {
      // Extended subscribe notification from broker (restored subscription)
      // Format (in buffer): [topicHash_h][topicHash_l][topicNameLen][topicName...]
      // Note: clientId was already extracted by processExtendedFrame from first byte
      if (length < 3) return;
      
      // The senderId parameter actually contains our clientId
      if (senderId != _clientId) return; // Not for us
      
      uint16_t topicHash = (data[0] << 8) | data[1];
      uint8_t topicLen = data[2];
      
      String topic = "";
      for (uint8_t i = 0; i < topicLen && (3 + i) < length; i++) {
        topic += (char)data[3 + i];
      }
      
      // Register the topic mapping locally
      if (topic.length() > 0) {
        registerTopic(topic);
      }
      
      // Add to local subscription list
      if (_subscribedTopicCount < MAX_CLIENT_TOPICS) {
        // Check if already subscribed
        bool alreadySubscribed = false;
        for (uint8_t i = 0; i < _subscribedTopicCount; i++) {
          if (_subscribedTopics[i] == topicHash) {
            alreadySubscribed = true;
            break;
          }
        }
        
        if (!alreadySubscribed) {
          _subscribedTopics[_subscribedTopicCount++] = topicHash;
        }
      }
      break;
    }
    
    case CAN_PS_SUB_RESTORE: {
      // Extended subscription restore from broker (with topic name)
      // Format (in buffer): [topicHash_h][topicHash_l][topicNameLen][topicName...]
      // Note: clientId was already extracted by processExtendedFrame from first byte
      if (length < 3) return;
      
      // The senderId parameter actually contains our clientId
      if (senderId != _clientId) return; // Not for us
      
      uint16_t topicHash = (data[0] << 8) | data[1];
      uint8_t topicLen = data[2];
      
      String topic = "";
      for (uint8_t i = 0; i < topicLen && (3 + i) < length; i++) {
        topic += (char)data[3 + i];
      }
      
      // Register the topic mapping locally
      if (topic.length() > 0) {
        registerTopic(topic);
      }
      
      // Add to local subscription list
      if (_subscribedTopicCount < MAX_CLIENT_TOPICS) {
        // Check if already subscribed
        bool alreadySubscribed = false;
        for (uint8_t i = 0; i < _subscribedTopicCount; i++) {
          if (_subscribedTopics[i] == topicHash) {
            alreadySubscribed = true;
            break;
          }
        }
        
        if (!alreadySubscribed) {
          _subscribedTopics[_subscribedTopicCount++] = topicHash;
        }
      }
      break;
    }
    
    case CAN_PS_TOPIC_DATA: {
      // Extended topic data
      // Format (in buffer): [topicHash_h][topicHash_l][message...]
      // Note: targetId (subscriber ID) was already extracted by processExtendedFrame from first byte
      if (length < 2) return;
      
      // The senderId parameter actually contains the targetId for this message type
      if (senderId != _clientId) return; // Not for us
      
      uint16_t topicHash = (data[0] << 8) | data[1];
      String message = "";
      
      for (size_t i = 2; i < length; i++) {
        message += (char)data[i];
      }
      
      if (_onMessage) {
        String topicName = getTopicName(topicHash);
        _onMessage(topicHash, topicName, message);
      }
      break;
    }
    
    case CAN_PS_DIRECT_MSG: {
      // Extended direct message
      // Format (in buffer): [targetId][message...]
      // Note: senderId was already extracted by processExtendedFrame from first byte
      if (length < 1) return;
      
      uint8_t targetId = data[0];
      
      if (targetId != _clientId) return; // Not for us
      
      String message = "";
      for (size_t i = 1; i < length; i++) {
        message += (char)data[i];
      }
      
      if (_onDirectMessage) {
        _onDirectMessage(senderId, message);
      }
      break;
    }
    
    case CAN_PS_PEER_MSG: {
      // Extended peer message (from another client)
      // Format (in buffer): [targetId][message...]
      // Note: senderId was already extracted by processExtendedFrame from first byte
      if (length < 1) return;
      
      uint8_t targetId = data[0];
      
      if (targetId != _clientId) return; // Not for us
      
      String message = "";
      for (size_t i = 1; i < length; i++) {
        message += (char)data[i];
      }
      
      // Deduplicate: skip if same sender and message within 50ms
      unsigned long now = millis();
      bool isDuplicate = (senderId == _lastPeerSenderId && 
                          message == String(_lastPeerMessage) &&
                          (now - _lastPeerMsgTime) < 50);
      
      // Call direct message callback (reuse for peer messages)
      if (!isDuplicate && _onDirectMessage) {
        _onDirectMessage(senderId, message);
        
        // Track this message to detect duplicates
        _lastPeerSenderId = senderId;
        _lastPeerMsgTime = now;
        strncpy(_lastPeerMessage, message.c_str(), 31);
        _lastPeerMessage[31] = '\0';
      }
      break;
    }
  }
}

// ===== Persistent Storage Implementation =====

void CANPubSubBroker::initStorage() {
  #ifdef ESP32
    // ESP32 uses Preferences (NVS)
    _preferences.begin(STORAGE_NAMESPACE, false);
  #else
    // Arduino uses EEPROM
    EEPROM.begin(EEPROM_SIZE);
  #endif
}

bool CANPubSubBroker::loadMappingsFromStorage() {
  #ifdef ESP32
    // ESP32 implementation using Preferences
    uint16_t magic = _preferences.getUShort("magic", 0);
    if (magic != STORAGE_MAGIC) {
      // No valid data stored
      return false;
    }
    
    _mappingCount = _preferences.getUChar("count", 0);
    _nextClientID = _preferences.getUChar("nextID", 0x01);
    
    if (_mappingCount > MAX_CLIENT_MAPPINGS) {
      _mappingCount = 0;
      return false;
    }
    
    // Load each mapping
    for (uint8_t i = 0; i < _mappingCount; i++) {
      String key = "map" + String(i);
      size_t len = _preferences.getBytesLength(key.c_str());
      if (len == sizeof(ClientMapping)) {
        _preferences.getBytes(key.c_str(), &_clientMappings[i], sizeof(ClientMapping));
      }
    }
    
    return true;
    
  #else
    // Arduino EEPROM implementation
    uint16_t magic;
    EEPROM.get(0, magic);
    
    if (magic != STORAGE_MAGIC) {
      // No valid data stored
      return false;
    }
    
    int addr = sizeof(uint16_t);
    EEPROM.get(addr, _mappingCount);
    addr += sizeof(uint8_t);
    
    EEPROM.get(addr, _nextClientID);
    addr += sizeof(uint8_t);
    
    if (_mappingCount > MAX_CLIENT_MAPPINGS) {
      _mappingCount = 0;
      return false;
    }
    
    // Load each mapping
    for (uint8_t i = 0; i < _mappingCount; i++) {
      EEPROM.get(addr, _clientMappings[i]);
      addr += sizeof(ClientMapping);
    }
    
    return true;
  #endif
}

bool CANPubSubBroker::saveMappingsToStorage() {
  #ifdef ESP32
    // ESP32 implementation using Preferences
    _preferences.putUShort("magic", STORAGE_MAGIC);
    _preferences.putUChar("count", _mappingCount);
    _preferences.putUChar("nextID", _nextClientID);
    
    // Save each mapping
    for (uint8_t i = 0; i < _mappingCount; i++) {
      String key = "map" + String(i);
      _preferences.putBytes(key.c_str(), &_clientMappings[i], sizeof(ClientMapping));
    }
    
    return true;
    
  #else
    // Arduino EEPROM implementation
    int addr = 0;
    
    // Write magic number
    EEPROM.put(addr, STORAGE_MAGIC);
    addr += sizeof(uint16_t);
    
    // Write count and next ID
    EEPROM.put(addr, _mappingCount);
    addr += sizeof(uint8_t);
    
    EEPROM.put(addr, _nextClientID);
    addr += sizeof(uint8_t);
    
    // Write each mapping
    for (uint8_t i = 0; i < _mappingCount; i++) {
      EEPROM.put(addr, _clientMappings[i]);
      addr += sizeof(ClientMapping);
    }
    
    #if defined(ESP8266)
      EEPROM.commit();
    #endif
    
    return true;
  #endif
}

bool CANPubSubBroker::clearStoredMappings() {
  _mappingCount = 0;
  _nextClientID = 0x01;
  memset(_clientMappings, 0, sizeof(_clientMappings));
  
  #ifdef ESP32
    _preferences.clear();
    return true;
  #else
    // Clear EEPROM by writing 0 to magic number
    uint16_t zero = 0;
    EEPROM.put(0, zero);
    #if defined(ESP8266)
      EEPROM.commit();
    #endif
    return true;
  #endif
}

// ===== Subscription Persistence Implementation =====

void CANPubSubBroker::storeClientSubscriptions(uint8_t clientId) {
  // Find or create stored subscription entry for this client
  int index = findStoredSubscription(clientId);
  
  if (index < 0) {
    // Create new entry if space available
    if (_storedSubCount >= MAX_CLIENT_MAPPINGS) return;
    index = _storedSubCount++;
    _storedSubscriptions[index].clientId = clientId;
  }
  
  // Collect all topics this client is subscribed to
  _storedSubscriptions[index].topicCount = 0;
  for (uint8_t i = 0; i < _subTableSize && _storedSubscriptions[index].topicCount < MAX_STORED_SUBS_PER_CLIENT; i++) {
    for (uint8_t j = 0; j < _subscriptions[i].subCount; j++) {
      if (_subscriptions[i].subscribers[j] == clientId) {
        _storedSubscriptions[index].topics[_storedSubscriptions[index].topicCount++] = _subscriptions[i].topicHash;
        break;
      }
    }
  }
  
  // Save to persistent storage
  saveSubscriptionsToStorage();
}

void CANPubSubBroker::restoreClientSubscriptions(uint8_t clientId) {
  // Find stored subscriptions for this client
  int index = findStoredSubscription(clientId);
  if (index < 0) return;
  
  // Restore each subscription to broker's internal table and notify client
  for (uint8_t i = 0; i < _storedSubscriptions[index].topicCount; i++) {
    uint16_t topicHash = _storedSubscriptions[index].topics[i];
    
    // Find if topic already exists in active subscriptions table
    int topicIndex = -1;
    for (uint8_t j = 0; j < _subTableSize; j++) {
      if (_subscriptions[j].topicHash == topicHash) {
        topicIndex = j;
        break;
      }
    }
    
    if (topicIndex >= 0) {
      // Topic exists - check if client is already subscribed
      bool alreadySubscribed = false;
      for (uint8_t k = 0; k < _subscriptions[topicIndex].subCount; k++) {
        if (_subscriptions[topicIndex].subscribers[k] == clientId) {
          alreadySubscribed = true;
          break;
        }
      }
      
      // Add client to this topic's subscribers if not already there
      if (!alreadySubscribed && _subscriptions[topicIndex].subCount < MAX_SUBSCRIBERS_PER_TOPIC) {
        _subscriptions[topicIndex].subscribers[_subscriptions[topicIndex].subCount++] = clientId;
      }
    } else {
      // Topic doesn't exist - create new topic entry
      if (_subTableSize < MAX_SUBSCRIPTIONS) {
        _subscriptions[_subTableSize].topicHash = topicHash;
        _subscriptions[_subTableSize].subscribers[0] = clientId;
        _subscriptions[_subTableSize].subCount = 1;
        _subTableSize++;
      }
    }
    
    // Get topic name from persistent storage (not just runtime mapping)
    String topicName = getStoredTopicName(topicHash);
    
    // Send topic name back to client using SUB_RESTORE message type
    // Format: [clientId][topicHash][topicNameLength][topicName]
    size_t totalSize = 1 + 2 + 1 + topicName.length();
    
    if (totalSize > CAN_FRAME_DATA_SIZE) {
      // Use extended message for long topic names
      uint8_t buffer[MAX_EXTENDED_MSG_SIZE];
      buffer[0] = clientId;
      buffer[1] = topicHash >> 8;
      buffer[2] = topicHash & 0xFF;
      buffer[3] = (uint8_t)topicName.length();
      memcpy(buffer + 4, topicName.c_str(), min(topicName.length(), (size_t)(MAX_EXTENDED_MSG_SIZE - 4)));
      
      sendExtendedMessage(CAN_PS_SUB_RESTORE, buffer, min(4 + topicName.length(), (size_t)MAX_EXTENDED_MSG_SIZE));
    } else {
      _can->beginPacket(CAN_PS_SUB_RESTORE);
      _can->write(clientId);
      _can->write(topicHash >> 8);
      _can->write(topicHash & 0xFF);
      _can->write((uint8_t)topicName.length());
      _can->print(topicName);
      _can->endPacket();
    }
    
    delay(15); // Small delay between messages
  }
}

int CANPubSubBroker::findStoredSubscription(uint8_t clientId) {
  for (uint8_t i = 0; i < _storedSubCount; i++) {
    if (_storedSubscriptions[i].clientId == clientId) {
      return i;
    }
  }
  return -1;
}

bool CANPubSubBroker::loadSubscriptionsFromStorage() {
  #ifdef ESP32
    // ESP32 implementation using Preferences
    uint16_t magic = _preferences.getUShort("subMagic", 0);
    if (magic != STORAGE_SUB_MAGIC) {
      // No valid subscription data stored
      return false;
    }
    
    _storedSubCount = _preferences.getUChar("subCount", 0);
    
    if (_storedSubCount > MAX_CLIENT_MAPPINGS) {
      _storedSubCount = 0;
      return false;
    }
    
    // Load each subscription set
    for (uint8_t i = 0; i < _storedSubCount; i++) {
      String key = "sub" + String(i);
      size_t len = _preferences.getBytesLength(key.c_str());
      if (len == sizeof(ClientSubscriptions)) {
        _preferences.getBytes(key.c_str(), &_storedSubscriptions[i], sizeof(ClientSubscriptions));
      }
    }
    
    return true;
    
  #else
    // Arduino EEPROM implementation
    // Calculate offset (after client mappings)
    int addr = sizeof(uint16_t) + sizeof(uint8_t) + sizeof(uint8_t) + 
               (MAX_CLIENT_MAPPINGS * sizeof(ClientMapping));
    
    uint16_t magic;
    EEPROM.get(addr, magic);
    
    if (magic != STORAGE_SUB_MAGIC) {
      // No valid subscription data stored
      return false;
    }
    
    addr += sizeof(uint16_t);
    EEPROM.get(addr, _storedSubCount);
    addr += sizeof(uint8_t);
    
    if (_storedSubCount > MAX_CLIENT_MAPPINGS) {
      _storedSubCount = 0;
      return false;
    }
    
    // Load each subscription set
    for (uint8_t i = 0; i < _storedSubCount; i++) {
      EEPROM.get(addr, _storedSubscriptions[i]);
      addr += sizeof(ClientSubscriptions);
    }
    
    return true;
  #endif
}

void CANPubSubBroker::restoreAllSubscriptionsToActiveTable() {
  // Restore all stored subscriptions to the active _subscriptions table
  // This runs at boot to make subscriptions immediately available
  
  for (uint8_t i = 0; i < _storedSubCount; i++) {
    ClientSubscriptions& clientSubs = _storedSubscriptions[i];
    
    // Process each topic this client is subscribed to
    for (uint8_t j = 0; j < clientSubs.topicCount && j < MAX_STORED_SUBS_PER_CLIENT; j++) {
      uint16_t topicHash = clientSubs.topics[j];
      uint8_t clientId = clientSubs.clientId;
      
      // Find if this topic already exists in active table
      int topicIndex = -1;
      for (uint8_t k = 0; k < _subTableSize; k++) {
        if (_subscriptions[k].topicHash == topicHash) {
          topicIndex = k;
          break;
        }
      }
      
      if (topicIndex >= 0) {
        // Topic exists, add client to subscribers if not already there
        bool alreadySubscribed = false;
        for (uint8_t s = 0; s < _subscriptions[topicIndex].subCount; s++) {
          if (_subscriptions[topicIndex].subscribers[s] == clientId) {
            alreadySubscribed = true;
            break;
          }
        }
        
        if (!alreadySubscribed && _subscriptions[topicIndex].subCount < MAX_SUBSCRIBERS_PER_TOPIC) {
          _subscriptions[topicIndex].subscribers[_subscriptions[topicIndex].subCount++] = clientId;
        }
      } else {
        // Topic doesn't exist, create new entry
        if (_subTableSize < MAX_SUBSCRIPTIONS) {
          _subscriptions[_subTableSize].topicHash = topicHash;
          _subscriptions[_subTableSize].subCount = 1;
          _subscriptions[_subTableSize].subscribers[0] = clientId;
          _subTableSize++;
        }
      }
    }
  }
}

bool CANPubSubBroker::saveSubscriptionsToStorage() {
  #ifdef ESP32
    // ESP32 implementation using Preferences
    _preferences.putUShort("subMagic", STORAGE_SUB_MAGIC);
    _preferences.putUChar("subCount", _storedSubCount);
    
    // Save each subscription set
    for (uint8_t i = 0; i < _storedSubCount; i++) {
      String key = "sub" + String(i);
      _preferences.putBytes(key.c_str(), &_storedSubscriptions[i], sizeof(ClientSubscriptions));
    }
    
    return true;
    
  #else
    // Arduino EEPROM implementation
    // Calculate offset (after client mappings)
    int addr = sizeof(uint16_t) + sizeof(uint8_t) + sizeof(uint8_t) + 
               (MAX_CLIENT_MAPPINGS * sizeof(ClientMapping));
    
    // Write magic number
    EEPROM.put(addr, STORAGE_SUB_MAGIC);
    addr += sizeof(uint16_t);
    
    // Write count
    EEPROM.put(addr, _storedSubCount);
    addr += sizeof(uint8_t);
    
    // Write each subscription set
    for (uint8_t i = 0; i < _storedSubCount; i++) {
      EEPROM.put(addr, _storedSubscriptions[i]);
      addr += sizeof(ClientSubscriptions);
    }
    
    #if defined(ESP8266)
      EEPROM.commit();
    #endif
    
    return true;
  #endif
}

bool CANPubSubBroker::clearStoredSubscriptions() {
  _storedSubCount = 0;
  memset(_storedSubscriptions, 0, sizeof(_storedSubscriptions));
  
  #ifdef ESP32
    _preferences.putUShort("subMagic", 0);
    _preferences.putUChar("subCount", 0);
    return true;
  #else
    // Clear subscription section by writing 0 to magic number
    int addr = sizeof(uint16_t) + sizeof(uint8_t) + sizeof(uint8_t) + 
               (MAX_CLIENT_MAPPINGS * sizeof(ClientMapping));
    uint16_t zero = 0;
    EEPROM.put(addr, zero);
    #if defined(ESP8266)
      EEPROM.commit();
    #endif
    return true;
  #endif
}

// ===== Ping Configuration Persistence Implementation =====

bool CANPubSubBroker::loadPingConfigFromStorage() {
  #ifdef ESP32
    // ESP32 implementation using Preferences
    // Load ping configuration values (use defaults if not set)
    _autoPingEnabled = _preferences.getBool("pingEnabled", false);
    _pingInterval = _preferences.getULong("pingInterval", 5000);
    _maxMissedPings = _preferences.getUChar("pingMaxMissed", 2);
    
    return true;
    
  #else
    // Arduino EEPROM implementation
    // Calculate offset (after client mappings and subscriptions)
    int addr = sizeof(uint16_t) + sizeof(uint8_t) + sizeof(uint8_t) + 
               (MAX_CLIENT_MAPPINGS * sizeof(ClientMapping)) +
               sizeof(uint16_t) + sizeof(uint8_t) +
               (MAX_CLIENT_MAPPINGS * sizeof(ClientSubscriptions));
    
    // Read ping configuration
    bool enabled;
    unsigned long interval;
    uint8_t maxMissed;
    
    EEPROM.get(addr, enabled);
    addr += sizeof(bool);
    
    EEPROM.get(addr, interval);
    addr += sizeof(unsigned long);
    
    EEPROM.get(addr, maxMissed);
    
    // Validate values before applying (simple sanity check)
    if (interval > 0 && interval < 3600000 && maxMissed > 0 && maxMissed < 255) {
      _autoPingEnabled = enabled;
      _pingInterval = interval;
      _maxMissedPings = maxMissed;
      return true;
    }
    
    // Invalid data, use defaults
    _autoPingEnabled = false;
    _pingInterval = 5000;
    _maxMissedPings = 2;
    return false;
  #endif
}

bool CANPubSubBroker::savePingConfigToStorage() {
  #ifdef ESP32
    // ESP32 implementation using Preferences
    _preferences.putBool("pingEnabled", _autoPingEnabled);
    _preferences.putULong("pingInterval", _pingInterval);
    _preferences.putUChar("pingMaxMissed", _maxMissedPings);
    
    return true;
    
  #else
    // Arduino EEPROM implementation
    // Calculate offset (after client mappings and subscriptions)
    int addr = sizeof(uint16_t) + sizeof(uint8_t) + sizeof(uint8_t) + 
               (MAX_CLIENT_MAPPINGS * sizeof(ClientMapping)) +
               sizeof(uint16_t) + sizeof(uint8_t) +
               (MAX_CLIENT_MAPPINGS * sizeof(ClientSubscriptions));
    
    // Write ping configuration
    EEPROM.put(addr, _autoPingEnabled);
    addr += sizeof(bool);
    
    EEPROM.put(addr, _pingInterval);
    addr += sizeof(unsigned long);
    
    EEPROM.put(addr, _maxMissedPings);
    
    #if defined(ESP8266)
      EEPROM.commit();
    #endif
    
    return true;
  #endif
}

bool CANPubSubBroker::clearStoredPingConfig() {
  // Reset to defaults
  _autoPingEnabled = false;
  _pingInterval = 5000;
  _maxMissedPings = 2;
  
  return savePingConfigToStorage();
}

// ===== Topic Name Persistence Implementation =====

void CANPubSubBroker::storeTopicName(uint16_t hash, const String& name) {
  // Check if topic name already stored
  int index = findStoredTopicName(hash);
  
  if (index >= 0) {
    // Update existing entry
    _storedTopicNames[index].setName(name);
    _storedTopicNames[index].active = true;
  } else {
    // Find empty slot or add new entry
    for (uint8_t i = 0; i < MAX_STORED_TOPIC_NAMES; i++) {
      if (!_storedTopicNames[i].active) {
        _storedTopicNames[i].hash = hash;
        _storedTopicNames[i].setName(name);
        _storedTopicNames[i].active = true;
        if (i >= _storedTopicCount) {
          _storedTopicCount = i + 1;
        }
        saveTopicNamesToStorage();
        return;
      }
    }
  }
  
  saveTopicNamesToStorage();
}

String CANPubSubBroker::getStoredTopicName(uint16_t hash) {
  int index = findStoredTopicName(hash);
  if (index >= 0) {
    return _storedTopicNames[index].getName();
  }
  return String("0x") + String(hash, HEX);
}

int CANPubSubBroker::findStoredTopicName(uint16_t hash) {
  for (uint8_t i = 0; i < _storedTopicCount; i++) {
    if (_storedTopicNames[i].active && _storedTopicNames[i].hash == hash) {
      return i;
    }
  }
  return -1;
}

bool CANPubSubBroker::loadTopicNamesFromStorage() {
  #ifdef ESP32
    // ESP32 implementation using Preferences
    uint16_t magic = _preferences.getUShort("topicMagic", 0);
    if (magic != STORAGE_TOPIC_MAGIC) {
      // No valid topic name data stored
      return false;
    }
    
    _storedTopicCount = _preferences.getUChar("topicCount", 0);
    
    if (_storedTopicCount > MAX_STORED_TOPIC_NAMES) {
      _storedTopicCount = 0;
      return false;
    }
    
    // Load each stored topic name
    for (uint8_t i = 0; i < _storedTopicCount; i++) {
      String key = "topic" + String(i);
      size_t len = _preferences.getBytesLength(key.c_str());
      if (len == sizeof(StoredTopicName)) {
        _preferences.getBytes(key.c_str(), &_storedTopicNames[i], sizeof(StoredTopicName));
        
        // Re-register topic in runtime mapping
        if (_storedTopicNames[i].active) {
          registerTopic(_storedTopicNames[i].getName());
        }
      }
    }
    
    return true;
    
  #else
    // Arduino EEPROM implementation
    // Calculate offset (after client mappings, subscriptions, and ping config)
    int addr = sizeof(uint16_t) + sizeof(uint8_t) + sizeof(uint8_t) + 
               (MAX_CLIENT_MAPPINGS * sizeof(ClientMapping)) +
               sizeof(uint16_t) + sizeof(uint8_t) +
               (MAX_CLIENT_MAPPINGS * sizeof(ClientSubscriptions)) +
               sizeof(bool) + sizeof(unsigned long) + sizeof(uint8_t);
    
    uint16_t magic;
    EEPROM.get(addr, magic);
    
    if (magic != STORAGE_TOPIC_MAGIC) {
      // No valid topic name data stored
      return false;
    }
    
    addr += sizeof(uint16_t);
    EEPROM.get(addr, _storedTopicCount);
    addr += sizeof(uint8_t);
    
    if (_storedTopicCount > MAX_STORED_TOPIC_NAMES) {
      _storedTopicCount = 0;
      return false;
    }
    
    // Load each stored topic name
    for (uint8_t i = 0; i < _storedTopicCount; i++) {
      EEPROM.get(addr, _storedTopicNames[i]);
      addr += sizeof(StoredTopicName);
      
      // Re-register topic in runtime mapping
      if (_storedTopicNames[i].active) {
        registerTopic(_storedTopicNames[i].getName());
      }
    }
    
    return true;
  #endif
}

bool CANPubSubBroker::saveTopicNamesToStorage() {
  #ifdef ESP32
    // ESP32 implementation using Preferences
    _preferences.putUShort("topicMagic", STORAGE_TOPIC_MAGIC);
    _preferences.putUChar("topicCount", _storedTopicCount);
    
    // Save each topic name
    for (uint8_t i = 0; i < _storedTopicCount; i++) {
      String key = "topic" + String(i);
      _preferences.putBytes(key.c_str(), &_storedTopicNames[i], sizeof(StoredTopicName));
    }
    
    return true;
    
  #else
    // Arduino EEPROM implementation
    // Calculate offset (after client mappings, subscriptions, and ping config)
    int addr = sizeof(uint16_t) + sizeof(uint8_t) + sizeof(uint8_t) + 
               (MAX_CLIENT_MAPPINGS * sizeof(ClientMapping)) +
               sizeof(uint16_t) + sizeof(uint8_t) +
               (MAX_CLIENT_MAPPINGS * sizeof(ClientSubscriptions)) +
               sizeof(bool) + sizeof(unsigned long) + sizeof(uint8_t);
    
    // Write magic number
    EEPROM.put(addr, STORAGE_TOPIC_MAGIC);
    addr += sizeof(uint16_t);
    
    // Write count
    EEPROM.put(addr, _storedTopicCount);
    addr += sizeof(uint8_t);
    
    // Write each topic name
    for (uint8_t i = 0; i < _storedTopicCount; i++) {
      EEPROM.put(addr, _storedTopicNames[i]);
      addr += sizeof(StoredTopicName);
    }
    
    #if defined(ESP8266)
      EEPROM.commit();
    #endif
    
    return true;
  #endif
}

bool CANPubSubBroker::clearStoredTopicNames() {
  _storedTopicCount = 0;
  memset(_storedTopicNames, 0, sizeof(_storedTopicNames));
  
  #ifdef ESP32
    _preferences.putUShort("topicMagic", 0);
    _preferences.putUChar("topicCount", 0);
    return true;
  #else
    // Clear topic names section by writing 0 to magic number
    int addr = sizeof(uint16_t) + sizeof(uint8_t) + sizeof(uint8_t) + 
               (MAX_CLIENT_MAPPINGS * sizeof(ClientMapping)) +
               sizeof(uint16_t) + sizeof(uint8_t) +
               (MAX_CLIENT_MAPPINGS * sizeof(ClientSubscriptions)) +
               sizeof(bool) + sizeof(unsigned long) + sizeof(uint8_t);
    uint16_t zero = 0;
    EEPROM.put(addr, zero);
    #if defined(ESP8266)
      EEPROM.commit();
    #endif
    return true;
  #endif
}

