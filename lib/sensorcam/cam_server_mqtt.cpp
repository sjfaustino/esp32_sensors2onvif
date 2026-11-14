// SensorCamServer: MQTT, through which Home Assistant finds the hub and its sensors by itself.
//
// The hub keeps one connection to the broker and sends one message at a time, built only when the one before has gone,
// so it never holds more than one. What it sends:
//   - the hub's status, "online", with "offline" left with the broker as the last will, so Home Assistant sees the hub
//     go the moment its connection drops, which nothing else can show;
//   - each sensor's state, retained, and sent again after every reconnect;
//   - every event in the history, read from the log as it is sent, so logging an alert needs nothing new and the events
//     of a broker outage are sent once it is back. Each waits for the broker's acknowledgement, and one not acknowledged
//     is sent again;
//   - Home Assistant's discovery messages, which create the devices and entities.
// And it hears one command: the alarm panel's, on <base>/armed/set. A disarm from there lasts the hub's re-arm time.
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <new>
#include "cam_mqtt.h"
#include "cam_net.h"
#include "cam_server.h"
#include "cam_server_internal.h"

static const int HUB_PARTS = 4;     // the hub's entities: alert, NVR listening, events, the alarm panel
// The hub's health, each a diagnostic sensor reading one member of <base>/hub/health.
struct HealthPart {
  const char* part;
  const char* name;
  const char* unit;          // nullptr for none
  const char* deviceClass;   // nullptr for none
  const char* stateClass;    // nullptr for none
};
static const HealthPart HEALTH[] = {
    {"free_kb", "Free memory", "kB", "data_size", "measurement"},
    {"lowest_kb", "Lowest free memory", "kB", "data_size", "measurement"},
    {"largest_kb", "Largest free block", "kB", "data_size", "measurement"},
    {"worst_largest_kb", "Smallest largest free block", "kB", "data_size", "measurement"},
    {"dropped", "Work dropped for lack of memory", nullptr, nullptr, "total_increasing"},
    {"espnow_dropped", "ESP-NOW messages dropped", nullptr, nullptr, "total_increasing"},
    {"rssi", "Wi-Fi signal", "dBm", "signal_strength", "measurement"},
    {"started", "Last started", nullptr, "timestamp", nullptr},
};
static const int HEALTH_PARTS = sizeof(HEALTH) / sizeof(HEALTH[0]);
static const int SENSOR_PARTS = 5;  // each sensor's: alert, battery, signal, problem, events
static const int SLOT_STEPS = SENSOR_PARTS + 1 + SENSOR_PARTS;  // taking an old sensor away (with its state), then telling of the new
static const int HUB_STEPS = HUB_PARTS + HEALTH_PARTS;
static const int DISCOVERY_STEPS = HUB_STEPS + CAM_MAX_SENSORS * SLOT_STEPS;
static const char* const SENSOR_PART[SENSOR_PARTS] = {"alert", "battery", "signal", "problem", "events"};
static const char* const SENSOR_COMPONENT[SENSOR_PARTS] = {"binary_sensor", "sensor", "sensor", "binary_sensor", "event"};
static const char* const SENSOR_EVENTS[] = {"alert", "delayed alert", "clear", "alert in quiet hours", "went silent", "low battery", "weak signal", "alert not delivered", "delayed alert not delivered", "alert while disarmed"};
static const char* const HUB_EVENTS[] = {"NVR not listening", "NVR listening again", "hub started", "network lost", "armed", "disarmed"};

// A sensor in topics and ids: its MAC address in lower-case hex, and its input.
static std::string sensorKey(const uint8_t mac[6], uint8_t input) {
  char key[20];
  snprintf(key, sizeof(key), "%02x%02x%02x%02x%02x%02x_%u", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], (unsigned)input);
  return key;
}

static std::string payload(const std::string& json) { return json.substr(0, json.size() - 1); }  // without the line break

std::string SensorCamServer::mqttBase() const { return "sensors2onvif/" + cfg_.serial; }

