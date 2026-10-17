// mac_gpu_power: the system's GPU (and CPU, DRAM) energy over an interval and
// the GPU's clock residency, from IOReport (Apple silicon, no root needed).
//
//   clang -O2 -o out/mac_gpu_power tools/mac_gpu_power.c -framework CoreFoundation \
//       -framework IOKit -lIOReport
//   out/mac_gpu_power 20
//
// Prints the average power of each "Energy Model" channel that starts with
// CPU, GPU or DRAM, then how long the GPU spent at each clock ("GPU Stats",
// "GPU Performance States") and its average clock while active. The GPU
// time in milliseconds per frame depends on that clock: at a light load the
// GPU runs slower, so less work can show up as a lower clock, not fewer
// milliseconds; energy counts the work either way. It measures the whole
// system: keep other GPU users (video, other games) quiet while measuring.
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct IOReportSubscription* IOReportSubscriptionRef;
extern CFDictionaryRef IOReportCopyChannelsInGroup(CFStringRef group, CFStringRef subgroup,
                                                   uint64_t a, uint64_t b, uint64_t c);
extern void IOReportMergeChannels(CFDictionaryRef a, CFDictionaryRef b, CFTypeRef c);
extern IOReportSubscriptionRef IOReportCreateSubscription(void* a, CFMutableDictionaryRef desired,
                                                          CFMutableDictionaryRef* subscribed,
                                                          uint64_t channel_id, CFTypeRef b);
extern CFDictionaryRef IOReportCreateSamples(IOReportSubscriptionRef subscription,
                                             CFMutableDictionaryRef subscribed, CFTypeRef a);
extern CFDictionaryRef IOReportCreateSamplesDelta(CFDictionaryRef a, CFDictionaryRef b,
                                                  CFTypeRef c);
extern CFStringRef IOReportChannelGetGroup(CFDictionaryRef channel);
extern CFStringRef IOReportChannelGetSubGroup(CFDictionaryRef channel);
extern CFStringRef IOReportChannelGetChannelName(CFDictionaryRef channel);
extern CFStringRef IOReportChannelGetUnitLabel(CFDictionaryRef channel);
extern int64_t IOReportSimpleGetIntegerValue(CFDictionaryRef channel, int32_t index);
extern int32_t IOReportStateGetCount(CFDictionaryRef channel);
extern CFStringRef IOReportStateGetNameForIndex(CFDictionaryRef channel, int32_t index);
extern int64_t IOReportStateGetResidency(CFDictionaryRef channel, int32_t index);

static void ToC(CFStringRef s, char* out, size_t size) {
  out[0] = 0;
  if (s) {
    CFStringGetCString(s, out, (CFIndex)size, kCFStringEncodingUTF8);
  }
}

// Energy unit labels -> joules per unit.
static double JoulesPerUnit(const char* unit) {
  if (!strcmp(unit, "mJ")) return 1e-3;
  if (!strcmp(unit, "uJ")) return 1e-6;
  if (!strcmp(unit, "nJ")) return 1e-9;
  return 0.0;
}

// The GPU clock of each performance state in MHz, from the pmgr device's
// "voltage-states9" (pairs of 32-bit clock in Hz and voltage; entry n is
// state Pn). Returns the number of entries.
static int GpuStateMhz(double* mhz, int max_count) {
  int count = 0;
  io_iterator_t iterator;
  if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("AppleARMIODevice"),
                                   &iterator) != KERN_SUCCESS) {
    return 0;
  }
  io_object_t entry;
  while ((entry = IOIteratorNext(iterator))) {
    io_name_t name;
    if (IORegistryEntryGetName(entry, name) == KERN_SUCCESS && !strcmp(name, "pmgr")) {
      CFTypeRef table = IORegistryEntryCreateCFProperty(entry, CFSTR("voltage-states9"),
                                                        kCFAllocatorDefault, 0);
      if (table && CFGetTypeID(table) == CFDataGetTypeID()) {
        const uint8_t* bytes = CFDataGetBytePtr((CFDataRef)table);
        CFIndex length = CFDataGetLength((CFDataRef)table);
        for (CFIndex i = 0; i + 8 <= length && count < max_count; i += 8) {
          uint32_t hz;
          memcpy(&hz, bytes + i, sizeof(hz));
          mhz[count++] = hz * 1e-6;
        }
      }
      if (table) {
        CFRelease(table);
      }
    }
    IOObjectRelease(entry);
  }
  IOObjectRelease(iterator);
  return count;
}

