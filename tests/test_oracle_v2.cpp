// Regression tests for the SCF v2.0 Oracle Category Recovery module.
//
// The suite is deliberately independent of the v1.x tests: it links only
// against scf_oracle_v2 and uses small parameterizations so that every check
// runs in well under a second.

#include "scf/oracle_v2.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
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

using namespace scf::v2;

std::vector<std::uint8_t> tokens_of(const OracleGrammar& grammar, const std::string& text) {
    std::vector<std::uint8_t> tokens;
    std::istringstream stream(text);
    std::string word;
    while (stream >> word) {
        const auto found =
            std::find(grammar.vocabulary.begin(), grammar.vocabulary.end(), word);
        if (found == grammar.vocabulary.end()) {
            throw TestFailure("unknown token " + word);
        }
        tokens.push_back(static_cast<std::uint8_t>(found - grammar.vocabulary.begin()));
    }
    return tokens;
}

std::uint32_t class_of(const Partition& partition,
                       const StringSpace& universe,
                       const OracleGrammar& grammar,
                       const std::string& text) {
    const auto tokens = tokens_of(grammar, text);
    return partition.class_of[universe.index(tokens)];
}

bool near(const double a, const double b) { return std::abs(a - b) < 1e-9; }

void test_string_space() {
    const auto space = StringSpace::make(3, 4);
    CHECK(space.size() == 3 + 9 + 27 + 81);
    CHECK(space.start[1] == 0);
    CHECK(space.start[2] == 3);
    CHECK(space.start[5] == space.size());
    for (std::size_t i = 0; i < space.size(); ++i) {
        const auto tokens = space.decode(i);
        CHECK(space.index(tokens) == i);
        CHECK(tokens.size() == space.length_of(i));
    }
    // Ordering: length first, then lexicographic by token id.
    const std::vector<std::uint8_t> ab{0, 1};
    const std::vector<std::uint8_t> ba{1, 0};
    CHECK(space.index(std::span<const std::uint8_t>(ab)) <
          space.index(std::span<const std::uint8_t>(ba)));
}

void test_parser_and_table_agree() {
    for (const auto& name : oracle_grammar_names()) {
        const auto grammar = make_oracle_grammar(name);
        const OracleParser parser(grammar);
        const CategoryTable table(grammar, 5);
        for (std::size_t i = 0; i < table.space().size(); ++i) {
            const auto tokens = table.space().decode(i);
            CHECK(table.at(i) == parser.categories(tokens));
        }
    }
    const auto grammar = make_oracle_grammar("simple_np_vp");
    const OracleParser parser(grammar);
    CHECK(parser.accept(tokens_of(grammar, "the dog sleeps")));
    CHECK(parser.accept(tokens_of(grammar, "the cat runs")));
    CHECK(!parser.accept(tokens_of(grammar, "dog the sleeps")));
    CHECK(!parser.accept(tokens_of(grammar, "the dog")));
    CHECK(parser.categories(tokens_of(grammar, "the dog")) == (CategoryMask{1} << 2));  // {NP}
    CHECK(parser.categories(tokens_of(grammar, "dog sleeps")) == 0);
    const auto recursive = make_oracle_grammar("recursive_modifier");
    const OracleParser recursive_parser(recursive);
    CHECK(recursive_parser.accept(tokens_of(recursive, "the big red dog sleeps")));
    CHECK(recursive_parser.accept(tokens_of(recursive, "the big big dog sleeps")));
    CHECK(!recursive_parser.accept(tokens_of(recursive, "the dog big sleeps")));
}

