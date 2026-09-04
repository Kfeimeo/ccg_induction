# Research line 05 (v2.4): counterexample-guided closed-world refinement.
# Bounded empirical equivalence u ==_D v  <=>  for all observed frames (L,R)
# in the context universe, [LuR in D] == [LvR in D]; computed by partition
# refinement (splitting on distinguishing contexts) with no frequencies,
# thresholds, similarity scores or pairwise candidate generation.
add_library(scf_closed_world_v24 STATIC src/closed_world.cpp)
target_include_directories(scf_closed_world_v24 PUBLIC include)
target_compile_features(scf_closed_world_v24 PUBLIC cxx_std_20)
target_link_libraries(scf_closed_world_v24 PUBLIC scf_clean_corpus_v231)
scf_configure_target(scf_closed_world_v24)

add_executable(scf_closed_world tools/scf_closed_world.cpp)
target_link_libraries(scf_closed_world PRIVATE scf_closed_world_v24)
scf_configure_target(scf_closed_world)

scf_add_test(scf_closed_world_v24_tests tests/test_closed_world.cpp scf_closed_world_v24)
