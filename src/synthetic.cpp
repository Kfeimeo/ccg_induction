#include "scf/synthetic.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace scf {
namespace {

std::vector<std::string> terminal_alternatives(const std::string& prefix, const std::size_t count) {
    std::vector<std::string> result;
    result.reserve(count);
    for (std::size_t index = 1; index <= count; ++index) {
        result.push_back(prefix + std::to_string(index));
    }
    return result;
}

void add_lexical_rules(Grammar& grammar,
                       const std::string& lhs,
                       const std::vector<std::string>& terminals) {
    for (const auto& terminal : terminals) {
        grammar.rules.push_back(Rule{lhs, {terminal}});
    }
}

std::set<std::string> nonterminal_set(const Grammar& grammar) {
    std::set<std::string> nonterminals;
    for (const auto& rule : grammar.rules) {
        nonterminals.insert(rule.lhs);
    }
    return nonterminals;
}

std::vector<GoldNode> expand_symbol(const Grammar& grammar,
                                    const std::set<std::string>& nonterminals,
                                    const std::string& symbol,
                                    std::vector<std::string>& expansion_stack) {
    if (!nonterminals.contains(symbol)) {
        return {GoldNode{symbol, {}}};
    }
    if (std::find(expansion_stack.begin(), expansion_stack.end(), symbol) != expansion_stack.end()) {
        throw std::runtime_error("grammar '" + grammar.name + "' is recursive at symbol '" + symbol +
                                 "': finite full-language expansion is impossible");
    }
    expansion_stack.push_back(symbol);
    std::vector<GoldNode> results;
    for (const auto& rule : grammar.rules) {
        if (rule.lhs != symbol) {
            continue;
        }
        if (rule.rhs.empty()) {
            throw std::runtime_error("grammar '" + grammar.name + "' has an empty rule for '" +
                                     symbol + "'");
        }
        std::vector<std::vector<GoldNode>> child_options;
        child_options.reserve(rule.rhs.size());
        for (const auto& child_symbol : rule.rhs) {
            child_options.push_back(
                expand_symbol(grammar, nonterminals, child_symbol, expansion_stack));
        }
        std::vector<std::size_t> choice(child_options.size(), 0);
        while (true) {
            GoldNode node{symbol, {}};
            for (std::size_t position = 0; position < child_options.size(); ++position) {
                node.children.push_back(child_options[position][choice[position]]);
            }
            results.push_back(std::move(node));
            bool advanced = false;
            for (std::size_t position = child_options.size(); position-- > 0;) {
                if (++choice[position] < child_options[position].size()) {
                    advanced = true;
                    break;
                }
                choice[position] = 0;
            }
            if (!advanced) {
                break;
            }
        }
    }
    expansion_stack.pop_back();
    if (results.empty()) {
        throw std::runtime_error("grammar '" + grammar.name + "' has no rule for nonterminal '" +
                                 symbol + "'");
    }
    return results;
}

std::string sentence_key(const std::vector<std::string>& tokens) {
    std::string key;
    for (const auto& token : tokens) {
        key += token;
        key += ' ';
    }
    return key;
}

std::vector<GoldSentence> finalize_language(const std::string& grammar_name,
                                            std::vector<GoldSentence> raw) {
    std::vector<GoldSentence> language;
    std::map<std::string, std::set<SpanPair>> seen_shapes;
    for (auto& sentence : raw) {
        sentence.tree = collapse_unary_chains(std::move(sentence.tree));
        const auto tree = gold_tree_from_node(sentence.tree);  // validates binary structure
        std::set<SpanPair> shape;
        for (const auto& span : tree.internal_spans) {
            shape.emplace(span.begin, span.end);
        }
        const auto key = sentence_key(sentence.tokens);
        const auto found = seen_shapes.find(key);
        if (found != seen_shapes.end()) {
            if (found->second != shape) {
                throw std::runtime_error("grammar '" + grammar_name +
                                         "' derives one sentence with two tree shapes: '" + key +
                                         "' has no single gold tree");
            }
            continue;
        }
        seen_shapes.emplace(key, std::move(shape));
        language.push_back(std::move(sentence));
    }
    return language;
}

// --- CCG-lite -----------------------------------------------------------

struct CategorySplit {
    std::string result;
    char direction{};
    std::string argument;
};

std::string strip_outer_parens(std::string category) {
    while (category.size() >= 2 && category.front() == '(' && category.back() == ')') {
        int depth = 0;
        bool wraps = true;
        for (std::size_t index = 0; index + 1 < category.size(); ++index) {
            if (category[index] == '(') {
                ++depth;
            } else if (category[index] == ')') {
                --depth;
            }
            if (depth == 0) {
                wraps = false;
                break;
            }
        }
        if (!wraps) {
            break;
        }
        category = category.substr(1, category.size() - 2);
    }
    return category;
}

std::optional<CategorySplit> split_category(const std::string& raw) {
    const auto category = strip_outer_parens(raw);
    int depth = 0;
    std::size_t position = std::string::npos;
    char direction = 0;
    for (std::size_t index = 0; index < category.size(); ++index) {
        const char ch = category[index];
        if (ch == '(') {
            ++depth;
        } else if (ch == ')') {
            --depth;
        } else if (depth == 0 && (ch == '/' || ch == '\\')) {
            position = index;  // rightmost top-level slash: slashes are left-associative
            direction = ch;
        }
    }
    if (position == std::string::npos) {
        return std::nullopt;
    }
    return CategorySplit{strip_outer_parens(category.substr(0, position)), direction,
                         strip_outer_parens(category.substr(position + 1))};
}

struct CcgItem {
    std::string category;
    std::vector<std::string> tokens;
    GoldNode tree;
};

constexpr std::size_t kCcgLiteMaxLength = 10;

const std::vector<std::pair<std::string, std::string>>& ccg_lite_lexicon() {
    static const std::vector<std::pair<std::string, std::string>> lexicon{
        {"the", "NP/N"},   {"a", "NP/N"},        {"dog", "N"},
        {"cat", "N"},      {"john", "NP"},       {"mary", "NP"},
        {"runs", "S\\NP"}, {"sleeps", "S\\NP"},  {"likes", "(S\\NP)/NP"},
        {"sees", "(S\\NP)/NP"},
    };
    return lexicon;
}

std::optional<CcgItem> apply_pair(const CcgItem& left, const CcgItem& right) {
    // Forward application: X/Y Y -> X. Backward application: Y X\Y -> X.
    std::string result;
    if (const auto forward = split_category(left.category);
        forward && forward->direction == '/' &&
        forward->argument == strip_outer_parens(right.category)) {
        result = forward->result;
    } else if (const auto backward = split_category(right.category);
               backward && backward->direction == '\\' &&
               backward->argument == strip_outer_parens(left.category)) {
        result = backward->result;
    } else {
        return std::nullopt;
    }
    CcgItem item;
    item.category = result;
    item.tokens = left.tokens;
    item.tokens.insert(item.tokens.end(), right.tokens.begin(), right.tokens.end());
    item.tree = GoldNode{result, {left.tree, right.tree}};
    return item;
}

}  // namespace

