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
    return {"ab_cartesian",
            "simple_np_vp",
            "symmetric_abc",
            "nested_balanced",
            "right_branching",
            "left_branching",
            "ambiguous_lexicon",
            "hierarchical_correlated_balanced",
            "hierarchical_correlated_right",
            "hierarchical_correlated_left",
            "ambiguous_surface_roles",
            "recursive_context_cascade",
            "ccg_lite"};
}

std::size_t resolve_lexical_cardinality(const std::string& name, const std::size_t k) {
    const bool hierarchical = name.rfind("hierarchical_correlated_", 0) == 0;
    const std::size_t family_default = name == "ab_cartesian" ? 3 : hierarchical ? 3 : 2;
    const auto resolved = k == 0 ? family_default : k;
    if (resolved < 2 || resolved > 5) {
        throw std::runtime_error("lexical cardinality must be in [2, 5]");
    }
    if (name == "ccg_lite" && resolved != 2) {
        throw std::runtime_error("ccg_lite has a fixed lexicon; --lexical-cardinality is unsupported");
    }
    if (name == "recursive_context_cascade" && resolved != 2) {
        throw std::runtime_error(
            "recursive_context_cascade is a fixed minimal corpus; --lexical-cardinality is "
            "unsupported");
    }
    if (name == "simple_np_vp" && resolved > 5) {
        throw std::runtime_error("simple_np_vp supports lexical cardinality up to 5");
    }
    return resolved;
}

