// Global critical region (guest interrupt lock) for the hand-written
// overrides: the same mutex and count updates as REX_ENTER/LEAVE_GLOBAL_LOCK.
// Expand after the file's hotfunc_gcr_mutex(). With REX_GLOBAL_LOCK_STATS=1
// they go through rex::thread::guest_lock_stats like the generated code, so
// the measurement build counts these sections too (site: function address).
#pragma once

#if defined(REX_GLOBAL_LOCK_STATS) && REX_GLOBAL_LOCK_STATS
#define HOTFUNC_GLOBAL_LOCK_ENTER(site) rex::thread::guest_lock_stats::Enter(site)
#define HOTFUNC_GLOBAL_LOCK_LEAVE(site) rex::thread::guest_lock_stats::Leave(site)
#else
#define HOTFUNC_GLOBAL_LOCK_ENTER(site) \
  do {                                  \
    hotfunc_gcr_mutex().lock();         \
    ppc_global_lock_count_().fetch_add(1); \
  } while (0)
#define HOTFUNC_GLOBAL_LOCK_LEAVE(site)                      \
  do {                                                       \
    auto old_ = ppc_global_lock_count_().fetch_sub(1);       \
    assert(old_ >= 1);                                       \
    (void)old_;                                              \
    hotfunc_gcr_mutex().unlock();                            \
  } while (0)
#endif