std::vector<std::string> known_grammar_names() {
    return {"ab_cartesian",   "simple_np_vp",  "symmetric_abc",     "nested_balanced",
            "right_branching", "left_branching", "ambiguous_lexicon", "ccg_lite"};
}

Grammar make_grammar(const std::string& name) {
    Grammar grammar;
    grammar.name = name;
    grammar.start_symbol = "S";
    if (name == "ab_cartesian") {
        grammar.rules.push_back(Rule{"S", {"A", "B"}});
        add_lexical_rules(grammar, "A", terminal_alternatives("a", 3));
        add_lexical_rules(grammar, "B", terminal_alternatives("b", 3));
    } else if (name == "simple_np_vp") {
        // A is deliberately paired (the dog | a cat) so full coverage matches
        // the v1.1 simple.txt regression corpus exactly.
        grammar.rules.push_back(Rule{"S", {"A", "B"}});
        grammar.rules.push_back(Rule{"A", {"Det1", "N1"}});
        grammar.rules.push_back(Rule{"A", {"Det2", "N2"}});
        grammar.rules.push_back(Rule{"B", {"V"}});
        grammar.rules.push_back(Rule{"Det1", {"the"}});
        grammar.rules.push_back(Rule{"N1", {"dog"}});
        grammar.rules.push_back(Rule{"Det2", {"a"}});
        grammar.rules.push_back(Rule{"N2", {"cat"}});
        grammar.rules.push_back(Rule{"V", {"runs"}});
        grammar.rules.push_back(Rule{"V", {"sleeps"}});
    } else if (name == "symmetric_abc") {
        // Fully symmetric A x B x C with gold fixed to ((A B) C). The corpus
        // itself never breaks the symmetry, so both binary trees stay optimal.
        grammar.rules.push_back(Rule{"S", {"X", "C"}});
        grammar.rules.push_back(Rule{"X", {"A", "B"}});
        add_lexical_rules(grammar, "A", terminal_alternatives("a", 2));
        add_lexical_rules(grammar, "B", terminal_alternatives("b", 2));
        add_lexical_rules(grammar, "C", terminal_alternatives("c", 2));
    } else if (name == "nested_balanced") {
        grammar.rules.push_back(Rule{"S", {"A", "B"}});
        grammar.rules.push_back(Rule{"A", {"C", "D"}});
        grammar.rules.push_back(Rule{"B", {"E", "F"}});
        add_lexical_rules(grammar, "C", terminal_alternatives("c", 2));
        add_lexical_rules(grammar, "D", terminal_alternatives("d", 2));
        add_lexical_rules(grammar, "E", terminal_alternatives("e", 2));
        add_lexical_rules(grammar, "F", terminal_alternatives("f", 2));
    } else if (name == "right_branching") {
        grammar.rules.push_back(Rule{"S", {"A", "X"}});
        grammar.rules.push_back(Rule{"X", {"B", "Y"}});
        grammar.rules.push_back(Rule{"Y", {"C", "D"}});
        add_lexical_rules(grammar, "A", terminal_alternatives("a", 2));
        add_lexical_rules(grammar, "B", terminal_alternatives("b", 2));
        add_lexical_rules(grammar, "C", terminal_alternatives("c", 2));
        add_lexical_rules(grammar, "D", terminal_alternatives("d", 2));
    } else if (name == "left_branching") {
        grammar.rules.push_back(Rule{"S", {"X", "D"}});
        grammar.rules.push_back(Rule{"X", {"Y", "C"}});
        grammar.rules.push_back(Rule{"Y", {"A", "B"}});
        add_lexical_rules(grammar, "A", terminal_alternatives("a", 2));
        add_lexical_rules(grammar, "B", terminal_alternatives("b", 2));
        add_lexical_rules(grammar, "C", terminal_alternatives("c", 2));
        add_lexical_rules(grammar, "D", terminal_alternatives("d", 2));
    } else if (name == "ambiguous_lexicon") {
        // Surface token "x" belongs to both latent classes A (position 0) and
        // B (position 3). Strict global equivalence is expected to over-merge
        // here; the acceptance criterion is that diagnostics expose it.
        grammar.rules.push_back(Rule{"S", {"X", "Y"}});
        grammar.rules.push_back(Rule{"X", {"A", "M"}});
        grammar.rules.push_back(Rule{"Y", {"N", "B"}});
        grammar.rules.push_back(Rule{"A", {"x"}});
        add_lexical_rules(grammar, "A", terminal_alternatives("a", 2));
        add_lexical_rules(grammar, "M", terminal_alternatives("m", 2));
        add_lexical_rules(grammar, "N", terminal_alternatives("n", 2));
        grammar.rules.push_back(Rule{"B", {"x"}});
        add_lexical_rules(grammar, "B", terminal_alternatives("b", 2));
    } else if (name == "ccg_lite") {
        return ccg_lite_lexicon_grammar();
    } else {
        throw std::runtime_error("unknown grammar '" + name + "'");
    }
    return grammar;
}