// Literal reimplementation of the spec: Sig_k(u) as an explicit set of
// triples (L, R, Accept(L u R)), partitioned by exact set equality.
void test_naive_signature_reference() {
    const auto grammar = make_oracle_grammar("observationally_equivalent_categories");
    const OracleParser parser(grammar);
    const std::size_t max_len = 3;
    const std::size_t max_k = 2;
    const std::size_t vocab = grammar.vocabulary.size();

    const auto enumerate = [vocab](const std::size_t len) {
        std::vector<std::vector<std::uint8_t>> out;
        if (len == 0) {
            out.emplace_back();
            return out;
        }
        std::vector<std::uint8_t> digits(len, 0);
        const auto total = static_cast<std::size_t>(std::pow(vocab, len));
        for (std::size_t i = 0; i < total; ++i) {
            out.push_back(digits);
            for (std::size_t d = len; d-- > 0;) {
                if (++digits[d] < vocab) {
                    break;
                }
                digits[d] = 0;
            }
        }
        return out;
    };

    std::vector<std::vector<std::uint8_t>> universe;
    for (std::size_t len = 1; len <= max_len; ++len) {
        for (auto& item : enumerate(len)) {
            universe.push_back(std::move(item));
        }
    }
    for (std::size_t k = 0; k <= max_k; ++k) {
        std::map<std::vector<bool>, std::uint32_t> ids;
        std::vector<std::uint32_t> naive(universe.size());
        std::vector<std::uint32_t> order;
        for (std::size_t u = 0; u < universe.size(); ++u) {
            std::vector<bool> signature;
            for (std::size_t w = 0; w <= k; ++w) {
                for (std::size_t llen = 0; llen <= w; ++llen) {
                    const std::size_t rlen = w - llen;
                    for (const auto& left : enumerate(llen)) {
                        for (const auto& right : enumerate(rlen)) {
                            std::vector<std::uint8_t> whole = left;
                            whole.insert(whole.end(), universe[u].begin(), universe[u].end());
                            whole.insert(whole.end(), right.begin(), right.end());
                            signature.push_back(parser.accept(whole));
                        }
                    }
                }
            }
            const auto found = ids.find(signature);
            if (found != ids.end()) {
                naive[u] = found->second;
            } else {
                const auto id = static_cast<std::uint32_t>(ids.size());
                ids.emplace(signature, id);
                naive[u] = id;
            }
        }

        const CategoryTable table(grammar, max_len + max_k);
        const auto hits = compute_signature_hits(table, max_len, max_k);
        const auto parts = refine_partitions(hits, {});
        CHECK(parts[k].class_of == naive);
        CHECK(parts[k].num_classes == ids.size());
    }
}

void test_partition_metrics() {
    const std::vector<std::uint32_t> a{0, 0, 1};
    const std::vector<std::uint32_t> b{0, 1, 1};
    const auto metrics = compare_partitions(a, b);
    CHECK(metrics.merge_error_pairs == 1);
    CHECK(metrics.split_error_pairs == 1);
    CHECK(near(metrics.pairwise_precision, 0.0));
    CHECK(near(metrics.pairwise_recall, 0.0));
    CHECK(near(metrics.ari, -0.5));
    CHECK(metrics.nmi >= 0.0 && metrics.nmi < 1.0);

    const auto identical = compare_partitions(a, a);
    CHECK(near(identical.ari, 1.0));
    CHECK(near(identical.nmi, 1.0));
    CHECK(near(identical.pairwise_precision, 1.0));
    CHECK(near(identical.pairwise_recall, 1.0));
    CHECK(identical.merge_error_pairs == 0);
    CHECK(identical.split_error_pairs == 0);

    const std::vector<std::uint32_t> single_a{0, 0, 0};
    const auto trivial = compare_partitions(single_a, single_a);
    CHECK(near(trivial.ari, 1.0));
    CHECK(near(trivial.nmi, 1.0));
}

