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
  // A full queue drops the new message, so a burst cannot use up memory.
  void add(std::string message) {
    if (count_ == SIZE) {
      dropped_++;
      return;
    }
    items_[(head_ + count_) % SIZE] = std::move(message);
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
  std::string items_[SIZE];
  int count_ = 0, head_ = 0;
  int tries_ = 0;  // of the oldest
  bool retrying_ = false;
  uint32_t retryAt_ = 0;
  uint32_t dropped_ = 0;
  std::string result_;
  time_t resultAt_ = 0;
};
