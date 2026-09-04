# Research line 02 (v2.0): oracle category recovery -- an independent module
# with no dependency on the v1.x core (only on the platform layer).
add_library(scf_oracle_v2 STATIC src/oracle_v2.cpp)
target_include_directories(scf_oracle_v2 PUBLIC include)
target_link_libraries(scf_oracle_v2 PUBLIC scf_platform)
target_compile_features(scf_oracle_v2 PUBLIC cxx_std_20)
scf_configure_target(scf_oracle_v2)

add_executable(scf_oracle_v2_cli tools/scf_oracle_v2.cpp)
target_link_libraries(scf_oracle_v2_cli PRIVATE scf_oracle_v2)
set_target_properties(scf_oracle_v2_cli PROPERTIES OUTPUT_NAME scf_oracle_v2)
scf_configure_target(scf_oracle_v2_cli)

scf_add_test(scf_oracle_v2_tests tests/test_oracle_v2.cpp scf_oracle_v2)
