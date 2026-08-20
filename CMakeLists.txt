cmake_minimum_required(VERSION 3.24)

project(scf VERSION 1.2.0 LANGUAGES CXX)

option(SCF_BUILD_TESTS "Build the SCF test suite" ON)

function(scf_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(scf_core
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
)

target_include_directories(scf_core PUBLIC include)
target_compile_features(scf_core PUBLIC cxx_std_20)
scf_enable_warnings(scf_core)

add_executable(scf_cli src/main.cpp)
target_link_libraries(scf_cli PRIVATE scf_core)
scf_enable_warnings(scf_cli)

add_executable(scf_synthetic_generator tools/generate_synthetic.cpp)
target_compile_features(scf_synthetic_generator PRIVATE cxx_std_20)
scf_enable_warnings(scf_synthetic_generator)

add_executable(scf_generate tools/scf_generate.cpp)
target_link_libraries(scf_generate PRIVATE scf_core)
scf_enable_warnings(scf_generate)

add_executable(scf_experiment tools/scf_experiment.cpp)
target_link_libraries(scf_experiment PRIVATE scf_core)
scf_enable_warnings(scf_experiment)

add_executable(scf_prepare_text tools/scf_prepare_text.cpp)
target_link_libraries(scf_prepare_text PRIVATE scf_core)
scf_enable_warnings(scf_prepare_text)

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
    scf_enable_warnings(scf_tests)
    add_test(NAME scf_tests COMMAND scf_tests)
endif()