std::vector<GoldSentence> generate_full_language(const Grammar& grammar) {
    const auto nonterminals = nonterminal_set(grammar);
    std::vector<std::string> expansion_stack;
    auto derivations = expand_symbol(grammar, nonterminals, grammar.start_symbol, expansion_stack);
    std::vector<GoldSentence> raw;
    raw.reserve(derivations.size());
    for (auto& derivation : derivations) {
        GoldSentence sentence;
        sentence.tokens = leaf_tokens(derivation);
        sentence.tree = std::move(derivation);
        raw.push_back(std::move(sentence));
    }
    return finalize_language(grammar.name, std::move(raw));
}

Grammar ccg_lite_lexicon_grammar() {
    Grammar grammar;
    grammar.name = "ccg_lite";
    grammar.start_symbol = "S";
    for (const auto& [word, category] : ccg_lite_lexicon()) {
        grammar.rules.push_back(Rule{category, {word}});
    }
    return grammar;
}

std::vector<GoldSentence> generate_ccg_lite_language() {
    std::vector<CcgItem> items;
    std::set<std::pair<std::string, std::string>> seen;
    for (const auto& [word, category] : ccg_lite_lexicon()) {
        CcgItem item{category, {word}, GoldNode{word, {}}};
        if (seen.emplace(category, word).second) {
            items.push_back(std::move(item));
        }
    }
    // Application-only closure over a finite lexicon terminates because every
    // combination consumes one slash and sentence length is capped.
    std::size_t previous_size = 0;
    while (previous_size != items.size()) {
        const auto snapshot = items.size();
        for (std::size_t left = 0; left < snapshot; ++left) {
            for (std::size_t right = 0; right < snapshot; ++right) {
                if (items[left].tokens.size() + items[right].tokens.size() > kCcgLiteMaxLength) {
                    continue;
                }
                auto combined = apply_pair(items[left], items[right]);
                if (!combined) {
                    continue;
                }
                std::string token_key = sentence_key(combined->tokens);
                if (seen.emplace(combined->category, std::move(token_key)).second) {
                    items.push_back(std::move(*combined));
                }
            }
        }
        previous_size = snapshot;
    }
    std::vector<GoldSentence> raw;
    for (auto& item : items) {
        if (item.category != "S") {
            continue;
        }
        raw.push_back(GoldSentence{std::move(item.tokens), std::move(item.tree)});
    }
    return finalize_language("ccg_lite", std::move(raw));
}

