// Every time the hub waits for, in one place. Each says where its value came from: "measured" from the real NVR, or
// "chosen", a judgement not yet checked against it. Change a chosen one when the NVR shows a better value.
#pragma once
#include <stdint.h>

// ---- the NVR ----
const uint32_t CAM_REPEAT_TRUE_MS = 1000;           // measured: the NVR ends its motion event about 2 s after a "true", so this repeats it sooner
const uint32_t CAM_NOTIFY_SEND_MS = 2000;           // chosen: for an event push to connect and be sent
const uint32_t CAM_NOTIFY_CLOSE_MS = 1000;          // measured: the NVR closes without replying, so a push waits this long for it
const uint32_t CAM_PUSH_RETRY_MS = 30000;           // chosen: a failed event push is tried again this often
const uint32_t CAM_PULL_ALIVE_MS = 60000;           // chosen: an NVR that pulls events and has not asked for this long is not listening
const uint32_t CAM_SUB_IDLE_MS = 10UL * 60 * 1000;  // measured: the NVR never renews, so a subscription lives while its NVR talks
const uint32_t CAM_DELIVERY_CHECK_MS = 5000;        // chosen: after an alert starts, time for its push to reach the NVR
const uint32_t CAM_NVR_GONE_MS = 3UL * 60 * 1000;   // chosen: not listening this long is logged once as the NVR having stopped
const uint32_t CAM_POLL_HOLD_MAX_MS = 10000;        // chosen: the longest a PullMessages is held, so it cannot keep a connection long
const uint32_t CAM_PAST_GAP_MS = 4000;              // chosen: between past-event records, so the NVR closes one before the next

// ---- sensors ----
const uint32_t CAM_PAST_AFTER_S = 30;                    // chosen: an older report is a past event, unless its node says otherwise
const uint32_t CAM_RESEND_WINDOW_MS = 60000;             // chosen: the same ESP-NOW report again within this is a resend
const uint32_t CAM_JAM_WINDOW_MS = 30UL * 60 * 1000;     // chosen: nodes overdue within this of each other went quiet together
const uint32_t CAM_BUSY_WINDOW_MS = 3600UL * 1000;       // chosen: a sensor's alerts are counted over an hour
const uint32_t CAM_MEMORY_LOW_WARN = 40 * 1024;          // chosen: free memory below this in a day is told in the daily check
const long long CAM_TEST_REMIND_AFTER_S = 30LL * 86400;  // chosen: the daily check asks for a test alert once the last is this old
const int CAM_BUSY_ALERTS = 20;                          // chosen: this many in that hour is reported as firing often
const uint32_t CAM_PICTURE_TURN_MS = 2000;               // chosen: with several alerts on, each sensor's picture shows this long in turn
// The alert hold, 10 s, is SENSOR_HOLD_MS in include/config.h; silence, 3 hours, is CamConfig::silentMs. Both chosen.

// ---- connections ----
const uint32_t CAM_HTTP_STALL_MS = 10000;  // chosen: a web connection that moves nothing for this long is closed
// chosen: a refused request's body is read and dropped for at most this long from the refusal, so the reply carries the
// reason, while a client trickling the body cannot keep the connection, one of the few the NVR also needs
const uint32_t CAM_HTTP_DRAIN_MS = 60000;
const uint32_t CAM_RTSP_IDLE_MS = 120000;  // chosen: a video connection not yet playing
const uint32_t CAM_RTSP_STALL_MS = 30000;  // chosen: a playing video connection that takes nothing
const uint32_t CAM_HOOK_WAIT_MS = 5000;    // chosen: for the webhook to connect, and again for its reply

// ---- MQTT ----
const uint16_t CAM_MQTT_KEEPALIVE_S = 60;   // chosen: the broker announces the hub gone after 1.5 times this without a word
const uint32_t CAM_MQTT_PING_MS = 20000;    // chosen: a ping after this long with nothing sent
const uint32_t CAM_MQTT_SILENT_MS = 60000;  // chosen: a broker that has said nothing for this long, pings included, is gone
const uint32_t CAM_MQTT_WAIT_MS = 10000;    // chosen: to connect, to be accepted, and for an event to be acknowledged
const uint32_t CAM_MQTT_RETRY_MS = 15000;   // chosen: after a failed connection, before the next
const uint32_t CAM_MQTT_SIGNAL_MS = 60000;  // chosen: a sensor's signal strength alone is published at most this often
const uint32_t CAM_MQTT_HEALTH_MS = 60000;  // chosen: how often the hub's own health is published

