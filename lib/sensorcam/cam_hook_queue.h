// The messages waiting for the webhook, sent one at a time, oldest first, and how the last one went. The sending itself
// is CamWebhookSender's, through a CamPost, as the ONVIF event pushes are.
#pragma once
#include <stdint.h>
#include <time.h>
#include <string>

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
  const std::string& oldest() const { return items_[head_]; }
  void takeOldest() {  // once its connection is open
    std::string().swap(items_[head_]);
    head_ = (head_ + 1) % SIZE;
    count_--;
  }
  void clear() {
    for (std::string& s : items_) std::string().swap(s);
    count_ = 0;
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
  uint32_t dropped_ = 0;
  std::string result_;
  time_t resultAt_ = 0;
};
