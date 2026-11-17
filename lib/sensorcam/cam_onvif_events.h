// How the NVR hears of an alert: its ONVIF event subscriptions, the events pushed to those that take pushes, and the
// WS-Discovery that lets it find the hub. Whether the NVR is listening is worked out here too. SensorCamServer owns one;
// the subscriptions that pull are answered over HTTP, by CamWeb.
#pragma once
#include <stdint.h>
#include <functional>
#include <string>
#include "cam_alarm.h"
#include "cam_config.h"
#include "cam_post.h"
#include "cam_sensors.h"
#include "cam_subs.h"

class CamOnvifEvents {
 public:
  // cfg holds the start-up settings, which never change; what the pages change later is the part's own. It and alarm outlive this.
  CamOnvifEvents(const CamConfig& cfg, const CamAlarm& alarm, std::function<std::string()> localIp);
  void begin();      // opens the discovery socket, and keeps a place for the NVR's subscription
  void networkUp();  // after each Wi-Fi (re)connect: the socket again, and a hello to anyone listening
  // Marks every subscription as owed the new state when an alert starts or ends, and starts the pushes owed. Pushes
  // share the sockets with others: each starts only while those and the pushes open stay within maxShared.
  void tick(uint32_t now, int otherSockets, int maxShared);
  void watch(fd_set& rd, fd_set& wr, int& maxFd) const;
  void serve(fd_set& rd, fd_set& wr, uint32_t now);
  int pushCount() const;  // pushes under way, each holding a socket
  // The NVR is listening when it holds a subscription and either the last event pushed to it arrived, or, for one that
  // pulls events, it asked for events within the last minute. With no NVR address set, any subscriber counts.
  bool nvrListening(uint32_t now) const;
  const std::string& nvrIp() const { return nvrIp_; }  // empty when not set
  void setNvr(const std::string& ip);                  // its subscriptions are then the last pushed out for room
  SubTable& subs() { return subs_; }
  const SubTable& subs() const { return subs_; }

 private:
  void openDiscovery();
  void startPush(int i, uint32_t now);
  void serviceDiscovery();

  const CamConfig& cfg_;
  const CamAlarm& alarm_;
  std::function<std::string()> localIp_;
  std::string nvrIp_;
  SubTable subs_;
  MotionSignal signal_;
  CamPost notify_[SubTable::SIZE];  // the event pushes, one for each subscription
  int udp_ = -1;                    // discovery
  uint32_t messageNo_ = 1;
};