// ---- pairing nodes ----
const uint32_t CAM_PAIR_WINDOW_MS = 120000;            // chosen: how long new nodes may say hello after the button is pressed
const uint32_t CAM_PAIR_FORGET_MS = 10UL * 60 * 1000;  // chosen: a node waiting for approval that stops saying hello is dropped
const uint32_t CAM_UPDATE_IDLE_MS = 120000;            // chosen: a node that has not asked for a firmware part this long is no longer updating

// ---- the hub itself ----
const uint32_t CAM_ALIVE_EVERY_MS = 10UL * 60 * 1000;  // chosen: how often the time is saved, so a restart can say how long it was off
const uint32_t CAM_START_CLOCK_WAIT_MS = 120000;       // chosen: "hub started" waits this long for the clock

// ---- wrong passwords ----
const int CAM_LOGIN_FREE_TRIES = 10;                      // chosen
const uint32_t CAM_LOGIN_FIRST_LOCK_MS = 60000;           // chosen: doubled for each wrong one after that
const uint32_t CAM_LOGIN_LONGEST_LOCK_MS = 15UL * 60000;  // chosen
const uint32_t CAM_LOGIN_FORGET_MS = 24UL * 3600 * 1000;  // chosen: how long wrong ones are remembered

// ---- the hub's network, in src/main.cpp ----
const uint32_t CAM_WIFI_TRY_MS = 30000;  // chosen: how long each saved network gets before moving on to the next
// A router that still holds the hub's connection from before a restart turns it away for a moment (Wi-Fi's "comeback
// time"): the same network is tried again this soon, this many times, before the next one.
const uint32_t CAM_WIFI_SAME_AFTER_MS = 2000;   // chosen
const int CAM_WIFI_SAME_TRIES = 3;              // chosen
const uint32_t CAM_ROUTER_PING_MS = 20000;      // chosen: how often the router is pinged, to tell a dead network from a quiet one
const uint32_t CAM_ROUTER_PING_WAIT_MS = 3000;  // chosen: how long each ping waits for its reply
const uint32_t CAM_RESTART_REPLY_MS = 1500;     // chosen: a restart asked for on the page waits this long, for the reply to reach the browser

// ---- the webhook ----
const uint32_t CAM_DAILY_ONCE_MS = 2UL * 60 * 1000;  // chosen: the daily check is sent once, though its minute is seen for a whole minute

// ---- when the hub restarts itself, or undoes a firmware update: cam_restart_rules.h ----
const uint32_t CAM_WIFI_LOST_RESTART_MS = 5UL * 60 * 1000;         // chosen: off Wi-Fi this long, the hub starts clean
const uint32_t CAM_LOW_HEAP_BYTES = 8 * 1024;                      // chosen: free memory below this stops the network stack
const uint32_t CAM_LOW_HEAP_RESTART_MS = 30000;                    // chosen: and lasting this long restarts the hub
const uint32_t CAM_STALLED_RESTART_MS = 30000;                     // chosen: the server serving nothing this long restarts the hub
const uint32_t CAM_ROUTER_SILENT_RESTART_MS = 90000;               // chosen: the router answering no ping this long, once it has
const uint32_t CAM_STATIC_CHECK_MS = 120000;                       // chosen: after joining with a fixed address, when it is checked
const uint32_t CAM_SCHEDULE_AFTER_START_MS = 180000;               // chosen: no scheduled restart this soon after a start
const uint32_t CAM_SCHEDULE_WAIT_MS = 30UL * 60 * 1000;            // chosen: a scheduled restart waits this long for alerts to end
const uint32_t CAM_FIRMWARE_PROOF_MS = 60000;                      // chosen: on the network this long proves a new firmware
const uint32_t CAM_FIRMWARE_PROOF_DEADLINE_MS = 10UL * 60 * 1000;  // chosen: a new firmware not proved by then is undone
