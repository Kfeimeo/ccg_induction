cmake_minimum_required(VERSION 3.24)

project(scf VERSION 2.4.0 LANGUAGES CXX)

option(SCF_BUILD_TESTS "Build the SCF test suites" ON)

# ---------------------------------------------------------------------------
# Repository layout (one directory per research line, in chronological order)
#
#   common/                              platform layer shared by every line
#   01_saturation_tree_induction/        v1.x  global equivalence saturation +
#                                              maximum-evidence tree induction
#   02_oracle_category_recovery/         v2.0  oracle category recovery
#   03_shared_context_evidence_scaling/  v2.1  shared-context evidence scaling,
#                                        v2.2  terminal x punctuation ablation
#   04_conservative_witness_merging/     v2.3  conservative transactional
#                                              merging, v2.3.1 clean corpus
#   05_closed_world_refinement/          v2.4  closed-world contextual
#                                              equivalence by partition
#                                              refinement (current line)
#
# Every line owns its include/, src/, tools/, tests/, results/ and reports/.
# Library targets are PUBLIC-linked so a later line can build on an earlier
# one (04 -> 03, 05 -> 04) without duplicating code.
# ---------------------------------------------------------------------------

# Applies the common warning level plus the Windows compatibility switches to a
# target.  Every library and executable in this project goes through here.
function(scf_configure_target target)
    get_target_property(target_type ${target} TYPE)

    if(MSVC)
        # /utf-8        : sources and string literals are UTF-8 regardless of the
        #                 system code page (the code base contains UTF-8 text).
        # /permissive-  : standard-conforming mode, same dialect as GCC/Clang.
        # /Zc:__cplusplus: report the real language level like other compilers.
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(WIN32)
        # Keep <windows.h> from defining min()/max() macros and shrink it, and
        # silence the MSVC "unsafe CRT function" warnings for standard C calls.
        target_compile_definitions(${target} PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
            _CRT_SECURE_NO_WARNINGS
        )
    endif()

    if(target_type STREQUAL "EXECUTABLE")
        # Linux gives the main thread 8 MiB of stack; Windows defaults to 1 MiB.
        # Match Linux so recursive solvers behave the same on both platforms.
        if(MSVC)
            target_link_options(${target} PRIVATE /STACK:8388608)
            # Embedded manifest: UTF-8 active code page for argv and
            # std::filesystem::path conversions (Windows 10 1903+).
            target_sources(${target} PRIVATE
                ${CMAKE_SOURCE_DIR}/common/windows/utf8.manifest
            )
        elseif(WIN32)
            target_link_options(${target} PRIVATE -Wl,--stack,8388608)
        endif()
    endif()
endfunction()

# Registers a test executable: one source file, linked against `library`.
function(scf_add_test name source library)
    if(NOT SCF_BUILD_TESTS)
        return()
    endif()
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ${library})
    scf_configure_target(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

if(SCF_BUILD_TESTS)
    enable_testing()
    set_property(
        DIRECTORY
        APPEND PROPERTY TEST_INCLUDE_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ctest_default_config.cmake
    )
endif()

add_subdirectory(common)
add_subdirectory(01_saturation_tree_induction)
add_subdirectory(02_oracle_category_recovery)
add_subdirectory(03_shared_context_evidence_scaling)
add_subdirectory(04_conservative_witness_merging)
add_subdirectory(05_closed_world_refinement)
