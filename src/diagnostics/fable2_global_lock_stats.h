// Measurement build of the guest's global lock (FABLE2_GLOBAL_LOCK_STATS=ON
// in CMake, which defines REX_GLOBAL_LOCK_STATS=1 and force-includes this
// header). The generated code's REX_CHECK/ENTER/LEAVE_GLOBAL_LOCK(site) and
// the overrides' HOTFUNC_GLOBAL_LOCK_ENTER/LEAVE then call these: the same
// locking of rex::thread::global_critical_region::mutex(), plus per-site and
// per-thread counts, contended acquisitions, wait and hold time, written to
// the log every REX_GLOBAL_LOCK_STATS_INTERVAL seconds (default 10).
#pragma once

#include <cstdint>

namespace rex::thread::guest_lock_stats {
void Enter(uint32_t site);
void Leave(uint32_t site);
uint64_t Check(uint32_t site);
}  // namespace rex::thread::guest_lock_stats
