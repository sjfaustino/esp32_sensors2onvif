// One message POSTed to a listener, without blocking: connect, send it, then wait for the reply or for the listener to
// close. The ONVIF event pushes to the NVR and the webhook each send theirs through one.
#pragma once
#include <stdint.h>
#include <string>
#include "cam_net.h"
#include "cam_soap.h"

class CamPost {
 public:
  enum State : uint8_t { IDLE, CONNECTING, SENDING, WAIT_CLOSE };
  enum Start : uint8_t { STARTED, NO_SOCKET, REFUSED };
  // Takes request, the whole of it, and starts sending it. connectMs: how long it may take to connect and send; replyMs:
  // how long, once sent, to wait for the reply or the close. NO_SOCKET leaves everything as it was, to be tried again;
  // REFUSED is a connection that failed at once.
  Start start(const HttpUrl& url, std::string& request, uint32_t connectMs, uint32_t replyMs, uint32_t now);
  bool idle() const { return state_ == IDLE; }
  int fd() const { return fd_; }  // -1 while idle
  void watch(fd_set& rd, fd_set& wr, int& maxFd) const;
  // True when it has just ended: result() says how, an HTTP status or what went wrong, and delivered() whether the
  // whole request was sent first. An NVR closes without replying, so for it sent is delivered.
  bool service(bool readable, bool writable, uint32_t now);
  const char* result() const { return result_; }
  bool delivered() const { return delivered_; }

 private:
  void finish(const char* result);
  State state_ = IDLE;
  int fd_ = -1;
  std::string out_;
  size_t outPos_ = 0;
  uint32_t deadline_ = 0, replyMs_ = 0;
  char result_[24] = "";
  bool delivered_ = false;
};
