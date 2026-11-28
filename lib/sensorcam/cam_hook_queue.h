// The messages waiting for the webhook, sent one at a time, oldest first, and how the last one went. The sending itself
// is CamWebhookSender's, through a CamPost, as the ONVIF event pushes are. The oldest stays until its server takes it,
// or until it has failed CAM_HOOK_TRIES times, CAM_HOOK_RETRY_MS apart.
#pragma once
#include <stdint.h>
#include <time.h>
#include <string>
#include "cam_timing.h"

class CamHookQueue {
 public:
  static const int SIZE = 8;
  // A full queue drops a message, so a burst cannot use up memory: the oldest of the lowest priority, if the new one
  // outranks it, else the new one. So an alert from while the server was away is never lost to a note such as "armed".
  // The oldest of all is never the one dropped, as it may be on its way.
  void add(std::string message, int priority = 0) {
    if (count_ == SIZE) {
      dropped_++;
      int low = 0;  // as a place after the oldest
      for (int n = 1; n < count_; n++)
        if (!low || priority_[at(n)] < priority_[at(low)]) low = n;
      if (!low || priority_[at(low)] >= priority) return;
      for (int n = low; n < count_ - 1; n++) {
        items_[at(n)] = std::move(items_[at(n + 1)]);
        priority_[at(n)] = priority_[at(n + 1)];
      }
      count_--;
    }
    items_[at(count_)] = std::move(message);
    priority_[at(count_)] = priority;
    count_++;
  }
  void dropOne() { dropped_++; }  // a message there was no memory for
  int count() const { return count_; }
  // A message waits, and the oldest is not waiting out a retry.
  bool ready(uint32_t now) const { return count_ && (!retrying_ || (int32_t)(now - retryAt_) >= 0); }
  const std::string& oldest() const { return items_[head_]; }
  void takeOldest() {  // once its server has taken it
    std::string().swap(items_[head_]);
    head_ = (head_ + 1) % SIZE;
    count_--;
    tries_ = 0;
    retrying_ = false;
  }
  // The oldest was not taken: tried again in a while, or, after the last try, dropped.
  void failed(uint32_t now) {
    if (!count_) return;
    if (++tries_ >= CAM_HOOK_TRIES) {
      takeOldest();
      dropped_++;
      return;
    }
    retrying_ = true;
    retryAt_ = now + CAM_HOOK_RETRY_MS;
  }
  void clear() {
    for (std::string& s : items_) std::string().swap(s);
    count_ = 0;
    tries_ = 0;
    retrying_ = false;
  }
  void noteResult(const std::string& result, time_t at) {
    result_ = result;
    resultAt_ = at;
  }
  const std::string& result() const { return result_; }  // empty before the first
  time_t resultAt() const { return resultAt_; }
  uint32_t dropped() const { return dropped_; }

 private:
  int at(int n) const { return (head_ + n) % SIZE; }  // the place of the nth from the oldest

  std::string items_[SIZE];
  int priority_[SIZE] = {0};
  int count_ = 0, head_ = 0;
  int tries_ = 0;  // of the oldest
  bool retrying_ = false;
  uint32_t retryAt_ = 0;
  uint32_t dropped_ = 0;
  std::string result_;
  time_t resultAt_ = 0;
};
