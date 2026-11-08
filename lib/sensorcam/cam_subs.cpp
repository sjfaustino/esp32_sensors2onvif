#include "cam_subs.h"

CamSub& SubTable::slot(uint32_t now) {
  CamSub* oldest = &subs_[0];
  for (CamSub& s : subs_) {
    if (!s.used) return s;
    if (now - s.heardAt > now - oldest->heardAt) oldest = &s;
  }
  return *oldest;
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