void deterministic_shuffle(std::vector<std::size_t>& values, const std::uint64_t seed) {
    std::mt19937_64 engine(seed);
    const auto bounded = [&engine](const std::uint64_t bound) {
        // Unbiased rejection sampling; mt19937_64 output is fully specified by
        // the standard, so results are identical across platforms.
        std::uint64_t value = 0;
        std::uint64_t remainder = 0;
        do {
            value = engine();
            remainder = value % bound;
        } while (value - remainder > std::numeric_limits<std::uint64_t>::max() - (bound - 1));
        return remainder;
    };
    for (std::size_t index = values.size(); index > 1; --index) {
        const auto other = static_cast<std::size_t>(bounded(static_cast<std::uint64_t>(index)));
        std::swap(values[index - 1], values[other]);
    }
}

SyntheticDataset generate_dataset(const std::string& grammar_name,
                                  const double coverage,
                                  const std::uint64_t seed,
                                  const std::size_t max_sentences) {
    if (!(coverage > 0.0) || coverage > 1.0) {
        throw std::runtime_error("coverage must be in (0, 1]");
    }
    SyntheticDataset dataset;
    dataset.grammar_name = grammar_name;
    dataset.grammar = make_grammar(grammar_name);
    dataset.seed = seed;
    dataset.coverage = coverage;
    dataset.max_sentences = max_sentences;

    auto language = grammar_name == "ccg_lite" ? generate_ccg_lite_language()
                                               : generate_full_language(dataset.grammar);
    dataset.full_sentence_count = language.size();

    std::vector<std::size_t> order(language.size());
    for (std::size_t index = 0; index < order.size(); ++index) {
        order[index] = index;
    }
    deterministic_shuffle(order, seed);

    // ceil(coverage * N) with a tiny guard against binary rounding artifacts
    // such as 0.2 * 5 evaluating to 1.0000000000000002.
    auto take = static_cast<std::size_t>(
        std::ceil(coverage * static_cast<double>(language.size()) - 1e-9));
    take = std::min(std::max<std::size_t>(take, language.empty() ? 0 : 1), language.size());
    order.resize(take);
    if (max_sentences != 0 && order.size() > max_sentences) {
        order.resize(max_sentences);
    }
    std::sort(order.begin(), order.end());

    dataset.sentences.reserve(order.size());
    for (const auto index : order) {
        dataset.sentences.push_back(std::move(language[index]));
    }
    return dataset;
}

