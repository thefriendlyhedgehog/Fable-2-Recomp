// fable_2 - Host-side guest-memory access for probes that run off the guest
// threads (monitor/classifier threads with no `base` argument).
//
// GuestBase(): where the guest's 4 GB virtual arena lives in host memory. The
// SDK maps it at 0x100000000 on Windows, but that is not a given elsewhere
// (on 64-bit macOS the low 4 GB is __PAGEZERO and 0x100000000 is where the
// executable itself is loaded), so ask the runtime instead of hardcoding it.
//
// SafeRead(): copy host memory without faulting. Windows probes use
// VirtualQuery / SEH for this; off Windows the kernel does the checking
// (mach_vm_read_overwrite on macOS, process_vm_readv on Linux) and an
// unmapped or PROT_NONE page just returns false.

#pragma once

#include <cstddef>
#include <cstdint>

#include <rex/runtime.h>
#include <rex/system/xmemory.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#elif !defined(_WIN32)
#include <sys/uio.h>
#include <unistd.h>
#endif

namespace fable2::guestmem {

// Fallback when the runtime isn't up yet: the SDK's Windows mapping.
inline constexpr uintptr_t kDefaultGuestBase = 0x100000000ull;

inline uintptr_t GuestBase() {
  if (auto* rt = rex::Runtime::instance()) {
    if (auto* mem = rt->memory()) {
      if (uint8_t* base = mem->virtual_membase())
        return reinterpret_cast<uintptr_t>(base);
    }
  }
  return kDefaultGuestBase;
}

#ifndef _WIN32
inline bool SafeRead(void* dst, const void* src, size_t n) {
  if (n == 0) return true;
#if defined(__APPLE__)
  mach_vm_size_t got = 0;
  kern_return_t kr = mach_vm_read_overwrite(
      mach_task_self(), static_cast<mach_vm_address_t>(reinterpret_cast<uintptr_t>(src)),
      static_cast<mach_vm_size_t>(n),
      static_cast<mach_vm_address_t>(reinterpret_cast<uintptr_t>(dst)), &got);
  return kr == KERN_SUCCESS && got == n;
#else
  iovec local{dst, n};
  iovec remote{const_cast<void*>(src), n};
  return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(n);
#endif
}

inline bool Readable(const void* p) {
  uint8_t b;
  return SafeRead(&b, p, 1);
}
#endif  // !_WIN32

}  // namespace fable2::guestmem
