#pragma once

// SCF v2.3.1 -- clean-corpus replication of the v2.3 conservative learner.
//
// This module changes ONLY the corpus and its preprocessing.  Observation
// (scf::v23::observe_sentences) and merging (scf::v23::ConservativeMerger)
// are called unchanged; the module adds a structure-preserving corpus reader,
// nested whole-sentence prefixes, and post-hoc diagnostics that classify
// every witness, candidate, accepted merge, and rejected merge by the
// boundary type of its exact full-sentence frame (L, R):
//
//   empty_frame     L = []  and R = []   (u is a complete sentence)
//   left_boundary   L = []  and R != []  (u is sentence-initial)
//   right_boundary  L != [] and R = []   (u is sentence-final)
//   internal_frame  L != [] and R != []
//
// <BOS>/<EOS> are represented only as the empty trie root of the exact
// frame; they never become objects and never enter the lexical space.

#include "scf/conservative_merging.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace scf::v231 {

enum class FrameType : std::uint8_t {
    empty_frame = 0,
    left_boundary = 1,
    right_boundary = 2,
    internal_frame = 3,
};

inline constexpr std::size_t kFrameTypes = 4;
std::string_view frame_type_name(FrameType type);
FrameType frame_type_of(const v23::ObservedDataset& data, v23::ContextId context);

// Which preprocessing produced the sentence list.
enum class Preprocess {
    // Structure-preserving: input is one body paragraph per line, documents
    // separated by an empty line; sentences are segmented inside paragraphs
    // at every . ? ! token (the terminator is absorbed into <EOS>); all
    // other punctuation tokens are kept as ordinary tokens.
    clean_body,
    // Exact v2.3 corpus construction (v2.2 condition D): one flattened
    // document per line, punctuation tokens removed, prefixes measured in
    // condition-A tokens.  Used to attach frame-type diagnostics to the
    // unchanged v2.3 baseline for the comparison.
    v23_condition_d,
};

struct SentenceCorpus {
    std::vector<std::string> token_text;             // [0] reserved "<boundary>"
    std::vector<std::vector<std::uint32_t>> sentences;
    std::vector<std::uint32_t> sentence_document;    // per sentence
    std::vector<std::uint32_t> sentence_paragraph;   // per sentence (global index)
    std::vector<std::uint64_t> cumulative_nominal;   // per sentence, prefix measure
    std::vector<std::uint64_t> cumulative_actual;    // per sentence, tokens observed
    std::uint64_t documents{};
    std::uint64_t paragraphs{};
    std::uint64_t punctuation_tokens_kept{};
    std::uint64_t punctuation_tokens_dropped{};
    std::uint64_t terminators_consumed{};
};

struct CleanCorpusReadOptions {
    Preprocess preprocess{Preprocess::clean_body};
    bool keep_punctuation{true};        // clean_body only; false = drop like D
    std::uint64_t token_budget{0};      // stop reading at a document boundary
                                        // once this many nominal tokens are
                                        // available (0 = read everything)
};

SentenceCorpus read_sentence_corpus(const std::filesystem::path& input_text,
                                    const CleanCorpusReadOptions& options);

// The first sentence_limit sentences of `corpus` contain >= scale nominal
// tokens (the sentence that crosses the scale is included, as in v2.3).
std::size_t prefix_sentence_limit(const SentenceCorpus& corpus, std::uint64_t scale);

struct FrameTypeRow {
    std::uint64_t witness_count{};
    std::uint64_t candidate_count_any{};    // >= 1 witness of this type
    std::uint64_t candidate_count_only{};   // all witnesses of this type
    std::uint64_t accepted_any{};
    std::uint64_t accepted_only{};
    std::uint64_t rejected_any{};
    std::uint64_t rejected_only{};
    std::uint64_t redundant_only{};
    std::uint64_t induced_unions_only{};
    std::uint64_t accepted_only_in_largest_class{};  // both objects end in it
};

