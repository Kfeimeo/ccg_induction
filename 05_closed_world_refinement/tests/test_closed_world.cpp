#include "scf/closed_world.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if (!(condition)) {                                                                 \
            throw Failure(std::string("CHECK failed: ") + #condition + " at line " +      \
                          std::to_string(__LINE__));                                         \
        }                                                                                   \
    } while (false)

using namespace scf::v24;

std::filesystem::path temp_dir(const std::string& name) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

ObjectId object(const ObservationTable& table, const std::string& text) {
    const auto found = table.find_object(text);
    if (!found) {
        throw Failure("missing object " + text);
    }
    return *found;
}

// The full closed-world relation as a set of (L, u, R) surface strings.
std::set<std::string> relation_text(const ObservationTable& table) {
    std::set<std::string> result;
    for (ObjectId object = 0; object < table.object_count(); ++object) {
        for (const ContextId context : table.contexts_of(object)) {
            result.insert(table.left_context_text(context) + " [" + table.object_text(object) +
                          "] " + table.right_context_text(context));
        }
    }
    return result;
}

// ---------------------------------------------------------------------------

void test_table_matches_v23_observation_records() {
    // Same sentences -> same objects and the same exact (L, u, R) relation as
    // the v2.3 observation table (which additionally builds witnesses).
    std::vector<std::string> text{"<unused>"};
    std::map<std::string, std::uint32_t> ids;
    std::vector<std::vector<std::uint32_t>> sentences;
    const auto add = [&](const std::string& line) {
        std::istringstream words(line);
        std::vector<std::uint32_t> sentence;
        std::string word;
        while (words >> word) {
            const auto [it, inserted] = ids.try_emplace(word, static_cast<std::uint32_t>(text.size()));
            if (inserted) {
                text.push_back(word);
            }
            sentence.push_back(it->second);
        }
        sentences.push_back(sentence);
    };
    add("the dog sleeps quietly");
    add("the cat sleeps");
    add("dog");
    add("a dog a dog a dog");   // repeated substrings inside one sentence
    add("the dog sleeps quietly");  // duplicate sentence: no new records
    const auto table = build_observation_table(sentences, text, sentences.size(), 3);
    const auto v23 = scf::v23::observe_sentences(sentences, text, sentences.size(), 3);
    CHECK(table.object_count() == v23.object_text.size());
    std::set<std::string> v23_relation;
    for (scf::v23::ObjectId u = 0; u < v23.object_text.size(); ++u) {
        for (const auto context : v23.contexts_of_object[u]) {
            v23_relation.insert(v23.left_context_text(context) + " [" + v23.object_text[u] + "] " +
                                v23.right_context_text(context));
        }
    }
    CHECK(relation_text(table) == v23_relation);
    CHECK(table.context_count() == v23.context_keys.size());
    CHECK(table.record_count() == v23_relation.size());
    CHECK(table.token_count == 4 + 3 + 1 + 6 + 4);
    // Object ids are assigned by first occurrence in both tables.
    for (scf::v23::ObjectId u = 0; u < v23.object_text.size(); ++u) {
        CHECK(table.object_text(u) == v23.object_text[u]);
    }
    // Frame types agree with the v2.3.1 classification of the epsilon root.
    for (ContextId c = 0; c < table.context_count(); ++c) {
        const bool left_empty = table.left_of(c).empty();
        const bool right_empty = table.right_of(c).empty();
        const auto expected = left_empty && right_empty ? FrameType::empty_frame
                              : left_empty              ? FrameType::left_boundary
                              : right_empty             ? FrameType::right_boundary
                                                        : FrameType::internal_frame;
        CHECK(table.context_frame[c] == expected);
    }
    // Membership is answered from the positive index only.
    const auto dog = object(table, "dog");
    const auto terminal = table.terminal_context();
    CHECK(terminal.has_value());
    CHECK(table.accepts(dog, *terminal));
    CHECK(!table.accepts(object(table, "cat"), *terminal));
    // Both CSR directions are sorted and consistent.
    for (ContextId c = 0; c < table.context_count(); ++c) {
        const auto objects = table.objects_of(c);
        CHECK(std::is_sorted(objects.begin(), objects.end()));
        for (const auto u : objects) {
            CHECK(table.accepts(u, c));
        }
    }
    for (ObjectId u = 0; u < table.object_count(); ++u) {
        const auto contexts = table.contexts_of(u);
        CHECK(std::is_sorted(contexts.begin(), contexts.end()));
    }
}

