cmake_minimum_required(VERSION 3.24)

project(scf VERSION 1.2.0 LANGUAGES CXX)

option(SCF_BUILD_TESTS "Build the SCF test suite" ON)

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
                ${CMAKE_CURRENT_SOURCE_DIR}/config/windows/utf8.manifest
            )
        elseif(WIN32)
            target_link_options(${target} PRIVATE -Wl,--stack,8388608)
        endif()
    endif()
endfunction()

# Operating-system specific code (peak RSS, console code page, ...).  Header
# include/scf/platform.hpp, implementation src/platform.cpp.  Every other
# module links this instead of touching OS headers directly.
add_library(scf_platform STATIC src/platform.cpp)
target_include_directories(scf_platform PUBLIC include)
target_compile_features(scf_platform PUBLIC cxx_std_20)
if(WIN32)
    target_link_libraries(scf_platform PUBLIC psapi)
endif()
scf_configure_target(scf_platform)

add_library(scf_core STATIC
    src/token_interner.cpp
    src/string_interner.cpp
    src/corpus.cpp
    src/equivalence_solver.cpp
    src/evidence_builder.cpp
    src/evidence.cpp
    src/tree_solver.cpp
    src/formatter.cpp
    src/gold.cpp
    src/synthetic.cpp
    src/enumerator.cpp
    src/evaluator.cpp
    src/pipeline.cpp
    src/prepare_text.cpp
    src/audit.cpp
    src/context_indexed.cpp
)

target_include_directories(scf_core PUBLIC include)
target_link_libraries(scf_core PUBLIC scf_platform)
target_compile_features(scf_core PUBLIC cxx_std_20)
scf_configure_target(scf_core)

add_executable(scf_cli src/main.cpp)
target_link_libraries(scf_cli PRIVATE scf_core)
scf_configure_target(scf_cli)

add_executable(scf_synthetic_generator tools/generate_synthetic.cpp)
target_link_libraries(scf_synthetic_generator PRIVATE scf_platform)
scf_configure_target(scf_synthetic_generator)

add_executable(scf_generate tools/scf_generate.cpp)
target_link_libraries(scf_generate PRIVATE scf_core)
scf_configure_target(scf_generate)

add_executable(scf_experiment tools/scf_experiment.cpp)
target_link_libraries(scf_experiment PRIVATE scf_core)
scf_configure_target(scf_experiment)

add_executable(scf_prepare_text tools/scf_prepare_text.cpp)
target_link_libraries(scf_prepare_text PRIVATE scf_core)
scf_configure_target(scf_prepare_text)

add_executable(scf_audit tools/scf_audit.cpp)
target_link_libraries(scf_audit PRIVATE scf_core)
scf_configure_target(scf_audit)

add_executable(scf_real_audit tools/scf_real_audit.cpp)
target_link_libraries(scf_real_audit PRIVATE scf_core)
scf_configure_target(scf_real_audit)

# SCF v2.0 — Oracle Category Recovery: an independent experimental module
# with no dependency on the v1.x core (only on the tiny scf_platform layer).
add_library(scf_oracle_v2 STATIC src/oracle_v2.cpp)
target_include_directories(scf_oracle_v2 PUBLIC include)
target_link_libraries(scf_oracle_v2 PUBLIC scf_platform)
target_compile_features(scf_oracle_v2 PUBLIC cxx_std_20)
scf_configure_target(scf_oracle_v2)

add_executable(scf_oracle_v2_cli tools/scf_oracle_v2.cpp)
target_link_libraries(scf_oracle_v2_cli PRIVATE scf_oracle_v2)
set_target_properties(scf_oracle_v2_cli PROPERTIES OUTPUT_NAME scf_oracle_v2)
scf_configure_target(scf_oracle_v2_cli)

