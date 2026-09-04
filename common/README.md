# common — platform layer

Everything operating-system specific is isolated here so that no research
line includes OS headers directly (library `scf_platform`).

- `include/scf/platform.hpp` / `src/platform.cpp`
  - `scf::platform::peak_rss_mb()` — peak resident memory (Windows:
    `GetProcessMemoryInfo`, POSIX: `getrusage`), used by every scaling
    experiment.
  - `scf::platform::initialize_console()` — first call in every `main()`;
    on Windows it switches the console to UTF-8, elsewhere it is a no-op.
  - `scf::platform::strip_trailing_cr()` — makes the line-oriented readers
    tolerate CRLF files on every platform.
  - preprocessor switches `SCF_PLATFORM_WINDOWS`, `SCF_PLATFORM_LINUX`,
    `SCF_PLATFORM_APPLE`, `SCF_PLATFORM_POSIX`.
- `windows/utf8.manifest` — embedded by `scf_configure_target` into every
  MSVC executable so `argv` and `std::filesystem::path` use the UTF-8 code
  page (Windows 10 1903+).

The root `CMakeLists.txt` defines `scf_configure_target` (warning level,
Windows compile definitions, 8 MiB stack on Windows to match Linux) and
`scf_add_test`; both are used by every line.
