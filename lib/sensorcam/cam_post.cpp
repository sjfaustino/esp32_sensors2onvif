#include "cam_post.h"
#include <stdio.h>
#include <string.h>

CamPost::Start CamPost::start(const HttpUrl& url, std::string& request, uint32_t connectMs, uint32_t replyMs, uint32_t now) {
  int fd = (int)socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return NO_SOCKET;
  netNonblock(fd);
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(url.port);
  to.sin_addr.s_addr = inet_addr(url.host.c_str());
  if (connect(fd, (sockaddr*)&to, sizeof(to)) != 0 && !netWouldBlock()) {
    netClose(fd);
    return REFUSED;
  }
  out_.swap(request);
  outPos_ = 0;
  fd_ = fd;
  state_ = CONNECTING;
  deadline_ = now + connectMs;
  replyMs_ = replyMs;
  return STARTED;
}

void CamPost::watch(fd_set& rd, fd_set& wr, int& maxFd) const {
  if (state_ == IDLE) return;
  netWatch(fd_, state_ == WAIT_CLOSE ? rd : wr, maxFd);
}

void CamPost::finish(const char* result, bool taken) {
  delivered_ = state_ == WAIT_CLOSE && taken;  // once sent, waiting for the close is all that is left, unless it says no
  snprintf(result_, sizeof(result_), "%s", result);
  if (fd_ >= 0) netClose(fd_);
  fd_ = -1;
  state_ = IDLE;
  std::string().swap(out_);
  outPos_ = 0;
}

bool CamPost::service(bool readable, bool writable, uint32_t now) {
  if (state_ == CONNECTING && writable) {
    int err = 0;
    socklen_t len = sizeof(err);
    getsockopt(fd_, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
    if (err) {
      finish("could not connect");
      return true;
    }
    state_ = SENDING;
  }
  if (state_ == SENDING && writable) {
    int sent = (int)send(fd_, out_.data() + outPos_, (int)(out_.size() - outPos_), 0);
    if (sent < 0 && !netWouldBlock()) {
      finish("the connection failed");
      return true;
    }
    if (sent > 0) outPos_ += sent;
    if (outPos_ >= out_.size()) {
      std::string().swap(out_);
      outPos_ = 0;
      state_ = WAIT_CLOSE;
      deadline_ = now + replyMs_;
    }
  }
  if (state_ == WAIT_CLOSE && readable) {
    char buf[256];
    int got = (int)recv(fd_, buf, sizeof(buf), 0);
    if (got >= 0 || !netWouldBlock()) {
      char code[16];
      const char* result = "closed without a reply";
      bool taken = true;
      if (got >= 12 && memcmp(buf, "HTTP/1.", 7) == 0) {
        snprintf(code, sizeof(code), "HTTP %.3s", buf + 9);
        result = code;
        taken = buf[9] < '3';  // 1xx and 2xx; a redirect or an error did not take it
      }
      finish(result, taken);
      return true;
    }
  }
  if ((int32_t)(now - deadline_) >= 0) {
    finish(state_ == WAIT_CLOSE ? "no reply" : "could not connect");
    return true;
  }
  return false;
}
