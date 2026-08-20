#include "scf/corpus.hpp"
#include "scf/dsu.hpp"
#include "scf/equivalence_solver.hpp"
#include "scf/evidence.hpp"
#include "scf/evidence_builder.hpp"
#include "scf/formatter.hpp"
#include "scf/string_interner.hpp"
#include "scf/tree_solver.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <iostream>
#include <map>
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
