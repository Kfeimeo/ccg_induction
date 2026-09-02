#include "scf/gold.hpp"
#include "scf/platform.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>

namespace scf {
namespace {

std::uint16_t collect_internal_spans(const GoldNode& node,
                                     const std::uint16_t begin,
                                     std::vector<LabeledSpan>& spans) {
    if (node.is_leaf()) {
        return 1;
    }
    if (node.children.size() != 2) {
        throw std::runtime_error("gold tree node with " + std::to_string(node.children.size()) +
                                 " children is unsupported: benchmark trees must be binary");
    }
    const auto left_width = collect_internal_spans(node.children[0], begin, spans);
    const auto right_width = collect_internal_spans(
        node.children[1], static_cast<std::uint16_t>(begin + left_width), spans);
    const auto width = static_cast<std::uint16_t>(left_width + right_width);
    spans.push_back(LabeledSpan{begin, static_cast<std::uint16_t>(begin + width), node.label});
    return width;
}

bool span_order(const LabeledSpan& lhs, const LabeledSpan& rhs) {
    return std::pair(lhs.begin, lhs.end) < std::pair(rhs.begin, rhs.end);
}

// A span [b,e) is available as a tree node when it is a leaf or listed.
bool node_available(const std::set<SpanPair>& spans, const std::uint16_t begin, const std::uint16_t end) {
    return end == begin + 1 || spans.contains({begin, end});
}

// Finds the unique split of [begin,end) into two available children.
// Returns 0 when no valid split exists; throws on multiple splits.
std::uint16_t find_unique_split(const std::set<SpanPair>& spans,
                                const std::uint16_t begin,
                                const std::uint16_t end,
                                const SentenceId sentence) {
    std::uint16_t found = 0;
    for (auto split = static_cast<std::uint16_t>(begin + 1); split < end; ++split) {
        if (node_available(spans, begin, split) && node_available(spans, split, end)) {
            if (found != 0) {
                throw std::runtime_error("gold spans of sentence " + std::to_string(sentence) +
                                         " admit multiple decompositions of [" + std::to_string(begin) +
                                         "," + std::to_string(end) + "): malformed gold tree");
            }
            found = split;
        }
    }
    return found;
}

void validate_gold_sentence(const std::set<SpanPair>& spans,
                            const std::uint16_t begin,
                            const std::uint16_t end,
                            const SentenceId sentence) {
    if (end == begin + 1) {
        return;
    }
    const auto split = find_unique_split(spans, begin, end, sentence);
    if (split == 0) {
        throw std::runtime_error("gold spans of sentence " + std::to_string(sentence) +
                                 " do not decompose [" + std::to_string(begin) + "," +
                                 std::to_string(end) + ") into two constituents");
    }
    validate_gold_sentence(spans, begin, split, sentence);
    validate_gold_sentence(spans, split, end, sentence);
}

std::string render_bracket(const std::set<SpanPair>& spans,
                           const std::span<const std::string> tokens,
                           const std::uint16_t begin,
                           const std::uint16_t end) {
    if (end == begin + 1) {
        return tokens[begin];
    }
    const auto split = find_unique_split(spans, begin, end, 0);
    if (split == 0) {
        throw std::runtime_error("cannot render bracket: gold spans are not a binary tree");
    }
    return "(" + render_bracket(spans, tokens, begin, split) + " " +
           render_bracket(spans, tokens, split, end) + ")";
}

}  // namespace

std::size_t leaf_count(const GoldNode& node) {
    if (node.is_leaf()) {
        return 1;
    }
    std::size_t total = 0;
    for (const auto& child : node.children) {
        total += leaf_count(child);
    }
    return total;
}

std::vector<std::string> leaf_tokens(const GoldNode& node) {
    std::vector<std::string> tokens;
    if (node.is_leaf()) {
        tokens.push_back(node.label);
        return tokens;
    }
    for (const auto& child : node.children) {
        auto part = leaf_tokens(child);
        tokens.insert(tokens.end(), std::make_move_iterator(part.begin()),
                      std::make_move_iterator(part.end()));
    }
    return tokens;
}

GoldNode collapse_unary_chains(GoldNode node) {
    for (auto& child : node.children) {
        child = collapse_unary_chains(std::move(child));
    }
    while (node.children.size() == 1) {
        GoldNode only = std::move(node.children.front());
        node = std::move(only);
    }
    return node;
}

GoldTree gold_tree_from_node(const GoldNode& node) {
    const auto leaves = leaf_count(node);
    if (leaves == 0 || leaves > std::numeric_limits<std::uint16_t>::max()) {
        throw std::runtime_error("gold tree has unsupported leaf count");
    }
    GoldTree tree;
    tree.length = static_cast<std::uint16_t>(leaves);
    collect_internal_spans(node, 0, tree.internal_spans);
    std::sort(tree.internal_spans.begin(), tree.internal_spans.end(), span_order);
    return tree;
}

std::set<SpanPair> gold_eval_spans(const GoldTree& tree,
                                   const bool include_root,
                                   const bool include_leaves) {
    std::set<SpanPair> spans;
    for (const auto& span : tree.internal_spans) {
        const bool is_root = span.begin == 0 && span.end == tree.length;
        if (is_root && !include_root) {
            continue;
        }
        spans.emplace(span.begin, span.end);
    }
    if (include_leaves) {
        for (std::uint16_t index = 0; index < tree.length; ++index) {
            spans.emplace(index, static_cast<std::uint16_t>(index + 1));
        }
    }
    return spans;
}

std::set<SpanPair> gold_scoring_spans(const GoldTree& tree) {
    return gold_eval_spans(tree, false, false);
}

std::string format_gold_bracket(const GoldNode& node) {
    if (node.is_leaf()) {
        return node.label;
    }
    std::string text = "(";
    for (std::size_t index = 0; index < node.children.size(); ++index) {
        if (index != 0) {
            text += ' ';
        }
        text += format_gold_bracket(node.children[index]);
    }
    text += ')';
    return text;
}

std::string bracket_from_gold_tree(const GoldTree& tree, const std::span<const std::string> tokens) {
    if (tokens.size() != tree.length) {
        throw std::runtime_error("token count does not match gold tree length");
    }
    if (tree.length == 1) {
        return tokens[0];
    }
    std::set<SpanPair> spans;
    for (const auto& span : tree.internal_spans) {
        spans.emplace(span.begin, span.end);
    }
    return render_bracket(spans, tokens, 0, tree.length);
}

GoldNode parse_bracket_tree(const std::string& text) {
    std::size_t position = 0;
    const auto skip_spaces = [&] {
        while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position]))) {
            ++position;
        }
    };
    std::function<GoldNode()> parse_node = [&]() -> GoldNode {
        skip_spaces();
        if (position >= text.size()) {
            throw std::runtime_error("unexpected end of bracket expression");
        }
        if (text[position] == '(') {
            ++position;
            GoldNode node;
            while (true) {
                skip_spaces();
                if (position >= text.size()) {
                    throw std::runtime_error("unbalanced bracket expression");
                }
                if (text[position] == ')') {
                    ++position;
                    break;
                }
                node.children.push_back(parse_node());
            }
            if (node.children.empty()) {
                throw std::runtime_error("empty bracket node");
            }
            return node;
        }
        std::string token;
        while (position < text.size() && text[position] != '(' && text[position] != ')' &&
               !std::isspace(static_cast<unsigned char>(text[position]))) {
            token += text[position];
            ++position;
        }
        if (token.empty()) {
            throw std::runtime_error("invalid bracket expression");
        }
        return GoldNode{token, {}};
    };
    auto node = parse_node();
    skip_spaces();
    if (position != text.size()) {
        throw std::runtime_error("trailing characters after bracket expression");
    }
    return node;
}

