#pragma once

// SCF v2.3.1 -- clean-corpus replication of the v2.3 conservative learner.
//
// This module does NOT touch the v2.3 merge semantics: it reuses
// scf::v23::observe_sentences and scf::v23::ConservativeMerger verbatim and
// only replaces (a) the corpus/preprocessing in front of them and (b) the
// diagnostics behind them (witness-frame boundary types).
//
// Preprocessing modes:
//   structured : the v2.3.1 structure-preserving pipeline.  Input is the
//                `.scs` file written by tools/prepare_clean_corpus.py
//                (`#doc`, `#par`, one raw sentence per line).  Tokenization
//                is the v2.1 deterministic tokenizer (lowercase, digits ->
//                <num>); internal punctuation is KEPT; the sentence-final
//                . ? ! tokens are consumed as the <EOS> boundary (metadata,
//                not an object).  Substrings never cross a sentence,
//                paragraph, or document boundary because every sentence is
//                observed on its own.
//   v23d       : the exact v2.3 condition-D preprocessing (v2.1 tokenizer,
//                every . ? ! ends a sentence, all punctuation removed, one
//                document per line), reproduced here so that the same
//                frame-type diagnostics can be computed for the FineWeb
//                baseline without modifying the v2.3 tool.

#include "scf/conservative_merging.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace scf::v231 {

// ---------------------------------------------------------------------------
// Corpus loading
// ---------------------------------------------------------------------------

struct SentenceCorpus {
    std::vector<std::string> token_text;
    std::vector<std::vector<std::uint32_t>> sentences;
    std::vector<std::uint32_t> sentence_document;
    std::vector<std::uint32_t> sentence_paragraph;
    // Cumulative token counts per sentence.  `nominal` drives nested-prefix
    // selection, `actual` is the number of tokens the learner sees.  They
    // are equal in structured mode; in v23d mode nominal counts condition-A
    // tokens (punctuation included) exactly like v2.3 and actual counts the
    // condition-D tokens.
    std::vector<std::uint64_t> cumulative_nominal;
    std::vector<std::uint64_t> cumulative_actual;
    std::uint64_t documents{};
    std::uint64_t paragraphs{};
    std::uint64_t consumed_final_punctuation{};
    std::uint64_t dropped_empty_sentences{};
};

// Reads a `.scs` structured corpus.  Stops after the document in which the
// cumulative nominal token count reaches token_limit (0 = read everything).
SentenceCorpus load_structured_corpus(const std::filesystem::path& input,
                                      std::uint64_t token_limit);

// Reproduces the v2.3 condition-D sentence construction from a one-document-
// per-line text file (see run_conservative_scaling in conservative_merging.cpp).
SentenceCorpus load_condition_d_corpus(const std::filesystem::path& input,
                                       std::uint64_t token_limit);

// Number of leading sentences that make up the nested prefix for `scale`:
// the smallest prefix whose cumulative nominal count reaches the scale.
std::size_t prefix_sentences(const SentenceCorpus& corpus, std::uint64_t scale);

// ---------------------------------------------------------------------------
// Witness frame boundary types
// ---------------------------------------------------------------------------

enum class FrameType : std::uint8_t {
    empty_frame = 0,     // L=[] and R=[]
    left_boundary = 1,   // L=[] and R!=[]
    right_boundary = 2,  // L!=[] and R=[]
    internal_frame = 3,  // L!=[] and R!=[]
};

inline constexpr std::array<std::string_view, 4> kFrameTypeNames{
    "empty_frame", "left_boundary", "right_boundary", "internal_frame"};

FrameType classify_context(const v23::ObservedDataset& data, v23::ContextId context);

struct FrameTypeRow {
    std::uint64_t witness_count{};
    std::uint64_t candidate_count{};          // by the candidate's recorded witness
    std::uint64_t accepted_merge_count{};
    std::uint64_t rejected_merge_count{};
    std::uint64_t redundant_candidate_count{};
    std::uint64_t induced_unions{};
    std::uint64_t exclusive_candidate_count{};  // all witnesses of the pair have this type
    std::uint64_t exclusive_accepted_count{};
    std::uint64_t largest_class_accepted_merges{};  // accepted merges inside the largest class
};

struct FrameDiagnostics {
    std::array<FrameTypeRow, 4> rows;
    std::uint64_t objects_with_empty_frame{};  // #{u : (eps, eps) observed for u}
    std::uint64_t largest_class_size{};
    std::uint64_t largest_class_members_with_empty_frame{};
};

// Recomputes the merger's candidate table (first witness per unordered pair
// in witness order -- the same rule ConservativeMerger::run uses) and
// classifies witnesses, candidates, accepted and rejected merges.
FrameDiagnostics compute_frame_diagnostics(const v23::ObservedDataset& data,
                                           const v23::ConservativeMerger& merger);

// ---------------------------------------------------------------------------
// Ladder driver
// ---------------------------------------------------------------------------

struct CleanCorpusConfig {
    std::filesystem::path input;
    std::filesystem::path output_dir{"04_conservative_witness_merging/results/v2_3_1_clean_corpus"};
    std::string corpus_label{"pes2o"};
    std::string preprocessing{"structured"};  // or "v23d"
    std::vector<std::uint64_t> scales{100'000, 1'000'000, 10'000'000};
    std::filesystem::path ud_conllu;
    std::size_t max_substring_length{3};
    std::size_t example_limit{20};
    std::size_t largest_classes{20};
};

struct CleanScaleResult {
    std::uint64_t nominal_tokens{};
    std::uint64_t actual_tokens{};
    std::uint64_t sentences{};
    std::uint64_t documents{};
    std::uint64_t paragraphs{};
    v23::MergeMetrics merge;
    v23::PosDiagnostics pos;
    v23::PartitionChange change;
    FrameDiagnostics frames;
    double runtime_seconds{};
    double peak_rss_mb{};
};

struct CleanCorpusResult {
    std::vector<CleanScaleResult> scales;
    std::uint64_t available_sentences{};
    std::uint64_t available_actual_tokens{};
};

// Runs the nested ladder and writes clean_corpus_scaling.csv,
// frame_type_metrics.csv, largest_classes.txt,
// successful_merges_by_frame_type.txt and rejected_merges_by_frame_type.txt.
CleanCorpusResult run_clean_corpus_scaling(const CleanCorpusConfig& config);

}  // namespace scf::v231
