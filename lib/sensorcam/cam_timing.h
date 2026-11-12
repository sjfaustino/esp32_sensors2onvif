// Every time the hub waits for, in one place. Each says where its value came from: "measured" from the real NVR, or
// "chosen", a judgement not yet checked against it. Change a chosen one when the NVR shows a better value.
#pragma once
#include <stdint.h>

// ---- the NVR ----
const uint32_t CAM_REPEAT_TRUE_MS = 1000;      // measured: the NVR ends its motion event about 2 s after a "true", so this repeats it sooner
const uint32_t CAM_NOTIFY_SEND_MS = 2000;      // chosen: for an event push to connect and be sent
const uint32_t CAM_NOTIFY_CLOSE_MS = 1000;     // measured: the NVR closes without replying, so a push waits this long for it
const uint32_t CAM_PUSH_RETRY_MS = 30000;      // chosen: a failed event push is tried again this often
const uint32_t CAM_PULL_ALIVE_MS = 60000;      // chosen: an NVR that pulls events and has not asked for this long is not listening
const uint32_t CAM_SUB_IDLE_MS = 10UL * 60 * 1000;  // measured: the NVR never renews, so a subscription lives while its NVR talks
const uint32_t CAM_DELIVERY_CHECK_MS = 5000;   // chosen: after an alert starts, time for its push to reach the NVR
const uint32_t CAM_NVR_GONE_MS = 3UL * 60 * 1000;  // chosen: not listening this long is logged once as the NVR having stopped
const uint32_t CAM_POLL_HOLD_MAX_MS = 10000;   // chosen: the longest a PullMessages is held, so it cannot keep a connection long
const uint32_t CAM_PAST_GAP_MS = 4000;         // chosen: between past-event records, so the NVR closes one before the next

// ---- sensors ----
const uint32_t CAM_PAST_AFTER_S = 30;          // chosen: an older report is a past event, unless its node says otherwise
const uint32_t CAM_RESEND_WINDOW_MS = 60000;   // chosen: the same ESP-NOW report again within this is a resend
// The alert hold, 10 s, is SENSOR_HOLD_MS in include/config.h; silence, 3 hours, is CamConfig::silentMs. Both chosen.

// ---- connections ----
const uint32_t CAM_HTTP_STALL_MS = 10000;      // chosen: a web connection that moves nothing for this long is closed
const uint32_t CAM_RTSP_IDLE_MS = 120000;      // chosen: a video connection not yet playing
const uint32_t CAM_RTSP_STALL_MS = 30000;      // chosen: a playing video connection that takes nothing
const uint32_t CAM_HOOK_WAIT_MS = 5000;        // chosen: for the webhook to connect, and again for its reply

// ---- the hub itself ----
const uint32_t CAM_ALIVE_EVERY_MS = 10UL * 60 * 1000;  // chosen: how often the time is saved, so a restart can say how long it was off
const uint32_t CAM_START_CLOCK_WAIT_MS = 120000;       // chosen: "hub started" waits this long for the clock

// ---- wrong passwords ----
const int CAM_LOGIN_FREE_TRIES = 10;                        // chosen
const uint32_t CAM_LOGIN_FIRST_LOCK_MS = 60000;             // chosen: doubled for each wrong one after that
const uint32_t CAM_LOGIN_LONGEST_LOCK_MS = 15UL * 60000;    // chosen
const uint32_t CAM_LOGIN_FORGET_MS = 24UL * 3600 * 1000;    // chosen: how long wrong ones are remembered
