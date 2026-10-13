// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// LD_PRELOAD shim for tests: adds an offset to the monotonic clocks of a single process so that
// timeouts mandated by a specification can be crossed without waiting for them in real time.
//
// CLOCKSHIFT_FILE  file holding the offset as a little-endian int64 in nanoseconds; it is mapped
//                  once at startup and read on every call, so it can be changed while the process runs
// CLOCKSHIFT_ONLY  if set, only the process whose argv[0] equals this value is shifted
//
// The offset must only ever grow, otherwise CLOCK_MONOTONIC would go backwards.
// CLOCK_MONOTONIC_RAW and all non-monotonic clocks are passed through unchanged.
//
// Limitations:
// - Only the clock is shifted, the kernel keeps waiting against the real clock. An absolute deadline
//   derived from a shifted reading therefore expires the whole offset later than intended. This
//   affects std::condition_variable::wait_for/wait_until, clock_nanosleep with TIMER_ABSTIME and
//   absolute timerfds; relative sleeps and timeouts are unaffected.
// - Readings that bypass the libc clock_gettime symbol are not shifted: calls inside glibc, raw
//   syscalls or direct vDSO reads, statically linked binaries and Go programs.
// - On 32-bit targets with a 64-bit time_t, callers bind to __clock_gettime64, which is not
//   interposed, so nothing is shifted there.
// - The shift applies to the whole process, including libraries such as gRPC: their timers can fire
//   early, since deadlines taken before an advance appear to have passed.

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string_view>
#include <type_traits>

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {

using clock_gettime_fn = int (*)(clockid_t, timespec*);

// clock_gettime() can be called by other libraries' static initializers before this library is
// initialized, from signal handlers and from threads still running during exit. The state is
// therefore constant-initialized and never destroyed, and the mapping is never unmapped.
std::atomic<clock_gettime_fn> real_clock_gettime{nullptr};
std::atomic<const std::atomic<std::int64_t>*> offset_ns{nullptr};

static_assert(std::is_trivially_destructible_v<decltype(real_clock_gettime)>);
static_assert(std::is_trivially_destructible_v<decltype(offset_ns)>);
// The mapped int64 is read through std::atomic, which needs the same layout and no lock.
static_assert(sizeof(std::atomic<std::int64_t>) == sizeof(std::int64_t));
static_assert(std::atomic<std::int64_t>::is_always_lock_free);

constexpr std::chrono::nanoseconds to_duration(const timespec& time) {
    return std::chrono::seconds(time.tv_sec) + std::chrono::nanoseconds(time.tv_nsec);
}

constexpr timespec to_timespec(std::chrono::nanoseconds duration) {
    const auto whole_seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
    timespec time{};
    time.tv_sec = static_cast<time_t>(whole_seconds.count());
    time.tv_nsec = static_cast<long>((duration - whole_seconds).count());
    return time;
}

static_assert(to_timespec(to_duration(timespec{1, 999'999'999}) + std::chrono::nanoseconds(2)).tv_sec == 2);
static_assert(to_timespec(to_duration(timespec{1, 999'999'999}) + std::chrono::nanoseconds(2)).tv_nsec == 1);

bool is_shifted_clock(clockid_t clk) {
    return clk == CLOCK_MONOTONIC || clk == CLOCK_MONOTONIC_COARSE || clk == CLOCK_BOOTTIME;
}

void report(const char* what, const char* path) {
    std::fprintf(stderr, "clockshift: %s %s: %s; the clock of %s is not shifted\n", what, path, std::strerror(errno),
                 program_invocation_name);
}

__attribute__((constructor)) void clockshift_init() {
    real_clock_gettime.store(reinterpret_cast<clock_gettime_fn>(dlsym(RTLD_NEXT, "clock_gettime")),
                             std::memory_order_release);

    const char* only = std::getenv("CLOCKSHIFT_ONLY");
    if (only != nullptr && std::string_view(only) != program_invocation_name) {
        return;
    }
    const char* path = std::getenv("CLOCKSHIFT_FILE");
    if (path == nullptr) {
        if (only != nullptr) {
            std::fprintf(stderr, "clockshift: CLOCKSHIFT_FILE is not set; the clock of %s is not shifted\n",
                         program_invocation_name);
        }
        return;
    }
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        report("cannot open", path);
        return;
    }
    void* map = mmap(nullptr, sizeof(std::int64_t), PROT_READ, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        report("cannot map", path);
        close(fd);
        return;
    }
    close(fd);
    offset_ns.store(static_cast<const std::atomic<std::int64_t>*>(map), std::memory_order_release);
}

} // namespace

extern "C" __attribute__((visibility("default"))) int clock_gettime(clockid_t clk, timespec* tp) noexcept {
    // Before the constructor has resolved the real function, and while dlsym() itself calls back in
    // (a sanitizer's allocator reads the clock), the syscall answers instead of a recursive dlsym().
    const auto real = real_clock_gettime.load(std::memory_order_acquire);
    const int rc = real != nullptr ? real(clk, tp) : static_cast<int>(syscall(SYS_clock_gettime, clk, tp));
    const auto* offset = offset_ns.load(std::memory_order_acquire);
    if (rc != 0 || offset == nullptr || !is_shifted_clock(clk)) {
        return rc;
    }
    *tp = to_timespec(to_duration(*tp) + std::chrono::nanoseconds(offset->load(std::memory_order_relaxed)));
    return rc;
}
