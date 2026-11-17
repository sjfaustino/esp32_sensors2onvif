// CamMqttClient: MQTT, through which Home Assistant finds the hub and its sensors by itself.
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
// And it hears two commands: the alarm panel's, on <base>/armed/set, where a disarm lasts the hub's re-arm time; and each
// sensor's test button, on <base>/<sensor>/test.
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <new>
#include "cam_mqtt.h"
#include "cam_number.h"
#include "cam_net.h"
#include "cam_mqtt_client.h"
#include "cam_server_internal.h"

static const int HUB_PARTS = 4;  // the hub's entities: alert, NVR listening, events, the alarm panel
// The hub's health, each a diagnostic sensor reading one member of <base>/hub/health.
struct HealthPart {
  const char* part;
  const char* name;
  const char* unit;         // nullptr for none
  const char* deviceClass;  // nullptr for none
  const char* stateClass;   // nullptr for none
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
static const int SENSOR_PARTS = 6;                              // each sensor's: alert, battery, signal, problem, events, test button
static const int SLOT_STEPS = SENSOR_PARTS + 1 + SENSOR_PARTS;  // taking an old sensor away (with its state), then telling of the new
static const int HUB_STEPS = HUB_PARTS + HEALTH_PARTS;
static const int DISCOVERY_STEPS = HUB_STEPS + CAM_MAX_SENSORS * SLOT_STEPS;
static const char* const SENSOR_PART[SENSOR_PARTS] = {"alert", "battery", "signal", "problem", "events", "test"};
static const char* const SENSOR_COMPONENT[SENSOR_PARTS] = {"binary_sensor", "sensor", "sensor", "binary_sensor", "event", "button"};
// Both kinds of events entity are told every name camEventName gives, so no event can arrive that Home Assistant was
// not told to expect, which would make it refuse the event.

// A sensor in topics and ids: its MAC address in lower-case hex, and its input.
static std::string sensorKey(const uint8_t mac[6], uint8_t input) {
  char key[20];
  snprintf(key, sizeof(key), "%02x%02x%02x%02x%02x%02x_%u", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], (unsigned)input);
  return key;
}

static std::string payload(const std::string& json) { return json.substr(0, json.size() - 1); }  // without the line break

std::string CamMqttClient::topicBase() const { return "sensors2onvif/" + cfg_.serial; }

void CamMqttClient::start(uint32_t now) {
  uint32_t host = inet_addr(settings_.host.c_str());
  if (host == INADDR_NONE) {
    stop("the broker address is not an IP address", now);
    return;
  }
  // Built before the connection is opened, so running out of memory leaves no connection behind.
  std::string hello =
      camMqttConnect("sensors2onvif-" + cfg_.serial, settings_.user, settings_.password, topicBase() + "/status", "offline", CAM_MQTT_KEEPALIVE_S);
  int fd = (int)socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return;  // tried again on the next pass
  netNonblock(fd);
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(settings_.port);
  to.sin_addr.s_addr = host;
  io_ = Io();
  io_.fd = fd;
  if (connect(fd, (sockaddr*)&to, sizeof(to)) != 0 && !netWouldBlock()) {
    stop("could not connect to the broker", now);
    return;
  }
  io_.out.swap(hello);
  io_.state = Io::CONNECTING;
  io_.deadline = now + CAM_MQTT_WAIT_MS;
  io_.heardAt = io_.sentAt = now;
}

// An event not yet acknowledged is sent again on the next connection, because eventsSent_ moves only on its PUBACK.
void CamMqttClient::stop(const char* why, uint32_t now) {
  if (io_.fd >= 0) netClose(io_.fd);
  io_ = Io();
  waiting_ = true;
  retryAt_ = now + CAM_MQTT_RETRY_MS;
  result_ = why;  // last, as the one step that needs memory
}