void test_universe_membership_and_parsing() {
    CHECK(parse_universe("all") == ContextUniverse::all_frames);
    CHECK(parse_universe("internal_only") == ContextUniverse::internal_only);
    CHECK(parse_universe("boundary") == ContextUniverse::boundary_frames);
    bool threw = false;
    try {
        parse_universe("frequency");
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
    for (const auto frame : {FrameType::empty_frame, FrameType::left_boundary,
                             FrameType::right_boundary, FrameType::internal_frame}) {
        CHECK(in_universe(frame, ContextUniverse::all_frames));
        CHECK(in_universe(frame, ContextUniverse::internal_only) ==
              (frame == FrameType::internal_frame));
        CHECK(in_universe(frame, ContextUniverse::boundary_frames) ==
              (frame != FrameType::internal_frame));
    }
}

void test_oracle_case_1_identical_behaviour_merges() {
    const auto table = table_from_lines({"the dog sleeps", "the cat sleeps", "a dog runs",
                                         "a cat runs", "i see the dog", "i see the cat", "dog",
                                         "cat"});
    for (std::size_t u = 0; u < kUniverseNames.size(); ++u) {
        Refiner refiner(table, static_cast<ContextUniverse>(u));
        refiner.run();
        CHECK(refiner.same_class(object(table, "dog"), object(table, "cat")));
        CHECK(refiner.same_class(object(table, "the dog"), object(table, "the cat")));
        CHECK(refiner.same_class(object(table, "dog sleeps"), object(table, "cat sleeps")));
    }
    Refiner all(table, ContextUniverse::all_frames);
    all.run();
    CHECK(!all.same_class(object(table, "dog"), object(table, "the")));
    CHECK(!all.same_class(object(table, "sleeps"), object(table, "runs")));  // (a dog, eps) differs
    CHECK(all.metrics().refinement_rounds >= 1);
    CHECK(all.metrics().initial_objects == table.object_count());
    CHECK(all.metrics().final_classes == all.classes().size());
    // dog/cat agree on four frames: a multi-context class, not a single-
    // observation coincidence.
    const auto diag = terminal_diagnostics(table, all);
    CHECK(diag.nontrivial_classes_multi_context >= 1);
    CHECK(diag.largest_multi_context_class >= 2);
    CHECK(diag.single_context_objects >= 1);  // e.g. "i see the"
}

void test_oracle_case_2_shared_context_with_counterexample_splits() {
    const auto table = table_from_lines({"mary is fun", "swimming is fun", "mary runs"});
    Refiner refiner(table, ContextUniverse::all_frames);
    refiner.run();
    const auto mary = object(table, "mary");
    const auto swimming = object(table, "swimming");
    // One shared positive frame (eps, is fun) ...
    CHECK(table.accepts(mary, table.contexts_of(swimming).front()));
    // ... is not enough: (eps, runs) is a (1, 0) distinguishing context.
    CHECK(!refiner.same_class(mary, swimming));
    const auto d = distinguishing_context(table, ContextUniverse::all_frames, mary, swimming);
    CHECK(d.has_value());
    CHECK(d->accepts_first && !d->accepts_second);
    CHECK(table.left_context_text(d->context).empty());
    CHECK(table.right_context_text(d->context) == "runs");
    // The split is recorded as a counterexample with Accept = 1 / 0 members.
    bool found = false;
    for (const auto& split : refiner.splits()) {
        if (split.context == d->context) {
            found = true;
            CHECK(table.accepts(split.in_member, split.context));
            CHECK(!table.accepts(split.out_member, split.context));
            CHECK(split.in_size >= 1 && split.in_size < split.block_size);
        }
    }
    CHECK(found);
}

void test_oracle_case_3_terminal_test_never_builds_a_clique() {
    const auto table = table_from_lines({"introduction", "<num>", "conclusions",
                                         "the introduction is long", "<num> mice ran",
                                         "see section <num>"});
    Refiner all(table, ContextUniverse::all_frames);
    all.run();
    const auto intro = object(table, "introduction");
    const auto num = object(table, "<num>");
    const auto concl = object(table, "conclusions");
    const auto terminal = table.terminal_context();
    CHECK(terminal.has_value());
    CHECK(table.accepts(intro, *terminal) && table.accepts(num, *terminal) &&
          table.accepts(concl, *terminal));
    CHECK(!all.same_class(intro, num));
    CHECK(!all.same_class(concl, num));
    CHECK(!all.same_class(concl, intro));
    const auto diag = terminal_diagnostics(table, all);
    // Complete spans of length <= 3: the three headers plus "<num> mice ran"
    // and "see section <num>" ("the introduction is long" has 4 tokens).
    CHECK(diag.terminal_objects == 5);
    // conclusions, "<num> mice ran" and "see section <num>" have the signature
    // {(eps,eps)} and nothing else: they are indistinguishable in D and form
    // one class; introduction and <num> are separated from it.
    CHECK(diag.terminal_only_objects == 3);
    CHECK(diag.terminal_classes == 3);
    CHECK(diag.largest_terminal_class == 3);
    // A pair that only shares (eps,eps) is split by the FIRST other frame.
    const auto d = distinguishing_context(table, ContextUniverse::all_frames, concl, intro);
    CHECK(d.has_value() && d->context != *terminal);
}

void test_oracle_case_4_indistinguishable_pair_merges() {
    const auto table = table_from_lines({"x alpha y", "x beta y", "p alpha", "p beta", "alpha q",
                                         "beta q"});
    for (std::size_t u = 0; u < kUniverseNames.size(); ++u) {
        Refiner refiner(table, static_cast<ContextUniverse>(u));
        refiner.run();
        CHECK(refiner.same_class(object(table, "alpha"), object(table, "beta")));
        CHECK(refiner.same_class(object(table, "x alpha"), object(table, "x beta")));
        CHECK(refiner.same_class(object(table, "alpha y"), object(table, "beta y")));
        CHECK(!distinguishing_context(table, static_cast<ContextUniverse>(u),
                                      object(table, "alpha"), object(table, "beta")));
    }
}

void test_oracle_case_5_more_data_repairs_a_closed_world_split() {
    const std::vector<std::string> small{"the dog sleeps", "the cat sleeps", "a dog runs"};
    std::vector<std::string> large = small;
    large.push_back("a cat runs");
    const auto small_table = table_from_lines(small);
    const auto large_table = table_from_lines(large);
    Refiner small_ref(small_table, ContextUniverse::all_frames);
    small_ref.run();
    Refiner large_ref(large_table, ContextUniverse::all_frames);
    large_ref.run();
    CHECK(!small_ref.same_class(object(small_table, "dog"), object(small_table, "cat")));
    const auto d = distinguishing_context(small_table, ContextUniverse::all_frames,
                                          object(small_table, "dog"), object(small_table, "cat"));
    CHECK(d && d->accepts_first && !d->accepts_second);  // closed-world negative for cat
    CHECK(large_ref.same_class(object(large_table, "dog"), object(large_table, "cat")));
    const auto change =
        compare_partitions(small_table, small_ref.labels(), large_table, large_ref.labels());
    CHECK(change.common_objects == small_table.object_count());
    CHECK(change.pairs_merged >= 1);
    CHECK(change.pairs_split == 0);
    CHECK(change.changed_pairs == change.pairs_merged);
    // Partitions are recomputed per scale: the small split is not an axiom.
    CHECK(large_ref.metrics().refinement_rounds >= 1);
}

std::vector<std::string> random_corpus(std::uint64_t state, const std::size_t count) {
    const std::vector<std::string> words{"a", "b", "c", "d", "e", "f"};
    std::vector<std::string> lines;
    for (std::size_t i = 0; i < count; ++i) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const std::size_t length = 1 + static_cast<std::size_t>((state >> 33U) % 5);
        std::string line;
        for (std::size_t j = 0; j < length; ++j) {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            line += (j == 0 ? "" : " ") + words[(state >> 33U) % words.size()];
        }
        lines.push_back(line);
    }
    return lines;
}