void test_simple_np_vp_recovery() {
    const auto grammar = make_oracle_grammar("simple_np_vp");
    const std::size_t max_len = 4;
    const std::size_t max_k = 3;
    const CategoryTable table(grammar, max_len + max_k);
    const auto hits = compute_signature_hits(table, max_len, max_k);
    const auto parts = refine_partitions(hits, {});
    const auto gold = gold_labeling(table, max_len);

    // Constituent sub-universe: every gold category must be exactly recovered
    // from k = 2 on (the deepest accepting context a constituent needs here
    // has weight 2, e.g. D in "the | dog sleeps").
    std::vector<std::size_t> constituents;
    for (std::size_t i = 0; i < gold.mask_of.size(); ++i) {
        if (gold.mask_of[i] != 0) {
            constituents.push_back(i);
        }
    }
    for (std::size_t k = 2; k <= max_k; ++k) {
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::size_t> joint;
        std::map<std::uint32_t, std::size_t> learned_ids;
        std::map<std::uint32_t, std::size_t> gold_ids;
        for (const auto i : constituents) {
            ++joint[{parts[k].class_of[i], gold.class_of[i]}];
            ++learned_ids[parts[k].class_of[i]];
            ++gold_ids[gold.class_of[i]];
        }
        CHECK(joint.size() == learned_ids.size());
        CHECK(joint.size() == gold_ids.size());  // bijective: exact recovery
    }

    // Category identities.
    CHECK(class_of(parts[max_k], hits.universe, grammar, "dog") ==
          class_of(parts[max_k], hits.universe, grammar, "cat"));
    CHECK(class_of(parts[max_k], hits.universe, grammar, "sleeps") ==
          class_of(parts[max_k], hits.universe, grammar, "runs"));
    CHECK(class_of(parts[max_k], hits.universe, grammar, "the dog") ==
          class_of(parts[max_k], hits.universe, grammar, "the cat"));
    CHECK(class_of(parts[max_k], hits.universe, grammar, "the") !=
          class_of(parts[max_k], hits.universe, grammar, "dog"));
    CHECK(class_of(parts[max_k], hits.universe, grammar, "the dog sleeps") ==
          class_of(parts[max_k], hits.universe, grammar, "the cat runs"));
}

void test_observational_equivalence_flagged() {
    const auto grammar = make_oracle_grammar("observationally_equivalent_categories");
    const std::size_t max_len = 4;
    const std::size_t max_k = 3;
    const CategoryTable table(grammar, max_len + max_k);
    const auto hits = compute_signature_hits(table, max_len, max_k);
    const auto parts = refine_partitions(hits, {});
    const auto gold = gold_labeling(table, max_len);

    // dog (Nm) and cat (Nf) are distributionally identical: merged forever.
    CHECK(class_of(parts[max_k], hits.universe, grammar, "dog") ==
          class_of(parts[max_k], hits.universe, grammar, "cat"));

    std::vector<std::size_t> constituents;
    for (std::size_t i = 0; i < gold.mask_of.size(); ++i) {
        if (gold.mask_of[i] != 0) {
            constituents.push_back(i);
        }
    }
    std::vector<std::uint32_t> learned;
    std::vector<std::uint32_t> gold_projected;
    std::map<std::uint32_t, std::uint32_t> learned_renumber;
    std::map<std::uint32_t, std::uint32_t> gold_renumber;
    for (const auto i : constituents) {
        learned.push_back(static_cast<std::uint32_t>(
            learned_renumber.try_emplace(parts[max_k].class_of[i],
                                         static_cast<std::uint32_t>(learned_renumber.size()))
                .first->second));
        gold_projected.push_back(static_cast<std::uint32_t>(
            gold_renumber.try_emplace(gold.class_of[i],
                                      static_cast<std::uint32_t>(gold_renumber.size()))
                .first->second));
    }
    const auto metrics = compare_partitions(learned, gold_projected);
    const auto obs = find_observationally_equivalent_gold_classes(learned, gold_projected);
    CHECK(obs.groups.size() == 1);
    CHECK(obs.groups[0].size() == 2);  // {Nm} ~ {Nf}
    CHECK(metrics.merge_error_pairs == obs.excluded_merge_pairs);
    CHECK(metrics.merge_error_pairs > 0);  // dog-cat pairs are merged...
    CHECK(metrics.merge_error_pairs - obs.excluded_merge_pairs == 0);  // ...but all flagged
    CHECK(metrics.split_error_pairs == 0);
}