int main(int argc, char** argv) {
  double seconds = argc > 1 ? atof(argv[1]) : 10.0;
  double state_mhz[64];
  int state_count = GpuStateMhz(state_mhz, 64);
  CFDictionaryRef energy =
      IOReportCopyChannelsInGroup(CFSTR("Energy Model"), NULL, 0, 0, 0);
  CFDictionaryRef gpu_stats = IOReportCopyChannelsInGroup(
      CFSTR("GPU Stats"), CFSTR("GPU Performance States"), 0, 0, 0);
  if (!energy) {
    fprintf(stderr, "no Energy Model channels\n");
    return 1;
  }
  CFMutableDictionaryRef channels = CFDictionaryCreateMutableCopy(kCFAllocatorDefault, 0, energy);
  if (gpu_stats) {
    IOReportMergeChannels(channels, gpu_stats, NULL);
  }
  CFMutableDictionaryRef subscribed = NULL;
  IOReportSubscriptionRef subscription =
      IOReportCreateSubscription(NULL, channels, &subscribed, 0, NULL);
  if (!subscription) {
    fprintf(stderr, "IOReportCreateSubscription failed\n");
    return 1;
  }
  CFDictionaryRef before = IOReportCreateSamples(subscription, subscribed, NULL);
  usleep((useconds_t)(seconds * 1e6));
  CFDictionaryRef after = IOReportCreateSamples(subscription, subscribed, NULL);
  CFDictionaryRef delta = IOReportCreateSamplesDelta(before, after, NULL);
  CFArrayRef items = CFDictionaryGetValue(delta, CFSTR("IOReportChannels"));
  double gpu_active_mhz_ns = 0.0, gpu_active_ns = 0.0, gpu_total_ns = 0.0;
  for (CFIndex i = 0; items && i < CFArrayGetCount(items); ++i) {
    CFDictionaryRef channel = CFArrayGetValueAtIndex(items, i);
    char group[128], subgroup[128], name[128], unit[32];
    ToC(IOReportChannelGetGroup(channel), group, sizeof(group));
    ToC(IOReportChannelGetSubGroup(channel), subgroup, sizeof(subgroup));
    ToC(IOReportChannelGetChannelName(channel), name, sizeof(name));
    if (!strcmp(group, "Energy Model")) {
      ToC(IOReportChannelGetUnitLabel(channel), unit, sizeof(unit));
      double joules = (double)IOReportSimpleGetIntegerValue(channel, 0) * JoulesPerUnit(unit);
      if (!strcmp(name, "GPU Energy") || (getenv("ALL_CHANNELS") && joules > 0.0)) {
        printf("%-24s %7.3f W\n", name, joules / seconds);
      }
    } else if (!strcmp(group, "GPU Stats") && !strcmp(name, "GPUPH")) {
      // States: OFF, IDLE, then P1..Pn (named by clock in MHz on some
      // machines, by index on others).
      int32_t count = IOReportStateGetCount(channel);
      for (int32_t s = 0; s < count; ++s) {
        gpu_total_ns += (double)IOReportStateGetResidency(channel, s);
      }
      printf("GPU clock residency %%:");
      for (int32_t s = 0; s < count; ++s) {
        char state[64];
        ToC(IOReportStateGetNameForIndex(channel, s), state, sizeof(state));
        double ns = (double)IOReportStateGetResidency(channel, s);
        if (strcmp(state, "OFF") && strcmp(state, "IDLE") && strcmp(state, "DOWN")) {
          gpu_active_ns += ns;
          // "P<n>": the clock from the table, else the name (if it is MHz).
          double mhz = atof(state);
          if (state[0] == 'P' && atoi(state + 1) < state_count) {
            mhz = state_mhz[atoi(state + 1)];
          }
          gpu_active_mhz_ns += mhz * ns;
        }
        if (ns > 0.0 && gpu_total_ns > 0.0) {
          printf(" %s %.1f", state, 100.0 * ns / gpu_total_ns);
        }
      }
      printf("\n");
    }
  }
  if (gpu_total_ns > 0.0) {
    printf("GPU active %.1f%% of the time", 100.0 * gpu_active_ns / gpu_total_ns);
    if (gpu_active_ns > 0.0 && gpu_active_mhz_ns > 0.0) {
      printf(", average active clock %.0f MHz", gpu_active_mhz_ns / gpu_active_ns);
    }
    printf("\n");
  }
  return 0;
}
