#include "scf/audit.hpp"
#include "scf/context_indexed.hpp"
#include "scf/corpus.hpp"
#include "scf/dsu.hpp"
#include "scf/enumerator.hpp"
#include "scf/equivalence_solver.hpp"
#include "scf/evaluator.hpp"
#include "scf/evidence.hpp"
#include "scf/evidence_builder.hpp"
#include "scf/formatter.hpp"
#include "scf/gold.hpp"
#include "scf/pipeline.hpp"
#include "scf/prepare_text.hpp"
#include "scf/string_interner.hpp"
#include "scf/synthetic.hpp"
#include "scf/tree_solver.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

#define CHECK(condition)                                                                            \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            throw TestFailure(std::string("CHECK failed: ") + #condition + " at " + __FILE__ + ":" + \
                              std::to_string(__LINE__));                                             \
        }                                                                                           \
    } while (false)

scf::Corpus load_corpus(const std::string_view text, scf::CorpusConfig config = {}) {
    std::istringstream input{std::string(text)};
    scf::Corpus corpus(std::move(config));
    corpus.load(input);
    return corpus;
}

scf::StringId string_id(const scf::Corpus& corpus, const std::string_view text) {
    for (scf::StringId id = 0; id < corpus.string_interner().size(); ++id) {
        if (corpus.string_interner().to_string(id, corpus.token_interner()) == text) {
            return id;
        }
    }
    throw TestFailure("missing string: " + std::string(text));
}

scf::EquivalenceSolver solve(const scf::Corpus& corpus) {
    scf::EquivalenceSolver solver(corpus.string_interner().size(),
                                  corpus.context_records(),
                                  corpus.concat_triples());
    solver.saturate();
    return solver;
}

scf::Corpus load_synthetic(const std::string& name) {
    scf::Corpus corpus;
    corpus.load_file((std::filesystem::path(SCF_SOURCE_DIR) / "data" / "synthetic" / name).string());
    return corpus;
}

const scf::SpanEvidence& find_evidence(const std::span<const scf::SpanEvidence> evidence,
                                       const scf::Span span) {
    const auto found = std::find_if(evidence.begin(), evidence.end(),
                                    [&](const auto& item) { return item.span == span; });
    if (found == evidence.end()) {
        throw TestFailure("missing evidence span");
    }
    return *found;
}

scf::TreeSolveResult analyze_sentence(const scf::Corpus& corpus,
                                      const scf::EvidenceBuilder& builder,
                                      const scf::SentenceId sentence) {
    std::vector<scf::SpanScore> scores;
    for (const auto& item : builder.span_evidence()) {
        if (item.span.sentence == sentence) {
            scores.push_back({item.span, item.score});
        }
    }
    return scf::solve_maximum_evidence_trees(
        sentence, static_cast<std::uint16_t>(corpus.sentences().at(sentence).size()), scores);
}

void test_interner() {
    scf::TokenInterner tokens;
    const auto the = tokens.intern_token("the");
    const auto dog = tokens.intern_token("dog");
    CHECK(tokens.intern_token("the") == the);
    CHECK(the != dog);
    CHECK(tokens.token_text(dog) == "dog");

    scf::StringInterner strings;
    CHECK(strings.epsilon_id() == 0);
    CHECK(strings.tokens(0).empty());
    const std::vector<scf::TokenId> sequence{the, dog};
    const auto first = strings.intern(sequence);
    CHECK(first != strings.epsilon_id());
    CHECK(strings.intern(sequence) == first);
    const std::vector<scf::TokenId> other{dog, the};
    CHECK(strings.intern(other) != first);
    CHECK(strings.to_string(first, tokens) == "the dog");
}

void test_dsu() {
    scf::DisjointSet dsu(5);
    CHECK(dsu.class_count() == 5);
    CHECK(dsu.unite(0, 1));
    CHECK(!dsu.unite(1, 0));
    CHECK(dsu.unite(1, 2));
    CHECK(dsu.class_count() == 3);
    CHECK(dsu.class_size(0) == 3);
    CHECK(dsu.largest_class_size() == 3);
    CHECK(dsu.members(2) == std::vector<scf::StringId>({0, 1, 2}));
}

void test_corpus_records_and_deduplication() {
    auto corpus = load_corpus("  The dog runs  \n\nThe dog runs\nA cat runs\n",
                              scf::CorpusConfig{10, true, true, false, "strict_global"});
    CHECK(corpus.summary().input_sentences == 3);
    CHECK(corpus.summary().duplicate_sentences == 1);
    CHECK(corpus.summary().ignored_empty_lines == 1);
    CHECK(corpus.sentences().size() == 2);
    CHECK(corpus.occurrences().size() == 12);
    for (const auto& record : corpus.context_records()) {
        CHECK(!(record.triple.left == corpus.string_interner().epsilon_id() &&
                record.triple.right == corpus.string_interner().epsilon_id()));
    }
    const auto before = corpus.string_interner().size();
    const auto solver = solve(corpus);
    CHECK(corpus.string_interner().size() == before);
    CHECK(!solver.statistics().empty());
}

void test_basic_context_substitution() {
    const auto corpus = load_corpus("the dog runs\na cat runs\n");
    const auto solver = solve(corpus);
    CHECK(solver.equivalent(string_id(corpus, "the dog"), string_id(corpus, "a cat")));
    CHECK(!solver.equivalent(string_id(corpus, "the"), string_id(corpus, "a")));
    CHECK(!solver.equivalent(string_id(corpus, "dog"), string_id(corpus, "cat")));
    CHECK(!solver.proof_chain(string_id(corpus, "the dog"), string_id(corpus, "a cat")).empty());
}

void test_raw_witnesses_and_simple_regression() {
    const auto corpus = load_synthetic("simple.txt");
    const auto solver = solve(corpus);
    const auto the_dog = string_id(corpus, "the dog");
    const auto a_cat = string_id(corpus, "a cat");
    const auto dog_runs = string_id(corpus, "dog runs");
    const auto dog_sleeps = string_id(corpus, "dog sleeps");
    CHECK(solver.equivalent(the_dog, a_cat));
    CHECK(solver.equivalent(string_id(corpus, "runs"), string_id(corpus, "sleeps")));

    const scf::EvidenceBuilder builder(corpus);
    CHECK(builder.pair_support(the_dog, a_cat) == 2);
    CHECK(builder.pair_support(dog_runs, dog_sleeps) == 1);
    CHECK(builder.pair_witnesses(the_dog, a_cat).size() == 2);
    CHECK(find_evidence(builder.span_evidence(), {0, 0, 2}).score == 2);
    CHECK(find_evidence(builder.span_evidence(), {0, 1, 3}).score == 1);

    const std::vector<std::string> expected_trees{
        "((the dog) runs)",
        "((a cat) runs)",
        "((the dog) sleeps)",
        "((a cat) sleeps)",
    };
    for (scf::SentenceId sentence = 0; sentence < corpus.sentences().size(); ++sentence) {
        const auto tree = analyze_sentence(corpus, builder, sentence);
        CHECK(tree.hard_consistent);
        CHECK(tree.best_score == 2);
        CHECK(tree.optimal_tree_count == 1);
        CHECK(tree.crossing_conflicts.empty());
        CHECK(scf::format_unique_tree(corpus, sentence, tree) == expected_trees.at(sentence));
    }
}

void test_duplicate_occurrences_do_not_increase_support() {
    auto config = scf::CorpusConfig{};
    config.deduplicate_sentence_types = false;
    const auto corpus = load_corpus(
        "the dog runs\na cat runs\nthe dog sleeps\na cat sleeps\n"
        "the dog runs\na cat runs\nthe dog sleeps\na cat sleeps\n",
        config);
    const scf::EvidenceBuilder builder(corpus);
    CHECK(builder.pair_support(string_id(corpus, "the dog"), string_id(corpus, "a cat")) == 2);
}

void test_derived_equality_is_not_a_direct_witness() {
    const auto corpus = load_corpus("x u r\nx v r\ny v s\ny z s\nq u t\n");
    const auto solver = solve(corpus);
    const auto u = string_id(corpus, "u");
    const auto v = string_id(corpus, "v");
    const auto z = string_id(corpus, "z");
    CHECK(solver.equivalent(u, v));
    CHECK(solver.equivalent(v, z));
    CHECK(solver.equivalent(u, z));
    const scf::EvidenceBuilder builder(corpus);
    CHECK(builder.pair_support(u, v) == 1);
    CHECK(builder.pair_support(v, z) == 1);
    CHECK(builder.pair_support(u, z) == 0);
    CHECK(std::none_of(builder.span_evidence().begin(), builder.span_evidence().end(), [](const auto& item) {
        return item.span == scf::Span{4, 1, 2};
    }));
}

void test_whole_sentence_context_is_excluded() {
    const auto corpus = load_corpus("abc\nxyz\n");
    CHECK(corpus.context_records().empty());
    const auto solver = solve(corpus);
    CHECK(!solver.equivalent(string_id(corpus, "abc"), string_id(corpus, "xyz")));
}

void test_concat_congruence() {
    // Context keys seed a~a' and b~b'; observed concat facts then lift equality to ab~a'b'.
    const std::vector<scf::ContextRecord> contexts{
        {{0, 1, 2}, {}}, {{0, 1, 3}, {}}, {{0, 4, 5}, {}}, {{0, 4, 6}, {}}};
    const std::vector<scf::ConcatTriple> concats{{2, 5, 7}, {3, 6, 8}};
    scf::EquivalenceSolver solver(9, contexts, concats);
    solver.saturate();
    CHECK(solver.equivalent(2, 3));
    CHECK(solver.equivalent(5, 6));
    CHECK(solver.equivalent(7, 8));
    CHECK(std::any_of(solver.reasons().begin(), solver.reasons().end(), [](const auto& reason) {
        return reason.kind == scf::ReasonKind::ConcatCongruence;
    }));
}

