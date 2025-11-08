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
  
  // Check if already registered
  for (uint8_t i = 0; i < _topicMappingCount; i++) {
    if (_topicMappings[i].hash == hash) {
      return; // Already registered
    }
  }
  
  // Add new mapping
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
    _nextClientID(0x10),
    _clientCount(0),
    _mappingCount(0),
    _storedSubCount(0),
    _onClientConnect(nullptr),
    _onClientDisconnect(nullptr),
    _onPublish(nullptr),
    _onDirectMessage(nullptr) {
  memset(_subscriptions, 0, sizeof(_subscriptions));
  memset(_connectedClients, 0, sizeof(_connectedClients));
  memset(_clientMappings, 0, sizeof(_clientMappings));
  memset(_storedSubscriptions, 0, sizeof(_storedSubscriptions));
}

bool CANPubSubBroker::begin() {
  _subTableSize = 0;
  _nextClientID = 0x10;
  _clientCount = 0;
  _mappingCount = 0;
  _storedSubCount = 0;
  
  // Initialize storage and load saved mappings
  initStorage();
  loadMappingsFromStorage();
  loadSubscriptionsFromStorage();
  
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
    case CAN_PS_PING:
      handlePing();
      break;
    case CAN_PS_ID_REQUEST:
      // Check if this is a request with serial number (has data)
      if (_can->available() > 0) {
        handleIdRequestWithSerial();
      } else {
        assignClientID(); // Old method for backward compatibility
      }
      break;
  }
}

void CANPubSubBroker::handleSubscribe() {
  if (_can->available() < 3) return;
  
  uint8_t clientId = _can->read();
  uint16_t topicHash = (_can->read() << 8) | _can->read();
  
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
  }
  
  addSubscription(clientId, topicHash);
  
  // Track connected client
  bool found = false;
  for (uint8_t i = 0; i < _clientCount; i++) {
    if (_connectedClients[i] == clientId) {
      found = true;
      break;
    }
  }
  if (!found && _clientCount < 256) {
    _connectedClients[_clientCount++] = clientId;
    
    // Restore stored subscriptions for this client (if any)
    restoreClientSubscriptions(clientId);
    
    if (_onClientConnect) {
      _onClientConnect(clientId);
    }
  }
}

void CANPubSubBroker::handleUnsubscribe() {
  if (_can->available() < 3) return;
  
  uint8_t clientId = _can->read();
  uint16_t topicHash = (_can->read() << 8) | _can->read();
  
  removeSubscription(clientId, topicHash);
}

void CANPubSubBroker::handlePublish() {
  if (_can->available() < 3) return;
  
  uint8_t publisherId = _can->read();
  uint16_t topicHash = (_can->read() << 8) | _can->read();
  
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
  
  // Send pong response
  _can->beginPacket(CAN_PS_PONG);
  _can->write(CAN_PS_BROKER_ID);
  _can->write(clientId);
  _can->endPacket();
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
          // Update stored subscriptions
          storeClientSubscriptions(clientId);
          return;
        }
      }
    }
  }
}