void test_oracle_case_6_refinement_equals_brute_force_signatures() {
    for (const std::uint64_t seed : {3ULL, 11ULL, 2024ULL, 99991ULL}) {
        const auto table = table_from_lines(random_corpus(seed, 120));
        for (std::size_t u = 0; u < kUniverseNames.size(); ++u) {
            const auto universe = static_cast<ContextUniverse>(u);
            Refiner refiner(table, universe);
            refiner.run();
            const auto dense = signature_partition_dense(table, universe);
            const auto sparse = signature_partition(table, universe);
            CHECK(same_partition(refiner.labels(), dense));
            CHECK(same_partition(refiner.labels(), sparse));
            CHECK(refiner.metrics().final_classes == class_count(dense));
            // Every split is a genuine (1, 0) counterexample on its context.
            for (const auto& split : refiner.splits()) {
                CHECK(table.accepts(split.in_member, split.context));
                CHECK(!table.accepts(split.out_member, split.context));
                CHECK(in_universe(table.context_frame[split.context], universe));
            }
            CHECK(refiner.metrics().block_splits == refiner.splits().size());
            CHECK(refiner.metrics().block_splits + 1 == refiner.metrics().final_classes);
            // Any two objects in different classes have a distinguishing context.
            const auto classes = refiner.classes();
            for (std::size_t i = 0; i + 1 < classes.size() && i < 30; ++i) {
                CHECK(distinguishing_context(table, universe, classes[i].front(),
                                             classes[i + 1].front())
                          .has_value());
            }
        }
    }
    // same_partition is a partition equality, not a label equality.
    CHECK(same_partition({0, 0, 1, 2}, {5, 5, 9, 1}));
    CHECK(!same_partition({0, 0, 1, 2}, {5, 5, 9, 9}));
}

