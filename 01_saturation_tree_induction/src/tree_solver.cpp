#include "scf/tree_solver.hpp"

#include <algorithm>
#include <cassert>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>

namespace scf {
namespace {

std::uint64_t safe_add(const std::uint64_t lhs, const std::uint64_t rhs, bool& overflowed) {
    if (std::numeric_limits<std::uint64_t>::max() - lhs < rhs) {
        overflowed = true;
        return std::numeric_limits<std::uint64_t>::max();
    }
    return lhs + rhs;
}

std::uint64_t safe_multiply(const std::uint64_t lhs, const std::uint64_t rhs, bool& overflowed) {
    if (lhs != 0 && rhs > std::numeric_limits<std::uint64_t>::max() / lhs) {
        overflowed = true;
        return std::numeric_limits<std::uint64_t>::max();
    }
    return lhs * rhs;
}

std::size_t index_of(const std::uint16_t begin,
                     const std::uint16_t end,
                     const std::size_t dimension) {
    return static_cast<std::size_t>(begin) * dimension + end;
}

using SpanSet = std::set<Span>;

SpanSet set_intersection(const SpanSet& lhs, const SpanSet& rhs) {
    SpanSet result;
    std::set_intersection(lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
                          std::inserter(result, result.end()));
    return result;
}

}  // namespace

bool spans_cross(const Span& lhs, const Span& rhs) noexcept {
    return (lhs.begin < rhs.begin && rhs.begin < lhs.end && lhs.end < rhs.end) ||
           (rhs.begin < lhs.begin && lhs.begin < rhs.end && rhs.end < lhs.end);
}

bool split_respects_constraints(const std::uint16_t begin,
                                const std::uint16_t split,
                                const std::uint16_t end,
                                const std::span<const Span> hard_spans) noexcept {
    for (const auto& hard : hard_spans) {
        const bool is_current_span = hard.begin == begin && hard.end == end;
        if (!is_current_span && begin <= hard.begin && hard.begin < split && split < hard.end &&
            hard.end <= end) {
            return false;
        }
    }
    return true;
}

TreeSolveResult solve_maximum_evidence_trees(const SentenceId sentence,
                                             const std::uint16_t sentence_length,
                                             const std::span<const SpanScore> evidence,
                                             const std::span<const Span> hard_spans) {
    TreeSolveResult result;
    result.sentence_length = sentence_length;
    result.observed_spans.assign(hard_spans.begin(), hard_spans.end());
    std::sort(result.observed_spans.begin(), result.observed_spans.end());
    result.observed_spans.erase(
        std::unique(result.observed_spans.begin(), result.observed_spans.end()), result.observed_spans.end());

    if (sentence_length == 0) {
        return result;
    }
    for (const auto& span : result.observed_spans) {
        if (span.sentence != sentence || span.begin >= span.end || span.end > sentence_length) {
            throw std::invalid_argument("hard span is outside the sentence");
        }
    }
    for (std::size_t first = 0; first < result.observed_spans.size(); ++first) {
        for (std::size_t second = first + 1; second < result.observed_spans.size(); ++second) {
            if (spans_cross(result.observed_spans[first], result.observed_spans[second])) {
                result.crossing_conflicts.emplace_back(result.observed_spans[first],
                                                       result.observed_spans[second]);
            }
        }
    }
    if (!result.crossing_conflicts.empty()) {
        return result;
    }

    const auto dimension = static_cast<std::size_t>(sentence_length) + 1;
    const auto cell_count = dimension * dimension;
    std::vector<std::uint64_t> span_scores(cell_count, 0);
    std::vector<bool> score_seen(cell_count, false);
    for (const auto& item : evidence) {
        const auto& span = item.span;
        if (span.sentence != sentence || span.begin >= span.end || span.end > sentence_length) {
            throw std::invalid_argument("evidence span is outside the sentence");
        }
        const auto index = index_of(span.begin, span.end, dimension);
        if (score_seen[index] && span_scores[index] != item.score) {
            throw std::invalid_argument("duplicate evidence span has conflicting scores");
        }
        score_seen[index] = true;
        span_scores[index] = item.score;
    }
    for (std::uint16_t begin = 0; begin < sentence_length; ++begin) {
        span_scores[index_of(begin, static_cast<std::uint16_t>(begin + 1), dimension)] = 0;
    }
    span_scores[index_of(0, sentence_length, dimension)] = 0;

    std::vector<std::uint64_t> best(cell_count, 0);
    std::vector<std::uint64_t> counts(cell_count, 0);
    result.optimal_splits.resize(cell_count);
    for (std::uint16_t begin = 0; begin < sentence_length; ++begin) {
        counts[index_of(begin, static_cast<std::uint16_t>(begin + 1), dimension)] = 1;
    }

    for (std::uint16_t length = 2; length <= sentence_length; ++length) {
        for (std::uint16_t begin = 0; begin + length <= sentence_length; ++begin) {
            const auto end = static_cast<std::uint16_t>(begin + length);
            const auto current = index_of(begin, end, dimension);
            bool has_model = false;
            std::uint64_t best_children = 0;
            for (std::uint16_t split = static_cast<std::uint16_t>(begin + 1); split < end; ++split) {
                if (!split_respects_constraints(begin, split, end, result.observed_spans)) {
                    continue;
                }
                const auto left = index_of(begin, split, dimension);
                const auto right = index_of(split, end, dimension);
                if (counts[left] == 0 || counts[right] == 0) {
                    continue;
                }
                const auto child_score = safe_add(best[left], best[right], result.overflowed);
                if (!has_model || child_score > best_children) {
                    has_model = true;
                    best_children = child_score;
                    result.optimal_splits[current].assign(1, split);
                } else if (child_score == best_children) {
                    result.optimal_splits[current].push_back(split);
                }
            }
            if (!has_model) {
                continue;
            }
            best[current] = safe_add(span_scores[current], best_children, result.overflowed);
            for (const auto split : result.optimal_splits[current]) {
                const auto product = safe_multiply(counts[index_of(begin, split, dimension)],
                                                   counts[index_of(split, end, dimension)],
                                                   result.overflowed);
                counts[current] = safe_add(counts[current], product, result.overflowed);
            }
        }
    }

    const auto root = index_of(0, sentence_length, dimension);
    result.best_score = best[root];
    result.optimal_tree_count = counts[root];
    result.tree_count = result.optimal_tree_count;
    if (result.optimal_tree_count == 0) {
        return result;
    }
    result.consistent = true;
    result.hard_consistent = true;

    std::vector<SpanSet> forced(cell_count);
    for (std::uint16_t begin = 0; begin < sentence_length; ++begin) {
        const auto end = static_cast<std::uint16_t>(begin + 1);
        forced[index_of(begin, end, dimension)].insert(Span{sentence, begin, end});
    }
    for (std::uint16_t length = 2; length <= sentence_length; ++length) {
        for (std::uint16_t begin = 0; begin + length <= sentence_length; ++begin) {
            const auto end = static_cast<std::uint16_t>(begin + length);
            const auto current = index_of(begin, end, dimension);
            bool first_alternative = true;
            SpanSet common;
            for (const auto split : result.optimal_splits[current]) {
                SpanSet alternative{Span{sentence, begin, end}};
                const auto& left = forced[index_of(begin, split, dimension)];
                const auto& right = forced[index_of(split, end, dimension)];
                alternative.insert(left.begin(), left.end());
                alternative.insert(right.begin(), right.end());
                if (first_alternative) {
                    common = std::move(alternative);
                    first_alternative = false;
                } else {
                    common = set_intersection(common, alternative);
                }
            }
            forced[current] = std::move(common);
        }
    }
    result.forced_spans.assign(forced[root].begin(), forced[root].end());

    if (result.optimal_tree_count == 1) {
        result.unique_tree_splits.assign(cell_count, -1);
        for (std::uint16_t length = 2; length <= sentence_length; ++length) {
            for (std::uint16_t begin = 0; begin + length <= sentence_length; ++begin) {
                const auto end = static_cast<std::uint16_t>(begin + length);
                const auto current = index_of(begin, end, dimension);
                if (counts[current] == 1) {
                    assert(result.optimal_splits[current].size() == 1);
                    result.unique_tree_splits[current] =
                        static_cast<std::int16_t>(result.optimal_splits[current].front());
                }
            }
        }
    }
    return result;
}

TreeSolveResult solve_binary_trees(const SentenceId sentence,
                                   const std::uint16_t sentence_length,
                                   const std::span<const Span> hard_spans) {
    return solve_maximum_evidence_trees(sentence, sentence_length, {}, hard_spans);
}

}  // namespace scf
