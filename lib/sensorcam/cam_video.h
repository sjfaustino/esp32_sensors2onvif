// The video the NVR records: the RTSP connections that carry it, and the picture they send, chosen from the folders on
// storage by which alert is on. SensorCamServer owns one, waits on its sockets, and serves the picture on the web pages.
#pragma once
#include <stdint.h>
#include <functional>
#include <string>
#include "cam_alarm.h"
#include "cam_auth.h"
#include "cam_config.h"
#include "cam_frames.h"
#include "cam_net.h"
#include "cam_onvif_events.h"
#include "cam_rtp.h"
#include "cam_storage.h"

class CamVideo {
 public:
  // cfg holds the start-up settings, which never change; it and the others outlive the video. A viewer heard from tells
  // events, as it keeps the NVR's subscription alive.
  CamVideo(const CamConfig& cfg, CamChip& chip, CamStorage& storage, const CamAlarm& alarm, const CamSensorTable& table, CamLoginGuard& logins,
           CamOnvifEvents& events, CamMemoryFailures& failures, std::function<uint32_t()> millisFn)
      : cfg_(cfg),
        chip_(&chip),
        storage_(storage),
        alarm_(alarm),
        table_(table),
        logins_(logins),
        events_(events),
        failures_(failures),
        millis_(millisFn),
        onvifPassword_(cfg.onvifPassword) {}
  bool begin(int listenFd);  // takes the RTSP port's listening socket; false when there is none

  // ---- in the server's loop ----
  void watch(fd_set& rd, fd_set& wr, int& maxFd, bool accepting) const;  // accepting: new viewers may connect
  void serve(fd_set& rd, fd_set& wr, uint32_t now);
  void tick(uint32_t now);  // the next frame to each viewer, when it is due
  int32_t untilFrame(uint32_t now) const { return (int32_t)(nextFrameAt_ - now); }
  void closeAll();  // before a firmware update: video uses the network's memory most

  // ---- what is being sent ----
  int openCount() const;    // connections open
  int streamCount() const;  // those playing
  bool playingTo(uint32_t peer) const;
  void viewers(CamJson& j) const;  // each playing viewer's address, as array members

  // ---- the picture ----
  const CamFrame& current(uint32_t now) { return frame_ ? *frame_ : show(now); }
  const std::string& file() const { return frameFile_; }  // the file behind it; empty for a built-in picture
  uint32_t seq() const { return liveSeq_; }               // changes whenever the picture changes, so the Live page fetches only new ones
  // Changes whenever the loaded picture is about to be overwritten, so a reply reading it stops. Only a loaded picture
  // changes; the built-in ones never do.
  uint32_t gen() const { return pictureGen_; }
  bool isLoaded(const CamFrame* f) const { return f == &loadedFrame_; }
  void changed() { frameKey_.clear(); }  // the sensors or their pictures changed: chosen again for the next frame
  // A picture upload borrows the one picture buffer, so it never needs a second block that size. The built-in pictures
  // are shown meanwhile.
  bool lent() const { return lent_; }
  std::string& lend();
  std::string& buffer() { return frameData_; }
  void giveBack() {
    lent_ = false;
    frameKey_.clear();
  }
  void setOnvifPassword(const std::string& password) { onvifPassword_ = password; }  // for the PC tests

 private:
  static const int MAX_RTSP = 2;  // the NVR and one viewer; each costs a packet buffer plus network buffers
  struct RtspConn {
    int fd = -1;
    uint32_t peer = 0;
    uint32_t activityAt = 0;
    std::string in, out;  // out holds the RTSP replies; video goes through packet
    size_t outPos = 0;
    RtpSend video;                   // the frame being sent, packet by packet
    uint8_t packet[RTP_PACKET_MAX];  // the packet being sent, which may go out in parts
    size_t packetLen = 0, packetPos = 0;
    bool playing = false;
    bool closeAfterSend = false;
    bool authed = false;
    std::string nonce;
    int channel = 0;
    RtpState rtp;
  };
  void acceptViewer(uint32_t now);
  void serviceConn(RtspConn& c, bool readable, bool writable, uint32_t now);
  void request(RtspConn& c, const std::string& text, uint32_t now);
  void flush(RtspConn& c, uint32_t now);
  void closeConn(RtspConn& c);
  const CamFrame& show(uint32_t now);  // works out the picture for now
  bool loadFrame(const std::string& path);
  void releasePicture();  // before frameData_ is overwritten: nothing may still be reading the old picture

  const CamConfig& cfg_;
  CamChip* chip_;
  CamStorage& storage_;
  const CamAlarm& alarm_;
  const CamSensorTable& table_;
  CamLoginGuard& logins_;
  CamOnvifEvents& events_;
  CamMemoryFailures& failures_;
  std::function<uint32_t()> millis_;
  std::string onvifPassword_;  // RTSP needs the user admin and this, when set
  int listen_ = -1;
  RtspConn rtsp_[MAX_RTSP];
  uint32_t nextFrameAt_ = 0;
  std::string frameKey_;   // what frame_ currently shows; empty forces a reload
  std::string frameFile_;  // the file behind frame_; empty means a built-in picture
  uint32_t liveSeq_ = 0;
  const CamFrame* frame_ = nullptr;
  std::string frameData_;  // the picture loaded from storage, in one buffer reserved at boot
  uint32_t pictureGen_ = 0;
  bool lent_ = false;
  CamNal frameNals_[3];
  CamFrame loadedFrame_{frameNals_, 3};
};