void test_no_pairwise_clique_from_terminal_frame() {
    // 200 distinct one-token complete spans plus one sentence-internal use of
    // one of them.  v2.3 would emit C(200, 2) empty-frame witnesses; here the
    // work is linear in the positive records and (eps,eps) splits one block.
    std::vector<std::string> lines;
    for (int i = 0; i < 200; ++i) {
        lines.push_back("h" + std::to_string(i));
    }
    lines.push_back("we read h7 today");
    const auto table = table_from_lines(lines);
    Refiner refiner(table, ContextUniverse::all_frames);
    refiner.run();
    CHECK(refiner.metrics().membership_queries <= 2 * table.record_count());
    CHECK(refiner.metrics().context_tests <= 2 * table.record_count());
    const auto diag = terminal_diagnostics(table, refiner);
    CHECK(diag.terminal_objects == 200);       // the 4-token sentence is not an object
    CHECK(diag.terminal_only_objects == 199);  // all headers except h7
    CHECK(diag.largest_terminal_class == 199);
    CHECK(!refiner.same_class(object(table, "h7"), object(table, "h8")));
    CHECK(refiner.same_class(object(table, "h8"), object(table, "h9")));  // identical signature
}

void test_run_oracle_cases_all_pass_and_write_file() {
    const auto dir = temp_dir("scf_v24_oracle");
    const auto report = run_oracle_cases(dir);
    CHECK(report.find("FAIL") == std::string::npos);
    CHECK(report.find("all checks passed") != std::string::npos);
    CHECK(std::filesystem::exists(dir / "oracle_comparison.txt"));
    std::filesystem::remove_all(dir);
}

