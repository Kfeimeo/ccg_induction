# Operating-system specific code (peak RSS, console code page, CRLF handling).
# Header include/scf/platform.hpp, implementation src/platform.cpp.  Every
# other module links this instead of touching OS headers directly.
add_library(scf_platform STATIC src/platform.cpp)
target_include_directories(scf_platform PUBLIC include)
target_compile_features(scf_platform PUBLIC cxx_std_20)
if(WIN32)
    target_link_libraries(scf_platform PUBLIC psapi)
endif()
scf_configure_target(scf_platform)
