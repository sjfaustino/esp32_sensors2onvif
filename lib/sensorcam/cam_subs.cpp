#include "cam_subs.h"

// A free entry, or else the oldest one not from the NVR, so other clients subscribing again and again cannot push the
// NVR's subscription out. Only when every entry is the NVR's does its oldest go.
CamSub& SubTable::slot(uint32_t now) {
  CamSub* oldest = nullptr;
  CamSub* oldestNvr = nullptr;
  for (CamSub& s : subs_) {
    if (!s.used) return s;
    CamSub*& pick = keep_ && s.peer == keep_ ? oldestNvr : oldest;
    if (!pick || now - s.heardAt > now - pick->heardAt) pick = &s;
  }
  return oldest ? *oldest : *oldestNvr;
}

CamSub& SubTable::addPush(const std::string& consumer, uint32_t peer, uint32_t now) {
  CamSub* entry = nullptr;
  for (CamSub& s : subs_)
    if (s.used && s.push && s.consumer == consumer) entry = &s;
  if (!entry) entry = &slot(now);
  *entry = CamSub();
  entry->used = true;
  entry->push = true;
  entry->id = nextId_++;
  entry->consumer = consumer;
  entry->peer = peer;
  entry->heardAt = now;
  entry->pending = true;
  return *entry;
}

CamSub& SubTable::addPull(uint32_t peer, uint32_t now) {
  CamSub& s = slot(now);
  s = CamSub();
  s.used = true;
  s.id = nextId_++;
  s.peer = peer;
  s.heardAt = now;
  return s;
}

CamSub* SubTable::find(uint32_t id) {
  for (CamSub& s : subs_)
    if (s.used && s.id == id) return &s;
  return nullptr;
}

bool SubTable::remove(uint32_t id) {
  CamSub* s = find(id);
  if (!s) return false;
  *s = CamSub();
  return true;
}

void SubTable::heard(uint32_t peer, uint32_t now) {
  for (CamSub& s : subs_)
    if (s.used && s.peer == peer) s.heardAt = now;
}

void SubTable::expire(uint32_t now) {
  for (CamSub& s : subs_)
    if (s.used && now - s.heardAt > IDLE_MS) s = CamSub();
}

void SubTable::markAllPending() {
  for (CamSub& s : subs_)
    if (s.used && s.push) s.pending = true;
}

int SubTable::count() const {
  int n = 0;
  for (const CamSub& s : subs_)
    if (s.used) n++;
  return n;
}
