# Research line 01 (v1.1 - v1.4): strict global equivalence saturation over a
# finite observed string universe, raw-witness substitution evidence and
# maximum-evidence projective binary tree induction, plus the synthetic
# benchmark, gold evaluator, batch experiments and identifiability audit.
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

foreach(tool scf_generate scf_experiment scf_prepare_text scf_audit scf_real_audit)
    add_executable(${tool} tools/${tool}.cpp)
    target_link_libraries(${tool} PRIVATE scf_core)
    scf_configure_target(${tool})
endforeach()

scf_add_test(scf_tests tests/test_main.cpp scf_core)
if(SCF_BUILD_TESTS)
    # The regression suite reads data/synthetic/*.txt of this line.
    target_compile_definitions(scf_tests PRIVATE SCF_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
endif()