void test_transitive_context_depth() {
    const auto grammar = make_oracle_grammar("transitive");
    const std::size_t max_len = 3;
    const std::size_t max_k = 4;
    const CategoryTable table(grammar, max_len + max_k);
    const auto hits = compute_signature_hits(table, max_len, max_k);
    const auto parts = refine_partitions(hits, {});

    // The shallowest accepting context of a transitive verb is
    // "the N _ the N" of weight 4, so up to k = 3 TV tokens are externally
    // indistinguishable from dead strings.
    for (std::size_t k = 0; k <= max_k; ++k) {
        CHECK(class_of(parts[k], hits.universe, grammar, "sees") ==
              class_of(parts[k], hits.universe, grammar, "likes"));
    }
    CHECK(class_of(parts[3], hits.universe, grammar, "sees") ==
          class_of(parts[3], hits.universe, grammar, "the the the"));
    CHECK(class_of(parts[4], hits.universe, grammar, "sees") !=
          class_of(parts[4], hits.universe, grammar, "the the the"));
    // Lexical VP "sleeps" and derived VP "sees the dog" belong together once
    // separated from the dead class.
    CHECK(class_of(parts[4], hits.universe, grammar, "sleeps") ==
          class_of(parts[4], hits.universe, grammar, "sees the dog"));
}

void test_composition_recovery_simple() {
    const auto grammar = make_oracle_grammar("simple_np_vp");
    const std::size_t max_len = 4;
    const std::size_t max_k = 3;
    const CategoryTable table(grammar, max_len + max_k);
    const auto hits = compute_signature_hits(table, max_len, max_k);
    const auto parts = refine_partitions(hits, {});

    const auto final_recovery = recover_composition(table, grammar, parts[max_k], max_len);
    CHECK(final_recovery.witnessable_gold_rules == 2);
    CHECK(final_recovery.recovered_gold_rules == 2);
    CHECK(near(final_recovery.composition_recall, 1.0));
    CHECK(near(final_recovery.composition_precision, 1.0));
    CHECK(final_recovery.congruence_violations == 0);
    CHECK(final_recovery.nonfunctional_input_pairs == 0);
    CHECK(final_recovery.functional_input_pairs > 0);

    // At k = 0 the two classes (accepted / everything else) cannot be a
    // concatenation congruence: "the dog" and "dog" share a class but behave
    // differently under "+ sleeps".
    const auto coarse = recover_composition(table, grammar, parts[0], max_len);
    CHECK(coarse.congruence_violations > 0);
    CHECK(coarse.first_violation_example.has_value());
}

void test_positive_only_full_coverage_matches_oracle() {
    for (const auto& name : oracle_grammar_names()) {
        const auto grammar = make_oracle_grammar(name);
        const std::size_t max_len = 3;
        const std::size_t max_k = 2;
        const CategoryTable table(grammar, max_len + max_k);
        const auto hits = compute_signature_hits(table, max_len, max_k);
        const auto oracle_parts = refine_partitions(hits, {});
        const auto sample = sample_positive(table, 1.0, 7);
        CHECK(sample.retained_count == sample.accepted_count);
        const auto positive_parts = refine_partitions(hits, sample.filter);
        for (std::size_t k = 0; k <= max_k; ++k) {
            CHECK(positive_parts[k] == oracle_parts[k]);
            CHECK(partition_hash(positive_parts[k]) == partition_hash(oracle_parts[k]));
        }
    }
}

void test_positive_only_determinism() {
    const auto grammar = make_oracle_grammar("simple_np_vp");
    const CategoryTable table(grammar, 5);
    const auto first = sample_positive(table, 0.4, 42);
    const auto second = sample_positive(table, 0.4, 42);
    CHECK(first.filter == second.filter);
    CHECK(first.retained_count == second.retained_count);
    CHECK(first.retained_count < first.accepted_count);
    CHECK(first.retained_count > 0);

    const auto hits = compute_signature_hits(table, 3, 2);
    const auto once = refine_partitions(hits, first.filter);
    const auto twice = refine_partitions(hits, second.filter);
    for (std::size_t k = 0; k < once.size(); ++k) {
        CHECK(partition_hash(once[k]) == partition_hash(twice[k]));
    }
}

