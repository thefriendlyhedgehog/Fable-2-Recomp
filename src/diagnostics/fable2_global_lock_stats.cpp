// Guest global lock counters for the measurement build; see
// fable2_global_lock_stats.h.
#if defined(REX_GLOBAL_LOCK_STATS) && REX_GLOBAL_LOCK_STATS

#include "fable2_global_lock_stats.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <rex/logging.h>
#include <rex/thread/mutex.h>

#if defined(__APPLE__)
#include <mach/mach_time.h>
#include <pthread.h>
#endif

namespace rex::thread::guest_lock_stats {
namespace {

inline uint64_t Ticks() {
#if defined(__APPLE__)
  return mach_absolute_time();
#else
  return std::chrono::steady_clock::now().time_since_epoch().count();
#endif
}

double TicksToNs() {
#if defined(__APPLE__)
  mach_timebase_info_data_t tb;
  mach_timebase_info(&tb);
  return double(tb.numer) / double(tb.denom);
#else
  return 1.0;
#endif
}

struct SiteStats {
  std::atomic<uint64_t> key{0};  // site + 1, 0 = empty
  std::atomic<uint64_t> enters{0}, checks{0}, leaves{0}, contended{0},
      wait_ticks{0}, hold_ticks{0}, unbalanced{0};
};

constexpr size_t kSiteSlots = 1 << 16;
SiteStats* g_sites = new SiteStats[kSiteSlots];

SiteStats* Slot(SiteStats* table, size_t slots, uint64_t key) {
  key += 1;
  size_t i = (key * 0x9E3779B97F4A7C15ull) >> 40;
  for (size_t n = 0; n < slots; ++n, ++i) {
    SiteStats& s = table[i & (slots - 1)];
    uint64_t k = s.key.load(std::memory_order_relaxed);
    if (k == key) return &s;
    if (k == 0) {
      uint64_t expected = 0;
      if (s.key.compare_exchange_strong(expected, key) || expected == key) return &s;
    }
  }
  return nullptr;
}

struct ThreadStats {
  std::string name;
  std::atomic<uint64_t> enters{0}, checks{0}, contended{0}, wait_ticks{0},
      hold_ticks{0};
};

std::mutex g_threads_mutex;
std::vector<ThreadStats*>& Threads() {
  static auto* v = new std::vector<ThreadStats*>();
  return *v;
}

struct ThreadState {
  ThreadStats* stats = nullptr;
  uint32_t depth = 0;
  uint32_t outer_site = 0;
  uint64_t outer_start = 0;
};
thread_local ThreadState t_state;

ThreadStats* CurrentThread() {
  if (!t_state.stats) {
    auto* ts = new ThreadStats();
    char name[64] = {};
#if defined(__APPLE__)
    pthread_getname_np(pthread_self(), name, sizeof(name));
#endif
    ts->name = name[0] ? name : "?";
    std::lock_guard<std::mutex> lock(g_threads_mutex);
    Threads().push_back(ts);
    t_state.stats = ts;
  }
  return t_state.stats;
}

void Reporter();

void StartOnce() {
  static std::once_flag once;
  std::call_once(once, [] { std::thread(Reporter).detach(); });
}

// Takes the mutex, timing it only when it was held by another thread.
void LockTimed(uint32_t site, SiteStats* s, ThreadStats* ts) {
  auto& m = global_critical_region::mutex();
  if (m.try_lock()) return;
  uint64_t t0 = Ticks();
  m.lock();
  uint64_t dt = Ticks() - t0;
  if (s) {
    s->contended.fetch_add(1, std::memory_order_relaxed);
    s->wait_ticks.fetch_add(dt, std::memory_order_relaxed);
  }
  ts->contended.fetch_add(1, std::memory_order_relaxed);
  ts->wait_ticks.fetch_add(dt, std::memory_order_relaxed);
  (void)site;
}

struct Snapshot {
  uint64_t key, enters, checks, leaves, contended, wait_ticks, hold_ticks, unbalanced;
};

std::vector<Snapshot> Take(SiteStats* table, size_t slots) {
  std::vector<Snapshot> out;
  for (size_t i = 0; i < slots; ++i) {
    SiteStats& s = table[i];
    uint64_t k = s.key.load(std::memory_order_relaxed);
    if (!k) continue;
    out.push_back({k - 1, s.enters.load(std::memory_order_relaxed),
                   s.checks.load(std::memory_order_relaxed),
                   s.leaves.load(std::memory_order_relaxed),
                   s.contended.load(std::memory_order_relaxed),
                   s.wait_ticks.load(std::memory_order_relaxed),
                   s.hold_ticks.load(std::memory_order_relaxed),
                   s.unbalanced.load(std::memory_order_relaxed)});
  }
  return out;
}

void Reporter() {
  int interval = 10;
  if (const char* e = std::getenv("REX_GLOBAL_LOCK_STATS_INTERVAL")) {
    interval = std::max(1, std::atoi(e));
  }
  const double ns = TicksToNs();
  std::unordered_map<uint64_t, Snapshot> prev_sites;
  struct PrevThread { uint64_t enters, checks, contended, wait, hold; };
  std::unordered_map<ThreadStats*, PrevThread> prev_threads;
  for (uint32_t report = 0;; ++report) {
    std::this_thread::sleep_for(std::chrono::seconds(interval));
    auto delta = [](std::vector<Snapshot> now,
                    std::unordered_map<uint64_t, Snapshot>& prev) {
      std::vector<Snapshot> d;
      for (auto& s : now) {
        Snapshot p{};
        auto it = prev.find(s.key);
        if (it != prev.end()) p = it->second;
        prev[s.key] = s;
        Snapshot x{s.key, s.enters - p.enters, s.checks - p.checks, s.leaves - p.leaves,
                   s.contended - p.contended, s.wait_ticks - p.wait_ticks,
                   s.hold_ticks - p.hold_ticks, s.unbalanced - p.unbalanced};
        if (x.enters || x.checks || x.leaves || x.unbalanced) d.push_back(x);
      }
      return d;
    };
    auto sites = delta(Take(g_sites, kSiteSlots), prev_sites);
    Snapshot t{};
    for (auto& s : sites) {
      t.enters += s.enters; t.checks += s.checks; t.leaves += s.leaves;
      t.contended += s.contended; t.wait_ticks += s.wait_ticks;
      t.hold_ticks += s.hold_ticks; t.unbalanced += s.unbalanced;
    }
    const double sec = interval;
    REXLOG_INFO(
        "GlobalLock report {}: per s {:.0f} enters, {:.0f} mfmsr checks, {:.0f} leaves, "
        "{:.0f} contended ({:.2f} ms waiting), {:.2f} ms held, {:.0f} leaves without "
        "enter",
        report, t.enters / sec, t.checks / sec, t.leaves / sec, t.contended / sec,
        t.wait_ticks * ns / 1e6 / sec, t.hold_ticks * ns / 1e6 / sec, t.unbalanced / sec);
    auto top = [&](const char* what, auto key, size_t n) {
      std::sort(sites.begin(), sites.end(),
                [&](const Snapshot& a, const Snapshot& b) { return key(a) > key(b); });
      for (size_t i = 0; i < std::min(n, sites.size()); ++i) {
        const auto& s = sites[i];
        if (!key(s)) break;
        REXLOG_INFO(
            "GlobalLock {} 0x{:08X}: {:.0f} enters/s {:.0f} checks/s {:.0f} leaves/s "
            "{:.0f} contended/s wait {:.3f} ms/s hold {:.3f} ms/s",
            what, uint32_t(s.key), s.enters / sec, s.checks / sec, s.leaves / sec,
            s.contended / sec, s.wait_ticks * ns / 1e6 / sec, s.hold_ticks * ns / 1e6 / sec);
      }
    };
    top("by count", [](const Snapshot& s) { return s.enters + s.checks; }, 20);
    top("by wait", [](const Snapshot& s) { return s.wait_ticks; }, 10);
    top("by hold", [](const Snapshot& s) { return s.hold_ticks; }, 10);
    {
      std::lock_guard<std::mutex> lock(g_threads_mutex);
      for (ThreadStats* ts : Threads()) {
        PrevThread now{ts->enters.load(), ts->checks.load(), ts->contended.load(),
                       ts->wait_ticks.load(), ts->hold_ticks.load()};
        PrevThread p = prev_threads[ts];
        prev_threads[ts] = now;
        if (now.enters == p.enters && now.checks == p.checks) continue;
        REXLOG_INFO(
            "GlobalLock thread {}: {:.0f} enters/s {:.0f} checks/s {:.0f} contended/s "
            "wait {:.3f} ms/s hold {:.3f} ms/s",
            ts->name, (now.enters - p.enters) / sec, (now.checks - p.checks) / sec,
            (now.contended - p.contended) / sec, (now.wait - p.wait) * ns / 1e6 / sec,
            (now.hold - p.hold) * ns / 1e6 / sec);
      }
    }
  }
}

}  // namespace

void Enter(uint32_t site) {
  StartOnce();
  ThreadStats* ts = CurrentThread();
  SiteStats* s = Slot(g_sites, kSiteSlots, site);
  LockTimed(site, s, ts);
  if (s) s->enters.fetch_add(1, std::memory_order_relaxed);
  ts->enters.fetch_add(1, std::memory_order_relaxed);
  if (t_state.depth++ == 0) {
    t_state.outer_site = site;
    t_state.outer_start = Ticks();
  }
}

void Leave(uint32_t site) {
  SiteStats* s = Slot(g_sites, kSiteSlots, site);
  if (t_state.depth == 0) {
    // mtmsrd from a register other than r13 without a matching enter; the
    // inline macro would unlock a mutex this thread doesn't hold.
    if (s) s->unbalanced.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (s) s->leaves.fetch_add(1, std::memory_order_relaxed);
  if (--t_state.depth == 0) {
    uint64_t dt = Ticks() - t_state.outer_start;
    if (SiteStats* o = Slot(g_sites, kSiteSlots, t_state.outer_site)) {
      o->hold_ticks.fetch_add(dt, std::memory_order_relaxed);
    }
    CurrentThread()->hold_ticks.fetch_add(dt, std::memory_order_relaxed);
  }
  global_critical_region::mutex().unlock();
}

uint64_t Check(uint32_t site) {
  StartOnce();
  ThreadStats* ts = CurrentThread();
  SiteStats* s = Slot(g_sites, kSiteSlots, site);
  LockTimed(site, s, ts);
  if (s) s->checks.fetch_add(1, std::memory_order_relaxed);
  ts->checks.fetch_add(1, std::memory_order_relaxed);
  uint64_t r = t_state.depth ? 0 : 0x8000;
  global_critical_region::mutex().unlock();
  return r;
}

}  // namespace rex::thread::guest_lock_stats

#endif  // REX_GLOBAL_LOCK_STATS