void SensorCamServer::startMqtt(uint32_t now) {
  uint32_t host = inet_addr(cfg_.mqtt.host.c_str());
  if (host == INADDR_NONE) {
    endMqtt("the broker address is not an IP address", now);
    return;
  }
  // Built before the connection is opened, so running out of memory leaves no connection behind.
  std::string hello = camMqttConnect("sensors2onvif-" + cfg_.serial, cfg_.mqtt.user, cfg_.mqtt.password, mqttBase() + "/status", "offline", CAM_MQTT_KEEPALIVE_S);
  int fd = (int)socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return;  // tried again on the next pass
  netNonblock(fd);
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(cfg_.mqtt.port);
  to.sin_addr.s_addr = host;
  mqtt_ = MqttIo();
  mqtt_.fd = fd;
  if (connect(fd, (sockaddr*)&to, sizeof(to)) != 0 && !netWouldBlock()) {
    endMqtt("could not connect to the broker", now);
    return;
  }
  mqtt_.out.swap(hello);
  mqtt_.state = MqttIo::CONNECTING;
  mqtt_.deadline = now + CAM_MQTT_WAIT_MS;
  mqtt_.heardAt = mqtt_.sentAt = now;
}

// An event not yet acknowledged is sent again on the next connection, because mqttEventsSent_ moves only on its PUBACK.
void SensorCamServer::endMqtt(const char* why, uint32_t now) {
  if (mqtt_.fd >= 0) netClose(mqtt_.fd);
  mqtt_ = MqttIo();
  mqttWaiting_ = true;
  mqttRetryAt_ = now + CAM_MQTT_RETRY_MS;
  mqttResult_ = why;  // last, as the one step that needs memory
}

void SensorCamServer::mqttPacket(const uint8_t* p, size_t len, uint32_t now) {
  mqtt_.heardAt = now;
  uint8_t type = p[0] >> 4;
  if (type == CAM_MQTT_CONNACK && mqtt_.state == MqttIo::WAIT_CONNACK) {
    uint8_t code = len >= 4 ? p[3] : 255;
    if (code != 0) {
      endMqtt(camMqttRefusal(code), now);
      return;
    }
    mqtt_.state = MqttIo::READY;
    mqttWaiting_ = false;
    mqttConnectedAt_ = time(nullptr);
    for (MqttShown& s : mqttShown_) s.told = false;  // the broker may have lost what it kept, so everything is sent again
    mqttResult_ = "connected";
  } else if (type == CAM_MQTT_PUBLISH) {  // the alarm panel, the one topic the hub subscribes to
    CamMqttMessage m;
    std::string topic = mqttBase() + "/armed/set";
    if (!camMqttReadPublish(p, len, m) || m.topicLen != topic.size() || memcmp(m.topic, topic.data(), m.topicLen) != 0) return;
    std::string command(m.payload, m.payloadLen);
    // ON and OFF are what the Armed switch of earlier firmware sent.
    if (command == "ARM_AWAY" || command == "ON") setArmed(true, "Home Assistant");
    if (command == "DISARM" || command == "OFF") setArmed(false, "Home Assistant", cfg_.rearmMinutes);
    mqtt_.hubTold = false;  // sent again either way, so a switch whose command failed shows the real state
  } else if (type == CAM_MQTT_PUBACK && len >= 4) {
    uint16_t id = (uint16_t)(p[2] << 8 | p[3]);
    if (mqtt_.ackId && id == mqtt_.ackId) {
      if (mqtt_.ackEvent > mqttEventsSent_) mqttEventsSent_ = mqtt_.ackEvent;
      mqtt_.ackId = 0;
    }
  }
}

