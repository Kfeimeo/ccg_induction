#pragma once

#include "scf/types.hpp"

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace scf {

struct SpanScore {
    Span span{};
    // Integer objective units: |W| for raw_count, round(strength * 1e12) for
    // normalized objectives (see kStrengthScale in evidence_builder.hpp).
    std::uint64_t score{};
};

struct TreeSolveResult {
    bool consistent{};
    bool hard_consistent{};
    bool overflowed{};
    std::uint64_t best_score{};
    std::uint64_t optimal_tree_count{};
    std::uint64_t tree_count{};
    std::vector<Span> observed_spans;
    std::vector<Span> forced_spans;
    std::vector<std::pair<Span, Span>> crossing_conflicts;
    std::vector<std::vector<std::uint16_t>> optimal_splits;
    std::vector<std::int16_t> unique_tree_splits;
    std::uint16_t sentence_length{};
};

bool spans_cross(const Span& lhs, const Span& rhs) noexcept;

bool split_respects_constraints(std::uint16_t begin,
                                std::uint16_t split,
                                std::uint16_t end,
                                std::span<const Span> hard_spans) noexcept;

TreeSolveResult solve_binary_trees(SentenceId sentence,
                                   std::uint16_t sentence_length,
                                   std::span<const Span> hard_spans);

TreeSolveResult solve_maximum_evidence_trees(SentenceId sentence,
                                             std::uint16_t sentence_length,
                                             std::span<const SpanScore> evidence,
                                             std::span<const Span> hard_spans = {});

}  // namespace scf
