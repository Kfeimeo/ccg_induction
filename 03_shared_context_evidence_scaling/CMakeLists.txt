# Research line 03 (v2.1 + v2.2): shared-context evidence on real corpora at
# nested token scales (frequency floor, evidence thresholds, hub cap, held-out
# replication) and the terminal x punctuation ablation.  Also provides the
# deterministic tokenizer reused by lines 04 and 05.
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

scf_add_test(scf_real_v21_tests tests/test_real_scaling.cpp scf_real_v21)
