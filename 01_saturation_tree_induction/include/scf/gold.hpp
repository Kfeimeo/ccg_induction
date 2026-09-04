#pragma once

#include "scf/types.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace scf {

// Gold trees are evaluation-only artifacts. The SCF parser never reads them,
// their labels, or the grammar nonterminals that produced them.

struct GoldNode {
    std::string label;               // nonterminal for internal nodes, terminal token for leaves
    std::vector<GoldNode> children;  // empty for leaves; exactly two for benchmark internal nodes

    [[nodiscard]] bool is_leaf() const noexcept { return children.empty(); }
};

using SpanPair = std::pair<std::uint16_t, std::uint16_t>;

struct LabeledSpan {
    std::uint16_t begin{};
    std::uint16_t end{};
    std::string label;
};

// Compact span-based representation of one gold binary tree.
// `internal_spans` holds every span of length >= 2 including the root and is
// sorted by (begin, end). Leaves are implicit.
struct GoldTree {
    std::uint16_t length{};
    std::vector<LabeledSpan> internal_spans;
};

std::size_t leaf_count(const GoldNode& node);
std::vector<std::string> leaf_tokens(const GoldNode& node);

// Removes unary chains (e.g. B -> V -> runs) by replacing each single-child
// node with its child, keeping the lower node.
GoldNode collapse_unary_chains(GoldNode node);

// Throws std::runtime_error when any node has a child count other than 0 or 2.
GoldTree gold_tree_from_node(const GoldNode& node);

// Spans used for matching/F1. Defaults exclude both leaves and the root.
std::set<SpanPair> gold_eval_spans(const GoldTree& tree,
                                   bool include_root = false,
                                   bool include_leaves = false);

// Spans that receive evidence score, exactly matching the parser objective:
// proper nontrivial spans only (never leaves, never the root).
std::set<SpanPair> gold_scoring_spans(const GoldTree& tree);

std::string format_gold_bracket(const GoldNode& node);
std::string bracket_from_gold_tree(const GoldTree& tree, std::span<const std::string> tokens);

// Parses "((a b) (c d))" into a GoldNode with empty internal labels.
GoldNode parse_bracket_tree(const std::string& text);

struct GoldSpanRow {
    SentenceId sentence{};
    std::uint16_t begin{};
    std::uint16_t end{};
    std::string label;
};

std::vector<GoldSpanRow> read_gold_span_rows(std::istream& input);
std::vector<GoldSpanRow> read_gold_span_file(const std::string& path);

// Groups rows by sentence id, validates them against the corpus sentence
// lengths, and returns one GoldTree per sentence. Fails loudly when sentence
// ids mismatch or the spans do not form a valid projective full binary tree.
std::vector<GoldTree> assemble_gold_trees(std::span<const GoldSpanRow> rows,
                                          std::span<const std::uint16_t> sentence_lengths);

void write_gold_spans_tsv(std::ostream& output, std::span<const GoldTree> trees);

std::string format_span_pair(const SpanPair& span);
std::string format_span_pairs(std::span<const SpanPair> spans);

}  // namespace scf