void test_pos_and_terminal_diagnostics() {
    const auto dir = temp_dir("scf_v24_pos");
    {
        std::ofstream ud(dir / "tiny.conllu");
        ud << "# sent\n1\tThe\tthe\tDET\n2\tdog\tdog\tNOUN\n3\tcat\tcat\tNOUN\n"
              "4\tsleeps\tsleep\tVERB\n5\truns\trun\tVERB\n1-2\tof\tof\t_\n";
    }
    const auto pos = load_pos_table(dir / "tiny.conllu");
    CHECK(pos.label.at("the") == "DET" && pos.label.at("dog") == "NOUN");
    const auto table = table_from_lines({"the dog sleeps", "the cat sleeps", "the dog runs",
                                         "the cat runs"});
    Refiner refiner(table, ContextUniverse::all_frames);
    refiner.run();
    const auto classes = refiner.classes();
    const auto diag = evaluate_pos(table, classes, pos);
    CHECK(diag.labeled_objects == 5);
    CHECK(diag.within_class_purity == 1.0);
    CHECK(diag.within_class_labeled_pairs == 2);  // dog/cat and sleeps/runs
    CHECK(diag.pairwise_same_pos_precision == 1.0);
    std::filesystem::remove_all(dir);
}

void test_v23_comparison_separates_terminal_merges() {
    std::vector<std::string> text{"<unused>"};
    std::map<std::string, std::uint32_t> ids;
    std::vector<std::vector<std::uint32_t>> sentences;
    const auto add = [&](const std::string& line) {
        std::istringstream words(line);
        std::vector<std::uint32_t> sentence;
        std::string word;
        while (words >> word) {
            const auto [it, inserted] = ids.try_emplace(word, static_cast<std::uint32_t>(text.size()));
            if (inserted) {
                text.push_back(word);
            }
            sentence.push_back(it->second);
        }
        sentences.push_back(sentence);
    };
    // v2.3 merges the two headers on the single (eps,eps) witness; the
    // closed-world partition separates them through (eps, is long).
    add("introduction");
    add("conclusions");
    add("the introduction is long");
    add("the x is long");
    add("x runs");
    add("introduction runs");
    add("x");
    const auto table = build_observation_table(sentences, text, sentences.size(), 3);
    Refiner refiner(table, ContextUniverse::all_frames);
    refiner.run();
    const auto partition = run_v23_merger(sentences, text, sentences.size(), 3, table);
    CHECK(partition.labels.size() == table.object_count());
    const auto comparison = compare_with_v23(partition, refiner);
    CHECK(comparison.ran);
    CHECK(comparison.accepted_merges >= 1);
    CHECK(comparison.accepted_by_frame[0] >= 1);
    CHECK(comparison.accepted_separated_by_frame[0] >= 1);
    CHECK(comparison.accepted_merges_separated <= comparison.accepted_merges);
    CHECK(comparison.v23_pairs_separated_by_v24 >= 1);
    CHECK(!refiner.same_class(object(table, "introduction"), object(table, "conclusions")));
    CHECK(refiner.same_class(object(table, "introduction"), object(table, "x")));
}