std::vector<GoldTree> dataset_gold_trees(const SyntheticDataset& dataset) {
    std::vector<GoldTree> trees;
    trees.reserve(dataset.sentences.size());
    for (const auto& sentence : dataset.sentences) {
        trees.push_back(gold_tree_from_node(sentence.tree));
    }
    return trees;
}

namespace {

std::string json_escape(const std::string& value) {
    std::string escaped;
    for (const char ch : value) {
        if (ch == '"' || ch == '\\') {
            escaped += '\\';
        }
        escaped += ch;
    }
    return escaped;
}

std::string format_coverage(const double coverage) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.6f", coverage);
    return buffer;
}

}  // namespace

std::string grammar_json(const SyntheticDataset& dataset) {
    std::vector<std::string> nonterminals;
    std::vector<std::string> terminals;
    std::set<std::string> lhs_set;
    for (const auto& rule : dataset.grammar.rules) {
        lhs_set.insert(rule.lhs);
    }
    std::set<std::string> emitted;
    for (const auto& rule : dataset.grammar.rules) {
        if (emitted.insert(rule.lhs).second) {
            nonterminals.push_back(rule.lhs);
        }
    }
    std::set<std::string> emitted_terminals;
    for (const auto& rule : dataset.grammar.rules) {
        for (const auto& symbol : rule.rhs) {
            if (!lhs_set.contains(symbol) && emitted_terminals.insert(symbol).second) {
                terminals.push_back(symbol);
            }
        }
    }

    std::ostringstream json;
    json << "{\n";
    json << "  \"grammar_name\": \"" << json_escape(dataset.grammar_name) << "\",\n";
    json << "  \"start_symbol\": \"" << json_escape(dataset.grammar.start_symbol) << "\",\n";
    json << "  \"seed\": " << dataset.seed << ",\n";
    json << "  \"coverage\": " << format_coverage(dataset.coverage) << ",\n";
    json << "  \"max_sentences\": " << dataset.max_sentences << ",\n";
    json << "  \"full_sentence_count\": " << dataset.full_sentence_count << ",\n";
    json << "  \"sampled_sentence_count\": " << dataset.sentences.size() << ",\n";
    json << "  \"deduplicated\": true,\n";
    json << "  \"nonterminals\": [";
    for (std::size_t index = 0; index < nonterminals.size(); ++index) {
        json << (index == 0 ? "" : ", ") << '"' << json_escape(nonterminals[index]) << '"';
    }
    json << "],\n";
    json << "  \"terminals\": [";
    for (std::size_t index = 0; index < terminals.size(); ++index) {
        json << (index == 0 ? "" : ", ") << '"' << json_escape(terminals[index]) << '"';
    }
    json << "],\n";
    json << "  \"rules\": [\n";
    for (std::size_t index = 0; index < dataset.grammar.rules.size(); ++index) {
        const auto& rule = dataset.grammar.rules[index];
        json << "    {\"lhs\": \"" << json_escape(rule.lhs) << "\", \"rhs\": [";
        for (std::size_t position = 0; position < rule.rhs.size(); ++position) {
            json << (position == 0 ? "" : ", ") << '"' << json_escape(rule.rhs[position]) << '"';
        }
        json << "]}" << (index + 1 == dataset.grammar.rules.size() ? "" : ",") << '\n';
    }
    json << "  ]\n";
    json << "}\n";
    return json.str();
}

void write_dataset(const SyntheticDataset& dataset, const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    const auto open = [&](const char* name) {
        std::ofstream output(directory / name);
        if (!output) {
            throw std::runtime_error("cannot write " + (directory / name).string());
        }
        return output;
    };
    {
        auto output = open("corpus.txt");
        for (const auto& sentence : dataset.sentences) {
            for (std::size_t index = 0; index < sentence.tokens.size(); ++index) {
                if (index != 0) {
                    output << ' ';
                }
                output << sentence.tokens[index];
            }
            output << '\n';
        }
    }
    const auto trees = dataset_gold_trees(dataset);
    {
        auto output = open("gold_spans.tsv");
        write_gold_spans_tsv(output, trees);
    }
    {
        auto output = open("gold_brackets.txt");
        for (const auto& sentence : dataset.sentences) {
            output << format_gold_bracket(sentence.tree) << '\n';
        }
    }
    {
        auto output = open("grammar.json");
        output << grammar_json(dataset);
    }
}

}  // namespace scf
