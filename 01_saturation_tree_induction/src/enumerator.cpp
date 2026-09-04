#include "scf/enumerator.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>

namespace scf {
namespace {

// Returns every tree over [begin,end) as its span list including [begin,end)
// itself (when length >= 2) but never leaves.
std::vector<std::vector<SpanPair>> enumerate_range(const std::uint16_t begin,
                                                   const std::uint16_t end) {
    if (end == begin + 1) {
        return {{}};
    }
    std::vector<std::vector<SpanPair>> trees;
    for (auto split = static_cast<std::uint16_t>(begin + 1); split < end; ++split) {
        const auto left_trees = enumerate_range(begin, split);
        const auto right_trees = enumerate_range(split, end);
        for (const auto& left : left_trees) {
            for (const auto& right : right_trees) {
                std::vector<SpanPair> spans;
                spans.reserve(left.size() + right.size() + 1);
                spans.emplace_back(begin, end);
                spans.insert(spans.end(), left.begin(), left.end());
                spans.insert(spans.end(), right.begin(), right.end());
                trees.push_back(std::move(spans));
            }
        }
    }
    return trees;
}

}  // namespace

std::vector<EnumeratedTree> enumerate_binary_trees(const std::uint16_t length) {
    if (length == 0) {
        throw std::invalid_argument("cannot enumerate trees for an empty sentence");
    }
    if (length > 12) {
        throw std::invalid_argument(
            "brute-force enumeration is limited to length <= 12; use the DP solver instead");
    }
    auto raw = enumerate_range(0, length);
    std::vector<EnumeratedTree> trees;
    trees.reserve(raw.size());
    for (auto& spans : raw) {
        EnumeratedTree tree;
        tree.proper_spans.reserve(spans.size());
        for (const auto& span : spans) {
            if (span.first == 0 && span.second == length) {
                continue;  // root is never scored
            }
            tree.proper_spans.push_back(span);
        }
        std::sort(tree.proper_spans.begin(), tree.proper_spans.end());
        trees.push_back(std::move(tree));
    }
    return trees;
}

std::uint64_t score_span_set(const std::span<const SpanPair> proper_spans,
                             const std::uint16_t length,
                             const std::span<const SpanScore> evidence) {
    std::map<SpanPair, std::uint64_t> scores;
    for (const auto& item : evidence) {
        if (item.span.end <= item.span.begin + 1 ||
            (item.span.begin == 0 && item.span.end == length)) {
            continue;
        }
        scores[{item.span.begin, item.span.end}] = item.score;
    }
    std::uint64_t total = 0;
    for (const auto& span : proper_spans) {
        const auto found = scores.find(span);
        if (found != scores.end()) {
            total += found->second;
        }
    }
    return total;
}

BruteForceReport brute_force_tree_scores(const std::uint16_t length,
                                         const std::span<const SpanScore> evidence) {
    const auto trees = enumerate_binary_trees(length);
    std::vector<std::uint64_t> scores;
    scores.reserve(trees.size());
    for (const auto& tree : trees) {
        scores.push_back(score_span_set(tree.proper_spans, length, evidence));
    }
    std::sort(scores.begin(), scores.end(), std::greater<>{});

    BruteForceReport report;
    report.tree_count = scores.size();
    report.best_score = scores.front();
    report.argmax_count = static_cast<std::uint64_t>(
        std::count(scores.begin(), scores.end(), scores.front()));
    report.all_trees_tied = report.argmax_count == report.tree_count;
    if (scores.size() > 1) {
        report.second_best_score = scores[1];
        report.margin = report.best_score - scores[1];
    }
    return report;
}

}  // namespace scf