void test_ladder_is_deterministic_and_writes_all_outputs() {
    const auto dir = temp_dir("scf_v24_ladder");
    std::ostringstream corpus;
    corpus << "#doc 1\n#par\n";
    for (int i = 0; i < 40; ++i) {
        corpus << "The dog sleeps, quietly.\nThe cat sleeps.\nA dog runs!\nA cat runs.\n"
               << "Rare" << i << " things happen.\nIntroduction\nConclusions\n"
               << "See section " << i << ".\n";
    }
    {
        std::ofstream out(dir / "ladder.scs", std::ios::binary);
        out << corpus.str();
    }
    ClosedWorldConfig config;
    config.input = dir / "ladder.scs";
    config.corpus_label = "tiny";
    config.scales = {60, 500};
    config.compare_v23_max_scale = 500;
    const auto run = [&](const std::string& name) {
        config.output_dir = dir / name;
        return run_closed_world_scaling(config);
    };
    const auto first = run("first");
    const auto second = run("second");
    CHECK(first.rows.size() == 2 * 3);
    CHECK(first.rows[0].nominal_tokens == 60 && first.rows[0].actual_tokens >= 60);
    CHECK(first.rows[3].sentences > first.rows[0].sentences);
    for (const auto& row : first.rows) {
        CHECK(row.oracle_identical == 1);
        CHECK(row.v23.ran);
        CHECK(row.metrics.final_classes == row.oracle_classes);
    }
    // The second scale compares against the first for every universe.
    CHECK(first.rows[3].change.common_objects > 0);
    CHECK(first.rows[4].change.common_objects > 0);
    CHECK(first.rows[5].change.common_objects > 0);
    // introduction / conclusions / <num> never share a class via (eps,eps) alone.
    for (const char* file : {"closed_world_scaling.csv", "distinguishing_contexts.txt",
                             "class_examples.txt", "oracle_comparison.txt"}) {
        CHECK(std::filesystem::exists(dir / "first" / file));
        const auto a = read_file(dir / "first" / file);
        const auto b = read_file(dir / "second" / file);
        if (std::string(file).find(".csv") != std::string::npos) {
            // The last four columns are timings / RSS; compare the rest.
            std::istringstream sa(a), sb(b);
            std::string la, lb;
            while (std::getline(sa, la) && std::getline(sb, lb)) {
                const auto cut = [](const std::string& line) {
                    std::size_t pos = line.size();
                    for (int k = 0; k < 4; ++k) {
                        pos = line.rfind(',', pos - 1);
                    }
                    return line.substr(0, pos);
                };
                CHECK(cut(la) == cut(lb));
            }
        } else {
            CHECK(a == b);
        }
    }
    const auto contexts = read_file(dir / "first" / "distinguishing_contexts.txt");
    CHECK(contexts.find("<num> vs conclusions: DIFFERENT") != std::string::npos);
    CHECK(contexts.find("<num> vs introduction: DIFFERENT") != std::string::npos);
    CHECK(contexts.find("introduction vs conclusions: SAME CLASS") != std::string::npos);
    const auto oracle = read_file(dir / "first" / "oracle_comparison.txt");
    CHECK(oracle.find("FAIL") == std::string::npos);
    CHECK(oracle.find("identical: yes") != std::string::npos);
    std::filesystem::remove_all(dir);
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, void (*)()>> tests{
        {"table_matches_v23_observation_records", test_table_matches_v23_observation_records},
        {"universe_membership_and_parsing", test_universe_membership_and_parsing},
        {"oracle_case_1_identical_behaviour_merges",
         test_oracle_case_1_identical_behaviour_merges},
        {"oracle_case_2_shared_context_with_counterexample_splits",
         test_oracle_case_2_shared_context_with_counterexample_splits},
        {"oracle_case_3_terminal_test_never_builds_a_clique",
         test_oracle_case_3_terminal_test_never_builds_a_clique},
        {"oracle_case_4_indistinguishable_pair_merges",
         test_oracle_case_4_indistinguishable_pair_merges},
        {"oracle_case_5_more_data_repairs_a_closed_world_split",
         test_oracle_case_5_more_data_repairs_a_closed_world_split},
        {"oracle_case_6_refinement_equals_brute_force_signatures",
         test_oracle_case_6_refinement_equals_brute_force_signatures},
        {"no_pairwise_clique_from_terminal_frame", test_no_pairwise_clique_from_terminal_frame},
        {"run_oracle_cases_all_pass_and_write_file", test_run_oracle_cases_all_pass_and_write_file},
        {"pos_and_terminal_diagnostics", test_pos_and_terminal_diagnostics},
        {"v23_comparison_separates_terminal_merges",
         test_v23_comparison_separates_terminal_merges},
        {"ladder_is_deterministic_and_writes_all_outputs",
         test_ladder_is_deterministic_and_writes_all_outputs},
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