void CamMqttClient::packet(const uint8_t* p, size_t len, uint32_t now) {
  io_.heardAt = now;
  uint8_t type = p[0] >> 4;
  if (type == CAM_MQTT_CONNACK && io_.state == Io::WAIT_CONNACK) {
    uint8_t code = len >= 4 ? p[3] : 255;
    if (code != 0) {
      stop(camMqttRefusal(code), now);
      return;
    }
    io_.state = Io::READY;
    waiting_ = false;
    connectedAt_ = time(nullptr);
    for (Shown& s : shown_) s.told = false;  // the broker may have lost what it kept, so everything is sent again
    result_ = "connected";
  } else if (type == CAM_MQTT_PUBLISH) {  // the alarm panel, or a sensor's test button
    CamMqttMessage m;
    // A retained command was sent some time ago and is repeated by the broker on every connection: obeying it could
    // keep disarming the hub. Home Assistant does not retain its commands.
    if ((p[0] & 0x01) || !camMqttReadPublish(p, len, m)) return;
    std::string topic(m.topic, m.topicLen), base = topicBase() + "/";
    if (topic.compare(0, base.size(), base) != 0) return;
    topic.erase(0, base.size());
    if (topic.size() > 5 && topic.compare(topic.size() - 5, 5, "/test") == 0) {  // <sensor>/test, the sensor as its MAC address in hex, _, its input
      std::string key = topic.substr(0, topic.size() - 5);
      uint8_t mac[6];
      size_t underscore = key.find('_');
      uint8_t input = 0;
      bool parsed = underscore == 12 && camParseHex(key.substr(0, 12), mac, 6) && camNumber(key.substr(13), input);
      if (parsed) alarm_.testAlert(alarm_.findSlot(mac, input));
      return;
    }
    if (topic != "armed/set") return;
    std::string command(m.payload, m.payloadLen);
    // ON and OFF are what the Armed switch of earlier firmware sent.
    if (command == "ARM_AWAY" || command == "ON") alarm_.setArmMode(CAM_ARMED_AWAY, "Home Assistant");
    if (command == "ARM_HOME") alarm_.setArmMode(CAM_ARMED_HOME, "Home Assistant");
    if (command == "DISARM" || command == "OFF") alarm_.setArmMode(CAM_DISARMED, "Home Assistant", alarm_.rearmMinutes());
    io_.hubTold = false;  // sent again either way, so a switch whose command failed shows the real state
  } else if (type == CAM_MQTT_PUBACK && len >= 4) {
    uint16_t id = (uint16_t)(p[2] << 8 | p[3]);
    if (io_.ackId && id == io_.ackId) {
      if (io_.ackEvent > eventsSent_) eventsSent_ = io_.ackEvent;
      io_.ackId = 0;
    }
  }
}