# SCF v2.1 — Real Corpus Scaling Experiment: another independent module.
add_library(scf_real_v21 STATIC src/real_scaling.cpp)
target_include_directories(scf_real_v21 PUBLIC include)
target_link_libraries(scf_real_v21 PUBLIC scf_platform)
target_compile_features(scf_real_v21 PUBLIC cxx_std_20)
scf_configure_target(scf_real_v21)

add_executable(scf_real_scaling tools/scf_real_scaling.cpp)
target_link_libraries(scf_real_scaling PRIVATE scf_real_v21)
scf_configure_target(scf_real_scaling)

add_executable(scf_terminal_punct_ablation tools/scf_terminal_punct_ablation.cpp)
target_link_libraries(scf_terminal_punct_ablation PRIVATE scf_real_v21)
scf_configure_target(scf_terminal_punct_ablation)

# SCF v2.3 -- conservative, frequency-free evidence-driven merging.
add_library(scf_conservative_v23 STATIC src/conservative_merging.cpp)
target_include_directories(scf_conservative_v23 PUBLIC include)
target_compile_features(scf_conservative_v23 PUBLIC cxx_std_20)
target_link_libraries(scf_conservative_v23 PUBLIC scf_real_v21)
scf_configure_target(scf_conservative_v23)

add_executable(scf_conservative_merging tools/scf_conservative_merging.cpp)
target_link_libraries(scf_conservative_merging PRIVATE scf_conservative_v23)
scf_configure_target(scf_conservative_merging)

# SCF v2.3.1 -- clean-corpus replication: new corpus reader and frame-type
# diagnostics around the UNCHANGED v2.3 observation and merge code.
add_library(scf_clean_corpus_v231 STATIC src/clean_corpus.cpp)
target_include_directories(scf_clean_corpus_v231 PUBLIC include)
target_compile_features(scf_clean_corpus_v231 PUBLIC cxx_std_20)
target_link_libraries(scf_clean_corpus_v231 PUBLIC scf_conservative_v23)
scf_configure_target(scf_clean_corpus_v231)

add_executable(scf_clean_corpus tools/scf_clean_corpus.cpp)
target_link_libraries(scf_clean_corpus PRIVATE scf_clean_corpus_v231)
scf_configure_target(scf_clean_corpus)

if(SCF_BUILD_TESTS)
    enable_testing()
    set_property(
        DIRECTORY
        APPEND PROPERTY TEST_INCLUDE_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/ctest_default_config.cmake
    )
    add_executable(scf_tests tests/test_main.cpp)
    target_link_libraries(scf_tests PRIVATE scf_core)
    target_compile_definitions(scf_tests PRIVATE SCF_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    scf_configure_target(scf_tests)
    add_test(NAME scf_tests COMMAND scf_tests)

    add_executable(scf_oracle_v2_tests tests/test_oracle_v2.cpp)
    target_link_libraries(scf_oracle_v2_tests PRIVATE scf_oracle_v2)
    scf_configure_target(scf_oracle_v2_tests)
    add_test(NAME scf_oracle_v2_tests COMMAND scf_oracle_v2_tests)

    add_executable(scf_real_v21_tests tests/test_real_scaling.cpp)
    target_link_libraries(scf_real_v21_tests PRIVATE scf_real_v21)
    scf_configure_target(scf_real_v21_tests)
    add_test(NAME scf_real_v21_tests COMMAND scf_real_v21_tests)

    add_executable(scf_conservative_v23_tests tests/test_conservative_merging.cpp)
    target_link_libraries(scf_conservative_v23_tests PRIVATE scf_conservative_v23)
    scf_configure_target(scf_conservative_v23_tests)
    add_test(NAME scf_conservative_v23_tests COMMAND scf_conservative_v23_tests)

    add_executable(scf_clean_corpus_v231_tests tests/test_clean_corpus.cpp)
    target_link_libraries(scf_clean_corpus_v231_tests PRIVATE scf_clean_corpus_v231)
    scf_configure_target(scf_clean_corpus_v231_tests)
    add_test(NAME scf_clean_corpus_v231_tests COMMAND scf_clean_corpus_v231_tests)
endif()