void test_multiround_fixed_point_cascade() {
    // Round 1: a~a', u~v, then ax~a'x. Round 2: u~z, then ut~zt.
    // Round 3: C~D through newly equivalent left contexts. Round 4 is the no-change check.
    const std::vector<scf::ContextRecord> contexts{
        {{0, 1, 2}, {}},   {{0, 1, 3}, {}},   {{5, 7, 8}, {}},
        {{5, 7, 9}, {}},   {{6, 7, 10}, {}},  {{12, 7, 14}, {}},
        {{13, 7, 15}, {}},
    };
    const std::vector<scf::ConcatTriple> concats{
        {2, 4, 5}, {3, 4, 6}, {8, 11, 12}, {10, 11, 13}};
    scf::EquivalenceSolver solver(16, contexts, concats);
    solver.saturate();
    CHECK(solver.equivalent(8, 10));
    CHECK(solver.equivalent(12, 13));
    CHECK(solver.equivalent(14, 15));
    CHECK(solver.statistics().size() == 5);
    CHECK(solver.statistics()[1].context_unions == 2);
    CHECK(solver.statistics()[1].concat_unions == 1);
    CHECK(solver.statistics()[2].context_unions == 1);
    CHECK(solver.statistics()[2].concat_unions == 1);
    CHECK(solver.statistics()[3].context_unions == 1);
    CHECK(solver.statistics()[4].fixed_point);
    CHECK(std::count_if(solver.statistics().begin(), solver.statistics().end(), [](const auto& stats) {
              return stats.context_unions + stats.concat_unions > 0;
          }) >= 3);
}

void test_observed_universe_does_not_grow() {
    const auto corpus = load_corpus("x a\ny b\n");
    const auto count = corpus.string_interner().size();
    const auto solver = solve(corpus);
    (void)solver;
    CHECK(corpus.string_interner().size() == count);
    CHECK(!corpus.string_interner().find(std::vector<scf::TokenId>{99, 100}).has_value());
}

void test_crossing_and_splits() {
    const std::vector<scf::Span> crossing{{0, 0, 3}, {0, 2, 5}};
    CHECK(scf::spans_cross(crossing[0], crossing[1]));
    const auto result = scf::solve_binary_trees(0, 5, crossing);
    CHECK(!result.consistent);
    CHECK(result.tree_count == 0);
    CHECK(result.crossing_conflicts.size() == 1);

    const std::vector<scf::Span> hard{{0, 1, 4}};
    CHECK(!scf::split_respects_constraints(0, 2, 5, hard));
    CHECK(scf::split_respects_constraints(0, 1, 5, hard));
    CHECK(scf::split_respects_constraints(1, 3, 4, hard));
}

void test_tree_count() {
    const auto unconstrained = scf::solve_binary_trees(0, 3, {});
    CHECK(unconstrained.consistent);
    CHECK(unconstrained.tree_count == 2);
    CHECK(unconstrained.unique_tree_splits.empty());

    const std::vector<scf::Span> hard{{0, 0, 2}};
    const auto constrained = scf::solve_binary_trees(0, 3, hard);
    CHECK(constrained.consistent);
    CHECK(constrained.tree_count == 1);
    CHECK(std::find(constrained.forced_spans.begin(), constrained.forced_spans.end(), hard.front()) !=
          constrained.forced_spans.end());
}

std::size_t proper_forced_count(const scf::TreeSolveResult& result) {
    return static_cast<std::size_t>(std::count_if(
        result.forced_spans.begin(), result.forced_spans.end(), [&](const auto& span) {
            return span.end > span.begin + 1 &&
                   !(span.begin == 0 && span.end == result.sentence_length);
        }));
}

void test_maximum_evidence_tree_optimization() {
    const auto corpus = load_corpus("w0 w1 w2\n");

    const std::vector<scf::SpanScore> left_scores{{{0, 0, 2}, 2}, {{0, 1, 3}, 1}};
    const auto left = scf::solve_maximum_evidence_trees(0, 3, left_scores);
    CHECK(left.hard_consistent);
    CHECK(left.best_score == 2);
    CHECK(left.optimal_tree_count == 1);
    CHECK(scf::format_unique_tree(corpus, 0, left) == "((w0 w1) w2)");

    const std::vector<scf::SpanScore> right_scores{{{0, 0, 2}, 1}, {{0, 1, 3}, 3}};
    const auto right = scf::solve_maximum_evidence_trees(0, 3, right_scores);
    CHECK(right.best_score == 3);
    CHECK(right.optimal_tree_count == 1);
    CHECK(scf::format_unique_tree(corpus, 0, right) == "(w0 (w1 w2))");

    const std::vector<scf::SpanScore> tied_scores{{{0, 0, 2}, 2}, {{0, 1, 3}, 2}};
    const auto tied = scf::solve_maximum_evidence_trees(0, 3, tied_scores);
    CHECK(tied.best_score == 2);
    CHECK(tied.optimal_tree_count == 2);
    CHECK(proper_forced_count(tied) == 0);

    const auto no_evidence_three = scf::solve_maximum_evidence_trees(0, 3, {});
    const auto no_evidence_four = scf::solve_maximum_evidence_trees(0, 4, {});
    CHECK(no_evidence_three.optimal_tree_count == 2);
    CHECK(no_evidence_four.optimal_tree_count == 5);

    const std::vector<scf::SpanScore> ignored_scores{
        {{0, 0, 1}, 1000}, {{0, 1, 2}, 1000}, {{0, 2, 3}, 1000},
        {{0, 0, 3}, 1000}, {{0, 0, 2}, 2},    {{0, 1, 3}, 1},
    };
    const auto ignored = scf::solve_maximum_evidence_trees(0, 3, ignored_scores);
    CHECK(ignored.best_score == 2);
    CHECK(ignored.optimal_tree_count == 1);
    CHECK(scf::format_unique_tree(corpus, 0, ignored) == "((w0 w1) w2)");

    const std::vector<scf::SpanScore> crossing_candidates{{{0, 0, 3}, 5}, {{0, 2, 5}, 4}};
    const auto competing = scf::solve_maximum_evidence_trees(0, 5, crossing_candidates);
    CHECK(competing.hard_consistent);
    CHECK(competing.crossing_conflicts.empty());
    CHECK(competing.optimal_tree_count > 0);
}

using SpanSet = std::set<std::pair<std::uint16_t, std::uint16_t>>;

std::vector<SpanSet> enumerate_trees(const std::uint16_t begin, const std::uint16_t end) {
    if (end == begin + 1) {
        return {SpanSet{{begin, end}}};
    }
    std::vector<SpanSet> result;
    for (std::uint16_t split = static_cast<std::uint16_t>(begin + 1); split < end; ++split) {
        for (const auto& left : enumerate_trees(begin, split)) {
            for (const auto& right : enumerate_trees(split, end)) {
                auto model = left;
                model.insert(right.begin(), right.end());
                model.emplace(begin, end);
                result.push_back(std::move(model));
            }
        }
    }
    return result;
}