void CamMqttClient::service(bool readable, bool writable, uint32_t now) {
  if (io_.state == Io::CONNECTING) {
    if (!writable) return;
    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(io_.fd, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
    if (err) {
      stop("could not connect to the broker", now);
      return;
    }
    io_.state = Io::WAIT_CONNACK;  // the CONNECT is already in out
    io_.deadline = now + CAM_MQTT_WAIT_MS;
  }
  if (readable) {
    uint8_t buf[256];
    int n = (int)recv(io_.fd, (char*)buf, sizeof(buf), 0);
    if (n == 0 || (n < 0 && !netWouldBlock())) {
      stop("the broker closed the connection", now);
      return;
    }
    for (int at = 0; n > 0 && at < n;) {
      if (io_.skip) {  // the rest of a packet too large to keep, which the hub has no use for
        size_t take = (size_t)(n - at) < io_.skip ? (size_t)(n - at) : io_.skip;
        io_.skip -= take;
        at += (int)take;
        io_.heardAt = now;
        continue;
      }
      size_t room = sizeof(io_.in) - io_.inLen, take = (size_t)(n - at) < room ? (size_t)(n - at) : room;
      memcpy(io_.in + io_.inLen, buf + at, take);
      io_.inLen += take;
      at += (int)take;
      while (io_.inLen) {
        long size = camMqttPacketSize(io_.in, io_.inLen);
        if (size < 0) {
          stop("the broker sent something that is not MQTT", now);
          return;
        }
        if (size == 0) break;
        if ((size_t)size > sizeof(io_.in)) {
          io_.skip = (size_t)size - io_.inLen;
          io_.inLen = 0;
          io_.heardAt = now;
          break;
        }
        if ((size_t)size > io_.inLen) break;
        packet(io_.in, (size_t)size, now);
        if (io_.fd < 0) return;
        memmove(io_.in, io_.in + size, io_.inLen - (size_t)size);
        io_.inLen -= (size_t)size;
      }
    }
  }
  if (writable && io_.outPos < io_.out.size()) {
    int sent = (int)send(io_.fd, io_.out.data() + io_.outPos, (int)(io_.out.size() - io_.outPos), 0);
    if (sent < 0 && !netWouldBlock()) {
      stop("the connection to the broker failed", now);
      return;
    }
    if (sent > 0) io_.outPos += (size_t)sent;
    if (io_.outPos >= io_.out.size()) {
      std::string().swap(io_.out);
      io_.outPos = 0;
      io_.sentAt = now;
    }
  }
}

void CamMqttClient::tick(uint32_t now, bool mayConnect) {
  if (settings_.host.empty()) {
    if (io_.fd >= 0) netClose(io_.fd);
    io_ = Io();
    waiting_ = false;
    eventsSent_ = alarm_.log().added();  // turned on later, it sends what happens from then on
    return;
  }
  if (io_.state == Io::OFF) {
    if ((!waiting_ || (int32_t)(now - retryAt_) >= 0) && mayConnect) start(now);
    return;
  }
  if (io_.state != Io::READY && (int32_t)(now - io_.deadline) >= 0) {
    stop(io_.state == Io::CONNECTING ? "could not connect to the broker" : "the broker did not answer", now);
    return;
  }
  if (io_.state != Io::READY) return;
  if (now - io_.heardAt > CAM_MQTT_SILENT_MS) {
    stop("the broker stopped answering", now);
    return;
  }
  if (io_.ackId && now - io_.ackSince > CAM_MQTT_WAIT_MS) {
    stop("the broker did not acknowledge an event", now);
    return;
  }
  if (!io_.out.empty()) return;
  std::string next;
  if (nextMessage(now, next)) io_.out.swap(next);
}

// Alerts first, then events, then Home Assistant's discovery, then the rest of the sensors' states, then a ping.
// Each piece of state is marked as sent only once its message is built.
bool CamMqttClient::nextMessage(uint32_t now, std::string& out) {
  if (!io_.online) {
    out = camMqttPublish(topicBase() + "/status", "online", true, 0);
    io_.online = true;
    return true;
  }
  if (io_.subscribed < 2) {
    out = camMqttSubscribe(topicBase() + (io_.subscribed == 0 ? "/armed/set" : "/+/test"), io_.nextId);
    io_.nextId = (uint16_t)(io_.nextId == 0xffff ? 1 : io_.nextId + 1);
    io_.subscribed++;
    return true;
  }
  bool alert = false;
  for (int id = 1; id <= alarm_.board().count(); id++)
    alert = alert || (alarm_.slotOf(id).used && alarm_.board().active(id) && !alarm_.board().pastEvent(id));
  bool nvr = outside_.nvrListening(now);
  bool armed = alarm_.armed(), home = alarm_.armMode() == CAM_ARMED_HOME;
  int64_t until = alarm_.disarmedUntil();
  if (!io_.hubTold || alert != io_.hubAlert || nvr != io_.hubNvr || armed != io_.hubArmed || home != io_.hubHome || until != io_.hubUntil) {
    bool known = !armed && until > 1;
    std::string json = jsonObject([&](CamJson& j) {
      j.str("alert", alert ? "ON" : "OFF").str("nvr", nvr ? "ON" : "OFF").str("armed", armed ? "ON" : "OFF");
      j.str("alarm", !armed  ? "disarmed"
                     : alert ? "triggered"
                     : home  ? "armed_home"
                             : "armed_away")
          .str("rearm_at", known, known ? iso((time_t)until) : std::string());
    });
    out = camMqttPublish(topicBase() + "/hub/state", payload(json), true, 0);
    io_.hubTold = true;
    io_.hubAlert = alert;
    io_.hubNvr = nvr;
    io_.hubArmed = armed;
    io_.hubHome = home;
    io_.hubUntil = until;
    return true;
  }
  for (int i = 0; i < CAM_MAX_SENSORS; i++)
    if (sensorState(i, true, now, out)) return true;
  if (!io_.ackId && eventMessage(out)) {
    io_.ackSince = now;
    return true;
  }
  if (!io_.oldSwitchGone) {  // earlier firmware had a switch where the alarm panel now is
    out = camMqttPublish("homeassistant/switch/" + cfg_.serial + "/hub_armed/config", "", true, 0);
    io_.oldSwitchGone = true;
    return true;
  }
  while (io_.discovery >= 0) {
    int step = io_.discovery;
    bool made = discoveryStep(step, out);
    io_.discovery = step + 1 < DISCOVERY_STEPS ? step + 1 : -1;
    if (made) return true;
  }
  for (int i = 0; i < CAM_MAX_SENSORS; i++)
    if (sensorState(i, false, now, out)) return true;
  if (chip_->tellsHealth() && (!io_.healthTold || now - io_.healthAt >= CAM_MQTT_HEALTH_MS)) {
    CamHealth h;
    chip_->health(h);
    time_t t = time(nullptr);
    bool clockOk = t >= CAM_CLOCK_VALID_AFTER && (uint64_t)t > h.uptimeS;
    std::string json = jsonObject([&](CamJson& j) {
      j.num("free_kb", h.freeHeap / 1024).num("lowest_kb", h.lowestFreeHeap / 1024).num("largest_kb", h.largestBlock / 1024);
      j.num("worst_largest_kb", h.worstLargestBlock / 1024).num("dropped", outside_.memoryFailures()).num("espnow_dropped", h.espNowDropped);
      j.num("rssi", h.wifiRssi != 0, h.wifiRssi).num("uptime_s", (long long)h.uptimeS);
      j.str("started", clockOk, clockOk ? iso(t - (time_t)h.uptimeS) : std::string());
    });
    out = camMqttPublish(topicBase() + "/hub/health", payload(json), true, 0);
    io_.healthTold = true;
    io_.healthAt = now;
    return true;
  }
  if (now - io_.sentAt >= CAM_MQTT_PING_MS) {
    out = camMqttPing();
    return true;
  }
  return false;
}

// alertsOnly: only a sensor whose alert started or ended, or that has never been sent.
bool CamMqttClient::sensorState(int i, bool alertsOnly, uint32_t now, std::string& out) {
  if (!alarm_.slotOf(i + 1).used) return false;
  const CamAlarm::Health& h = alarm_.healthOf(i + 1);
  Shown& s = shown_[i];
  bool alert = alarm_.board().active(i + 1) && !alarm_.board().pastEvent(i + 1);  // a past event replaying for the NVR is not happening now
  bool changed = !s.told || alert != s.alert;
  if (alertsOnly && !changed) return false;
  int moved = h.rssi - s.rssi;
  bool signal = h.rssi != s.rssi && (s.rssi == 0 || h.rssi == 0 || moved >= 3 || moved <= -3) && now - s.at >= CAM_MQTT_SIGNAL_MS;
  if (!changed && h.silent == s.silent && h.lowBattery == s.low && h.weak == s.weak && h.battery == s.battery && !signal) return false;
  std::string json = jsonObject([&](CamJson& j) {
    j.str("alert", alert ? "ON" : "OFF").num("battery", h.battery != CAM_BATTERY_UNKNOWN, h.battery).num("rssi", h.rssi != 0, h.rssi);
    j.str("problem", h.silent || h.lowBattery || h.weak ? "ON" : "OFF")
        .flag("silent", h.silent)
        .flag("low_battery", h.lowBattery)
        .flag("weak_signal", h.weak);
  });
  out = camMqttPublish(topicBase() + "/" + sensorKey(alarm_.slotOf(i + 1).mac, alarm_.slotOf(i + 1).input) + "/state", payload(json), true, 0);
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
bool CamMqttClient::eventMessage(std::string& out) {
  uint32_t added = alarm_.log().added(), kept = (uint32_t)alarm_.log().count();
  if (eventsSent_ >= added) return false;
  if (added - eventsSent_ > kept) {  // more happened while the broker was away than the log keeps in memory
    eventsLost_ += added - eventsSent_ - kept;
    eventsSent_ = added - kept;
    if (!kept) return false;
  }
  uint32_t number = eventsSent_ + 1;
  const CamEvent& e = alarm_.log().recent((int)(added - number));
  bool hub = strcmp(e.sensor, "hub") == 0;
  std::string key = "hub";
  if (!hub) {
    uint8_t mac[6], input = 0;
    const char* slash = strchr(e.sensor, '/');
    if (!slash || !camParseMac(std::string(e.sensor, slash - e.sensor), mac) || !camNumber(std::string(slash + 1), input)) {
      eventsSent_ = number;  // cannot be placed; never happens for an event this hub logged
      return false;
    }
    key = sensorKey(mac, input);
  }
  bool clockOk = e.received >= CAM_CLOCK_VALID_AFTER;
  std::string who = hub ? cfg_.hostName : e.text[0] ? std::string(e.text) + " (" + e.sensor + ")" : std::string(e.sensor);
  std::string json = jsonObject([&](CamJson& j) {
    j.str("event_type", camEventName(e.kind, e.active, e.delayed)).str("sensor", e.sensor).str(hub ? "detail" : "name", e.text);
    j.num("battery", e.battery != CAM_BATTERY_UNKNOWN, e.battery).str("time", clockOk, clockOk ? iso(e.received) : std::string());
    j.str("message", camEventMessage(e.kind, who, hub ? e.text : "", e.offS, e.delayed));
  });
  uint16_t id = io_.nextId;
  out = camMqttPublish(topicBase() + "/" + key + "/event", payload(json), false, id);
  io_.nextId = (uint16_t)(id == 0xffff ? 1 : id + 1);
  io_.ackId = id;
  io_.ackEvent = number;
  return true;
}

// Home Assistant's discovery: one retained message per entity, under homeassistant/, naming the topic it reads.
bool CamMqttClient::discoveryStep(int step, std::string& out) {
  std::string base = topicBase(), hubId = "sensors2onvif_" + cfg_.serial;
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
    std::string
        id = hubId + "_hub_" + part,
        json = jsonObject([&](CamJson& j) {
          common(j, id, NAMES[step], base + "/hub/" + (step == 2 ? "event" : "state"));
          if (step == 0) j.str("device_class", "motion").str("value_template", "{{ value_json.alert }}");
          if (step == 1) j.str("device_class", "connectivity").str("entity_category", "diagnostic").str("value_template", "{{ value_json.nvr }}");
          if (step == 2) {
            j.array("event_types");
            for (const char* type : camEventNames()) j.str(type);
            j.end();
          }
          if (step == 3) {  // no code: Home Assistant guards who may use it
            j.str("value_template", "{{ value_json.alarm }}").str("command_topic", base + "/armed/set").str("payload_arm_away", "ARM_AWAY");
            j.str("payload_arm_home", "ARM_HOME").str("payload_disarm", "DISARM").array("supported_features").str("arm_away").str("arm_home").end();
            j.flag("code_arm_required", false).flag("code_disarm_required", false);
            j.str("json_attributes_topic", base + "/hub/state");
          }
          j.object("device")
              .array("identifiers")
              .str(hubId)
              .end()
              .str("name", hubName)
              .str("manufacturer", "sensors2onvif")
              .str("model", "ESP32 sensor hub");
          j.str("sw_version", cfg_.firmwareVersion).str("configuration_url", web).end();
        });
    out = camMqttPublish(std::string("homeassistant/") + COMPONENTS[step] + "/" + cfg_.serial + "/hub_" + part + "/config", payload(json), true, 0);
    return true;
  }
  if (step < HUB_STEPS) {  // the hub's health
    if (!chip_->tellsHealth()) return false;
    const HealthPart& p = HEALTH[step - HUB_PARTS];
    std::string id = hubId + "_hub_" + p.part,
                json = jsonObject([&](CamJson& j) {
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
  const CamAlarm::Slot& s = alarm_.slotOf(i + 1);
  Told& t = told_[i];
  if (k <= SENSOR_PARTS) {  // a sensor deleted, or replaced in its slot: its entities and its retained state are taken away
    if (!t.used || (s.used && memcmp(t.mac, s.mac, 6) == 0 && t.input == s.input)) return false;
    std::string key = sensorKey(t.mac, t.input);
    out = k < SENSOR_PARTS
              ? camMqttPublish(std::string("homeassistant/") + SENSOR_COMPONENT[k] + "/" + cfg_.serial + "/" + key + "_" + SENSOR_PART[k] + "/config",
                               "", true, 0)
              : camMqttPublish(base + "/" + key + "/state", "", true, 0);
    if (k == SENSOR_PARTS) t.used = false;
    return true;
  }
  if (!s.used) return false;
  int part = k - SENSOR_PARTS - 1;
  std::string key = sensorKey(s.mac, s.input), id = hubId + "_" + key + "_" + SENSOR_PART[part], sensor = alarm_.slotName(i + 1);
  std::string json = jsonObject([&](CamJson& j) {
    static const char* const NAMES[SENSOR_PARTS] = {"Alert", "Battery", "Signal", "Problem", "Events", "Send test alert"};
    if (part == 5) {  // a button reads nothing, so it has a command topic instead of a state topic
      j.str("name", NAMES[part]).str("unique_id", id).str("availability_topic", base + "/status").str("command_topic", base + "/" + key + "/test");
      j.str("payload_press", "PRESS").str("icon", "mdi:bell-ring-outline");
    } else {
      common(j, id, NAMES[part], base + "/" + key + (part == 4 ? "/event" : "/state"));
    }
    if (part == 0) j.str("device_class", "motion").str("value_template", "{{ value_json.alert }}");
    if (part == 1)
      j.str("device_class", "battery")
          .str("unit_of_measurement", "%")
          .str("state_class", "measurement")
          .str("entity_category", "diagnostic")
          .str("value_template", "{{ value_json.battery }}");
    if (part == 2)
      j.str("device_class", "signal_strength")
          .str("unit_of_measurement", "dBm")
          .str("state_class", "measurement")
          .str("entity_category", "diagnostic")
          .str("value_template", "{{ value_json.rssi }}");
    if (part == 3)
      j.str("device_class", "problem").str("value_template", "{{ value_json.problem }}").str("json_attributes_topic", base + "/" + key + "/state");
    if (part == 4) {
      j.array("event_types");
      for (const char* type : camEventNames()) j.str(type);
      j.end();
    }
    j.object("device").array("identifiers").str(hubId + "_" + key).end().str("name", s.name.empty() ? "Sensor " + sensor : s.name);
    j.str("model", "Sensor input " + std::to_string(s.input)).str("via_device", hubId).end();
  });
  out = camMqttPublish(std::string("homeassistant/") + SENSOR_COMPONENT[part] + "/" + cfg_.serial + "/" + key + "_" + SENSOR_PART[part] + "/config",
                       payload(json), true, 0);
  if (part == SENSOR_PARTS - 1) {
    t.used = true;
    memcpy(t.mac, s.mac, 6);
    t.input = s.input;
  }
  return true;
}

void CamMqttClient::setSettings(const CamMqtt& m, uint32_t now) {
  settings_ = m;
  stop("connecting", now);
  waiting_ = false;
}

std::string CamMqttClient::state() const {
  if (settings_.host.empty()) return "not set up";
  return io_.state == Io::READY ? "connected" : io_.state != Io::OFF ? "connecting" : result_;
}

std::string CamMqttClient::pageJson() {
  const CamMqtt& m = settings_;
  bool ready = io_.state == Io::READY, clockOk = connectedAt_ >= CAM_CLOCK_VALID_AFTER;
  uint32_t waiting = alarm_.log().added() - eventsSent_;
  return jsonObject([&](CamJson& j) {
    j.str("host", m.host).num("port", m.port).str("user", m.user).flag("password", !m.password.empty());
    j.flag("connected", ready).str("state", state());
    j.str("since", ready && clockOk, ready && clockOk ? iso(connectedAt_) : std::string())
        .num("waiting", m.host.empty() ? 0 : waiting)
        .num("lost", eventsLost_);
    j.str("topic", topicBase()).str("hub", cfg_.hostName.substr(0, cfg_.hostName.find('.')));
  });
}