std::vector<GoldSpanRow> read_gold_span_rows(std::istream& input) {
    std::vector<GoldSpanRow> rows;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        platform::strip_trailing_cr(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        std::istringstream fields(line);
        unsigned long long sentence = 0;
        unsigned long long begin = 0;
        unsigned long long end = 0;
        std::string label;
        if (!(fields >> sentence >> begin >> end)) {
            throw std::runtime_error("gold spans line " + std::to_string(line_number) +
                                     " is not 'sentence_id begin end [label]'");
        }
        fields >> label;
        if (begin >= end || end > std::numeric_limits<std::uint16_t>::max()) {
            throw std::runtime_error("gold spans line " + std::to_string(line_number) +
                                     " has an invalid half-open interval");
        }
        rows.push_back(GoldSpanRow{static_cast<SentenceId>(sentence),
                                   static_cast<std::uint16_t>(begin),
                                   static_cast<std::uint16_t>(end),
                                   std::move(label)});
    }
    return rows;
}

std::vector<GoldSpanRow> read_gold_span_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open gold spans: " + path);
    }
    return read_gold_span_rows(input);
}

std::vector<GoldTree> assemble_gold_trees(const std::span<const GoldSpanRow> rows,
                                          const std::span<const std::uint16_t> sentence_lengths) {
    std::vector<std::map<SpanPair, std::string>> grouped(sentence_lengths.size());
    std::vector<bool> mentioned(sentence_lengths.size(), false);
    for (const auto& row : rows) {
        if (row.sentence >= sentence_lengths.size()) {
            throw std::runtime_error("gold spans reference sentence " + std::to_string(row.sentence) +
                                     " but the corpus has only " +
                                     std::to_string(sentence_lengths.size()) + " sentences");
        }
        const auto length = sentence_lengths[row.sentence];
        if (row.end > length) {
            throw std::runtime_error("gold span [" + std::to_string(row.begin) + "," +
                                     std::to_string(row.end) + ") exceeds the length of sentence " +
                                     std::to_string(row.sentence));
        }
        mentioned[row.sentence] = true;
        grouped[row.sentence].emplace(SpanPair{row.begin, row.end}, row.label);
    }

    std::vector<GoldTree> trees;
    trees.reserve(sentence_lengths.size());
    for (std::size_t sentence = 0; sentence < sentence_lengths.size(); ++sentence) {
        if (!mentioned[sentence]) {
            throw std::runtime_error("gold spans are missing sentence " + std::to_string(sentence) +
                                     ": corpus and gold file mismatch");
        }
        const auto length = sentence_lengths[sentence];
        GoldTree tree;
        tree.length = length;
        std::set<SpanPair> internal;
        for (const auto& [span, label] : grouped[sentence]) {
            if (span.second == span.first + 1) {
                continue;  // leaves may be present but are implicit
            }
            internal.insert(span);
        }
        if (length >= 2) {
            internal.emplace(0, length);  // the root may be omitted in the file
            if (internal.size() != static_cast<std::size_t>(length) - 1) {
                throw std::runtime_error(
                    "gold spans of sentence " + std::to_string(sentence) + " contain " +
                    std::to_string(internal.size()) + " internal spans but a full binary tree over " +
                    std::to_string(length) + " tokens requires " + std::to_string(length - 1));
            }
            validate_gold_sentence(internal, 0, length, static_cast<SentenceId>(sentence));
        }
        for (const auto& span : internal) {
            const auto found = grouped[sentence].find(span);
            tree.internal_spans.push_back(LabeledSpan{
                span.first, span.second,
                found != grouped[sentence].end() ? found->second : std::string{}});
        }
        std::sort(tree.internal_spans.begin(), tree.internal_spans.end(), span_order);
        trees.push_back(std::move(tree));
    }
    return trees;
}

void write_gold_spans_tsv(std::ostream& output, const std::span<const GoldTree> trees) {
    for (std::size_t sentence = 0; sentence < trees.size(); ++sentence) {
        const auto& tree = trees[sentence];
        if (tree.length == 1) {
            // Length-1 sentences have no internal structure; a single leaf row
            // keeps every sentence id present in the file.
            output << sentence << "\t0\t1\t-\n";
            continue;
        }
        for (const auto& span : tree.internal_spans) {
            output << sentence << '\t' << span.begin << '\t' << span.end << '\t'
                   << (span.label.empty() ? "-" : span.label) << '\n';
        }
    }
}

std::string format_span_pair(const SpanPair& span) {
    return "[" + std::to_string(span.first) + "," + std::to_string(span.second) + ")";
}

std::string format_span_pairs(const std::span<const SpanPair> spans) {
    if (spans.empty()) {
        return "-";
    }
    std::string text;
    for (std::size_t index = 0; index < spans.size(); ++index) {
        if (index != 0) {
            text += ';';
        }
        text += format_span_pair(spans[index]);
    }
    return text;
}

}  // namespace scf
