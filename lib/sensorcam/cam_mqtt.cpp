#include "cam_mqtt.h"

// The remaining length, 7 bits a byte, lowest first, the top bit saying another byte follows.
static void putLength(std::string& out, size_t n) {
  do {
    uint8_t b = n % 128;
    n /= 128;
    out += (char)(n ? b | 0x80 : b);
  } while (n);
}

static void putText(std::string& out, const std::string& s) {
  out += (char)(s.size() >> 8);
  out += (char)(s.size() & 0xff);
  out += s;
}

static std::string packet(uint8_t first, const std::string& body) {
  std::string out;
  out.reserve(body.size() + 5);
  out += (char)first;
  putLength(out, body.size());
  out += body;
  return out;
}

std::string camMqttConnect(const std::string& clientId, const std::string& user, const std::string& password, const std::string& willTopic,
                           const std::string& willMessage, uint16_t keepAliveS) {
  std::string body;
  putText(body, "MQTT");
  body += (char)4;  // protocol level: 3.1.1
  uint8_t flags = 0x02 | 0x04 | 0x08 | 0x20;  // clean session, a will at QoS 1, retained
  if (!user.empty()) flags |= 0x80 | 0x40;
  body += (char)flags;
  body += (char)(keepAliveS >> 8);
  body += (char)(keepAliveS & 0xff);
  putText(body, clientId);
  putText(body, willTopic);
  putText(body, willMessage);
  if (!user.empty()) {
    putText(body, user);
    putText(body, password);
  }
  return packet(0x10, body);
}

std::string camMqttPublish(const std::string& topic, const std::string& payload, bool retain, uint16_t packetId) {
  std::string body;
  body.reserve(topic.size() + payload.size() + 4);
  putText(body, topic);
  if (packetId) {
    body += (char)(packetId >> 8);
    body += (char)(packetId & 0xff);
  }
  body += payload;
  return packet((uint8_t)(0x30 | (packetId ? 0x02 : 0) | (retain ? 0x01 : 0)), body);
}

std::string camMqttSubscribe(const std::string& topic, uint16_t packetId) {
  std::string body;
  body += (char)(packetId >> 8);
  body += (char)(packetId & 0xff);
  putText(body, topic);
  body += (char)0;  // QoS 0
  return packet(0x82, body);
}

bool camMqttReadPublish(const uint8_t* p, size_t len, CamMqttMessage& out) {
  long size = camMqttPacketSize(p, len);
  if (size <= 0 || (size_t)size != len || (p[0] >> 4) != CAM_MQTT_PUBLISH) return false;
  size_t at = 1;
  while (p[at] & 0x80) at++;
  at++;  // past the length
  if (at + 2 > len) return false;
  size_t topicLen = (size_t)(p[at] << 8 | p[at + 1]);
  at += 2;
  if (at + topicLen > len) return false;
  out.topic = (const char*)p + at;
  out.topicLen = topicLen;
  at += topicLen;
  if ((p[0] >> 1) & 3) at += 2;  // a packet id, at QoS 1 or 2
  if (at > len) return false;
  out.payload = (const char*)p + at;
  out.payloadLen = len - at;
  return true;
}

std::string camMqttPing() { return std::string("\xc0\x00", 2); }
std::string camMqttDisconnect() { return std::string("\xe0\x00", 2); }

long camMqttPacketSize(const uint8_t* data, size_t len) {
  size_t length = 0, scale = 1;
  for (size_t i = 1; i <= 4; i++) {
    if (i >= len) return 0;
    length += (data[i] & 0x7f) * scale;
    if (!(data[i] & 0x80)) return (long)(1 + i + length);
    scale *= 128;
  }
  return -1;  // a length may take at most four bytes
}

const char* camMqttRefusal(uint8_t code) {
  switch (code) {
    case 1: return "the broker refused: it does not speak MQTT 3.1.1";
    case 2: return "the broker refused the hub's client name";
    case 3: return "the broker refused: it is not available";
    case 4: return "the broker refused the user name or password";
    case 5: return "the broker refused: not authorized";
    default: return "the broker refused the connection";
  }
}
