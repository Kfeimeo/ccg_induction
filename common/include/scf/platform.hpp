// Platform abstraction layer.
//
// Every operating-system specific piece of SCF lives behind this header so
// that the rest of the code base stays portable C++20.  The switches below are
// plain preprocessor macros and can be tested with `#if SCF_PLATFORM_WINDOWS`
// wherever a platform-specific branch is unavoidable.
//
//   SCF_PLATFORM_WINDOWS  1 on Windows (MSVC, clang-cl, MinGW), otherwise 0
//   SCF_PLATFORM_APPLE    1 on macOS / iOS, otherwise 0
//   SCF_PLATFORM_LINUX    1 on Linux, otherwise 0
//   SCF_PLATFORM_POSIX    1 on any non-Windows platform, otherwise 0
//
// This header deliberately includes no operating-system headers, so it can be
// included anywhere without leaking <windows.h> macros such as min/max.  The
// operating-system calls are implemented in src/platform.cpp (library
// `scf_platform`).
#pragma once

#include <string>

#if defined(_WIN32) || defined(_WIN64)
#define SCF_PLATFORM_WINDOWS 1
#else
#define SCF_PLATFORM_WINDOWS 0
#endif

#if defined(__APPLE__) && defined(__MACH__)
#define SCF_PLATFORM_APPLE 1
#else
#define SCF_PLATFORM_APPLE 0
#endif

#if defined(__linux__)
#define SCF_PLATFORM_LINUX 1
#else
#define SCF_PLATFORM_LINUX 0
#endif

#if SCF_PLATFORM_WINDOWS
#define SCF_PLATFORM_POSIX 0
#else
#define SCF_PLATFORM_POSIX 1
#endif

namespace scf::platform {

// Human-readable platform name, e.g. "windows", "linux", "macos".
const char* name() noexcept;

// Peak resident set size of the current process in mebibytes.
//   Windows : PROCESS_MEMORY_COUNTERS::PeakWorkingSetSize
//   Linux   : getrusage(RUSAGE_SELF).ru_maxrss (KiB)
//   macOS   : getrusage(RUSAGE_SELF).ru_maxrss (bytes)
// Returns 0.0 when the platform offers no way to measure it.
double peak_rss_mb() noexcept;

// Prepares the process console for the UTF-8 text that the tools print.
//   Windows : switches the console input/output code pages to UTF-8 so that
//             corpus tokens written to std::cout are not mangled.
//   POSIX   : no-op (terminals are UTF-8 by convention).
// Safe to call more than once and when no console is attached.
void initialize_console() noexcept;

// Removes one trailing carriage return, if present.  Text files written on
// Windows carry "\r\n" line endings; when such a file is read on a POSIX
// system (or through a binary stream) std::getline leaves the '\r' behind.
inline void strip_trailing_cr(std::string& line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
}

}  // namespace scf::platform
