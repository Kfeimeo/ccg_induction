#include "scf/platform.hpp"

#if SCF_PLATFORM_WINDOWS

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// <psapi.h> must follow <windows.h>.
#include <psapi.h>
#if defined(_MSC_VER)
#pragma comment(lib, "psapi.lib")
#endif

#else  // SCF_PLATFORM_POSIX

#include <sys/resource.h>

#endif

namespace scf::platform {

const char* name() noexcept {
#if SCF_PLATFORM_WINDOWS
    return "windows";
#elif SCF_PLATFORM_APPLE
    return "macos";
#elif SCF_PLATFORM_LINUX
    return "linux";
#else
    return "posix";
#endif
}

double peak_rss_mb() noexcept {
#if SCF_PLATFORM_WINDOWS
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) == 0) {
        return 0.0;
    }
    return static_cast<double>(counters.PeakWorkingSetSize) / (1024.0 * 1024.0);
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0.0;
    }
#if SCF_PLATFORM_APPLE
    return static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0);  // bytes
#else
    return static_cast<double>(usage.ru_maxrss) / 1024.0;  // KiB
#endif
#endif
}

void initialize_console() noexcept {
#if SCF_PLATFORM_WINDOWS
    // Both calls fail harmlessly (return FALSE) when the process has no console.
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

}  // namespace scf::platform
