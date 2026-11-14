// MQTT 3.1.1, only the packets the hub sends and the replies it reads: enough to publish to a broker such as Home
// Assistant's Mosquitto add-on. The hub subscribes to nothing.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>

enum CamMqttType : uint8_t { CAM_MQTT_CONNACK = 2, CAM_MQTT_PUBACK = 4, CAM_MQTT_PINGRESP = 13 };

// A clean session with a last will: the broker publishes willMessage, retained, when the connection drops without a
// goodbye, which is how Home Assistant learns the hub is gone. An empty user sends neither user nor password.
std::string camMqttConnect(const std::string& clientId, const std::string& user, const std::string& password, const std::string& willTopic,
                           const std::string& willMessage, uint16_t keepAliveS);
// packetId 0 sends at QoS 0, anything else at QoS 1, which the broker acknowledges with a PUBACK of the same id.
std::string camMqttPublish(const std::string& topic, const std::string& payload, bool retain, uint16_t packetId);
std::string camMqttPing();
std::string camMqttDisconnect();

// The size of the packet at the start of data, header included: 0 when more bytes are needed to tell, -1 when what is
// there cannot be MQTT.
long camMqttPacketSize(const uint8_t* data, size_t len);

// Why a broker refused a connection, from the CONNACK's return code.
const char* camMqttRefusal(uint8_t code);
