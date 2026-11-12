#pragma once
#include <stdint.h>
#include <string>

// ONVIF event subscribers.
//
// Lifetime follows whether the subscriber is still talking to us, not TerminationTime: the
// NVR was never seen to Renew, so expiring on the advertised time would silently stop events.
struct CamSub {
  bool used = false;
  bool push = false;
  uint32_t id = 0;
  std::string consumer;  // push only: the listener URL the NVR gave us
  uint32_t peer = 0;     // address the subscriber connects from
  uint32_t heardAt = 0;
  bool pending = false;       // push: a notification should be sent
  bool initialized = false;   // push: first message (PropertyOperation "Initialized") was sent
  int lastPull = -1;          // pull: last state delivered, -1 for none
  uint32_t pulledAt = 0;      // pull: when it last asked for events, which is what shows it is listening
  bool pushFailed = false;    // push: the last notification could not be delivered
  uint32_t retryAt = 0;       // push: when a failed one was last tried again
};

class SubTable {
 public:
  static const int SIZE = 6;
  static const uint32_t IDLE_MS = 10UL * 60UL * 1000UL;

  // The NVR subscribes twice with the same URL; one entry means one notification per event.
  CamSub& addPush(const std::string& consumer, uint32_t peer, uint32_t now);
  CamSub& addPull(uint32_t peer, uint32_t now);
  CamSub* find(uint32_t id);
  bool remove(uint32_t id);
  void heard(uint32_t peer, uint32_t now);
  void expire(uint32_t now);
  void markAllPending();
  int count() const;
  void protect(uint32_t peer) { keep_ = peer; }  // the NVR's address, whose subscriptions are pushed out last; 0 for none
  CamSub& at(int i) { return subs_[i]; }
  const CamSub& at(int i) const { return subs_[i]; }

 private:
  CamSub& slot(uint32_t now);
  CamSub subs_[SIZE];
  uint32_t nextId_ = 1;
  uint32_t keep_ = 0;
};