void check_forced_against_brute_force(const std::uint16_t length,
                                      const std::vector<scf::Span>& constraints) {
    auto models = enumerate_trees(0, length);
    models.erase(std::remove_if(models.begin(), models.end(), [&](const auto& model) {
                     return std::any_of(constraints.begin(), constraints.end(), [&](const auto& hard) {
                         return !model.contains({hard.begin, hard.end});
                     });
                 }),
                 models.end());
    const auto solved = scf::solve_binary_trees(0, length, constraints);
    CHECK(solved.tree_count == models.size());
    CHECK(!models.empty());
    SpanSet intersection = models.front();
    for (const auto& model : models) {
        for (auto iterator = intersection.begin(); iterator != intersection.end();) {
            if (!model.contains(*iterator)) {
                iterator = intersection.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }
    SpanSet actual;
    for (const auto& span : solved.forced_spans) {
        actual.emplace(span.begin, span.end);
    }
    CHECK(actual == intersection);
}

void test_forced_spans_against_brute_force() {
    check_forced_against_brute_force(4, {});
    check_forced_against_brute_force(4, {{0, 0, 2}});
    check_forced_against_brute_force(4, {{0, 1, 3}});
    check_forced_against_brute_force(4, {{0, 0, 2}, {0, 2, 4}});
    check_forced_against_brute_force(5, {{0, 0, 4}, {0, 1, 3}});
}

void check_optimal_forced_against_brute_force(const std::uint16_t length,
                                              const std::vector<scf::SpanScore>& evidence,
                                              const std::vector<scf::Span>& constraints = {}) {
    auto models = enumerate_trees(0, length);
    models.erase(std::remove_if(models.begin(), models.end(), [&](const auto& model) {
                     return std::any_of(constraints.begin(), constraints.end(), [&](const auto& hard) {
                         return !model.contains({hard.begin, hard.end});
                     });
                 }),
                 models.end());
    std::map<std::pair<std::uint16_t, std::uint16_t>, std::uint64_t> scores;
    for (const auto& item : evidence) {
        if (item.span.end > item.span.begin + 1 &&
            !(item.span.begin == 0 && item.span.end == length)) {
            scores[{item.span.begin, item.span.end}] = item.score;
        }
    }
    const auto model_score = [&](const SpanSet& model) {
        std::uint64_t total = 0;
        for (const auto& span : model) {
            const auto found = scores.find(span);
            if (found != scores.end()) {
                total += found->second;
            }
        }
        return total;
    };
    std::uint64_t best_score = 0;
    for (const auto& model : models) {
        best_score = std::max(best_score, model_score(model));
    }
    models.erase(std::remove_if(models.begin(), models.end(),
                                [&](const auto& model) { return model_score(model) != best_score; }),
                 models.end());

    const auto solved = scf::solve_maximum_evidence_trees(0, length, evidence, constraints);
    CHECK(solved.best_score == best_score);
    CHECK(solved.optimal_tree_count == models.size());
    CHECK(!models.empty());
    SpanSet intersection = models.front();
    for (const auto& model : models) {
        for (auto iterator = intersection.begin(); iterator != intersection.end();) {
            if (!model.contains(*iterator)) {
                iterator = intersection.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }
    SpanSet actual;
    for (const auto& span : solved.forced_spans) {
        actual.emplace(span.begin, span.end);
    }
    CHECK(actual == intersection);
}

void test_forced_optimal_spans_against_brute_force() {
    check_optimal_forced_against_brute_force(
        4, {{{0, 0, 2}, 2}, {{0, 1, 3}, 3}, {{0, 2, 4}, 1}});
    check_optimal_forced_against_brute_force(
        4, {{{0, 0, 3}, 2}, {{0, 1, 4}, 2}});
    check_optimal_forced_against_brute_force(
        5, {{{0, 0, 2}, 1}, {{0, 1, 4}, 4}, {{0, 3, 5}, 2}}, {{0, 1, 4}});
}

void test_synthetic_cartesian_grammar() {
    const auto corpus = load_synthetic("cartesian.txt");
    const auto solver = solve(corpus);
    CHECK(solver.equivalent(string_id(corpus, "a1"), string_id(corpus, "a2")));
    CHECK(solver.equivalent(string_id(corpus, "a2"), string_id(corpus, "a3")));
    CHECK(solver.equivalent(string_id(corpus, "b1"), string_id(corpus, "b2")));
    CHECK(solver.equivalent(string_id(corpus, "b2"), string_id(corpus, "b3")));
    CHECK(!solver.equivalent(string_id(corpus, "a1"), string_id(corpus, "b1")));
    CHECK(solver.equivalent(string_id(corpus, "a1 b1"), string_id(corpus, "a3 b3")));
    const scf::EvidenceBuilder builder(corpus);
    for (scf::SentenceId sentence = 0; sentence < corpus.sentences().size(); ++sentence) {
        const auto tree = analyze_sentence(corpus, builder, sentence);
        CHECK(tree.hard_consistent);
        CHECK(tree.best_score == 0);
        CHECK(tree.optimal_tree_count == 1);
    }
}

void test_deep_synthetic_end_to_end() {
    const auto corpus = load_synthetic("deep.txt");
    const auto solver = solve(corpus);
    CHECK(solver.equivalent(string_id(corpus, "c1"), string_id(corpus, "c2")));
    CHECK(solver.equivalent(string_id(corpus, "d1"), string_id(corpus, "d2")));
    CHECK(solver.equivalent(string_id(corpus, "b1"), string_id(corpus, "b2")));
    CHECK(solver.equivalent(string_id(corpus, "c1 d1"), string_id(corpus, "c2 d2")));
    const scf::EvidenceBuilder builder(corpus);
    CHECK(find_evidence(builder.span_evidence(), {0, 0, 2}).score == 2);
    CHECK(find_evidence(builder.span_evidence(), {0, 1, 3}).score == 2);
    for (scf::SentenceId sentence = 0; sentence < corpus.sentences().size(); ++sentence) {
        const auto tree = analyze_sentence(corpus, builder, sentence);
        CHECK(tree.hard_consistent);
        CHECK(tree.best_score == 2);
        CHECK(tree.optimal_tree_count == 2);
        CHECK(proper_forced_count(tree) == 0);
        CHECK(tree.crossing_conflicts.empty());
    }
}

// --- v1.2: gold tree infrastructure ---------------------------------------

std::set<scf::SpanPair> shape_of(const scf::GoldTree& tree) {
    std::set<scf::SpanPair> shape;
    for (const auto& span : tree.internal_spans) {
        shape.emplace(span.begin, span.end);
    }
    return shape;
}

scf::GoldNode leaf(const std::string& token) { return scf::GoldNode{token, {}}; }

scf::GoldNode branch(const std::string& label, scf::GoldNode left, scf::GoldNode right) {
    scf::GoldNode node{label, {}};
    node.children.push_back(std::move(left));
    node.children.push_back(std::move(right));
    return node;
}

void test_gold_tree_infrastructure() {
    const auto tree_node = branch("S", branch("A", leaf("c1"), leaf("d1")),
                                  branch("B", leaf("e1"), leaf("f1")));
    const auto tree = scf::gold_tree_from_node(tree_node);
    CHECK(tree.length == 4);
    CHECK(shape_of(tree) == std::set<scf::SpanPair>({{0, 2}, {2, 4}, {0, 4}}));
    CHECK(scf::gold_eval_spans(tree) == std::set<scf::SpanPair>({{0, 2}, {2, 4}}));
    CHECK(scf::gold_eval_spans(tree, true, false) ==
          std::set<scf::SpanPair>({{0, 2}, {2, 4}, {0, 4}}));
    CHECK(scf::gold_eval_spans(tree, false, true) ==
          std::set<scf::SpanPair>({{0, 2}, {2, 4}, {0, 1}, {1, 2}, {2, 3}, {3, 4}}));
    CHECK(scf::gold_scoring_spans(tree) == std::set<scf::SpanPair>({{0, 2}, {2, 4}}));
    CHECK(scf::format_gold_bracket(tree_node) == "((c1 d1) (e1 f1))");

    const std::vector<std::string> tokens{"c1", "d1", "e1", "f1"};
    CHECK(scf::bracket_from_gold_tree(tree, tokens) == "((c1 d1) (e1 f1))");
    const auto reparsed = scf::gold_tree_from_node(scf::parse_bracket_tree("((c1 d1) (e1 f1))"));
    CHECK(shape_of(reparsed) == shape_of(tree));

    // Unary chains collapse to the lower node before span extraction.
    scf::GoldNode unary{"B", {}};
    unary.children.push_back(scf::GoldNode{"V", {leaf("runs")}});
    const auto collapsed = scf::collapse_unary_chains(unary);
    CHECK(collapsed.is_leaf());
    CHECK(collapsed.label == "runs");

    scf::GoldNode ternary{"S", {leaf("a"), leaf("b"), leaf("c")}};
    bool threw = false;
    try {
        (void)scf::gold_tree_from_node(ternary);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

void test_gold_spans_tsv_roundtrip() {
    const auto dataset = scf::generate_dataset("nested_balanced", 1.0, 1);
    const auto trees = scf::dataset_gold_trees(dataset);
    std::ostringstream written;
    scf::write_gold_spans_tsv(written, trees);

    std::vector<std::uint16_t> lengths(trees.size(), 4);
    {
        std::istringstream input(written.str());
        const auto rows = scf::read_gold_span_rows(input);
        const auto reread = scf::assemble_gold_trees(rows, lengths);
        CHECK(reread.size() == trees.size());
        for (std::size_t index = 0; index < trees.size(); ++index) {
            CHECK(shape_of(reread[index]) == shape_of(trees[index]));
        }
    }
    {
        // The root row may be omitted; assemble re-adds it.
        std::istringstream input("0\t0\t2\tA\n0\t2\t4\tB\n");
        const auto rows = scf::read_gold_span_rows(input);
        const auto reread = scf::assemble_gold_trees(rows, std::vector<std::uint16_t>{4});
        CHECK(shape_of(reread[0]) == std::set<scf::SpanPair>({{0, 2}, {2, 4}, {0, 4}}));
    }
    const auto expect_throw = [&](const std::string& text,
                                  const std::vector<std::uint16_t>& sentence_lengths) {
        bool threw = false;
        try {
            std::istringstream input(text);
            const auto rows = scf::read_gold_span_rows(input);
            (void)scf::assemble_gold_trees(rows, sentence_lengths);
        } catch (const std::exception&) {
            threw = true;
        }
        CHECK(threw);
    };
    expect_throw("0\t0\t2\tA\n", {4});               // too few internal spans
    expect_throw("0\t0\t2\tA\n0\t1\t3\tB\n", {3});   // crossing spans, no valid decomposition
    expect_throw("1\t0\t2\tA\n1\t2\t4\tB\n", {4});   // sentence 0 missing entirely
    expect_throw("0\t0\t5\tA\n", {4});               // span exceeds sentence length
    expect_throw("2\t0\t2\tA\n", {4});               // sentence id out of range
}

// --- v1.2: synthetic generator --------------------------------------------

scf::Corpus corpus_from_dataset(const scf::SyntheticDataset& dataset) {
    std::ostringstream text;
    for (const auto& sentence : dataset.sentences) {
        for (std::size_t index = 0; index < sentence.tokens.size(); ++index) {
            text << (index == 0 ? "" : " ") << sentence.tokens[index];
        }
        text << '\n';
    }
    std::istringstream input(text.str());
    scf::Corpus corpus;
    corpus.load(input);
    return corpus;
}

void test_generator_language_counts() {
    const auto count_of = [](const std::string& name) {
        return scf::generate_dataset(name, 1.0, 1).full_sentence_count;
    };
    CHECK(count_of("ab_cartesian") == 9);
    CHECK(count_of("simple_np_vp") == 4);
    CHECK(count_of("symmetric_abc") == 8);
    CHECK(count_of("nested_balanced") == 16);
    CHECK(count_of("right_branching") == 16);
    CHECK(count_of("left_branching") == 16);
    CHECK(count_of("ambiguous_lexicon") == 36);
    CHECK(count_of("ccg_lite") == 84);

    const auto simple = scf::generate_dataset("simple_np_vp", 1.0, 1);
    std::set<std::string> sentences;
    for (const auto& sentence : simple.sentences) {
        std::string joined;
        for (const auto& token : sentence.tokens) {
            joined += token + " ";
        }
        sentences.insert(joined);
    }
    CHECK(sentences == std::set<std::string>({"the dog runs ", "the dog sleeps ", "a cat runs ",
                                              "a cat sleeps "}));
}

void test_generator_gold_consistency() {
    for (const auto& name : scf::known_grammar_names()) {
        const auto dataset = scf::generate_dataset(name, 1.0, 1);
        CHECK(!dataset.sentences.empty());
        for (const auto& sentence : dataset.sentences) {
            const auto tree = scf::gold_tree_from_node(sentence.tree);
            CHECK(tree.length == sentence.tokens.size());
            // Root span present; brackets and spans describe the same tree.
            CHECK(shape_of(tree).contains({0, tree.length}) || tree.length == 1);
            const auto bracket = scf::format_gold_bracket(sentence.tree);
            const auto reparsed = scf::gold_tree_from_node(scf::parse_bracket_tree(bracket));
            CHECK(shape_of(reparsed) == shape_of(tree));
            CHECK(scf::leaf_tokens(scf::parse_bracket_tree(bracket)) == sentence.tokens);
            // Internal span count of a full binary tree.
            CHECK(tree.internal_spans.size() == static_cast<std::size_t>(tree.length) - 1);
        }
        const auto json = scf::grammar_json(dataset);
        CHECK(json.find("\"grammar_name\": \"" + name + "\"") != std::string::npos);
        CHECK(json.find("\"full_sentence_count\": ") != std::string::npos);
    }
}

void test_coverage_sampling() {
    // Determinism: identical seeds give byte-identical datasets.
    const auto first = scf::generate_dataset("nested_balanced", 0.4, 7);
    const auto second = scf::generate_dataset("nested_balanced", 0.4, 7);
    CHECK(first.sentences.size() == second.sentences.size());
    for (std::size_t index = 0; index < first.sentences.size(); ++index) {
        CHECK(first.sentences[index].tokens == second.sentences[index].tokens);
    }
    CHECK(scf::grammar_json(first) == scf::grammar_json(second));

    // ceil(coverage * N) sampling.
    CHECK(scf::generate_dataset("nested_balanced", 1.0, 1).sentences.size() == 16);
    CHECK(scf::generate_dataset("nested_balanced", 0.25, 1).sentences.size() == 4);
    CHECK(scf::generate_dataset("nested_balanced", 0.05, 1).sentences.size() == 1);
    CHECK(scf::generate_dataset("nested_balanced", 0.5, 3, 5).sentences.size() == 5);

    // Sampled corpora contain no duplicate sentences.
    const auto sampled = scf::generate_dataset("ccg_lite", 0.6, 3);
    std::set<std::vector<std::string>> unique_sentences;
    for (const auto& sentence : sampled.sentences) {
        CHECK(unique_sentences.insert(sentence.tokens).second);
    }

    // Different seeds usually select different subsets; shuffling is real.
    const auto other_seed = scf::generate_dataset("ccg_lite", 0.2, 4);
    CHECK(other_seed.sentences.size() == sampled.sentences.size() ||
          other_seed.sentences.size() == 17);
}

// --- v1.2: evaluator --------------------------------------------------------

scf::GoldTree gold_from_bracket(const std::string& bracket) {
    return scf::gold_tree_from_node(scf::parse_bracket_tree(bracket));
}

void test_evaluator_handcrafted_sentences() {
    // Length 3, left evidence stronger: unique left optimum.
    const std::vector<scf::SpanScore> left_scores{{{0, 0, 2}, 2}, {{0, 1, 3}, 1}};
    const auto left = scf::solve_maximum_evidence_trees(0, 3, left_scores);

    const auto correct =
        scf::evaluate_sentence(0, left, gold_from_bracket("((a b) c)"), left_scores);
    CHECK(correct.outcome == scf::SentenceOutcome::UniqueCorrect);
    CHECK(correct.unique_optimal);
    CHECK(correct.exact_unique_match);
    CHECK(correct.gold_in_argmax);
    CHECK(correct.best_score == 2 && correct.gold_score == 2);
    CHECK(correct.second_best_score == std::optional<std::uint64_t>(1));
    CHECK(correct.margin == std::optional<std::uint64_t>(1));
    CHECK(correct.f1 == std::optional<double>(1.0));

    const auto wrong = scf::evaluate_sentence(0, left, gold_from_bracket("(a (b c))"), left_scores);
    CHECK(wrong.outcome == scf::SentenceOutcome::UniqueWrong);
    CHECK(!wrong.gold_in_argmax);
    CHECK(wrong.gold_score == 1);
    CHECK(wrong.missing_gold_spans == std::vector<scf::SpanPair>({{1, 3}}));
    CHECK(wrong.extra_predicted_spans == std::vector<scf::SpanPair>({{0, 2}}));
    CHECK(wrong.precision == std::optional<double>(0.0));
    CHECK(wrong.f1 == std::optional<double>(0.0));

    // Tied evidence: ambiguity is preserved and classified, never tie-broken.
    const std::vector<scf::SpanScore> tied_scores{{{0, 0, 2}, 2}, {{0, 1, 3}, 2}};
    const auto tied = scf::solve_maximum_evidence_trees(0, 3, tied_scores);
    const auto ambiguous =
        scf::evaluate_sentence(0, tied, gold_from_bracket("((a b) c)"), tied_scores);
    CHECK(ambiguous.outcome == scf::SentenceOutcome::AmbiguousGoldIncluded);
    CHECK(!ambiguous.unique_optimal);
    CHECK(ambiguous.gold_in_argmax);
    CHECK(ambiguous.optimal_tree_count == 2);
    CHECK(ambiguous.second_best_score == std::optional<std::uint64_t>(2));
    CHECK(ambiguous.margin == std::optional<std::uint64_t>(0));
    CHECK(!ambiguous.f1.has_value());

    // Ambiguous with gold outside the argmax.
    const std::vector<scf::SpanScore> excluded_scores{{{0, 0, 2}, 2}, {{0, 2, 4}, 2},
                                                      {{0, 1, 3}, 1}};
    const auto four = scf::solve_maximum_evidence_trees(0, 4, excluded_scores);
    const auto excluded =
        scf::evaluate_sentence(0, four, gold_from_bracket("(a ((b c) d))"), excluded_scores);
    CHECK(excluded.optimal_tree_count == 1
              ? excluded.outcome == scf::SentenceOutcome::UniqueWrong
              : excluded.outcome == scf::SentenceOutcome::AmbiguousGoldExcluded);
    CHECK(!excluded.gold_in_argmax);

    // Length 2: no proper spans on either side; perfect by definition.
    const auto two = scf::solve_maximum_evidence_trees(0, 2, {});
    const auto trivial = scf::evaluate_sentence(0, two, gold_from_bracket("(a b)"), {});
    CHECK(trivial.outcome == scf::SentenceOutcome::UniqueCorrect);
    CHECK(trivial.exact_unique_match);
    CHECK(trivial.f1 == std::optional<double>(1.0));
    CHECK(!trivial.second_best_score.has_value());
    CHECK(!trivial.margin.has_value());

    // Length 1: no binary structure at all; evaluation must not crash.
    const auto one = scf::solve_maximum_evidence_trees(0, 1, {});
    const auto single = scf::evaluate_sentence(0, one, gold_from_bracket("a"), {});
    CHECK(single.outcome == scf::SentenceOutcome::UniqueCorrect);

    // All trees tied at score zero: Catalan-sized argmax, gold included.
    const auto no_evidence = scf::solve_maximum_evidence_trees(0, 4, {});
    const auto all_tied =
        scf::evaluate_sentence(0, no_evidence, gold_from_bracket("((a b) (c d))"), {});
    CHECK(all_tied.optimal_tree_count == 5);
    CHECK(all_tied.all_trees_tied);
    CHECK(all_tied.gold_in_argmax);
    CHECK(all_tied.outcome == scf::SentenceOutcome::AmbiguousGoldIncluded);
}

void test_brute_force_enumerator() {
    CHECK(scf::enumerate_binary_trees(1).size() == 1);
    CHECK(scf::enumerate_binary_trees(2).size() == 1);
    CHECK(scf::enumerate_binary_trees(3).size() == 2);
    CHECK(scf::enumerate_binary_trees(4).size() == 5);
    CHECK(scf::enumerate_binary_trees(5).size() == 14);
    CHECK(scf::enumerate_binary_trees(6).size() == 42);

    // DP and brute force agree on best score and argmax count.
    const std::vector<std::vector<scf::SpanScore>> cases{
        {{{0, 0, 2}, 2}, {{0, 1, 3}, 3}, {{0, 2, 4}, 1}},
        {{{0, 0, 3}, 2}, {{0, 1, 4}, 2}},
        {{{0, 0, 2}, 1}, {{0, 1, 4}, 4}, {{0, 3, 5}, 2}},
        {{{0, 0, 3}, 5}, {{0, 2, 5}, 4}},
        {},
    };
    const std::vector<std::uint16_t> lengths{4, 4, 5, 5, 6};
    for (std::size_t index = 0; index < cases.size(); ++index) {
        const auto solved = scf::solve_maximum_evidence_trees(0, lengths[index], cases[index]);
        const auto brute = scf::brute_force_tree_scores(lengths[index], cases[index]);
        CHECK(solved.best_score == brute.best_score);
        CHECK(solved.optimal_tree_count == brute.argmax_count);
    }
}

void test_no_hidden_tie_break() {
    // Under tied evidence the parser must never silently pick one branch:
    // this test fails if a unique tree is reported.
    const std::vector<scf::SpanScore> tied{{{0, 0, 2}, 3}, {{0, 1, 3}, 3}};
    const auto solved = scf::solve_maximum_evidence_trees(0, 3, tied);
    CHECK(solved.optimal_tree_count == 2);
    CHECK(solved.unique_tree_splits.empty());
    const auto evaluated = scf::evaluate_sentence(0, solved, gold_from_bracket("((a b) c)"), tied);
    CHECK(!evaluated.unique_optimal);
    CHECK(evaluated.predicted_spans.empty());
}

// --- v1.2: parser/evaluator integration ------------------------------------

scf::CorpusEvaluation evaluate_grammar(const std::string& name,
                                       const double coverage,
                                       const std::uint64_t seed,
                                       scf::CollapseDiagnostics* diagnostics_out = nullptr) {
    const auto dataset = scf::generate_dataset(name, coverage, seed);
    const auto corpus = corpus_from_dataset(dataset);
    const auto bundle = scf::analyze_corpus(corpus);
    const auto gold = scf::dataset_gold_trees(dataset);
    CHECK(gold.size() == corpus.sentences().size());
    if (diagnostics_out != nullptr) {
        *diagnostics_out = scf::collapse_diagnostics(corpus, bundle.solver);
    }
    return scf::evaluate_corpus(bundle.analyses, gold, bundle.builder.span_evidence());
}

void test_integration_simple_np_vp() {
    const auto evaluation = evaluate_grammar("simple_np_vp", 1.0, 1);
    CHECK(evaluation.sentence_count == 4);
    CHECK(evaluation.unique_optimal_rate == 1.0);
    CHECK(evaluation.exact_unique_match_rate == 1.0);
    CHECK(evaluation.gold_in_argmax_rate == 1.0);
    CHECK(evaluation.unique_correct == 4);
    CHECK(evaluation.mean_unlabeled_f1_given_unique == std::optional<double>(1.0));
}

void test_integration_ab_cartesian() {
    const auto evaluation = evaluate_grammar("ab_cartesian", 1.0, 1);
    CHECK(evaluation.sentence_count == 9);
    // Length-2 sentences have exactly one binary tree and empty proper spans.
    CHECK(evaluation.unique_optimal_rate == 1.0);
    CHECK(evaluation.exact_unique_match_rate == 1.0);
    CHECK(evaluation.gold_in_argmax_rate == 1.0);
}

void test_integration_symmetric_abc() {
    const auto evaluation = evaluate_grammar("symmetric_abc", 1.0, 1);
    CHECK(evaluation.sentence_count == 8);
    // Full symmetry is exactly preserved: structural non-identifiability.
    CHECK(evaluation.gold_in_argmax_rate == 1.0);
    CHECK(evaluation.ambiguous_optimal_rate == 1.0);
    CHECK(evaluation.unique_optimal_rate == 0.0);
    CHECK(evaluation.mean_argmax_size == 2.0);
    CHECK(evaluation.zero_margin_rate == 1.0);
    CHECK(evaluation.ambiguous_gold_included == 8);
    CHECK(evaluation.ambiguous_gold_excluded == 0);
}

void test_integration_nested_balanced() {
    // Documented empirical behavior at full coverage: the crossing spans
    // [1,3), [0,3), [1,4) receive strictly less support (2) than the gold
    // constituents [0,2) and [2,4) (4 each), so recovery is unique and exact.
    const auto evaluation = evaluate_grammar("nested_balanced", 1.0, 1);
    CHECK(evaluation.sentence_count == 16);
    CHECK(evaluation.unique_optimal_rate == 1.0);
    CHECK(evaluation.exact_unique_match_rate == 1.0);
    CHECK(evaluation.gold_in_argmax_rate == 1.0);
    CHECK(evaluation.mean_finite_margin == std::optional<double>(2.0));
}

void test_integration_branching_grammars_are_symmetric() {
    // Documented empirical behavior: under full Cartesian lexical sampling the
    // substitution-evidence table of right_branching and left_branching is
    // identical to nested_balanced's, so SCF uniquely recovers the balanced
    // tree in both cases. The point of the pair is direction neutrality: both
    // orientations must behave exactly the same (no hidden branching bias),
    // and the deeper gold trees are correctly reported as not identifiable
    // from this evidence rather than being rescued by a directional prior.
    const auto right = evaluate_grammar("right_branching", 1.0, 1);
    const auto left = evaluate_grammar("left_branching", 1.0, 1);
    CHECK(right.sentence_count == 16 && left.sentence_count == 16);
    CHECK(right.unique_optimal_rate == left.unique_optimal_rate);
    CHECK(right.gold_in_argmax_rate == left.gold_in_argmax_rate);
    CHECK(right.exact_unique_match_rate == left.exact_unique_match_rate);
    CHECK(right.mean_best_score == left.mean_best_score);
    CHECK(right.mean_gold_score == left.mean_gold_score);
    CHECK(right.unique_wrong == left.unique_wrong);
    // Both fail toward the same balanced structure, not toward "their" side.
    CHECK(right.gold_in_argmax_rate == 0.0);
    CHECK(right.unique_optimal_rate == 1.0);
}

void test_integration_ambiguous_lexicon_diagnostics() {
    scf::CollapseDiagnostics diagnostics;
    const auto evaluation = evaluate_grammar("ambiguous_lexicon", 1.0, 1, &diagnostics);
    CHECK(evaluation.sentence_count == 36);
    // Acceptance is not accuracy: the diagnostic must expose the collapse
    // caused by the shared token "x".
    CHECK(diagnostics.suspicious_collapse);
    CHECK(diagnostics.collapse_ratio > 0.8);
    CHECK(diagnostics.largest_eclass_ratio > 0.25);
    CHECK(evaluation.hard_inconsistent == 0);
}

void test_integration_ccg_lite_pipeline() {
    const auto evaluation = evaluate_grammar("ccg_lite", 1.0, 1);
    CHECK(evaluation.sentence_count == 84);
    CHECK(evaluation.gold_in_argmax_rate == 1.0);
    CHECK(evaluation.hard_inconsistent == 0);
    CHECK(evaluation.unique_correct + evaluation.unique_wrong +
              evaluation.ambiguous_gold_included + evaluation.ambiguous_gold_excluded ==
          84);
}

void test_metrics_deterministic() {
    const auto run_once = [](std::string* json_out) {
        const auto dataset = scf::generate_dataset("nested_balanced", 0.4, 2);
        const auto corpus = corpus_from_dataset(dataset);
        const auto bundle = scf::analyze_corpus(corpus);
        const auto gold = scf::dataset_gold_trees(dataset);
        const auto evaluation =
            scf::evaluate_corpus(bundle.analyses, gold, bundle.builder.span_evidence());
        const auto diagnostics = scf::collapse_diagnostics(corpus, bundle.solver);
        scf::RunInfo info;
        info.grammar = dataset.grammar_name;
        info.seed = dataset.seed;
        info.coverage = dataset.coverage;
        info.full_sentence_count = dataset.full_sentence_count;
        info.sampled_sentence_count = dataset.sentences.size();
        std::ostringstream json;
        scf::write_metrics_json(json, info, corpus, diagnostics, evaluation);
        *json_out = json.str() + "\n" +
                    scf::summary_csv_row(info, corpus, diagnostics, evaluation);
    };
    std::string first;
    std::string second;
    run_once(&first);
    run_once(&second);
    CHECK(!first.empty());
    CHECK(first == second);
}

// --- v1.2.1: audit infrastructure -----------------------------------------

struct FamilyHashes {
    std::uint64_t surface{};
    std::uint64_t surface_renamed{};
    std::uint64_t context{};
    std::uint64_t context_renamed{};
    std::uint64_t witness{};
    std::uint64_t witness_renamed{};
    std::uint64_t gold_renamed{};
};

FamilyHashes family_hashes(const std::string& name, const std::size_t k) {
    const auto dataset = scf::generate_dataset(name, 1.0, 1, 0, k);
    const auto corpus = corpus_from_dataset(dataset);
    const scf::EvidenceBuilder builder(corpus);
    const auto tokens = scf::sentence_tokens(dataset.sentences);
    const auto renaming = scf::build_canonical_renaming(tokens);
    FamilyHashes hashes;
    hashes.surface = scf::sentence_set_hash(tokens);
    hashes.surface_renamed = scf::sentence_set_hash(scf::apply_renaming(tokens, renaming));
    hashes.context = scf::raw_context_relation_hash(corpus);
    hashes.context_renamed = scf::raw_context_relation_hash(corpus, &renaming);
    hashes.witness = scf::raw_witness_relation_hash(corpus, builder);
    hashes.witness_renamed = scf::raw_witness_relation_hash(corpus, builder, &renaming);
    hashes.gold_renamed = scf::gold_shape_hash(dataset.sentences, &renaming);
    return hashes;
}

void test_observational_equivalence_hashes() {
    // Hashes are order-independent.
    scf::TokenSentences forward{{"a", "b"}, {"c", "d"}};
    scf::TokenSentences backward{{"c", "d"}, {"a", "b"}};
    CHECK(scf::sentence_set_hash(forward) == scf::sentence_set_hash(backward));
    CHECK(scf::sentence_set_hash(forward) != scf::sentence_set_hash({{"a", "b"}}));

    const auto nested = family_hashes("nested_balanced", 2);
    const auto right = family_hashes("right_branching", 2);
    const auto left = family_hashes("left_branching", 2);
    // right and left generate the exact same corpus, contexts, and witnesses,
    // while their gold trees differ: strict observational equivalence.
    CHECK(right.surface == left.surface);
    CHECK(right.context == left.context);
    CHECK(right.witness == left.witness);
    CHECK(right.gold_renamed != left.gold_renamed);
    // nested_balanced differs only by token names: equal after canonical
    // renaming, unequal exactly.
    CHECK(nested.surface != right.surface);
    CHECK(nested.surface_renamed == right.surface_renamed);
    CHECK(nested.context_renamed == right.context_renamed);
    CHECK(nested.witness_renamed == right.witness_renamed);
}

void test_hierarchical_families_break_observational_equivalence() {
    const auto balanced = family_hashes("hierarchical_correlated_balanced", 3);
    const auto right = family_hashes("hierarchical_correlated_right", 3);
    const auto left = family_hashes("hierarchical_correlated_left", 3);
    // Acceptance F: the correlated families have genuinely different surface
    // languages, even up to token renaming.
    CHECK(balanced.surface != right.surface);
    CHECK(balanced.surface != left.surface);
    CHECK(right.surface != left.surface);
    CHECK(balanced.surface_renamed != right.surface_renamed);
    CHECK(balanced.surface_renamed != left.surface_renamed);
    CHECK(right.surface_renamed != left.surface_renamed);
    // And they differ from their Cartesian counterpart.
    const auto cartesian = family_hashes("nested_balanced", 3);
    CHECK(balanced.surface_renamed != cartesian.surface_renamed);
}

void test_lexical_cardinality() {
    CHECK(scf::generate_dataset("nested_balanced", 1.0, 1, 0, 3).full_sentence_count == 81);
    CHECK(scf::generate_dataset("nested_balanced", 1.0, 1, 0, 4).full_sentence_count == 256);
    CHECK(scf::generate_dataset("symmetric_abc", 1.0, 1, 0, 4).full_sentence_count == 64);
    CHECK(scf::generate_dataset("hierarchical_correlated_balanced", 1.0, 1, 0, 4)
              .full_sentence_count == 16);
    CHECK(scf::generate_dataset("simple_np_vp", 1.0, 1, 0, 3).full_sentence_count == 9);
    // The default cardinality reproduces the v1.2 languages exactly.
    CHECK(scf::generate_dataset("simple_np_vp", 1.0, 1).full_sentence_count == 4);
    CHECK(scf::generate_dataset("ab_cartesian", 1.0, 1).full_sentence_count == 9);
    bool threw = false;
    try {
        (void)scf::generate_dataset("nested_balanced", 1.0, 1, 0, 7);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

void test_symmetry_breaking_rate() {
    CHECK(scf::generate_dataset("symmetric_abc", 1.0, 1, 0, 2, 0.0).full_sentence_count == 8);
    CHECK(scf::generate_dataset("symmetric_abc", 1.0, 1, 0, 2, 1.0).full_sentence_count == 12);
    const auto half = scf::generate_dataset("symmetric_abc", 1.0, 1, 0, 3, 0.5);
    CHECK(half.full_sentence_count == 32);  // 27 base + ceil(0.5 * 9) markers
    const auto& marker = half.sentences.back();
    CHECK(marker.tokens.size() == 3 && marker.tokens.back() == "p");
    const auto shape = scf::gold_tree_from_node(marker.tree);
    CHECK(scf::gold_eval_spans(shape, true) ==
          std::set<scf::SpanPair>({{0, 2}, {0, 3}}));  // ((a b) p)
    bool threw = false;
    try {
        (void)scf::generate_dataset("nested_balanced", 1.0, 1, 0, 0, 0.5);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

void test_hierarchical_identifiability() {
    // Documented v1.2.1 result: correlated blocks make the top-level bracket
    // observable in all three variants. The balanced variant is fully
    // identifiable; in the right/left variants the *inside* of a correlated
    // block never varies, so the inner bracket stays population-ambiguous
    // (gold in argmax, argmax size 2). That residual ambiguity is a property
    // of the observations, not of the objective.
    const auto balanced = evaluate_grammar("hierarchical_correlated_balanced", 1.0, 1);
    CHECK(balanced.sentence_count == 9);
    CHECK(balanced.unique_correct == 9);
    CHECK(balanced.gold_in_argmax_rate == 1.0);

    const auto right = evaluate_grammar("hierarchical_correlated_right", 1.0, 1);
    const auto left = evaluate_grammar("hierarchical_correlated_left", 1.0, 1);
    for (const auto* evaluation : {&right, &left}) {
        CHECK(evaluation->sentence_count == 9);
        CHECK(evaluation->gold_in_argmax_rate == 1.0);
        CHECK(evaluation->unique_optimal_rate == 0.0);
        CHECK(evaluation->mean_argmax_size == 2.0);
        CHECK(evaluation->ambiguous_gold_included == 9);
    }
}

void test_saturation_is_decoupled_from_parsing() {
    // Engineering check of the v1.2.1 ablation: with and without running the
    // saturation engine, evidence, DP scores, forests, and forced spans are
    // identical, because tree evidence is built from exact raw surface
    // contexts only.
    const auto dataset = scf::generate_dataset("nested_balanced", 0.4, 1);
    const auto corpus = corpus_from_dataset(dataset);
    const auto with_saturation = scf::analyze_corpus(corpus);  // runs saturate()
    const scf::EvidenceBuilder without_solver(corpus);
    const auto analyses = scf::analyze_sentences(corpus, without_solver.span_evidence());
    CHECK(scf::raw_witness_relation_hash(corpus, with_saturation.builder) ==
          scf::raw_witness_relation_hash(corpus, without_solver));
    CHECK(with_saturation.analyses.size() == analyses.size());
    for (std::size_t sentence = 0; sentence < analyses.size(); ++sentence) {
        const auto& a = with_saturation.analyses[sentence];
        const auto& b = analyses[sentence];
        CHECK(a.best_score == b.best_score);
        CHECK(a.optimal_tree_count == b.optimal_tree_count);
        CHECK(a.forced_spans == b.forced_spans);
        CHECK(a.optimal_splits == b.optimal_splits);
    }
}

void test_span_length_support_theory() {
    // Full Cartesian with |X_k| = q: support(i,j) = q^(n - (j - i)).
    for (const std::size_t q : {std::size_t{2}, std::size_t{3}}) {
        const auto dataset = scf::generate_dataset("nested_balanced", 1.0, 1, 0, q);
        const auto corpus = corpus_from_dataset(dataset);
        const scf::EvidenceBuilder builder(corpus);
        const auto lengths = scf::corpus_sentence_lengths(corpus);
        const auto gold = scf::dataset_gold_trees(dataset);
        const auto stats = scf::score_by_span_length(lengths, builder.span_evidence(), gold);
        CHECK(stats.size() == 2);
        CHECK(stats[0].span_length == 2);
        CHECK(stats[0].max_score == static_cast<std::uint64_t>(q * q));
        CHECK(stats[1].span_length == 3);
        CHECK(stats[1].max_score == static_cast<std::uint64_t>(q));
        CHECK(stats[0].mean_score > stats[1].mean_score);
    }
}

void test_tree_shape_scores_balance_preference() {
    const auto dataset = scf::generate_dataset("nested_balanced", 1.0, 1);
    const auto corpus = corpus_from_dataset(dataset);
    const scf::EvidenceBuilder builder(corpus);
    const auto lengths = scf::corpus_sentence_lengths(corpus);
    const auto rows = scf::tree_shape_scores(lengths, builder.span_evidence());
    CHECK(rows.size() == 16);
    for (const auto& row : rows) {
        CHECK(row.balanced_score == 8);
        CHECK(row.left_score == 6);
        CHECK(row.right_score == 6);
        CHECK(row.left_score == row.right_score);  // no directional bias
        CHECK(row.best_shape == "balanced");       // objective-induced balance preference
    }
}

void test_summary_row_new_columns() {
    const auto header = scf::summary_csv_header();
    for (const char* column :
         {"requested_coverage", "effective_coverage", "lexical_cardinality",
          "symmetry_breaking_rate", "surface_language_hash", "sampled_corpus_hash",
          "raw_context_relation_hash", "raw_witness_relation_hash"}) {
        CHECK(header.find(column) != std::string::npos);
    }
    scf::RunInfo info;
    info.full_sentence_count = 16;
    info.sampled_sentence_count = 4;
    CHECK(info.effective_coverage() == std::optional<double>(0.25));
    CHECK(!scf::RunInfo{}.effective_coverage().has_value());
}

// --- v1.3: evidence objective laboratory -----------------------------------

const scf::SpanEvidence* find_span_evidence(const scf::EvidenceBuilder& builder,
                                            const scf::Span span) {
    for (const auto& item : builder.span_evidence()) {
        if (item.span == span) {
            return &item;
        }
    }
    return nullptr;
}

void test_evidence_score_formulas() {
    // C(x) = {(a,b), (c,d)}, C(y) = {(a,b)}, W(x,y) = {(a,b)}.
    // raw = 1; conditional = (1/2 + 1/1)/2 = 0.75; jaccard = 1/(2+1-1) = 0.5;
    // opportunity at geometry (1,1): U_g = {(a,b), (c,d)} => 1/2.
    const auto corpus = load_corpus("a x b\na y b\nc x d\n");
    const scf::Span span{0, 1, 2};  // "x" in "a x b"
    const auto check = [&](const scf::EvidenceObjective objective, const double strength,
                           const std::uint64_t score) {
        const scf::EvidenceBuilder builder(corpus, objective);
        const auto* item = find_span_evidence(builder, span);
        CHECK(item != nullptr);
        CHECK(std::abs(item->strength - strength) < 1e-9);
        CHECK(item->score == score);
        CHECK(item->confidence == 1);  // |W(x, y)| = 1 under every objective
    };
    check(scf::EvidenceObjective::RawCount, 1.0, 1);
    check(scf::EvidenceObjective::SymmetricConditional, 0.75, 750000000000ULL);
    check(scf::EvidenceObjective::Jaccard, 0.5, 500000000000ULL);
    check(scf::EvidenceObjective::OpportunityNormalized, 0.5, 500000000000ULL);
}

void test_evidence_symmetry_and_ranges() {
    // C(u) = C(v) => conditional = jaccard = 1.
    const auto twin = load_corpus("a x b\na y b\n");
    for (const auto objective : {scf::EvidenceObjective::SymmetricConditional,
                                 scf::EvidenceObjective::Jaccard}) {
        const scf::EvidenceBuilder builder(twin, objective);
        const auto* item = find_span_evidence(builder, {0, 1, 2});
        CHECK(item != nullptr);
        CHECK(item->strength == 1.0);
    }
    // All normalized strengths live in [0, 1] on a nontrivial corpus.
    const auto dataset = scf::generate_dataset("hierarchical_correlated_right", 1.0, 1);
    const auto corpus = corpus_from_dataset(dataset);
    for (const auto objective :
         {scf::EvidenceObjective::OpportunityNormalized,
          scf::EvidenceObjective::SymmetricConditional, scf::EvidenceObjective::Jaccard}) {
        const scf::EvidenceBuilder builder(corpus, objective);
        CHECK(!builder.pair_scores().empty());
        for (const auto& row : builder.pair_scores()) {
            CHECK(row.strength >= 0.0 && row.strength <= 1.0);
        }
        for (const auto& item : builder.span_evidence()) {
            CHECK(item.strength >= 0.0 && item.strength <= 1.0);
        }
    }
}

void test_cartesian_bias_regression_per_objective() {
    const auto dataset = scf::generate_dataset("nested_balanced", 1.0, 1);
    const auto corpus = corpus_from_dataset(dataset);
    const auto lengths = scf::corpus_sentence_lengths(corpus);
    {
        // raw_count must keep reproducing the opportunity-induced preference.
        const scf::EvidenceBuilder builder(corpus, scf::EvidenceObjective::RawCount);
        for (const auto& row : scf::tree_shape_scores(lengths, builder.span_evidence())) {
            CHECK(row.balanced_score > row.left_score);
            CHECK(row.left_score == row.right_score);
        }
    }
    for (const auto objective :
         {scf::EvidenceObjective::OpportunityNormalized,
          scf::EvidenceObjective::SymmetricConditional, scf::EvidenceObjective::Jaccard}) {
        // Every normalized objective removes the purely opportunity-induced
        // balance preference on the full Cartesian corpus...
        const scf::EvidenceBuilder builder(corpus, objective);
        for (const auto& row : scf::tree_shape_scores(lengths, builder.span_evidence())) {
            CHECK(row.balanced_score == row.left_score);
            CHECK(row.left_score == row.right_score);
        }
        // ...and the resulting exact integer tie keeps the full Catalan
        // argmax: no hidden floating-point tie-break.
        const auto analyses = scf::analyze_sentences(corpus, builder.span_evidence());
        for (const auto& analysis : analyses) {
            CHECK(analysis.optimal_tree_count == 5);
        }
    }
}

void test_forced_span_metrics() {
    // Correlated right chain, full coverage: the observable block-level split
    // [1,4) is forced in every optimal tree; the frozen block's internal
    // bracket is not demanded by the observable gold.
    const auto dataset = scf::generate_dataset("hierarchical_correlated_right", 1.0, 1);
    const auto corpus = corpus_from_dataset(dataset);
    const scf::EvidenceBuilder builder(corpus);
    const auto analyses = scf::analyze_sentences(corpus, builder.span_evidence());
    const auto gold = scf::dataset_gold_trees(dataset);
    const auto observable = scf::dataset_observable_gold(dataset);
    CHECK(observable.front() == std::set<scf::SpanPair>({{1, 4}}));
    const auto evaluation =
        scf::evaluate_corpus(analyses, gold, builder.span_evidence(), {}, observable);
    CHECK(evaluation.forced_precision_observable_gold == 1.0);
    CHECK(evaluation.forced_recall_observable_gold == 1.0);
    CHECK(evaluation.forced_precision_full_gold == 1.0);   // forced spans are gold spans
    CHECK(evaluation.forced_recall_full_gold == 0.5);      // inner bracket not forced
    for (const auto& sentence : evaluation.sentences) {
        CHECK(sentence.forced_spans == std::set<scf::SpanPair>({{1, 4}}));
    }
    // Balanced correlated: everything forced and recovered.
    const auto balanced = evaluate_grammar("hierarchical_correlated_balanced", 1.0, 1);
    CHECK(balanced.forced_precision_full_gold == 1.0);
    CHECK(balanced.forced_recall_full_gold == 1.0);
}

void test_strength_confidence_separation() {
    // simple_np_vp full coverage: the [0,2) evidence has confidence 2 under
    // every objective, while strength depends on the objective; ranking uses
    // strength (via the fixed-point score) and never confidence.
    const auto dataset = scf::generate_dataset("simple_np_vp", 1.0, 1);
    const auto corpus = corpus_from_dataset(dataset);
    const scf::EvidenceBuilder raw(corpus, scf::EvidenceObjective::RawCount);
    const scf::EvidenceBuilder opportunity(corpus,
                                           scf::EvidenceObjective::OpportunityNormalized);
    const auto* raw_item = find_span_evidence(raw, {0, 0, 2});
    const auto* opp_item = find_span_evidence(opportunity, {0, 0, 2});
    CHECK(raw_item != nullptr && opp_item != nullptr);
    CHECK(raw_item->confidence == 2 && opp_item->confidence == 2);
    CHECK(raw_item->strength == 2.0);
    CHECK(opp_item->strength == 1.0);  // 2 shared / 2 opportunity contexts
    CHECK(opp_item->score == 1000000000000ULL);
    CHECK(raw.summary().mean_pair_confidence == opportunity.summary().mean_pair_confidence);
}

// --- v1.4: context-indexed equivalence -------------------------------------

scf::ContextIndexedSolver solve_indexed(
    const scf::Corpus& corpus,
    const scf::AbstractionSignature signature = scf::AbstractionSignature::ContextOnly) {
    scf::ContextIndexedSolver solver(corpus, signature);
    solver.run();
    return solver;
}

void test_cross_context_bridge() {
    // u ~_{c1} v (context a_b) and v ~_{c2} w (context c_d) with c1 != c2:
    // the bridge must NOT merge u and w, locally or via abstraction classes.
    const auto corpus = load_corpus("a u b\na v b\nc v d\nc w d\n");
    const auto solver = solve_indexed(corpus);
    const auto u = string_id(corpus, "u");
    const auto v = string_id(corpus, "v");
    const auto w = string_id(corpus, "w");
    const auto c1 = *solver.final_key_for(string_id(corpus, "a"), string_id(corpus, "b"));
    const auto c2 = *solver.final_key_for(string_id(corpus, "c"), string_id(corpus, "d"));
    CHECK(!(c1 == c2));
    CHECK(solver.locally_related(u, v, c1));
    CHECK(solver.locally_related(v, w, c2));
    CHECK(!solver.locally_related(u, w, c1));
    CHECK(!solver.locally_related(u, w, c2));
    CHECK(!solver.locally_related_any(u, w));
    CHECK(solver.abstraction_class(u) != solver.abstraction_class(w));
    // v sits in both blocks; u and v have different profiles, hence
    // different ContextAbstractionClasses even though locally related.
    CHECK(solver.abstraction_class(u) != solver.abstraction_class(v));
    // The diagnostic projection graph DOES connect u-v-w; that is exactly the
    // object that must never be mistaken for a v1.4 equivalence class.
    const auto diagnostics = solver.diagnostics();
    CHECK(diagnostics.projected_giant_component_size >= 3);
    // Legacy global DSU (baseline) merges u, v, w through the bridge.
    const auto legacy = solve(corpus);
    CHECK(legacy.equivalent(u, w));
}

void test_ambiguous_surface_roles_multirole() {
    // Token x fills the N role and the V role. It must belong to several
    // LocalRoleBlocks without any split and without bridging n* and v* into
    // one class.
    const auto dataset = scf::generate_dataset("ambiguous_surface_roles", 1.0, 1);
    CHECK(dataset.full_sentence_count == 2 * 3 * 3);
    const auto corpus = corpus_from_dataset(dataset);
    const auto solver = solve_indexed(corpus);
    const auto x = string_id(corpus, "x");
    const auto n1 = string_id(corpus, "n1");
    const auto v1 = string_id(corpus, "v1");
    CHECK(solver.keys_of_yield(x).size() >= 2);
    bool with_n = false;
    bool with_v = false;
    for (const auto& key : solver.keys_of_yield(x)) {
        with_n |= solver.locally_related(x, n1, key);
        with_v |= solver.locally_related(x, v1, key);
        CHECK(!solver.locally_related(n1, v1, key));
    }
    CHECK(with_n && with_v);
    CHECK(!solver.locally_related_any(n1, v1));
    CHECK(solver.abstraction_class(n1) != solver.abstraction_class(v1));
}

void test_recursive_cascade_rounds() {
    const auto dataset = scf::generate_dataset("recursive_context_cascade", 1.0, 1);
    CHECK(dataset.full_sentence_count == 2);  // "w a m" / "w b m"
    const auto corpus = corpus_from_dataset(dataset);
    const auto a = string_id(corpus, "a");
    const auto b = string_id(corpus, "b");
    const auto am = string_id(corpus, "a m");
    const auto bm = string_id(corpus, "b m");
    {
        // context_only: the exact complete-profile operator saturates in ONE
        // productive round (idempotence — see the one-round theorem note).
        const auto solver = solve_indexed(corpus, scf::AbstractionSignature::ContextOnly);
        CHECK(solver.round_count() == 1);
        CHECK(solver.abstraction_class(a) == solver.abstraction_class(b));
        CHECK(solver.abstraction_class(am) == solver.abstraction_class(bm));
        CHECK(!solver.monotonicity_violated());
    }
    {
        // context_plus_concat: decomposition signatures delay the merges into
        // a genuine multi-round cascade: round 1 merges a~b, round 2 merges
        // "a m"~"b m" (their D-signature only matches after a~b), round 3
        // merges the full sentences.
        const auto solver =
            solve_indexed(corpus, scf::AbstractionSignature::ContextPlusConcat);
        CHECK(solver.round_count() >= 2);
        CHECK(solver.abstraction_class(a) == solver.abstraction_class(b));
        CHECK(solver.abstraction_class(am) == solver.abstraction_class(bm));
        CHECK(!solver.monotonicity_violated());
        // Genuine new merges strictly after round 1:
        std::size_t late_merges = 0;
        for (const auto& stats : solver.round_stats()) {
            if (stats.round >= 2) {
                late_merges += stats.new_context_class_merges;
            }
        }
        CHECK(late_merges > 0);
    }
}

void test_epsilon_stays_singleton() {
    const auto corpus = load_corpus("a\nb\na b\n");
    const auto solver = solve_indexed(corpus);
    const auto epsilon = corpus.string_interner().epsilon_id();
    for (scf::StringId s = 0; s < corpus.string_interner().size(); ++s) {
        if (s != epsilon) {
            CHECK(solver.abstraction_class(s) != solver.abstraction_class(epsilon));
        }
    }
}

// Naive reference implementation (spec §37): sets and maps all the way down.
struct NaiveIndexedResult {
    std::vector<std::set<scf::StringId>> classes;      // canonical partition
    std::set<std::pair<std::set<scf::StringId>, std::set<scf::StringId>>> blocks_by_content;
    std::size_t rounds{};
};

NaiveIndexedResult naive_context_indexed(const scf::Corpus& corpus,
                                         const scf::AbstractionSignature signature) {
    const auto epsilon = corpus.string_interner().epsilon_id();
    const auto count = corpus.string_interner().size();
    std::vector<std::size_t> label(count);
    for (scf::StringId s = 0; s < count; ++s) {
        label[s] = s;
    }
    std::size_t rounds = 0;
    while (true) {
        using Key = std::pair<std::size_t, std::size_t>;
        std::map<scf::StringId, std::set<Key>> profile;
        std::map<scf::StringId, std::set<Key>> decomposition;
        for (const auto& record : corpus.context_records()) {
            profile[record.triple.yield].insert(
                {label[record.triple.left], label[record.triple.right]});
        }
        if (signature == scf::AbstractionSignature::ContextPlusConcat) {
            for (const auto& triple : corpus.concat_triples()) {
                decomposition[triple.result].insert({label[triple.left], label[triple.right]});
            }
        }
        std::map<std::pair<std::set<Key>, std::set<Key>>, std::size_t> groups;
        std::vector<std::size_t> next(count);
        for (scf::StringId s = 0; s < count; ++s) {
            if (s == epsilon) {
                next[s] = count + 1;  // reserved singleton label
                continue;
            }
            const auto signature_value = std::pair(profile[s], decomposition[s]);
            const auto entry = groups.emplace(signature_value, groups.size());
            next[s] = entry.first->second;
        }
        // partition-content comparison
        std::map<std::size_t, std::set<scf::StringId>> old_parts, new_parts;
        for (scf::StringId s = 0; s < count; ++s) {
            old_parts[label[s]].insert(s);
            new_parts[next[s]].insert(s);
        }
        std::set<std::set<scf::StringId>> old_set, new_set;
        for (const auto& [key, members] : old_parts) old_set.insert(members);
        for (const auto& [key, members] : new_parts) new_set.insert(members);
        if (old_set == new_set) {
            NaiveIndexedResult result;
            result.rounds = rounds;
            for (const auto& members : new_set) result.classes.push_back(members);
            std::map<Key, std::set<scf::StringId>> blocks;
            for (const auto& record : corpus.context_records()) {
                blocks[{label[record.triple.left], label[record.triple.right]}].insert(
                    record.triple.yield);
            }
            // canonicalize block identity by the *content* of the context
            // classes, immune to label numbering
            for (const auto& [key, yields] : blocks) {
                std::set<scf::StringId> content_key;
                for (scf::StringId s = 0; s < count; ++s) {
                    if (label[s] == key.first) content_key.insert(s);
                    if (label[s] == key.second) content_key.insert(s + count);  // offset right side
                }
                result.blocks_by_content.insert({content_key, yields});
            }
            return result;
        }
        label = next;
        ++rounds;
    }
}

void test_naive_reference_agreement() {
    // 100 random small corpora: optimized fixed point == naive fixed point
    // (partition, blocks, round count) modulo canonical ID renaming.
    std::mt19937_64 engine(12345);
    const std::vector<std::string> alphabet{"a", "b", "c", "d", "e", "f"};
    for (int trial = 0; trial < 100; ++trial) {
        std::ostringstream text;
        const auto sentences = 2 + engine() % 6;
        for (std::size_t sentence = 0; sentence < sentences; ++sentence) {
            const auto length = 1 + engine() % 4;
            for (std::size_t token = 0; token < length; ++token) {
                text << (token == 0 ? "" : " ") << alphabet[engine() % alphabet.size()];
            }
            text << '\n';
        }
        std::istringstream input(text.str());
        scf::Corpus corpus;
        corpus.load(input);
        for (const auto signature : {scf::AbstractionSignature::ContextOnly,
                                     scf::AbstractionSignature::ContextPlusConcat}) {
            const auto solver = solve_indexed(corpus, signature);
            const auto naive = naive_context_indexed(corpus, signature);
            CHECK(solver.round_count() == naive.rounds);
            CHECK(!solver.monotonicity_violated());
            // partitions agree as content
            std::map<scf::ContextClassId, std::set<scf::StringId>> optimized_parts;
            for (scf::StringId s = 0; s < corpus.string_interner().size(); ++s) {
                optimized_parts[solver.abstraction_class(s)].insert(s);
            }
            std::set<std::set<scf::StringId>> optimized_set;
            for (const auto& [key, members] : optimized_parts) optimized_set.insert(members);
            std::set<std::set<scf::StringId>> naive_set(naive.classes.begin(),
                                                        naive.classes.end());
            CHECK(optimized_set == naive_set);
            // context_only: the one-round idempotence theorem, empirically —
            // the exact complete-profile operator never needs a second
            // productive round.
            if (signature == scf::AbstractionSignature::ContextOnly) {
                CHECK(solver.round_count() <= 1);
            }
            // Relation-invariance theorem: abstraction merges context classes
            // but never creates a new locally-related yield pair — the
            // distinct-pair set equals the exact round-0 one in every
            // signature.
            std::set<std::pair<scf::StringId, scf::StringId>> round0_pairs, final_pairs;
            {
                std::map<std::pair<scf::StringId, scf::StringId>, std::set<scf::StringId>>
                    exact_blocks;
                for (const auto& record : corpus.context_records()) {
                    exact_blocks[{record.triple.left, record.triple.right}].insert(
                        record.triple.yield);
                }
                for (const auto& [key, yields] : exact_blocks) {
                    for (auto a = yields.begin(); a != yields.end(); ++a) {
                        for (auto b = std::next(a); b != yields.end(); ++b) {
                            round0_pairs.emplace(*a, *b);
                        }
                    }
                }
            }
            for (const auto& block : solver.blocks()) {
                for (std::size_t a = 0; a < block.yields.size(); ++a) {
                    for (std::size_t b = a + 1; b < block.yields.size(); ++b) {
                        final_pairs.emplace(block.yields[a], block.yields[b]);
                    }
                }
            }
            CHECK(round0_pairs == final_pairs);
        }
    }
}

void test_indexed_determinism_hashes() {
    const auto dataset = scf::generate_dataset("hierarchical_correlated_right", 1.0, 1);
    const auto corpus_a = corpus_from_dataset(dataset);
    const auto corpus_b = corpus_from_dataset(dataset);
    const auto first = solve_indexed(corpus_a);
    const auto second = solve_indexed(corpus_b);
    CHECK(first.context_partition_hash() == second.context_partition_hash());
    CHECK(first.local_relation_hash() == second.local_relation_hash());
    CHECK(first.round_trace_hash() == second.round_trace_hash());
}

void test_indexed_shadow_symmetric_honesty() {
    // Shadow parser must keep symmetric_abc fully ambiguous at full coverage.
    const auto dataset = scf::generate_dataset("symmetric_abc", 1.0, 1);
    const auto corpus = corpus_from_dataset(dataset);
    const auto solver = solve_indexed(corpus);
    const auto evidence = scf::indexed_shadow_evidence(corpus, solver);
    const auto analyses = scf::analyze_sentences(corpus, evidence);
    const auto gold = scf::dataset_gold_trees(dataset);
    const auto evaluation = scf::evaluate_corpus(analyses, gold, evidence);
    CHECK(evaluation.gold_in_argmax_rate == 1.0);
    CHECK(evaluation.unique_optimal_rate == 0.0);
    CHECK(evaluation.mean_argmax_size == 2.0);
}

void test_prepare_text() {
    std::istringstream input(
        "Hello World. This is a TEST, with punctuation! And a very very very very very very "
        "very very very long sentence here now.\n"
        "Hello World.\n"
        "numbers 123 here.\n");
    scf::PrepareTextConfig config;
    config.drop_digits = true;
    const auto result = scf::prepare_text(input, config);
    CHECK(result.input_sentence_count == 5);
    CHECK(result.kept_sentence_count == 2);
    CHECK(result.filtered_long == 1);
    CHECK(result.filtered_symbols == 1);
    CHECK(result.duplicate_sentences == 1);
    CHECK(result.sentences.front() == "hello world");
    CHECK(result.sentences.back() == "this is a test with punctuation");
    CHECK(result.distinct_tokens == 8);
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"interner", test_interner},
        {"dsu", test_dsu},
        {"corpus_records_and_deduplication", test_corpus_records_and_deduplication},
        {"basic_context_substitution", test_basic_context_substitution},
        {"raw_witnesses_and_simple_regression", test_raw_witnesses_and_simple_regression},
        {"duplicate_occurrences_do_not_increase_support",
         test_duplicate_occurrences_do_not_increase_support},
        {"derived_equality_is_not_a_direct_witness", test_derived_equality_is_not_a_direct_witness},
        {"whole_sentence_context_is_excluded", test_whole_sentence_context_is_excluded},
        {"concat_congruence", test_concat_congruence},
        {"multiround_fixed_point_cascade", test_multiround_fixed_point_cascade},
        {"observed_universe_does_not_grow", test_observed_universe_does_not_grow},
        {"crossing_and_splits", test_crossing_and_splits},
        {"tree_count", test_tree_count},
        {"maximum_evidence_tree_optimization", test_maximum_evidence_tree_optimization},
        {"forced_spans_against_brute_force", test_forced_spans_against_brute_force},
        {"forced_optimal_spans_against_brute_force",
         test_forced_optimal_spans_against_brute_force},
        {"synthetic_cartesian_grammar", test_synthetic_cartesian_grammar},
        {"deep_synthetic_end_to_end", test_deep_synthetic_end_to_end},
        {"gold_tree_infrastructure", test_gold_tree_infrastructure},
        {"gold_spans_tsv_roundtrip", test_gold_spans_tsv_roundtrip},
        {"generator_language_counts", test_generator_language_counts},
        {"generator_gold_consistency", test_generator_gold_consistency},
        {"coverage_sampling", test_coverage_sampling},
        {"evaluator_handcrafted_sentences", test_evaluator_handcrafted_sentences},
        {"brute_force_enumerator", test_brute_force_enumerator},
        {"no_hidden_tie_break", test_no_hidden_tie_break},
        {"integration_simple_np_vp", test_integration_simple_np_vp},
        {"integration_ab_cartesian", test_integration_ab_cartesian},
        {"integration_symmetric_abc", test_integration_symmetric_abc},
        {"integration_nested_balanced", test_integration_nested_balanced},
        {"integration_branching_grammars_are_symmetric",
         test_integration_branching_grammars_are_symmetric},
        {"integration_ambiguous_lexicon_diagnostics",
         test_integration_ambiguous_lexicon_diagnostics},
        {"integration_ccg_lite_pipeline", test_integration_ccg_lite_pipeline},
        {"metrics_deterministic", test_metrics_deterministic},
        {"prepare_text", test_prepare_text},
        {"observational_equivalence_hashes", test_observational_equivalence_hashes},
        {"hierarchical_families_break_observational_equivalence",
         test_hierarchical_families_break_observational_equivalence},
        {"lexical_cardinality", test_lexical_cardinality},
        {"symmetry_breaking_rate", test_symmetry_breaking_rate},
        {"hierarchical_identifiability", test_hierarchical_identifiability},
        {"saturation_is_decoupled_from_parsing", test_saturation_is_decoupled_from_parsing},
        {"span_length_support_theory", test_span_length_support_theory},
        {"tree_shape_scores_balance_preference", test_tree_shape_scores_balance_preference},
        {"summary_row_new_columns", test_summary_row_new_columns},
        {"evidence_score_formulas", test_evidence_score_formulas},
        {"evidence_symmetry_and_ranges", test_evidence_symmetry_and_ranges},
        {"cartesian_bias_regression_per_objective",
         test_cartesian_bias_regression_per_objective},
        {"forced_span_metrics", test_forced_span_metrics},
        {"strength_confidence_separation", test_strength_confidence_separation},
        {"cross_context_bridge", test_cross_context_bridge},
        {"ambiguous_surface_roles_multirole", test_ambiguous_surface_roles_multirole},
        {"recursive_cascade_rounds", test_recursive_cascade_rounds},
        {"epsilon_stays_singleton", test_epsilon_stays_singleton},
        {"naive_reference_agreement", test_naive_reference_agreement},
        {"indexed_determinism_hashes", test_indexed_determinism_hashes},
        {"indexed_shadow_symmetric_honesty", test_indexed_shadow_symmetric_honesty},
    };

    std::size_t failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << tests.size() - failures << '/' << tests.size() << " tests passed\n";
    return failures == 0 ? 0 : 1;
}