void CANPubSubBroker::removeAllSubscriptions(uint8_t clientId) {
  for (uint8_t i = 0; i < _subTableSize; i++) {
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
  _can->write(_nextClientID);
  _can->endPacket();
  
  _nextClientID++;
  if (_nextClientID == 0xFF) {
    _nextClientID = 0x10; // Wrap around, skip special IDs
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
  
  for (uint8_t i = 0; i < _subTableSize; i++) {
    uint16_t hash = _subscriptions[i].topicHash;
    String name = getTopicName(hash);
    uint8_t count = _subscriptions[i].subCount;
    callback(hash, name, count);
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
    // No serial number provided, use old method
    assignClientID();
    return;
  }
  
  // Find or create client ID for this serial number
  uint8_t assignedId = findOrCreateClientId(serialNumber);
  
  // Send response
  _can->beginPacket(CAN_PS_ID_RESPONSE);
  _can->write(assignedId);
  _can->endPacket();
}

uint8_t CANPubSubBroker::findOrCreateClientId(const String& serialNumber) {
  // Check if this serial number already has an ID
  int index = findClientMapping(serialNumber);
  
  if (index >= 0) {
    // Found existing mapping, mark as active and return the same ID
    _clientMappings[index].active = true;
    saveMappingsToStorage(); // Save state change
    return _clientMappings[index].clientId;
  }
  
  // No existing mapping, create a new one
  if (_mappingCount < MAX_CLIENT_MAPPINGS) {
    _clientMappings[_mappingCount].clientId = _nextClientID;
    _clientMappings[_mappingCount].setSerial(serialNumber);
    _clientMappings[_mappingCount].active = true;
    _mappingCount++;
    
    uint8_t assignedId = _nextClientID;
    
    // Increment for next client
    _nextClientID++;
    if (_nextClientID == 0xFF) {
      _nextClientID = 0x10; // Wrap around, skip special IDs
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
    _clientMappings[index].active = false;
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
    _clientMappings[index].active = false;
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
  return _mappingCount;
}

void CANPubSubBroker::listRegisteredClients(std::function<void(uint8_t id, const String& serial, bool active)> callback) {
  if (!callback) return;
  
  for (uint8_t i = 0; i < _mappingCount; i++) {
    callback(_clientMappings[i].clientId, 
             _clientMappings[i].getSerial(), 
             _clientMappings[i].active);
  }
}

void CANPubSubBroker::onExtendedMessageComplete(uint8_t msgType, uint8_t senderId, const uint8_t* data, size_t length) {
  // Handle extended messages based on type
  switch (msgType) {
    case CAN_PS_ID_REQUEST: {
      // Extended ID request with serial number (senderId is included in first byte by client)
      String serialNumber = "";
      for (size_t i = 0; i < length; i++) {
        serialNumber += (char)data[i];
      }
      
      uint8_t assignedId = findOrCreateClientId(serialNumber);
      
      // Send response
      _can->beginPacket(CAN_PS_ID_RESPONSE);
      _can->write(assignedId);
      _can->endPacket();
      break;
    }
    
    case CAN_PS_SUBSCRIBE: {
      // Extended subscribe with full topic name
      // Format: [clientId][topicHash_h][topicHash_l][topic_name...]
      if (length < 3) return;
      
      uint8_t clientId = data[0];
      uint16_t topicHash = (data[1] << 8) | data[2];
      String topicName = "";
      
      for (size_t i = 3; i < length; i++) {
        topicName += (char)data[i];
      }
      
      if (topicName.length() > 0) {
        registerTopic(topicName);
      }
      
      addSubscription(clientId, topicHash);
      
      // Track connected client
      bool found = false;
      for (uint8_t i = 0; i < _clientCount; i++) {
        if (_connectedClients[i] == clientId) {
          found = true;
          break;
        }
      }
      if (!found && _clientCount < 256) {
        _connectedClients[_clientCount++] = clientId;
        restoreClientSubscriptions(clientId);
        
        if (_onClientConnect) {
          _onClientConnect(clientId);
        }
      }
      break;
    }
    
    case CAN_PS_PUBLISH: {
      // Extended publish with long message
      // Format: [publisherId][topicHash_h][topicHash_l][message...]
      if (length < 3) return;
      
      uint8_t publisherId = data[0];
      uint16_t topicHash = (data[1] << 8) | data[2];
      String message = "";
      
      for (size_t i = 3; i < length; i++) {
        message += (char)data[i];
      }
      
      String topicName = getTopicName(topicHash);
      
      if (_onPublish) {
        _onPublish(topicHash, topicName, message);
      }
      
      forwardToSubscribers(topicHash, message);
      break;
    }
    
    case CAN_PS_DIRECT_MSG: {
      // Extended direct message
      // Format: [senderId][message...]
      if (length < 1) return;
      
      uint8_t actualSenderId = data[0];
      String message = "";
      
      for (size_t i = 1; i < length; i++) {
        message += (char)data[i];
      }
      
      if (_onDirectMessage) {
        _onDirectMessage(actualSenderId, message);
      }
      
      // Send acknowledgment
      _can->beginPacket(CAN_PS_ACK);
      _can->write(CAN_PS_BROKER_ID);
      _can->write(actualSenderId);
      _can->print("ACK");
      _can->endPacket();
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
    _onMessage(nullptr),
    _onDirectMessage(nullptr),
    _onConnect(nullptr),
    _onDisconnect(nullptr) {
  memset(_subscribedTopics, 0, sizeof(_subscribedTopics));
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
  _serialNumber = serialNumber;
  requestClientIDWithSerial(serialNumber);
  
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
    case CAN_PS_TOPIC_DATA:
      handleTopicData();
      break;
    case CAN_PS_DIRECT_MSG:
      handleDirectMessageReceived();
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
  
  _clientId = _can->read();
  _connected = true;
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
  }
}

void CANPubSubClient::requestClientID() {
  _can->beginPacket(CAN_PS_ID_REQUEST);
  _can->endPacket();
}

void CANPubSubClient::requestClientIDWithSerial(const String& serialNumber) {
  // Use extended message for serial numbers > 8 bytes
  if (serialNumber.length() > CAN_FRAME_DATA_SIZE) {
    sendExtendedMessage(CAN_PS_ID_REQUEST, (const uint8_t*)serialNumber.c_str(), serialNumber.length());
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
    case CAN_PS_TOPIC_DATA: {
      // Extended topic data
      // Format: [targetId][topicHash_h][topicHash_l][message...]
      if (length < 3) return;
      
      uint8_t targetId = data[0];
      if (targetId != _clientId) return; // Not for us
      
      uint16_t topicHash = (data[1] << 8) | data[2];
      String message = "";
      
      for (size_t i = 3; i < length; i++) {
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
      // Format: [senderId][targetId][message...]
      if (length < 2) return;
      
      uint8_t actualSenderId = data[0];
      uint8_t targetId = data[1];
      
      if (targetId != _clientId) return; // Not for us
      
      String message = "";
      for (size_t i = 2; i < length; i++) {
        message += (char)data[i];
      }
      
      if (_onDirectMessage) {
        _onDirectMessage(actualSenderId, message);
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
    _nextClientID = _preferences.getUChar("nextID", 0x10);
    
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
  _nextClientID = 0x10;
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
  
  // Restore each subscription
  for (uint8_t i = 0; i < _storedSubscriptions[index].topicCount; i++) {
    uint16_t topicHash = _storedSubscriptions[index].topics[i];
    addSubscription(clientId, topicHash);
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
