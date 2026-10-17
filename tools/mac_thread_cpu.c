// mac_thread_cpu: the CPU use of a process over an interval, by thread, plus
// the process's energy and instruction counts (macOS, no root needed).
//
//   clang -O2 -o out/mac_thread_cpu tools/mac_thread_cpu.c
//   out/mac_thread_cpu $(pgrep -n fable_2) 20
//
// Prints each thread that used 2% of a core or more (its kernel share, its
// thread ID as `sample` prints it, its name), then the process total, the
// CPU energy the kernel estimates for the process (as average watts, and
// the share on the performance cores), and the instructions and cycles.
// The kernel's accounting, unlike `sample`, doesn't slow the process down:
// `sample` at its default 1 ms interval cut the game's CPU use by more than
// half and showed waiting threads as running.
#include <libproc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/proc_info.h>
#include <sys/resource.h>
#include <unistd.h>

#ifndef PROC_PIDLISTTHREADIDS
#define PROC_PIDLISTTHREADIDS 28
#endif

#define kMaxThreads 1024

typedef struct {
  uint64_t id;
  uint64_t ns;
  uint64_t system_ns;
  char name[64];
} ThreadTime;

static int Snapshot(int pid, ThreadTime* threads) {
  static uint64_t ids[kMaxThreads];
  int count = proc_pidinfo(pid, PROC_PIDLISTTHREADIDS, 0, ids, sizeof(ids)) / (int)sizeof(uint64_t);
  int n = 0;
  for (int i = 0; i < count; ++i) {
    struct proc_threadinfo info;
    if (proc_pidinfo(pid, PROC_PIDTHREADID64INFO, ids[i], &info, sizeof(info)) != sizeof(info)) {
      continue;
    }
    threads[n].id = ids[i];
    threads[n].ns = info.pth_user_time + info.pth_system_time;
    threads[n].system_ns = info.pth_system_time;
    strncpy(threads[n].name, info.pth_name, sizeof(threads[n].name) - 1);
    threads[n].name[sizeof(threads[n].name) - 1] = 0;
    ++n;
  }
  return n;
}

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <pid> <seconds>\n", argv[0]);
    return 1;
  }
  int pid = atoi(argv[1]);
  double seconds = atof(argv[2]);
  static ThreadTime before[kMaxThreads], after[kMaxThreads];
  struct rusage_info_v6 usage_before, usage_after;
  int n_before = Snapshot(pid, before);
  if (proc_pid_rusage(pid, RUSAGE_INFO_V6, (rusage_info_t*)&usage_before)) {
    perror("proc_pid_rusage");
    return 1;
  }
  usleep((useconds_t)(seconds * 1e6));
  int n_after = Snapshot(pid, after);
  if (proc_pid_rusage(pid, RUSAGE_INFO_V6, (rusage_info_t*)&usage_after)) {
    perror("proc_pid_rusage");
    return 1;
  }
  double total = 0.0;
  for (int i = 0; i < n_after; ++i) {
    for (int j = 0; j < n_before; ++j) {
      if (before[j].id != after[i].id) {
        continue;
      }
      double percent = (double)(after[i].ns - before[j].ns) / (seconds * 1e7);
      total += percent;
      if (percent >= 2.0) {
        printf("%6.1f%%  (kernel %4.1f%%)  %llu %s\n", percent,
               (double)(after[i].system_ns - before[j].system_ns) / (seconds * 1e7),
               (unsigned long long)after[i].id, after[i].name[0] ? after[i].name : "(unnamed)");
      }
    }
  }
  double energy = (double)(usage_after.ri_energy_nj - usage_before.ri_energy_nj);
  double instructions = (double)(usage_after.ri_instructions - usage_before.ri_instructions);
  printf("%6.1f%%  total; CPU %.2f W (performance cores %.0f%%), %.2f G instructions/s, "
         "%.2f G cycles/s\n",
         total, energy / (seconds * 1e9),
         energy > 0.0 ? 100.0 * (double)(usage_after.ri_penergy_nj - usage_before.ri_penergy_nj) /
                            energy
                      : 0.0,
         instructions / (seconds * 1e9),
         (double)(usage_after.ri_cycles - usage_before.ri_cycles) / (seconds * 1e9));
  return 0;
}