struct FrameDiagnostics {
    std::array<FrameTypeRow, kFrameTypes> rows{};
    std::uint64_t mixed_candidates{};       // witnesses of >= 2 types
    std::uint64_t mixed_accepted{};
    std::uint64_t mixed_rejected{};
    std::uint64_t objects_with_empty_frame{};   // #{u : (eps,eps) observed}
    std::uint64_t largest_class_size{};
    std::uint64_t largest_class_complete_sentence_members{};
    std::uint64_t largest_class_single_token_members{};
    std::uint64_t largest_class_num_members{};   // contain <num>
};

// Per-candidate witness-type mask (bit i = FrameType i), sorted by the
// unordered object pair exactly like the merger's candidate table.
struct CandidateTypeMask {
    v23::ObjectId first{};
    v23::ObjectId second{};
    std::uint8_t mask{};
};
std::vector<CandidateTypeMask> candidate_type_masks(const v23::ObservedDataset& data);
std::uint8_t lookup_mask(const std::vector<CandidateTypeMask>& masks,
                         v23::ObjectId first,
                         v23::ObjectId second);

FrameDiagnostics diagnose_frames(const v23::ObservedDataset& data,
                                 const v23::ConservativeMerger& merger);

struct CleanScaleResult {
    std::uint64_t nominal_tokens{};
    std::uint64_t actual_tokens{};
    std::uint64_t sentences{};
    std::uint64_t documents{};
    std::uint64_t paragraphs{};
    v23::MergeMetrics merge;
    FrameDiagnostics frames;
    v23::PartitionChange change;
    double runtime_seconds{};
    double peak_rss_mb{};
};

struct CleanCorpusConfig {
    std::filesystem::path input_text;
    std::filesystem::path output_dir{"results_v2_3_1_clean_corpus"};
    std::string corpus_label{"clean_wiki_body"};
    CleanCorpusReadOptions read;
    std::vector<std::uint64_t> scales{100'000, 1'000'000, 10'000'000};
    std::size_t max_substring_length{3};
    std::size_t example_limit{20};
    std::size_t largest_classes{20};
    std::size_t class_members_shown{40};
    // Objects whose final class is reported in full detail in
    // probe_object_classes.txt (the patterns named in the v2.3 audit).
    std::vector<std::string> probe_objects{"<num>", "the", "a", "in", "to", ",", "and", "of"};
};

struct CleanCorpusResult {
    std::vector<CleanScaleResult> scales;
    std::uint64_t available_sentences{};
    std::uint64_t available_nominal_tokens{};
};

// Size of the (eps,eps) hub per nested prefix WITHOUT running the learner:
// the number of distinct complete sentences of length <= max_substring_length
// (every such sentence is an object with an empty frame, and every pair of
// them is a direct merge candidate), plus the resulting candidate count
// C(d,2).  Used to forecast the cost of the unchanged v2.3 learner.
struct HubStats {
    std::uint64_t nominal_tokens{};
    std::uint64_t actual_tokens{};
    std::uint64_t sentences{};
    std::uint64_t short_sentence_occurrences{};   // sentences with <= max_len tokens
    std::uint64_t distinct_complete_spans{};      // d = |{u : (eps,eps) observed}|
    std::uint64_t hub_candidate_pairs{};          // C(d, 2)
};
std::vector<HubStats> empty_frame_hub_stats(const SentenceCorpus& corpus,
                                            const std::vector<std::uint64_t>& scales,
                                            std::size_t max_substring_length);

// Runs the nested ladder and writes into output_dir:
//   clean_corpus_scaling.csv
//   frame_type_metrics.csv
//   largest_classes.txt
//   successful_merges_by_frame_type.txt
//   rejected_merges_by_frame_type.txt
//   largest_class_members.txt      (every member of the largest class)
//   probe_object_classes.txt       (full class of each probe object)
CleanCorpusResult run_clean_corpus_scaling(const CleanCorpusConfig& config);

}  // namespace scf::v231