void SensorCamServer::serviceMqtt(bool readable, bool writable, uint32_t now) {
  if (mqtt_.state == MqttIo::CONNECTING) {
    if (!writable) return;
    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(mqtt_.fd, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
    if (err) {
      endMqtt("could not connect to the broker", now);
      return;
    }
    mqtt_.state = MqttIo::WAIT_CONNACK;  // the CONNECT is already in out
    mqtt_.deadline = now + CAM_MQTT_WAIT_MS;
  }
  if (readable) {
    uint8_t buf[256];
    int n = (int)recv(mqtt_.fd, (char*)buf, sizeof(buf), 0);
    if (n == 0 || (n < 0 && !netWouldBlock())) {
      endMqtt("the broker closed the connection", now);
      return;
    }
    for (int at = 0; n > 0 && at < n;) {
      if (mqtt_.skip) {  // the rest of a packet too large to keep, which the hub has no use for
        size_t take = (size_t)(n - at) < mqtt_.skip ? (size_t)(n - at) : mqtt_.skip;
        mqtt_.skip -= take;
        at += (int)take;
        mqtt_.heardAt = now;
        continue;
      }
      size_t room = sizeof(mqtt_.in) - mqtt_.inLen, take = (size_t)(n - at) < room ? (size_t)(n - at) : room;
      memcpy(mqtt_.in + mqtt_.inLen, buf + at, take);
      mqtt_.inLen += take;
      at += (int)take;
      while (mqtt_.inLen) {
        long size = camMqttPacketSize(mqtt_.in, mqtt_.inLen);
        if (size < 0) {
          endMqtt("the broker sent something that is not MQTT", now);
          return;
        }
        if (size == 0) break;
        if ((size_t)size > sizeof(mqtt_.in)) {
          mqtt_.skip = (size_t)size - mqtt_.inLen;
          mqtt_.inLen = 0;
          mqtt_.heardAt = now;
          break;
        }
        if ((size_t)size > mqtt_.inLen) break;
        mqttPacket(mqtt_.in, (size_t)size, now);
        if (mqtt_.fd < 0) return;
        memmove(mqtt_.in, mqtt_.in + size, mqtt_.inLen - (size_t)size);
        mqtt_.inLen -= (size_t)size;
      }
    }
  }
  if (writable && mqtt_.outPos < mqtt_.out.size()) {
    int sent = (int)send(mqtt_.fd, mqtt_.out.data() + mqtt_.outPos, (int)(mqtt_.out.size() - mqtt_.outPos), 0);
    if (sent < 0 && !netWouldBlock()) {
      endMqtt("the connection to the broker failed", now);
      return;
    }
    if (sent > 0) mqtt_.outPos += (size_t)sent;
    if (mqtt_.outPos >= mqtt_.out.size()) {
      std::string().swap(mqtt_.out);
      mqtt_.outPos = 0;
      mqtt_.sentAt = now;
    }
  }
}

// In housekeeping, with its own guard: running out of memory here leaves the message unbuilt, to be built on a later
// pass, and the rest of housekeeping still runs.
void SensorCamServer::mqttHousekeeping(uint32_t now) {
  if (cfg_.mqtt.host.empty()) {
    if (mqtt_.fd >= 0) netClose(mqtt_.fd);
    mqtt_ = MqttIo();
    mqttWaiting_ = false;
    mqttEventsSent_ = log_.added();  // turned on later, it sends what happens from then on
    return;
  }
  try {
    if (mqtt_.state == MqttIo::OFF) {
      if ((!mqttWaiting_ || (int32_t)(now - mqttRetryAt_) >= 0) && sharedSockets() < MAX_HTTP && !updating()) startMqtt(now);
      return;
    }
    if (mqtt_.state != MqttIo::READY && (int32_t)(now - mqtt_.deadline) >= 0) {
      endMqtt(mqtt_.state == MqttIo::CONNECTING ? "could not connect to the broker" : "the broker did not answer", now);
      return;
    }
    if (mqtt_.state != MqttIo::READY) return;
    if (now - mqtt_.heardAt > CAM_MQTT_SILENT_MS) {
      endMqtt("the broker stopped answering", now);
      return;
    }
    if (mqtt_.ackId && now - mqtt_.ackSince > CAM_MQTT_WAIT_MS) {
      endMqtt("the broker did not acknowledge an event", now);
      return;
    }
    if (!mqtt_.out.empty()) return;
    std::string next;
    if (mqttNext(now, next)) mqtt_.out.swap(next);
  } catch (const std::bad_alloc&) {
    memoryFailures_++;
  }
}

// Alerts first, then events, then Home Assistant's discovery, then the rest of the sensors' states, then a ping.
// Each piece of state is marked as sent only once its message is built.
bool SensorCamServer::mqttNext(uint32_t now, std::string& out) {
  if (!mqtt_.online) {
    out = camMqttPublish(mqttBase() + "/status", "online", true, 0);
    mqtt_.online = true;
    return true;
  }
  if (!mqtt_.subscribed) {
    out = camMqttSubscribe(mqttBase() + "/armed/set", mqtt_.nextId);
    mqtt_.nextId = (uint16_t)(mqtt_.nextId == 0xffff ? 1 : mqtt_.nextId + 1);
    mqtt_.subscribed = true;
    return true;
  }
  bool alert = false;
  for (int id = 1; id <= sensors_.count(); id++) alert = alert || (slots_[id - 1].used && sensors_.active(id) && !sensors_.pastEvent(id));
  bool nvr = nvrListening(now);
  bool armed = cfg_.armed;
  int64_t until = cfg_.disarmedUntil;
  if (!mqtt_.hubTold || alert != mqtt_.hubAlert || nvr != mqtt_.hubNvr || armed != mqtt_.hubArmed || until != mqtt_.hubUntil) {
    bool known = !armed && until > 1;
    std::string json = jsonObject([&](CamJson& j) {
      j.str("alert", alert ? "ON" : "OFF").str("nvr", nvr ? "ON" : "OFF").str("armed", armed ? "ON" : "OFF");
      j.str("alarm", !armed ? "disarmed" : alert ? "triggered" : "armed_away").str("rearm_at", known, known ? iso((time_t)until) : std::string());
    });
    out = camMqttPublish(mqttBase() + "/hub/state", payload(json), true, 0);
    mqtt_.hubTold = true;
    mqtt_.hubAlert = alert;
    mqtt_.hubNvr = nvr;
    mqtt_.hubArmed = armed;
    mqtt_.hubUntil = until;
    return true;
  }
  for (int i = 0; i < CAM_MAX_SENSORS; i++)
    if (mqttSensorState(i, true, now, out)) return true;
  if (!mqtt_.ackId && mqttEvent(out)) {
    mqtt_.ackSince = now;
    return true;
  }
  if (!mqtt_.oldSwitchGone) {  // earlier firmware had a switch where the alarm panel now is
    out = camMqttPublish("homeassistant/switch/" + cfg_.serial + "/hub_armed/config", "", true, 0);
    mqtt_.oldSwitchGone = true;
    return true;
  }
  while (mqtt_.discovery >= 0) {
    int step = mqtt_.discovery;
    bool made = mqttDiscoveryStep(step, out);
    mqtt_.discovery = step + 1 < DISCOVERY_STEPS ? step + 1 : -1;
    if (made) return true;
  }
  for (int i = 0; i < CAM_MAX_SENSORS; i++)
    if (mqttSensorState(i, false, now, out)) return true;
  if (cfg_.healthInfo && (!mqtt_.healthTold || now - mqtt_.healthAt >= CAM_MQTT_HEALTH_MS)) {
    CamHealth h;
    cfg_.healthInfo(h);
    time_t t = time(nullptr);
    bool clockOk = t >= CAM_CLOCK_VALID_AFTER && (uint64_t)t > h.uptimeS;
    std::string json = jsonObject([&](CamJson& j) {
      j.num("free_kb", h.freeHeap / 1024).num("lowest_kb", h.lowestFreeHeap / 1024).num("largest_kb", h.largestBlock / 1024);
      j.num("worst_largest_kb", h.worstLargestBlock / 1024).num("dropped", memoryFailures_).num("espnow_dropped", h.espNowDropped);
      j.num("rssi", h.wifiRssi != 0, h.wifiRssi).num("uptime_s", (long long)h.uptimeS);
      j.str("started", clockOk, clockOk ? iso(t - (time_t)h.uptimeS) : std::string());
    });
    out = camMqttPublish(mqttBase() + "/hub/health", payload(json), true, 0);
    mqtt_.healthTold = true;
    mqtt_.healthAt = now;
    return true;
  }
  if (now - mqtt_.sentAt >= CAM_MQTT_PING_MS) {
    out = camMqttPing();
    return true;
  }
  return false;
}

// alertsOnly: only a sensor whose alert started or ended, or that has never been sent.
bool SensorCamServer::mqttSensorState(int i, bool alertsOnly, uint32_t now, std::string& out) {
  if (!slots_[i].used) return false;
  const Health& h = health_[i];
  MqttShown& s = mqttShown_[i];
  bool alert = sensors_.active(i + 1) && !sensors_.pastEvent(i + 1);  // a past event replaying for the NVR is not happening now
  bool changed = !s.told || alert != s.alert;
  if (alertsOnly && !changed) return false;
  int moved = h.rssi - s.rssi;
  bool signal = h.rssi != s.rssi && (s.rssi == 0 || h.rssi == 0 || moved >= 3 || moved <= -3) && now - s.at >= CAM_MQTT_SIGNAL_MS;
  if (!changed && h.silent == s.silent && h.lowBattery == s.low && h.weak == s.weak && h.battery == s.battery && !signal) return false;
  std::string json = jsonObject([&](CamJson& j) {
    j.str("alert", alert ? "ON" : "OFF").num("battery", h.battery != CAM_BATTERY_UNKNOWN, h.battery).num("rssi", h.rssi != 0, h.rssi);
    j.str("problem", h.silent || h.lowBattery || h.weak ? "ON" : "OFF").flag("silent", h.silent).flag("low_battery", h.lowBattery).flag("weak_signal", h.weak);
  });
  out = camMqttPublish(mqttBase() + "/" + sensorKey(slots_[i].mac, slots_[i].input) + "/state", payload(json), true, 0);
  s.told = true;
  s.alert = alert;
  s.silent = h.silent;
  s.low = h.lowBattery;
  s.weak = h.weak;
  s.battery = h.battery;
  s.rssi = h.rssi;
  s.at = now;
  return true;
}

// The next event of the history not yet acknowledged. Events from before the start were sent by the run before.
bool SensorCamServer::mqttEvent(std::string& out) {
  uint32_t added = log_.added(), kept = (uint32_t)log_.count();
  if (mqttEventsSent_ >= added) return false;
  if (added - mqttEventsSent_ > kept) {  // more happened while the broker was away than the log keeps in memory
    mqttEventsLost_ += added - mqttEventsSent_ - kept;
    mqttEventsSent_ = added - kept;
    if (!kept) return false;
  }
  uint32_t number = mqttEventsSent_ + 1;
  const CamEvent& e = log_.recent((int)(added - number));
  bool hub = strcmp(e.sensor, "hub") == 0;
  std::string key = "hub";
  if (!hub) {
    uint8_t mac[6];
    const char* slash = strchr(e.sensor, '/');
    if (!slash || !camParseMac(std::string(e.sensor, slash - e.sensor), mac)) {
      mqttEventsSent_ = number;  // cannot be placed; never happens for an event this hub logged
      return false;
    }
    key = sensorKey(mac, (uint8_t)atoi(slash + 1));
  }
  bool clockOk = e.received >= CAM_CLOCK_VALID_AFTER;
  std::string who = hub ? cfg_.hostName : e.text[0] ? std::string(e.text) + " (" + e.sensor + ")" : std::string(e.sensor);
  std::string json = jsonObject([&](CamJson& j) {
    j.str("event_type", camEventName(e.kind, e.active, e.delayed)).str("sensor", e.sensor).str(hub ? "detail" : "name", e.text);
    j.num("battery", e.battery != CAM_BATTERY_UNKNOWN, e.battery).str("time", clockOk, clockOk ? iso(e.received) : std::string());
    j.str("message", camEventMessage(e.kind, who, hub ? e.text : "", e.offS, e.delayed));
  });
  uint16_t id = mqtt_.nextId;
  out = camMqttPublish(mqttBase() + "/" + key + "/event", payload(json), false, id);
  mqtt_.nextId = (uint16_t)(id == 0xffff ? 1 : id + 1);
  mqtt_.ackId = id;
  mqtt_.ackEvent = number;
  return true;
}

// Home Assistant's discovery: one retained message per entity, under homeassistant/, naming the topic it reads.
bool SensorCamServer::mqttDiscoveryStep(int step, std::string& out) {
  std::string base = mqttBase(), hubId = "sensors2onvif_" + cfg_.serial;
  std::string hubName = cfg_.hostName.substr(0, cfg_.hostName.find('.'));
  auto common = [&](CamJson& j, const std::string& id, const char* name, const std::string& stateTopic) {
    j.str("name", name).str("unique_id", id).str("state_topic", stateTopic);
    j.str("availability_topic", base + "/status");
  };
  if (step < HUB_PARTS) {
    std::string web = "http://" + localIp_() + (cfg_.webPort == 80 ? std::string() : ":" + std::to_string(cfg_.webPort)) + "/";
    static const char* const PARTS[HUB_PARTS] = {"alert", "nvr", "events", "alarm"};
    static const char* const NAMES[HUB_PARTS] = {"Alert", "NVR listening", "Events", "Alarm"};
    static const char* const COMPONENTS[HUB_PARTS] = {"binary_sensor", "binary_sensor", "event", "alarm_control_panel"};
    const char* part = PARTS[step];
    std::string id = hubId + "_hub_" + part, json = jsonObject([&](CamJson& j) {
      common(j, id, NAMES[step], base + "/hub/" + (step == 2 ? "event" : "state"));
      if (step == 0) j.str("device_class", "motion").str("value_template", "{{ value_json.alert }}");
      if (step == 1) j.str("device_class", "connectivity").str("entity_category", "diagnostic").str("value_template", "{{ value_json.nvr }}");
      if (step == 2) {
        j.array("event_types");
        for (const char* type : HUB_EVENTS) j.str(type);
        j.end();
      }
      if (step == 3) {  // no code: Home Assistant guards who may use it
        j.str("value_template", "{{ value_json.alarm }}").str("command_topic", base + "/armed/set").str("payload_arm_away", "ARM_AWAY");
        j.str("payload_disarm", "DISARM").array("supported_features").str("arm_away").end().flag("code_arm_required", false).flag("code_disarm_required", false);
        j.str("json_attributes_topic", base + "/hub/state");
      }
      j.object("device").array("identifiers").str(hubId).end().str("name", hubName).str("manufacturer", "sensors2onvif").str("model", "ESP32 sensor hub");
      j.str("sw_version", cfg_.firmwareVersion).str("configuration_url", web).end();
    });
    out = camMqttPublish(std::string("homeassistant/") + COMPONENTS[step] + "/" + cfg_.serial + "/hub_" + part + "/config", payload(json), true, 0);
    return true;
  }
  if (step < HUB_STEPS) {  // the hub's health
    if (!cfg_.healthInfo) return false;
    const HealthPart& p = HEALTH[step - HUB_PARTS];
    std::string id = hubId + "_hub_" + p.part, json = jsonObject([&](CamJson& j) {
      common(j, id, p.name, base + "/hub/health");
      j.str("value_template", std::string("{{ value_json.") + p.part + " }}").str("entity_category", "diagnostic");
      if (p.unit) j.str("unit_of_measurement", p.unit);
      if (p.deviceClass) j.str("device_class", p.deviceClass);
      if (p.stateClass) j.str("state_class", p.stateClass);
      j.object("device").array("identifiers").str(hubId).end().end();
    });
    out = camMqttPublish("homeassistant/sensor/" + cfg_.serial + "/hub_" + p.part + "/config", payload(json), true, 0);
    return true;
  }
  int i = (step - HUB_STEPS) / SLOT_STEPS, k = (step - HUB_STEPS) % SLOT_STEPS;
  const Slot& s = slots_[i];
  MqttTold& t = mqttTold_[i];
  if (k <= SENSOR_PARTS) {  // a sensor deleted, or replaced in its slot: its entities and its retained state are taken away
    if (!t.used || (s.used && memcmp(t.mac, s.mac, 6) == 0 && t.input == s.input)) return false;
    std::string key = sensorKey(t.mac, t.input);
    out = k < SENSOR_PARTS ? camMqttPublish(std::string("homeassistant/") + SENSOR_COMPONENT[k] + "/" + cfg_.serial + "/" + key + "_" + SENSOR_PART[k] + "/config", "", true, 0)
                           : camMqttPublish(base + "/" + key + "/state", "", true, 0);
    if (k == SENSOR_PARTS) t.used = false;
    return true;
  }
  if (!s.used) return false;
  int part = k - SENSOR_PARTS - 1;
  std::string key = sensorKey(s.mac, s.input), id = hubId + "_" + key + "_" + SENSOR_PART[part], sensor = slotName(i + 1);
  std::string json = jsonObject([&](CamJson& j) {
    static const char* const NAMES[SENSOR_PARTS] = {"Alert", "Battery", "Signal", "Problem", "Events"};
    common(j, id, NAMES[part], base + "/" + key + (part == 4 ? "/event" : "/state"));
    if (part == 0) j.str("device_class", "motion").str("value_template", "{{ value_json.alert }}");
    if (part == 1) j.str("device_class", "battery").str("unit_of_measurement", "%").str("state_class", "measurement").str("entity_category", "diagnostic").str("value_template", "{{ value_json.battery }}");
    if (part == 2) j.str("device_class", "signal_strength").str("unit_of_measurement", "dBm").str("state_class", "measurement").str("entity_category", "diagnostic").str("value_template", "{{ value_json.rssi }}");
    if (part == 3) j.str("device_class", "problem").str("value_template", "{{ value_json.problem }}").str("json_attributes_topic", base + "/" + key + "/state");
    if (part == 4) {
      j.array("event_types");
      for (const char* type : SENSOR_EVENTS) j.str(type);
      j.end();
    }
    j.object("device").array("identifiers").str(hubId + "_" + key).end().str("name", s.name.empty() ? "Sensor " + sensor : s.name);
    j.str("model", "Sensor input " + std::to_string(s.input)).str("via_device", hubId).end();
  });
  out = camMqttPublish(std::string("homeassistant/") + SENSOR_COMPONENT[part] + "/" + cfg_.serial + "/" + key + "_" + SENSOR_PART[part] + "/config", payload(json), true, 0);
  if (part == SENSOR_PARTS - 1) {
    t.used = true;
    memcpy(t.mac, s.mac, 6);
    t.input = s.input;
  }
  return true;
}

std::string SensorCamServer::mqttJson() {
  const CamMqtt& m = cfg_.mqtt;
  bool ready = mqtt_.state == MqttIo::READY, clockOk = mqttConnectedAt_ >= CAM_CLOCK_VALID_AFTER;
  uint32_t waiting = log_.added() - mqttEventsSent_;
  return jsonObject([&](CamJson& j) {
    j.str("host", m.host).num("port", m.port).str("user", m.user).flag("password", !m.password.empty());
    j.flag("connected", ready).str("state", m.host.empty() ? std::string("not set up") : ready ? std::string("connected") : mqtt_.state != MqttIo::OFF ? std::string("connecting") : mqttResult_);
    j.str("since", ready && clockOk, ready && clockOk ? iso(mqttConnectedAt_) : std::string()).num("waiting", m.host.empty() ? 0 : waiting).num("lost", mqttEventsLost_);
    j.str("topic", mqttBase()).str("hub", cfg_.hostName.substr(0, cfg_.hostName.find('.')));
  });
}