// Pinned canonical partition hashes at (L = 3, K = 2). The hash covers the
// canonical class_of sequence via explicit little-endian FNV-1a, so any
// platform- or refactoring-induced drift in the learned partitions fails
// loudly here.
void test_pinned_partition_hashes() {
    const std::vector<std::pair<std::string, std::vector<std::string>>> expected{
        {"simple_np_vp",
         {"1a3903643cde6921", "0370210e86450e97", "c302bad5e33a1e34"}},
        {"transitive", {"2aa40882b2c80fd1", "7a4612cc700edbe7", "b937a497c3dedbc9"}},
        {"recursive_modifier",
         {"f15454d7ebd244a0", "af806140750bab47", "f273366ccc470113"}},
        {"observationally_equivalent_categories",
         {"e5c710237d1b1d11", "a5c21b813f9ca467", "a243402654523526"}},
    };
    for (const auto& [name, hashes] : expected) {
        const auto grammar = make_oracle_grammar(name);
        const CategoryTable table(grammar, 3 + 2);
        const auto hits = compute_signature_hits(table, 3, 2);
        const auto parts = refine_partitions(hits, {});
        CHECK(parts.size() == hashes.size());
        for (std::size_t k = 0; k < parts.size(); ++k) {
            CHECK(hash_hex(partition_hash(parts[k])) == hashes[k]);
        }
    }
}

void test_restriction_consistency() {
    const auto grammar = make_oracle_grammar("recursive_modifier");
    const std::size_t max_k = 2;
    const CategoryTable table(grammar, 4 + max_k);
    const auto large_hits = compute_signature_hits(table, 4, max_k);
    const auto large_parts = refine_partitions(large_hits, {});
    const auto small_hits = compute_signature_hits(table, 3, max_k);
    const auto small_parts = refine_partitions(small_hits, {});
    for (std::size_t k = 0; k <= max_k; ++k) {
        const auto restricted =
            restrict_partition(large_parts[k], small_hits.universe.size());
        CHECK(restricted == small_parts[k]);
    }
}

void test_experiment_writes_files() {
    OracleExperimentConfig config;
    config.grammars = {"observationally_equivalent_categories"};
    config.min_len = 2;
    config.max_len = 3;
    config.max_k = 2;
    config.coverages = {1.0};
    config.seeds = {1};
    const auto dir =
        std::filesystem::temp_directory_path() / "scf_oracle_v2_test_output";
    std::filesystem::remove_all(dir);
    run_oracle_experiment(config, dir);
    for (const auto* name : {"category_recovery.csv", "composition_recovery.csv",
                             "positive_only_recovery.csv", "oracle_summary.txt"}) {
        CHECK(std::filesystem::exists(dir / name));
    }
    std::ifstream category(dir / "category_recovery.csv");
    std::string line;
    std::size_t rows = 0;
    while (std::getline(category, line)) {
        ++rows;
    }
    CHECK(rows == 1 + 2 * 3 * 2);  // header + L in {2,3} x k in {0,1,2} x 2 scopes

    // Determinism: a second run reproduces byte-identical outputs.
    const auto read_all = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    };
    const auto first_category = read_all(dir / "category_recovery.csv");
    const auto first_positive = read_all(dir / "positive_only_recovery.csv");
    run_oracle_experiment(config, dir);
    CHECK(read_all(dir / "category_recovery.csv") == first_category);
    CHECK(read_all(dir / "positive_only_recovery.csv") == first_positive);
    std::filesystem::remove_all(dir);
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, void (*)()>> tests{
        {"string_space", test_string_space},
        {"parser_and_table_agree", test_parser_and_table_agree},
        {"naive_signature_reference", test_naive_signature_reference},
        {"partition_metrics", test_partition_metrics},
        {"simple_np_vp_recovery", test_simple_np_vp_recovery},
        {"observational_equivalence_flagged", test_observational_equivalence_flagged},
        {"transitive_context_depth", test_transitive_context_depth},
        {"composition_recovery_simple", test_composition_recovery_simple},
        {"positive_only_full_coverage_matches_oracle",
         test_positive_only_full_coverage_matches_oracle},
        {"positive_only_determinism", test_positive_only_determinism},
        {"pinned_partition_hashes", test_pinned_partition_hashes},
        {"restriction_consistency", test_restriction_consistency},
        {"experiment_writes_files", test_experiment_writes_files},
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
