#pragma once

#include "scf/gold.hpp"
#include "scf/tree_solver.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace scf {

// One full binary tree over a sentence, represented compactly as the set of
// proper nontrivial spans it contains (leaves and root excluded).
struct EnumeratedTree {
    std::vector<SpanPair> proper_spans;
};

// Enumerates all Catalan(length - 1) full binary trees for length >= 1.
// Intended for reference evaluation and tests only; length is capped at 12.
std::vector<EnumeratedTree> enumerate_binary_trees(std::uint16_t length);

struct BruteForceReport {
    std::uint64_t tree_count{};
    std::uint64_t best_score{};
    std::uint64_t argmax_count{};
    // Second element of the descending multiset of all tree scores. Equal to
    // best_score when the optimum is ambiguous; absent when only one tree
    // exists (length <= 2).
    std::optional<std::uint64_t> second_best_score;
    std::optional<std::uint64_t> margin;  // best_score - second_best_score
    bool all_trees_tied{};
};

// Scores every tree with the exact parser objective (evidence on proper
// nontrivial spans; leaf and root scores ignored). The sentence field of the
// evidence spans is ignored.
BruteForceReport brute_force_tree_scores(std::uint16_t length,
                                         std::span<const SpanScore> evidence);

std::uint64_t score_span_set(std::span<const SpanPair> proper_spans,
                             std::uint16_t length,
                             std::span<const SpanScore> evidence);

}  // namespace scf
