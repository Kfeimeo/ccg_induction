# Research line 04 (v2.3 + v2.3.1): conservative, frequency-free evidence-
# driven merging -- one exact full-sentence substitution witness makes a
# candidate, a rollback transaction checks observed composition -- and its
# clean-corpus replication (structure-preserving peS2o preprocessing plus
# witness-frame-type diagnostics).  Superseded by line 05, kept as baseline.
add_library(scf_conservative_v23 STATIC src/conservative_merging.cpp)
target_include_directories(scf_conservative_v23 PUBLIC include)
target_compile_features(scf_conservative_v23 PUBLIC cxx_std_20)
target_link_libraries(scf_conservative_v23 PUBLIC scf_real_v21)
scf_configure_target(scf_conservative_v23)

add_executable(scf_conservative_merging tools/scf_conservative_merging.cpp)
target_link_libraries(scf_conservative_merging PRIVATE scf_conservative_v23)
scf_configure_target(scf_conservative_merging)

add_library(scf_clean_corpus_v231 STATIC src/clean_corpus.cpp)
target_include_directories(scf_clean_corpus_v231 PUBLIC include)
target_compile_features(scf_clean_corpus_v231 PUBLIC cxx_std_20)
target_link_libraries(scf_clean_corpus_v231 PUBLIC scf_conservative_v23)
scf_configure_target(scf_clean_corpus_v231)

add_executable(scf_clean_corpus tools/scf_clean_corpus.cpp)
target_link_libraries(scf_clean_corpus PRIVATE scf_clean_corpus_v231)
scf_configure_target(scf_clean_corpus)

scf_add_test(scf_conservative_v23_tests tests/test_conservative_merging.cpp scf_conservative_v23)
scf_add_test(scf_clean_corpus_v231_tests tests/test_clean_corpus.cpp scf_clean_corpus_v231)
