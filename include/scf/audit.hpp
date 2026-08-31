#pragma once

#include "scf/corpus.hpp"
#include "scf/evidence_builder.hpp"
#include "scf/gold.hpp"
#include "scf/synthetic.hpp"

#include <cstdint>
#include <iosfwd>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace scf {

// v1.2.1 audit utilities: observational-equivalence hashes, span-length bias
// diagnostics, and explicit tree-shape scoring. Everything here is read-only
// with respect to the parser: no objective, evidence, or DP change.

std::string hash_hex(std::uint64_t hash);

// FNV-1a 64 over a canonical serialization; deterministic across platforms.
std::uint64_t fnv1a(const std::string& bytes);

using TokenSentences = std::vector<std::vector<std::string>>;

TokenSentences sentence_tokens(std::span<const GoldSentence> sentences);

// Hash of a sentence set: sentences are serialized, lexicographically sorted,
// and hashed, so the value is independent of grammar labels, gold trees, and
// generation order.
std::uint64_t sentence_set_hash(const TokenSentences& sentences);

// Greedy canonical token renaming: tokens are renamed t0, t1, ... in first-
// occurrence order over the lexicographically sorted sentence set. This is a
// heuristic canonical form, sufficient to detect the token-renaming
// isomorphisms between the factorized families audited here; it is not a
// general graph canonization.
using TokenRenaming = std::unordered_map<std::string, std::string>;
TokenRenaming build_canonical_renaming(const TokenSentences& sentences);
TokenSentences apply_renaming(const TokenSentences& sentences, const TokenRenaming& renaming);

// Hash of the exact raw context relation {(L, R) -> yield}: canonical sort,
// deduplicated, serialized as token text. Optionally token-renamed.
std::uint64_t raw_context_relation_hash(const Corpus& corpus,
                                        const TokenRenaming* renaming = nullptr);

// Hash of the raw witness relation {yield_pair -> set(raw_context)} exactly as
// the tree objective consumes it.
std::uint64_t raw_witness_relation_hash(const Corpus& corpus,
                                        const EvidenceBuilder& builder,
                                        const TokenRenaming* renaming = nullptr);

// Hash of sentence -> gold span shape, used to report same_gold_tree.
std::uint64_t gold_shape_hash(std::span<const GoldSentence> sentences,
                              const TokenRenaming* renaming = nullptr);

struct DatasetHashes {
    std::uint64_t surface_language{};
    std::uint64_t sampled_corpus{};
    std::uint64_t raw_context_relation{};
    std::uint64_t raw_witness_relation{};
};

// The four per-run hashes of the v1.2.1 audit. The full language is
// regenerated from the dataset parameters; corpus/builder must come from the
// dataset's sampled corpus.
DatasetHashes compute_dataset_hashes(const SyntheticDataset& dataset,
                                     const Corpus& corpus,
                                     const EvidenceBuilder& builder);

// --- span-length bias diagnostics ---------------------------------------

struct SpanLengthStats {
    std::uint16_t span_length{};
    std::size_t total_span_count{};      // all proper nontrivial spans of this length
    std::size_t candidate_span_count{};  // spans with evidence score > 0
    double mean_score{};
    double median_score{};
    std::uint64_t max_score{};
    double mean_gold_span_score{};
    double mean_non_gold_span_score{};
};

// Statistics over every proper nontrivial span of every sentence (score 0 when
// a span carries no evidence), grouped by span length.
std::vector<SpanLengthStats> score_by_span_length(std::span<const std::uint16_t> sentence_lengths,
                                                  std::span<const SpanEvidence> evidence,
                                                  std::span<const GoldTree> gold);

void write_score_by_span_length_csv(std::ostream& output,
                                    std::span<const SpanLengthStats> stats);

// --- explicit tree-shape scores (length-4 sentences) ---------------------

struct TreeShapeScores {
    SentenceId sentence{};
    std::uint64_t balanced_score{};  // {[0,2), [2,4)}
    std::uint64_t left_score{};      // {[0,2), [0,3)}
    std::uint64_t right_score{};     // {[1,4), [2,4)}
    std::string best_shape;          // ties joined with '|'
};

std::vector<TreeShapeScores> tree_shape_scores(std::span<const std::uint16_t> sentence_lengths,
                                               std::span<const SpanEvidence> evidence);

void write_tree_shape_scores_tsv(std::ostream& output, std::span<const TreeShapeScores> scores);

}  // namespace scf