Grammar make_grammar(const std::string& name, const std::size_t lexical_cardinality) {
    const auto k = resolve_lexical_cardinality(name, lexical_cardinality);
    Grammar grammar;
    grammar.name = name;
    grammar.start_symbol = "S";
    if (name == "ab_cartesian") {
        grammar.rules.push_back(Rule{"S", {"A", "B"}});
        add_lexical_rules(grammar, "A", terminal_alternatives("a", k));
        add_lexical_rules(grammar, "B", terminal_alternatives("b", k));
    } else if (name == "simple_np_vp") {
        // The subject pairs are deliberately bound (the dog | a cat | ...) so
        // that K=2 full coverage matches the v1.1 simple.txt corpus exactly.
        static const std::vector<std::pair<std::string, std::string>> subjects{
            {"the", "dog"}, {"a", "cat"}, {"every", "fox"}, {"some", "bird"}, {"each", "fish"}};
        static const std::vector<std::string> verbs{"runs", "sleeps", "eats", "jumps", "swims"};
        grammar.rules.push_back(Rule{"S", {"A", "B"}});
        for (std::size_t index = 0; index < k; ++index) {
            const auto tag = std::to_string(index + 1);
            grammar.rules.push_back(Rule{"A", {"Det" + tag, "N" + tag}});
        }
        grammar.rules.push_back(Rule{"B", {"V"}});
        for (std::size_t index = 0; index < k; ++index) {
            const auto tag = std::to_string(index + 1);
            grammar.rules.push_back(Rule{"Det" + tag, {subjects[index].first}});
            grammar.rules.push_back(Rule{"N" + tag, {subjects[index].second}});
        }
        for (std::size_t index = 0; index < k; ++index) {
            grammar.rules.push_back(Rule{"V", {verbs[index]}});
        }
    } else if (name == "symmetric_abc") {
        // Fully symmetric A x B x C with gold fixed to ((A B) C). The corpus
        // itself never breaks the symmetry, so both binary trees stay optimal.
        grammar.rules.push_back(Rule{"S", {"X", "C"}});
        grammar.rules.push_back(Rule{"X", {"A", "B"}});
        add_lexical_rules(grammar, "A", terminal_alternatives("a", k));
        add_lexical_rules(grammar, "B", terminal_alternatives("b", k));
        add_lexical_rules(grammar, "C", terminal_alternatives("c", k));
    } else if (name == "nested_balanced") {
        grammar.rules.push_back(Rule{"S", {"A", "B"}});
        grammar.rules.push_back(Rule{"A", {"C", "D"}});
        grammar.rules.push_back(Rule{"B", {"E", "F"}});
        add_lexical_rules(grammar, "C", terminal_alternatives("c", k));
        add_lexical_rules(grammar, "D", terminal_alternatives("d", k));
        add_lexical_rules(grammar, "E", terminal_alternatives("e", k));
        add_lexical_rules(grammar, "F", terminal_alternatives("f", k));
    } else if (name == "right_branching") {
        grammar.rules.push_back(Rule{"S", {"A", "X"}});
        grammar.rules.push_back(Rule{"X", {"B", "Y"}});
        grammar.rules.push_back(Rule{"Y", {"C", "D"}});
        add_lexical_rules(grammar, "A", terminal_alternatives("a", k));
        add_lexical_rules(grammar, "B", terminal_alternatives("b", k));
        add_lexical_rules(grammar, "C", terminal_alternatives("c", k));
        add_lexical_rules(grammar, "D", terminal_alternatives("d", k));
    } else if (name == "left_branching") {
        grammar.rules.push_back(Rule{"S", {"X", "D"}});
        grammar.rules.push_back(Rule{"X", {"Y", "C"}});
        grammar.rules.push_back(Rule{"Y", {"A", "B"}});
        add_lexical_rules(grammar, "A", terminal_alternatives("a", k));
        add_lexical_rules(grammar, "B", terminal_alternatives("b", k));
        add_lexical_rules(grammar, "C", terminal_alternatives("c", k));
        add_lexical_rules(grammar, "D", terminal_alternatives("d", k));
    } else if (name == "ambiguous_lexicon") {
        // Surface token "x" belongs to both latent classes A (position 0) and
        // B (position 3). Strict global equivalence is expected to over-merge
        // here; the acceptance criterion is that diagnostics expose it.
        grammar.rules.push_back(Rule{"S", {"X", "Y"}});
        grammar.rules.push_back(Rule{"X", {"A", "M"}});
        grammar.rules.push_back(Rule{"Y", {"N", "B"}});
        grammar.rules.push_back(Rule{"A", {"x"}});
        add_lexical_rules(grammar, "A", terminal_alternatives("a", k));
        add_lexical_rules(grammar, "M", terminal_alternatives("m", k));
        add_lexical_rules(grammar, "N", terminal_alternatives("n", k));
        grammar.rules.push_back(Rule{"B", {"x"}});
        add_lexical_rules(grammar, "B", terminal_alternatives("b", k));
    } else if (name == "hierarchical_correlated_balanced") {
        // Correlated blocks: A = {a_i b_i}, B = {c_j d_j}, S = A x B. The
        // block-internal correlation makes the surface language K^2 sentences
        // (not K^4), so the balanced bracket is genuinely observable.
        grammar.rules.push_back(Rule{"S", {"A", "B"}});
        for (std::size_t index = 1; index <= k; ++index) {
            const auto tag = std::to_string(index);
            grammar.rules.push_back(Rule{"A", {"P" + tag}});
            grammar.rules.push_back(Rule{"P" + tag, {"a" + tag, "b" + tag}});
        }
        for (std::size_t index = 1; index <= k; ++index) {
            const auto tag = std::to_string(index);
            grammar.rules.push_back(Rule{"B", {"Q" + tag}});
            grammar.rules.push_back(Rule{"Q" + tag, {"c" + tag, "d" + tag}});
        }
    } else if (name == "hierarchical_correlated_right") {
        // Correlated right nest: S = A x {b_j (c_j d_j)}. Positions 1-3 are
        // correlated while position 0 varies freely, so the surface language
        // differs from both the balanced and the left variant.
        grammar.rules.push_back(Rule{"S", {"A", "X"}});
        add_lexical_rules(grammar, "A", terminal_alternatives("a", k));
        for (std::size_t index = 1; index <= k; ++index) {
            const auto tag = std::to_string(index);
            grammar.rules.push_back(Rule{"X", {"X" + tag}});
            grammar.rules.push_back(Rule{"X" + tag, {"b" + tag, "Y" + tag}});
            grammar.rules.push_back(Rule{"Y" + tag, {"c" + tag, "d" + tag}});
        }
    } else if (name == "hierarchical_correlated_left") {
        // Correlated left nest: S = {((a_j b_j) c_j)} x D. Positions 0-2 are
        // correlated while position 3 varies freely.
        grammar.rules.push_back(Rule{"S", {"X", "D"}});
        for (std::size_t index = 1; index <= k; ++index) {
            const auto tag = std::to_string(index);
            grammar.rules.push_back(Rule{"X", {"X" + tag}});
            grammar.rules.push_back(Rule{"X" + tag, {"Y" + tag, "c" + tag}});
            grammar.rules.push_back(Rule{"Y" + tag, {"a" + tag, "b" + tag}});
        }
        add_lexical_rules(grammar, "D", terminal_alternatives("d", k));
    } else if (name == "ambiguous_surface_roles") {
        // v1.4: surface token "x" fills both the N and the V role. Under
        // context-indexed equivalence it must join both local role blocks
        // without any lexical split and without bridging N-class and V-class
        // tokens into one global class.
        grammar.rules.push_back(Rule{"S", {"D", "X"}});
        grammar.rules.push_back(Rule{"X", {"N", "V"}});
        add_lexical_rules(grammar, "D", terminal_alternatives("d", k));
        add_lexical_rules(grammar, "N", terminal_alternatives("n", k));
        grammar.rules.push_back(Rule{"N", {"x"}});
        add_lexical_rules(grammar, "V", terminal_alternatives("v", k));
        grammar.rules.push_back(Rule{"V", {"x"}});
    } else if (name == "recursive_context_cascade") {
        // v1.4: fixed two-sentence corpus "w a m" / "w b m". Under the
        // context_plus_concat signature the contraction cascades over three
        // genuine rounds (a~b, then "a m"~"b m" and "w a"~"w b", then the
        // full sentences); under context_only the exact-profile operator
        // saturates in one round (see IMPLEMENTATION_NOTES on idempotence).
        grammar.rules.push_back(Rule{"S", {"W", "X"}});
        grammar.rules.push_back(Rule{"X", {"A", "M"}});
        grammar.rules.push_back(Rule{"W", {"w"}});
        grammar.rules.push_back(Rule{"A", {"a"}});
        grammar.rules.push_back(Rule{"A", {"b"}});
        grammar.rules.push_back(Rule{"M", {"m"}});
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
        raw.push_back(GoldSentence{std::move(item.tokens), std::move(item.tree), {}});
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

std::vector<GoldSentence> generate_family_language(const std::string& grammar_name,
                                                   const std::size_t lexical_cardinality,
                                                   const double symmetry_breaking_rate) {
    if (symmetry_breaking_rate < 0.0 || symmetry_breaking_rate > 1.0) {
        throw std::runtime_error("symmetry-breaking rate must be in [0, 1]");
    }
    if (symmetry_breaking_rate > 0.0 && grammar_name != "symmetric_abc") {
        throw std::runtime_error("--symmetry-breaking-rate applies only to symmetric_abc");
    }
    const auto k = resolve_lexical_cardinality(grammar_name, lexical_cardinality);
    auto language = grammar_name == "ccg_lite"
                        ? generate_ccg_lite_language()
                        : generate_full_language(make_grammar(grammar_name, k));
    // Observable gold: the correlated chains' inner blocks are frozen (their
    // internal bracket has no observable evidence), so only the block-level
    // split is demanded of the parser.
    if (grammar_name == "hierarchical_correlated_right") {
        for (auto& sentence : language) {
            sentence.observable_spans = {{1, 4}};
        }
    } else if (grammar_name == "hierarchical_correlated_left") {
        for (auto& sentence : language) {
            sentence.observable_spans = {{0, 3}};
        }
    }
    if (symmetry_breaking_rate > 0.0) {
        // Marker sentences "a_i b_j p" give the AB block an additional shared
        // block-level context (epsilon, p). Taken in canonical (i, j) order,
        // ceil(rho * K^2) of them are appended, gradually making the gold
        // ((A B) C) bracket observable.
        const auto marker_total = k * k;
        auto marker_count = static_cast<std::size_t>(
            std::ceil(symmetry_breaking_rate * static_cast<double>(marker_total) - 1e-9));
        marker_count = std::min(marker_count, marker_total);
        std::size_t emitted = 0;
        for (std::size_t i = 1; i <= k && emitted < marker_count; ++i) {
            for (std::size_t j = 1; j <= k && emitted < marker_count; ++j) {
                const auto a = "a" + std::to_string(i);
                const auto b = "b" + std::to_string(j);
                GoldNode block{"X", {GoldNode{a, {}}, GoldNode{b, {}}}};
                GoldNode root{"S", {std::move(block), GoldNode{"p", {}}}};
                language.push_back(GoldSentence{{a, b, "p"}, std::move(root), {}});
                ++emitted;
            }
        }
    }
    return language;
}

SyntheticDataset generate_dataset(const std::string& grammar_name,
                                  const double coverage,
                                  const std::uint64_t seed,
                                  const std::size_t max_sentences,
                                  const std::size_t lexical_cardinality,
                                  const double symmetry_breaking_rate) {
    if (!(coverage > 0.0) || coverage > 1.0) {
        throw std::runtime_error("coverage must be in (0, 1]");
    }
    SyntheticDataset dataset;
    dataset.grammar_name = grammar_name;
    dataset.lexical_cardinality = resolve_lexical_cardinality(grammar_name, lexical_cardinality);
    dataset.symmetry_breaking_rate = symmetry_breaking_rate;
    dataset.grammar = make_grammar(grammar_name, dataset.lexical_cardinality);
    dataset.seed = seed;
    dataset.coverage = coverage;
    dataset.max_sentences = max_sentences;

    auto language = generate_family_language(grammar_name, dataset.lexical_cardinality,
                                             symmetry_breaking_rate);
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

std::vector<std::set<SpanPair>> dataset_observable_gold(const SyntheticDataset& dataset) {
    std::vector<std::set<SpanPair>> observable;
    observable.reserve(dataset.sentences.size());
    for (const auto& sentence : dataset.sentences) {
        if (sentence.observable_spans.empty()) {
            observable.push_back(gold_scoring_spans(gold_tree_from_node(sentence.tree)));
        } else {
            observable.emplace_back(sentence.observable_spans.begin(),
                                    sentence.observable_spans.end());
        }
    }
    return observable;
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
    json << "  \"lexical_cardinality\": " << dataset.lexical_cardinality << ",\n";
    json << "  \"symmetry_breaking_rate\": " << format_coverage(dataset.symmetry_breaking_rate)
         << ",\n";
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
    {
        auto output = open("gold_observable_spans.tsv");
        const auto observable = dataset_observable_gold(dataset);
        for (std::size_t sentence = 0; sentence < observable.size(); ++sentence) {
            if (observable[sentence].empty()) {
                output << sentence << "\t0\t0\t-\n";  // no observable proper span demanded
                continue;
            }
            for (const auto& span : observable[sentence]) {
                output << sentence << '\t' << span.first << '\t' << span.second << "\t-\n";
            }
        }
    }
}

}  // namespace scf
