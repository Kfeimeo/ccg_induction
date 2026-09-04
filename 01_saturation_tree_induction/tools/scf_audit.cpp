// SCF v1.2.1 theoretical / engineering audit driver.
//
// Separates three failure sources that v1.2 reporting conflated:
//   1. observational non-identifiability of the data itself,
//   2. systematic bias of the current tree objective,
//   3. whether saturation actually participates in tree induction.
// No parser, objective, or evidence change is made here (Acceptance G).

#include "scf/audit.hpp"
#include "scf/platform.hpp"
#include "scf/context_indexed.hpp"
#include "scf/corpus.hpp"
#include "scf/enumerator.hpp"
#include "scf/equivalence_solver.hpp"
#include "scf/evaluator.hpp"
#include "scf/pipeline.hpp"
#include "scf/synthetic.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct RunOutput {
    scf::SyntheticDataset dataset;
    scf::Corpus corpus;
    scf::EvidenceBuilder builder;
    std::vector<scf::TreeSolveResult> analyses;
    std::vector<scf::GoldTree> gold;
    scf::CorpusEvaluation evaluation;
};

RunOutput run_pipeline(const std::string& grammar,
                       const double coverage,
                       const std::uint64_t seed,
                       const std::size_t k = 0,
                       const double rho = 0.0,
                       const scf::EvidenceObjective objective = scf::EvidenceObjective::RawCount) {
    auto dataset = scf::generate_dataset(grammar, coverage, seed, 0, k, rho);
    std::ostringstream text;
    for (const auto& sentence : dataset.sentences) {
        for (std::size_t index = 0; index < sentence.tokens.size(); ++index) {
            text << (index == 0 ? "" : " ") << sentence.tokens[index];
        }
        text << '\n';
    }
    scf::Corpus corpus;
    std::istringstream input(text.str());
    corpus.load(input);
    scf::EvidenceBuilder builder(corpus, objective);
    auto analyses = scf::analyze_sentences(corpus, builder.span_evidence());
    auto gold = scf::dataset_gold_trees(dataset);
    const auto observable_gold = scf::dataset_observable_gold(dataset);
    auto evaluation =
        scf::evaluate_corpus(analyses, gold, builder.span_evidence(), {}, observable_gold);
    return RunOutput{std::move(dataset), std::move(corpus), std::move(builder),
                     std::move(analyses), std::move(gold), std::move(evaluation)};
}

std::string fmt6(const double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6f", value);
    return buffer;
}

std::string bool_text(const bool value) { return value ? "true" : "false"; }

std::ofstream open_file(const std::filesystem::path& path) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("cannot write " + path.string());
    }
    return output;
}

struct Aggregate {
    double mean{}, stddev{}, min{}, max{}, ci95_low{}, ci95_high{};
    std::size_t n{};
};

Aggregate aggregate(const std::vector<double>& values) {
    Aggregate result;
    result.n = values.size();
    if (values.empty()) {
        return result;
    }
    double total = 0.0;
    result.min = values.front();
    result.max = values.front();
    for (const auto value : values) {
        total += value;
        result.min = std::min(result.min, value);
        result.max = std::max(result.max, value);
    }
    result.mean = total / static_cast<double>(values.size());
    double squares = 0.0;
    for (const auto value : values) {
        squares += (value - result.mean) * (value - result.mean);
    }
    result.stddev = values.size() > 1
                        ? std::sqrt(squares / static_cast<double>(values.size() - 1))
                        : 0.0;
    // Normal-approximation 95% CI over seeds.
    const auto half = 1.96 * result.stddev / std::sqrt(static_cast<double>(values.size()));
    result.ci95_low = result.mean - half;
    result.ci95_high = result.mean + half;
    return result;
}

// --- Section 1/2: observational equivalence -------------------------------

struct FamilySignature {
    std::string name;
    std::uint64_t surface{};
    std::uint64_t surface_renamed{};
    std::uint64_t sampled{};
    std::uint64_t raw_context{};
    std::uint64_t raw_context_renamed{};
    std::uint64_t raw_witness{};
    std::uint64_t raw_witness_renamed{};
    std::uint64_t gold_shape{};
    std::uint64_t gold_shape_renamed{};
};

FamilySignature family_signature(const std::string& grammar, const std::size_t k) {
    const auto run = run_pipeline(grammar, 1.0, 1, k);
    FamilySignature signature;
    signature.name = grammar + "[K=" + std::to_string(run.dataset.lexical_cardinality) + "]";
    const auto tokens = scf::sentence_tokens(run.dataset.sentences);
    const auto renaming = scf::build_canonical_renaming(tokens);
    signature.surface = scf::sentence_set_hash(tokens);
    signature.surface_renamed = scf::sentence_set_hash(scf::apply_renaming(tokens, renaming));
    signature.sampled = signature.surface;  // full coverage: sampled corpus == full language
    signature.raw_context = scf::raw_context_relation_hash(run.corpus);
    signature.raw_context_renamed = scf::raw_context_relation_hash(run.corpus, &renaming);
    signature.raw_witness = scf::raw_witness_relation_hash(run.corpus, run.builder);
    signature.raw_witness_renamed =
        scf::raw_witness_relation_hash(run.corpus, run.builder, &renaming);
    signature.gold_shape = scf::gold_shape_hash(run.dataset.sentences);
    signature.gold_shape_renamed = scf::gold_shape_hash(run.dataset.sentences, &renaming);
    return signature;
}

void write_observational_equivalence_report(const std::filesystem::path& directory) {
    const std::vector<std::pair<std::string, std::size_t>> families{
        {"nested_balanced", 2},
        {"right_branching", 2},
        {"left_branching", 2},
        {"hierarchical_correlated_balanced", 3},
        {"hierarchical_correlated_right", 3},
        {"hierarchical_correlated_left", 3},
    };
    std::vector<FamilySignature> signatures;
    for (const auto& [name, k] : families) {
        signatures.push_back(family_signature(name, k));
    }
    auto output = open_file(directory / "observational_equivalence_report.txt");
    output << "SCF v1.2.1 observational equivalence report (full coverage, seed 1)\n"
           << "same_* columns compare exact token identity; *_renamed applies the greedy\n"
           << "canonical token renaming, detecting equality up to alphabet isomorphism.\n\n";
    output << "per-family hashes:\n";
    for (const auto& signature : signatures) {
        output << "  " << signature.name << '\n'
               << "    surface_language_hash = " << scf::hash_hex(signature.surface) << '\n'
               << "    surface_language_hash_renamed = "
               << scf::hash_hex(signature.surface_renamed) << '\n'
               << "    sampled_corpus_hash = " << scf::hash_hex(signature.sampled) << '\n'
               << "    raw_context_relation_hash = " << scf::hash_hex(signature.raw_context)
               << '\n'
               << "    raw_witness_relation_hash = " << scf::hash_hex(signature.raw_witness)
               << '\n'
               << "    gold_shape_hash = " << scf::hash_hex(signature.gold_shape) << '\n';
    }
    output << "\npairwise comparison:\n";
    for (std::size_t a = 0; a < signatures.size(); ++a) {
        for (std::size_t b = a + 1; b < signatures.size(); ++b) {
            const auto& lhs = signatures[a];
            const auto& rhs = signatures[b];
            const bool same_surface = lhs.surface == rhs.surface;
            const bool same_surface_renamed = lhs.surface_renamed == rhs.surface_renamed;
            const bool same_context = lhs.raw_context == rhs.raw_context;
            const bool same_context_renamed =
                lhs.raw_context_renamed == rhs.raw_context_renamed;
            const bool same_witness = lhs.raw_witness == rhs.raw_witness;
            const bool same_witness_renamed =
                lhs.raw_witness_renamed == rhs.raw_witness_renamed;
            const bool same_gold =
                same_surface ? lhs.gold_shape == rhs.gold_shape
                             : lhs.gold_shape_renamed == rhs.gold_shape_renamed;
            output << "grammar A = " << lhs.name << '\n'
                   << "grammar B = " << rhs.name << '\n'
                   << "same_surface_language = " << bool_text(same_surface) << '\n'
                   << "same_surface_language_renamed = " << bool_text(same_surface_renamed)
                   << '\n'
                   << "same_sampled_corpus = " << bool_text(lhs.sampled == rhs.sampled) << '\n'
                   << "same_raw_context_relation = " << bool_text(same_context) << '\n'
                   << "same_raw_context_relation_renamed = " << bool_text(same_context_renamed)
                   << '\n'
                   << "same_raw_witness_relation = " << bool_text(same_witness) << '\n'
                   << "same_raw_witness_relation_renamed = " << bool_text(same_witness_renamed)
                   << '\n'
                   << "same_gold_tree = " << bool_text(same_gold) << '\n';
            if (same_surface) {
                output << "observationally_equivalent_support = true\n";
            }
            if (same_context) {
                output << "observationally_equivalent_raw_context = true\n";
            }
            if (same_witness) {
                output << "observationally_equivalent_current_tree_evidence = true\n";
            }
            if ((same_surface || same_surface_renamed) && !same_gold) {
                output << "verdict = latent grammars observationally indistinguishable under "
                          "current observations"
                       << (same_surface ? "" : " (up to token renaming)") << '\n';
            }
            output << '\n';
        }
    }
    std::cout << "wrote observational_equivalence_report.txt\n";
}

// --- Section 3: saturation ablation ---------------------------------------

bool same_predictions(const std::vector<scf::TreeSolveResult>& lhs,
                      const std::vector<scf::TreeSolveResult>& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t sentence = 0; sentence < lhs.size(); ++sentence) {
        const auto& a = lhs[sentence];
        const auto& b = rhs[sentence];
        if (a.best_score != b.best_score || a.optimal_tree_count != b.optimal_tree_count ||
            a.forced_spans != b.forced_spans || a.optimal_splits != b.optimal_splits) {
            return false;
        }
    }
    return true;
}

void run_saturation_ablation(const std::filesystem::path& directory) {
    const std::vector<std::string> grammars{
        "ab_cartesian",   "simple_np_vp",   "symmetric_abc",     "nested_balanced",
        "right_branching", "left_branching", "ambiguous_lexicon", "ccg_lite"};
    const std::vector<double> coverages{0.05, 0.10, 0.20, 0.40, 0.60, 0.80, 1.00};
    auto output = open_file(directory / "saturation_ablation.csv");
    output << "grammar,coverage,seed,same_best_score,same_optimal_tree_count,"
              "same_gold_in_argmax,same_unique_status,same_prediction,same_raw_witness_hash,"
              "parse_outputs_identical\n";
    std::size_t changed = 0;
    std::size_t unchanged = 0;
    for (const auto& grammar : grammars) {
        for (const auto coverage : coverages) {
            for (std::uint64_t seed = 1; seed <= 5; ++seed) {
                // Mode A: normal pipeline including saturation.
                auto mode_a = run_pipeline(grammar, coverage, seed);
                scf::EquivalenceSolver solver(mode_a.corpus.string_interner().size(),
                                              mode_a.corpus.context_records(),
                                              mode_a.corpus.concat_triples());
                solver.saturate();
                // Mode B: the same corpus without any saturation. Evidence and
                // DP are recomputed from scratch.
                scf::EvidenceBuilder builder_b(mode_a.corpus);
                const auto analyses_b =
                    scf::analyze_sentences(mode_a.corpus, builder_b.span_evidence());
                const auto evaluation_b = scf::evaluate_corpus(
                    analyses_b, mode_a.gold, builder_b.span_evidence());

                bool same_best = true;
                bool same_count = true;
                bool same_gia = true;
                bool same_unique = true;
                for (std::size_t s = 0; s < mode_a.evaluation.sentences.size(); ++s) {
                    const auto& ea = mode_a.evaluation.sentences[s];
                    const auto& eb = evaluation_b.sentences[s];
                    same_best &= ea.best_score == eb.best_score;
                    same_count &= ea.optimal_tree_count == eb.optimal_tree_count;
                    same_gia &= ea.gold_in_argmax == eb.gold_in_argmax;
                    same_unique &= ea.unique_optimal == eb.unique_optimal &&
                                   ea.exact_unique_match == eb.exact_unique_match;
                }
                const bool same_pred = same_predictions(mode_a.analyses, analyses_b);
                const bool same_hash =
                    scf::raw_witness_relation_hash(mode_a.corpus, mode_a.builder) ==
                    scf::raw_witness_relation_hash(mode_a.corpus, builder_b);
                const bool identical =
                    same_best && same_count && same_gia && same_unique && same_pred && same_hash;
                (identical ? unchanged : changed) += 1;
                output << grammar << ',' << fmt6(coverage) << ',' << seed << ','
                       << bool_text(same_best) << ',' << bool_text(same_count) << ','
                       << bool_text(same_gia) << ',' << bool_text(same_unique) << ','
                       << bool_text(same_pred) << ',' << bool_text(same_hash) << ','
                       << bool_text(identical) << '\n';
            }
        }
    }
    output << "# parse_outputs_changed_runs = " << changed << '\n';
    output << "# parse_outputs_unchanged_runs = " << unchanged << '\n';
    std::cout << "saturation ablation: parse_outputs_changed_runs = " << changed
              << ", parse_outputs_unchanged_runs = " << unchanged << '\n';
}

// --- Section 4/5: span-length bias ----------------------------------------

void run_span_length_bias(const std::filesystem::path& directory) {
    auto output = open_file(directory / "span_length_bias.csv");
    output << "grammar,K,span_length,theoretical_external_contexts,actual_max_score,"
              "actual_mean_score,actual_median_score,candidate_span_count,total_span_count,"
              "mean_gold_span_score,mean_non_gold_span_score\n";
    bool len2_gt_len3 = true;
    for (const std::size_t k : {std::size_t{2}, std::size_t{3}, std::size_t{4}}) {
        const auto run = run_pipeline("nested_balanced", 1.0, 1, k);
        const auto lengths = scf::corpus_sentence_lengths(run.corpus);
        const auto stats =
            scf::score_by_span_length(lengths, run.builder.span_evidence(), run.gold);
        std::map<std::uint16_t, double> mean_by_length;
        for (const auto& row : stats) {
            // Full Cartesian X1 x ... x X4 with |X_i| = K: a span [i,j) has
            // exactly prod_{k<i}|X_k| * prod_{k>=j}|X_k| = K^(4-(j-i))
            // distinct external contexts.
            const auto theoretical =
                std::pow(static_cast<double>(k), static_cast<double>(4 - row.span_length));
            output << "nested_balanced," << k << ',' << row.span_length << ','
                   << fmt6(theoretical) << ',' << row.max_score << ',' << fmt6(row.mean_score)
                   << ',' << fmt6(row.median_score) << ',' << row.candidate_span_count << ','
                   << row.total_span_count << ',' << fmt6(row.mean_gold_span_score) << ','
                   << fmt6(row.mean_non_gold_span_score) << '\n';
            mean_by_length[row.span_length] = row.mean_score;
        }
        if (mean_by_length.contains(2) && mean_by_length.contains(3)) {
            len2_gt_len3 &= mean_by_length[2] > mean_by_length[3];
        }
    }
    output << "# len2_score_gt_len3_score = " << bool_text(len2_gt_len3) << '\n';
    std::cout << "span-length bias: len2 > len3 holds = " << bool_text(len2_gt_len3) << '\n';
}

void run_tree_shape_scores(const std::filesystem::path& directory) {
    auto output = open_file(directory / "tree_shape_scores.tsv");
    output << "grammar\tK\tsentence_id\tbalanced_score\tleft_score\tright_score\tbest_shape\n";
    bool balanced_beats_left = true;
    bool balanced_beats_right = true;
    bool left_equals_right = true;
    for (const std::size_t k : {std::size_t{2}, std::size_t{3}, std::size_t{4}}) {
        const auto run = run_pipeline("nested_balanced", 1.0, 1, k);
        const auto lengths = scf::corpus_sentence_lengths(run.corpus);
        const auto rows = scf::tree_shape_scores(lengths, run.builder.span_evidence());
        for (const auto& row : rows) {
            output << "nested_balanced\t" << k << '\t' << row.sentence << '\t'
                   << row.balanced_score << '\t' << row.left_score << '\t' << row.right_score
                   << '\t' << row.best_shape << '\n';
            balanced_beats_left &= row.balanced_score > row.left_score;
            balanced_beats_right &= row.balanced_score > row.right_score;
            left_equals_right &= row.left_score == row.right_score;
        }
    }
    output << "# balanced_gt_left = " << bool_text(balanced_beats_left) << '\n';
    output << "# balanced_gt_right = " << bool_text(balanced_beats_right) << '\n';
    output << "# left_eq_right = " << bool_text(left_equals_right) << '\n';
    if (balanced_beats_left && balanced_beats_right) {
        output << "# verdict = objective-induced balance preference (not left/right "
                  "directional bias)\n";
    }
    std::cout << "tree shapes: balanced>left=" << bool_text(balanced_beats_left)
              << " balanced>right=" << bool_text(balanced_beats_right)
              << " left==right=" << bool_text(left_equals_right) << '\n';
}

// --- Section 6-8: audit grid, population/sample split, symmetry breaking ---

struct PopulationStatus {
    bool identifiable{};  // every full-language sentence unique & exact
    double gold_in_argmax_rate{};
    double mean_argmax{};
    std::map<std::string, const scf::SentenceEvaluation*> by_sentence;  // owned below
    std::shared_ptr<RunOutput> run;
};

std::string sentence_key(const std::vector<std::string>& tokens) {
    std::string key;
    for (const auto& token : tokens) {
        key += token;
        key += ' ';
    }
    return key;
}

PopulationStatus population_status(const std::string& grammar, const std::size_t k) {
    PopulationStatus status;
    status.run = std::make_shared<RunOutput>(run_pipeline(grammar, 1.0, 1, k));
    const auto& evaluation = status.run->evaluation;
    status.identifiable = evaluation.unique_correct == evaluation.sentence_count;
    status.gold_in_argmax_rate = evaluation.gold_in_argmax_rate;
    status.mean_argmax = evaluation.mean_argmax_size;
    for (std::size_t s = 0; s < status.run->dataset.sentences.size(); ++s) {
        status.by_sentence.emplace(sentence_key(status.run->dataset.sentences[s].tokens),
                                   &evaluation.sentences[s]);
    }
    return status;
}

void run_audit_grid(const std::filesystem::path& directory, const std::uint64_t seed_count) {
    const std::vector<std::pair<std::string, std::size_t>> configs{
        {"simple_np_vp", 0},        {"symmetric_abc", 0},
        {"nested_balanced", 0},     {"right_branching", 0},
        {"left_branching", 0},      {"nested_balanced", 4},
        {"hierarchical_correlated_balanced", 0},
        {"hierarchical_correlated_right", 0},
        {"hierarchical_correlated_left", 0},
    };
    const std::vector<double> coverages{0.05, 0.10, 0.20, 0.40, 0.60, 0.80, 1.00};

    auto runs = open_file(directory / "audit_grid_runs.csv");
    runs << "grammar,lexical_cardinality,requested_coverage,effective_coverage,seed,"
            "full_sentence_count,sampled_sentence_count,gold_in_argmax_rate,"
            "unique_optimal_rate,exact_unique_match_rate,mean_argmax_size,collapse_ratio,"
            "population_identifiable,population_gold_in_argmax_rate,population_mean_argmax,"
            "sample_identified_correctly,sample_identified_wrongly,sample_ambiguous\n";
    auto symmetry = open_file(directory / "finite_sample_symmetry_breaking.csv");
    symmetry << "coverage,seed,sentences,spurious_unique,spurious_unique_rate,"
                "spurious_wrong_unique,spurious_wrong_unique_rate,"
                "symmetry_restored_at_full_coverage\n";

    std::map<std::pair<std::string, double>, std::map<std::string, std::vector<double>>> series;
    for (const auto& [grammar, k] : configs) {
        const auto population = population_status(grammar, k);
        const auto label =
            grammar + (k != 0 ? "_K" + std::to_string(k) : std::string{});
        for (const auto coverage : coverages) {
            for (std::uint64_t seed = 1; seed <= seed_count; ++seed) {
                auto run = run_pipeline(grammar, coverage, seed, k);
                scf::EquivalenceSolver solver(run.corpus.string_interner().size(),
                                              run.corpus.context_records(),
                                              run.corpus.concat_triples());
                solver.saturate();
                const auto diagnostics = scf::collapse_diagnostics(run.corpus, solver);
                const auto& evaluation = run.evaluation;
                const auto effective =
                    static_cast<double>(run.dataset.sentences.size()) /
                    static_cast<double>(run.dataset.full_sentence_count);
                const auto ambiguous = evaluation.ambiguous_gold_included +
                                       evaluation.ambiguous_gold_excluded;
                runs << grammar << ',' << run.dataset.lexical_cardinality << ','
                     << fmt6(coverage) << ',' << fmt6(effective) << ',' << seed << ','
                     << run.dataset.full_sentence_count << ',' << run.dataset.sentences.size()
                     << ',' << fmt6(evaluation.gold_in_argmax_rate) << ','
                     << fmt6(evaluation.unique_optimal_rate) << ','
                     << fmt6(evaluation.exact_unique_match_rate) << ','
                     << fmt6(evaluation.mean_argmax_size) << ','
                     << fmt6(diagnostics.collapse_ratio) << ','
                     << bool_text(population.identifiable) << ','
                     << fmt6(population.gold_in_argmax_rate) << ','
                     << fmt6(population.mean_argmax) << ',' << evaluation.unique_correct << ','
                     << evaluation.unique_wrong << ',' << ambiguous << '\n';
                auto& bucket = series[{label, coverage}];
                bucket["gold_in_argmax_rate"].push_back(evaluation.gold_in_argmax_rate);
                bucket["unique_optimal_rate"].push_back(evaluation.unique_optimal_rate);
                bucket["exact_unique_match_rate"].push_back(evaluation.exact_unique_match_rate);
                bucket["mean_argmax_size"].push_back(evaluation.mean_argmax_size);
                bucket["collapse_ratio"].push_back(diagnostics.collapse_ratio);

                if (grammar == "symmetric_abc") {
                    std::size_t spurious_unique = 0;
                    std::size_t spurious_wrong = 0;
                    for (std::size_t s = 0; s < run.dataset.sentences.size(); ++s) {
                        const auto key = sentence_key(run.dataset.sentences[s].tokens);
                        const auto found = population.by_sentence.find(key);
                        if (found == population.by_sentence.end()) {
                            continue;
                        }
                        const auto& pop = *found->second;
                        const auto& sample = evaluation.sentences[s];
                        if (!pop.unique_optimal && sample.unique_optimal) {
                            ++spurious_unique;
                        }
                        if (pop.gold_in_argmax && sample.unique_optimal &&
                            !sample.exact_unique_match) {
                            ++spurious_wrong;
                        }
                    }
                    const auto total = static_cast<double>(run.dataset.sentences.size());
                    const bool restored =
                        coverage == 1.0 &&
                        evaluation.unique_optimal_rate == 0.0 &&
                        evaluation.gold_in_argmax_rate == 1.0;
                    symmetry << fmt6(coverage) << ',' << seed << ','
                             << run.dataset.sentences.size() << ',' << spurious_unique << ','
                             << fmt6(total > 0 ? spurious_unique / total : 0.0) << ','
                             << spurious_wrong << ','
                             << fmt6(total > 0 ? spurious_wrong / total : 0.0) << ','
                             << bool_text(restored) << '\n';
                }
            }
        }
        std::cout << "audit grid: " << label << " done\n";
    }

    auto agg = open_file(directory / "audit_grid_aggregate.csv");
    agg << "grammar,coverage,metric,n,mean,std,min,max,ci95_low,ci95_high\n";
    for (const auto& [key, metrics] : series) {
        for (const auto& [metric, values] : metrics) {
            const auto stats = aggregate(values);
            agg << key.first << ',' << fmt6(key.second) << ',' << metric << ',' << stats.n << ','
                << fmt6(stats.mean) << ',' << fmt6(stats.stddev) << ',' << fmt6(stats.min) << ','
                << fmt6(stats.max) << ',' << fmt6(stats.ci95_low) << ',' << fmt6(stats.ci95_high)
                << '\n';
        }
    }
    std::cout << "wrote audit_grid_runs.csv / audit_grid_aggregate.csv / "
                 "finite_sample_symmetry_breaking.csv\n";
}

// --- Section 9: symmetry-breaking rho sweep -------------------------------

void run_rho_sweep(const std::filesystem::path& directory) {
    const std::vector<double> rhos{0.0, 0.05, 0.10, 0.20, 0.40, 0.60, 0.80, 1.00};
    auto output = open_file(directory / "identifiability_vs_rho.csv");
    output << "rho,K,full_sentence_count,marker_sentences,gold_in_argmax_rate,"
              "unique_optimal_rate,exact_unique_match_rate,mean_argmax_size\n";
    for (const auto rho : rhos) {
        const std::size_t k = 3;
        const auto run = run_pipeline("symmetric_abc", 1.0, 1, k, rho);
        const auto markers = run.dataset.full_sentence_count - k * k * k;
        output << fmt6(rho) << ',' << k << ',' << run.dataset.full_sentence_count << ','
               << markers << ',' << fmt6(run.evaluation.gold_in_argmax_rate) << ','
               << fmt6(run.evaluation.unique_optimal_rate) << ','
               << fmt6(run.evaluation.exact_unique_match_rate) << ','
               << fmt6(run.evaluation.mean_argmax_size) << '\n';
    }
    std::cout << "wrote identifiability_vs_rho.csv\n";
}

// --- v1.3: objective laboratory sections ----------------------------------

double strength_units(const scf::EvidenceObjective objective, const std::uint64_t fixed) {
    return objective == scf::EvidenceObjective::RawCount
               ? static_cast<double>(fixed)
               : static_cast<double>(fixed) / scf::kStrengthScale;
}

// Full-Cartesian neutrality test: explicit balanced/left/right tree scores
// under each objective, n = 4, K in the requested set (spec v1.3 §11).
void run_objective_bias(const std::filesystem::path& directory,
                        const std::vector<std::size_t>& cardinalities,
                        const std::vector<scf::EvidenceObjective>& objectives) {
    auto output = open_file(directory / "objective_bias.csv");
    output << "grammar,K,objective,balanced_score,left_score,right_score,best_shape,"
              "optimal_tree_count,all_sentences_agree\n";
    for (const auto k : cardinalities) {
        for (const auto objective : objectives) {
            const auto run = run_pipeline("nested_balanced", 1.0, 1, k, 0.0, objective);
            const auto lengths = scf::corpus_sentence_lengths(run.corpus);
            const auto rows = scf::tree_shape_scores(lengths, run.builder.span_evidence());
            bool agree = true;
            for (const auto& row : rows) {
                agree &= row.balanced_score == rows.front().balanced_score &&
                         row.left_score == rows.front().left_score &&
                         row.right_score == rows.front().right_score;
            }
            const auto& first = rows.front();
            output << "nested_balanced," << k << ','
                   << scf::evidence_objective_name(objective) << ','
                   << fmt6(strength_units(objective, first.balanced_score)) << ','
                   << fmt6(strength_units(objective, first.left_score)) << ','
                   << fmt6(strength_units(objective, first.right_score)) << ','
                   << first.best_shape << ',' << run.analyses.front().optimal_tree_count << ','
                   << bool_text(agree) << '\n';
        }
    }
    std::cout << "wrote objective_bias.csv\n";
}

// Primary objective-comparison grid (spec v1.3 §17): K = 4, the correlated
// families plus simple_np_vp and symmetric_abc, all four objectives.
void run_objective_grid(const std::filesystem::path& directory,
                        const std::uint64_t seed_count,
                        const std::vector<scf::EvidenceObjective>& objectives) {
    const std::vector<std::string> grammars{
        "simple_np_vp", "symmetric_abc", "hierarchical_correlated_balanced",
        "hierarchical_correlated_right", "hierarchical_correlated_left"};
    const std::vector<double> coverages{0.05, 0.10, 0.20, 0.40, 0.60, 0.80, 1.00};
    constexpr std::size_t kCardinality = 4;

    auto runs = open_file(directory / "objective_grid_runs.csv");
    runs << "grammar,objective,K,requested_coverage,effective_coverage,seed,"
            "sampled_sentence_count,gold_in_argmax_rate,unique_optimal_rate,"
            "exact_unique_match_rate,mean_argmax_size,forced_precision_full,"
            "forced_recall_full,forced_precision_observable,forced_recall_observable,"
            "mean_pair_strength,mean_pair_confidence\n";
    std::map<std::tuple<std::string, std::string, double>,
             std::map<std::string, std::vector<double>>>
        series;
    for (const auto& grammar : grammars) {
        for (const auto objective : objectives) {
            const auto objective_name = scf::evidence_objective_name(objective);
            for (const auto coverage : coverages) {
                for (std::uint64_t seed = 1; seed <= seed_count; ++seed) {
                    const auto run =
                        run_pipeline(grammar, coverage, seed, kCardinality, 0.0, objective);
                    const auto& evaluation = run.evaluation;
                    const auto effective =
                        static_cast<double>(run.dataset.sentences.size()) /
                        static_cast<double>(run.dataset.full_sentence_count);
                    runs << grammar << ',' << objective_name << ','
                         << run.dataset.lexical_cardinality << ',' << fmt6(coverage) << ','
                         << fmt6(effective) << ',' << seed << ','
                         << run.dataset.sentences.size() << ','
                         << fmt6(evaluation.gold_in_argmax_rate) << ','
                         << fmt6(evaluation.unique_optimal_rate) << ','
                         << fmt6(evaluation.exact_unique_match_rate) << ','
                         << fmt6(evaluation.mean_argmax_size) << ','
                         << fmt6(evaluation.forced_precision_full_gold) << ','
                         << fmt6(evaluation.forced_recall_full_gold) << ','
                         << fmt6(evaluation.forced_precision_observable_gold) << ','
                         << fmt6(evaluation.forced_recall_observable_gold) << ','
                         << fmt6(run.builder.summary().mean_pair_strength) << ','
                         << fmt6(run.builder.summary().mean_pair_confidence) << '\n';
                    auto& bucket = series[{grammar, objective_name, coverage}];
                    bucket["gold_in_argmax_rate"].push_back(evaluation.gold_in_argmax_rate);
                    bucket["unique_optimal_rate"].push_back(evaluation.unique_optimal_rate);
                    bucket["exact_unique_match_rate"].push_back(
                        evaluation.exact_unique_match_rate);
                    bucket["mean_argmax_size"].push_back(evaluation.mean_argmax_size);
                    bucket["forced_precision_observable"].push_back(
                        evaluation.forced_precision_observable_gold);
                    bucket["forced_recall_observable"].push_back(
                        evaluation.forced_recall_observable_gold);
                }
            }
            std::cout << "objective grid: " << grammar << " / " << objective_name << " done\n";
        }
    }
    auto agg = open_file(directory / "objective_grid_aggregate.csv");
    agg << "grammar,objective,coverage,metric,n,mean,std,min,max,ci95_low,ci95_high\n";
    for (const auto& [key, metrics] : series) {
        for (const auto& [metric, values] : metrics) {
            const auto stats = aggregate(values);
            agg << std::get<0>(key) << ',' << std::get<1>(key) << ',' << fmt6(std::get<2>(key))
                << ',' << metric << ',' << stats.n << ',' << fmt6(stats.mean) << ','
                << fmt6(stats.stddev) << ',' << fmt6(stats.min) << ',' << fmt6(stats.max) << ','
                << fmt6(stats.ci95_low) << ',' << fmt6(stats.ci95_high) << '\n';
        }
    }
    std::cout << "wrote objective_grid_runs.csv / objective_grid_aggregate.csv\n";
}

// rho sweep per objective (spec v1.3 §19).
void run_rho_by_objective(const std::filesystem::path& directory,
                          const std::vector<scf::EvidenceObjective>& objectives) {
    const std::vector<double> rhos{0.0, 0.05, 0.10, 0.20, 0.40, 0.60, 0.80, 1.00};
    auto output = open_file(directory / "rho_by_objective.csv");
    output << "rho,objective,K,gold_in_argmax_rate,unique_optimal_rate,"
              "exact_unique_match_rate,forced_precision_observable,forced_recall_observable,"
              "mean_argmax_size\n";
    for (const auto objective : objectives) {
        for (const auto rho : rhos) {
            const auto run = run_pipeline("symmetric_abc", 1.0, 1, 3, rho, objective);
            output << fmt6(rho) << ',' << scf::evidence_objective_name(objective) << ",3,"
                   << fmt6(run.evaluation.gold_in_argmax_rate) << ','
                   << fmt6(run.evaluation.unique_optimal_rate) << ','
                   << fmt6(run.evaluation.exact_unique_match_rate) << ','
                   << fmt6(run.evaluation.forced_precision_observable_gold) << ','
                   << fmt6(run.evaluation.forced_recall_observable_gold) << ','
                   << fmt6(run.evaluation.mean_argmax_size) << '\n';
        }
    }
    std::cout << "wrote rho_by_objective.csv\n";
}

// --- v1.4: context-indexed synthetic audit ---------------------------------

// Latent role of a single-token yield: the lexical-rule lhs with trailing
// digits stripped when one exists (Det1 -> Det); otherwise the token with
// trailing digits stripped (a1 -> a). Multi-token yields carry no role and
// are excluded from precision/purity (reported separately).
std::map<std::string, std::set<std::string>> latent_roles(const scf::Grammar& grammar) {
    const auto strip_digits = [](std::string text) {
        while (!text.empty() && std::isdigit(static_cast<unsigned char>(text.back()))) {
            text.pop_back();
        }
        return text.empty() ? std::string("#") : text;
    };
    std::set<std::string> lhs_set;
    for (const auto& rule : grammar.rules) {
        lhs_set.insert(rule.lhs);
    }
    std::map<std::string, std::set<std::string>> roles;
    for (const auto& rule : grammar.rules) {
        if (rule.rhs.size() == 1 && !lhs_set.contains(rule.rhs.front())) {
            roles[rule.rhs.front()].insert(strip_digits(rule.lhs));
        }
    }
    // terminals appearing only inside multi-symbol rules fall back to their
    // own stripped spelling
    for (const auto& rule : grammar.rules) {
        for (const auto& symbol : rule.rhs) {
            if (!lhs_set.contains(symbol) && !roles.contains(symbol)) {
                roles[symbol].insert(strip_digits(symbol));
            }
        }
    }
    return roles;
}

struct IndexedSyntheticResult {
    scf::ContextIndexedDiagnostics diagnostics;
    double recursive_relation_gain{};
    std::uint64_t relations_round0{};
    std::uint64_t relations_final{};
    double raw_evidence_coverage{};
    double indexed_evidence_coverage{};
    std::size_t local_relation_pairs{};
    std::size_t true_pairs{};
    std::size_t false_pairs{};
    std::size_t unknown_pairs{};  // pairs involving multi-token yields
    double precision{};           // over role-known pairs
    std::optional<double> recall;
    double mean_key_purity{};
    double weighted_key_purity{};
    double min_key_purity{1.0};
    std::size_t multi_role_surfaces{};
    double mean_roles_per_surface{};
    std::size_t max_roles_per_surface{};
    // Distinct unordered yield pairs related under ANY key: round 0 (exact
    // contexts) vs the final abstraction. The relation-invariance theorem
    // predicts equality in every signature.
    std::size_t distinct_pairs_round0{};
    std::size_t distinct_pairs_final{};
};

IndexedSyntheticResult analyze_indexed(const scf::SyntheticDataset& dataset,
                                       const scf::Corpus& corpus,
                                       const scf::ContextIndexedSolver& solver) {
    IndexedSyntheticResult result;
    result.diagnostics = solver.diagnostics();
    result.recursive_relation_gain = solver.recursive_relation_gain();
    result.relations_round0 = solver.relation_count_round0();
    result.relations_final = solver.relation_count_final();

    // Evidence coverage (spec §29): raw partner vs final-context partner.
    {
        const scf::EvidenceBuilder raw(corpus);
        std::set<scf::Span> raw_spans;
        for (const auto& item : raw.span_evidence()) {
            raw_spans.insert(item.span);
        }
        std::size_t proper = 0;
        std::size_t raw_hits = 0;
        std::size_t indexed_hits = 0;
        std::map<scf::RawContextKey, bool> indexed_context;
        for (const auto& record : corpus.context_records()) {
            const auto key =
                *solver.final_key_for(record.triple.left, record.triple.right);
            indexed_context[{record.triple.left, record.triple.right}] =
                solver.locally_related(record.triple.yield, record.triple.yield, key) &&
                [&] {
                    const auto& blocks = solver.blocks();
                    const auto found = std::lower_bound(
                        blocks.begin(), blocks.end(), key,
                        [](const scf::LocalRoleBlock& block, const scf::ContextKey& target) {
                            return block.context < target;
                        });
                    return found != blocks.end() && found->context == key &&
                           found->yields.size() >= 2;
                }();
        }
        std::map<scf::Span, bool> indexed_span;
        for (const auto& record : corpus.context_records()) {
            const bool hit =
                indexed_context[{record.triple.left, record.triple.right}];
            for (const auto occurrence_id : record.occurrences) {
                const auto& occurrence =
                    corpus.occurrences().at(static_cast<std::size_t>(occurrence_id));
                indexed_span[scf::Span{occurrence.sentence, occurrence.begin,
                                       occurrence.end}] = hit;
            }
        }
        for (std::size_t sentence = 0; sentence < corpus.sentences().size(); ++sentence) {
            const auto length = static_cast<std::uint16_t>(corpus.sentences()[sentence].size());
            for (std::uint16_t span_length = 2; span_length < length; ++span_length) {
                for (std::uint16_t begin = 0; begin + span_length <= length; ++begin) {
                    const auto end = static_cast<std::uint16_t>(begin + span_length);
                    if (begin == 0 && end == length) continue;
                    const scf::Span span{static_cast<scf::SentenceId>(sentence), begin, end};
                    ++proper;
                    raw_hits += raw_spans.contains(span) ? 1 : 0;
                    const auto found = indexed_span.find(span);
                    indexed_hits += found != indexed_span.end() && found->second ? 1 : 0;
                }
            }
        }
        result.raw_evidence_coverage =
            proper > 0 ? static_cast<double>(raw_hits) / proper : 0.0;
        result.indexed_evidence_coverage =
            proper > 0 ? static_cast<double>(indexed_hits) / proper : 0.0;
    }

    // Local-relation soundness and context-key purity vs latent roles.
    const auto roles = latent_roles(dataset.grammar);
    const auto role_of = [&](const scf::StringId yield)
        -> std::optional<std::set<std::string>> {
        const auto tokens = corpus.string_interner().tokens(yield);
        if (tokens.size() != 1) {
            return std::nullopt;
        }
        const auto text = corpus.string_interner().to_string(yield, corpus.token_interner());
        const auto found = roles.find(text);
        if (found == roles.end()) {
            return std::nullopt;
        }
        return found->second;
    };
    double purity_sum = 0.0;
    double purity_weight_sum = 0.0;
    double purity_weighted = 0.0;
    std::size_t purity_keys = 0;
    for (const auto& block : solver.blocks()) {
        std::vector<std::set<std::string>> block_roles;
        for (const auto yield : block.yields) {
            if (const auto role = role_of(yield)) {
                block_roles.push_back(*role);
            }
        }
        // pair-level soundness
        for (std::size_t a = 0; a < block.yields.size(); ++a) {
            for (std::size_t b = a + 1; b < block.yields.size(); ++b) {
                ++result.local_relation_pairs;
                const auto role_a = role_of(block.yields[a]);
                const auto role_b = role_of(block.yields[b]);
                if (!role_a || !role_b) {
                    ++result.unknown_pairs;
                    continue;
                }
                std::vector<std::string> shared;
                std::set_intersection(role_a->begin(), role_a->end(), role_b->begin(),
                                      role_b->end(), std::back_inserter(shared));
                if (shared.empty()) {
                    ++result.false_pairs;
                } else {
                    ++result.true_pairs;
                }
            }
        }
        if (block_roles.size() < 2) {
            continue;
        }
        std::map<std::string, std::size_t> role_counts;
        for (const auto& role_set : block_roles) {
            for (const auto& role : role_set) {
                ++role_counts[role];
            }
        }
        std::size_t majority = 0;
        for (const auto& [role, count] : role_counts) {
            majority = std::max(majority, count);
        }
        const auto purity = static_cast<double>(majority) /
                            static_cast<double>(block_roles.size());
        purity_sum += purity;
        purity_weighted += purity * static_cast<double>(block_roles.size());
        purity_weight_sum += static_cast<double>(block_roles.size());
        result.min_key_purity = std::min(result.min_key_purity, purity);
        ++purity_keys;
    }
    result.precision =
        result.true_pairs + result.false_pairs > 0
            ? static_cast<double>(result.true_pairs) /
                  static_cast<double>(result.true_pairs + result.false_pairs)
            : 1.0;
    result.mean_key_purity = purity_keys > 0 ? purity_sum / purity_keys : 1.0;
    result.weighted_key_purity =
        purity_weight_sum > 0 ? purity_weighted / purity_weight_sum : 1.0;
    // recall over enumerable gold substitutable token pairs (same-role
    // single tokens observed in the corpus)
    {
        std::vector<std::pair<std::string, scf::StringId>> tokens;
        for (scf::StringId s = 1; s < corpus.string_interner().size(); ++s) {
            if (corpus.string_interner().tokens(s).size() == 1) {
                tokens.emplace_back(
                    corpus.string_interner().to_string(s, corpus.token_interner()), s);
            }
        }
        std::size_t gold_pairs = 0;
        std::size_t recovered = 0;
        for (std::size_t a = 0; a < tokens.size(); ++a) {
            for (std::size_t b = a + 1; b < tokens.size(); ++b) {
                const auto role_a = roles.find(tokens[a].first);
                const auto role_b = roles.find(tokens[b].first);
                if (role_a == roles.end() || role_b == roles.end()) {
                    continue;
                }
                std::vector<std::string> shared;
                std::set_intersection(role_a->second.begin(), role_a->second.end(),
                                      role_b->second.begin(), role_b->second.end(),
                                      std::back_inserter(shared));
                if (shared.empty()) {
                    continue;
                }
                ++gold_pairs;
                recovered += solver.locally_related_any(tokens[a].second, tokens[b].second)
                                 ? 1
                                 : 0;
            }
        }
        if (gold_pairs > 0) {
            result.recall = static_cast<double>(recovered) / gold_pairs;
        }
    }
    // distinct yield pairs: exact round-0 blocks vs final abstract blocks
    {
        std::map<scf::RawContextKey, std::set<scf::StringId>> exact_blocks;
        for (const auto& record : corpus.context_records()) {
            exact_blocks[{record.triple.left, record.triple.right}].insert(
                record.triple.yield);
        }
        std::set<std::pair<scf::StringId, scf::StringId>> round0_pairs, final_pairs;
        for (const auto& [key, yields] : exact_blocks) {
            for (auto a = yields.begin(); a != yields.end(); ++a) {
                for (auto b = std::next(a); b != yields.end(); ++b) {
                    round0_pairs.emplace(*a, *b);
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
        result.distinct_pairs_round0 = round0_pairs.size();
        result.distinct_pairs_final = final_pairs.size();
    }

    // multi-role surfaces: distinct final keys with block >= 2 per yield
    {
        std::map<scf::StringId, std::size_t> role_keys;
        for (const auto& block : solver.blocks()) {
            if (block.yields.size() < 2) {
                continue;
            }
            for (const auto yield : block.yields) {
                ++role_keys[yield];
            }
        }
        double total = 0.0;
        for (const auto& [yield, count] : role_keys) {
            total += static_cast<double>(count);
            result.multi_role_surfaces += count >= 2 ? 1 : 0;
            result.max_roles_per_surface = std::max(result.max_roles_per_surface, count);
        }
        result.mean_roles_per_surface =
            role_keys.empty() ? 0.0 : total / static_cast<double>(role_keys.size());
    }
    return result;
}

void run_v14_synthetic(const std::filesystem::path& directory) {
    const std::vector<std::string> grammars{
        "ab_cartesian",          "simple_np_vp",
        "symmetric_abc",         "hierarchical_correlated_balanced",
        "hierarchical_correlated_right", "hierarchical_correlated_left",
        "ambiguous_surface_roles", "recursive_context_cascade",
        "ambiguous_lexicon",     "ccg_lite"};
    auto metrics = open_file(directory / "synthetic_indexed_metrics.csv");
    metrics << "grammar,signature,initial_context_classes,final_context_classes,"
               "context_abstraction_collapse_ratio,largest_context_abstraction_class,"
               "largest_context_abstraction_class_ratio,round_count,context_key_count,"
               "mean_local_role_block_size,median_local_role_block_size,"
               "p90_local_role_block_size,p99_local_role_block_size,"
               "max_local_role_block_size,max_local_role_block_ratio,"
               "projected_graph_components,projected_giant_component_ratio,"
               "relations_round0,relations_final,recursive_relation_gain,"
               "distinct_pairs_round0,distinct_pairs_final,"
               "raw_evidence_coverage,indexed_evidence_coverage,coverage_gain,"
               "multi_role_surfaces,mean_roles_per_surface,max_roles_per_surface\n";
    auto role_metrics = open_file(directory / "local_role_metrics.csv");
    role_metrics << "grammar,signature,local_relation_pairs,local_relation_true_pairs,"
                    "local_relation_false_pairs,unknown_pairs,local_relation_precision,"
                    "local_relation_recall,mean_context_key_purity,"
                    "weighted_context_key_purity,min_context_key_purity\n";
    for (const auto& grammar : grammars) {
        const auto dataset = scf::generate_dataset(grammar, 1.0, 1);
        std::ostringstream text;
        for (const auto& sentence : dataset.sentences) {
            for (std::size_t index = 0; index < sentence.tokens.size(); ++index) {
                text << (index == 0 ? "" : " ") << sentence.tokens[index];
            }
            text << '\n';
        }
        scf::Corpus corpus;
        std::istringstream input(text.str());
        corpus.load(input);
        for (const auto signature : {scf::AbstractionSignature::ContextOnly,
                                     scf::AbstractionSignature::ContextPlusConcat}) {
            scf::ContextIndexedSolver solver(corpus, signature);
            solver.run();
            const auto result = analyze_indexed(dataset, corpus, solver);
            const auto& d = result.diagnostics;
            metrics << grammar << ',' << scf::abstraction_signature_name(signature) << ','
                    << d.initial_context_classes << ',' << d.final_context_classes << ','
                    << fmt6(d.context_abstraction_collapse_ratio) << ','
                    << d.largest_context_abstraction_class << ','
                    << fmt6(d.largest_context_abstraction_class_ratio) << ','
                    << d.round_count << ',' << d.context_key_count << ','
                    << fmt6(d.mean_local_role_block_size) << ','
                    << fmt6(d.median_local_role_block_size) << ','
                    << fmt6(d.p90_local_role_block_size) << ','
                    << fmt6(d.p99_local_role_block_size) << ','
                    << d.max_local_role_block_size << ','
                    << fmt6(d.max_local_role_block_ratio) << ','
                    << d.projected_graph_components << ','
                    << fmt6(d.projected_giant_component_ratio) << ','
                    << result.relations_round0 << ',' << result.relations_final << ','
                    << fmt6(result.recursive_relation_gain) << ','
                    << result.distinct_pairs_round0 << ',' << result.distinct_pairs_final << ','
                    << fmt6(result.raw_evidence_coverage) << ','
                    << fmt6(result.indexed_evidence_coverage) << ','
                    << fmt6(result.indexed_evidence_coverage - result.raw_evidence_coverage)
                    << ',' << result.multi_role_surfaces << ','
                    << fmt6(result.mean_roles_per_surface) << ','
                    << result.max_roles_per_surface << '\n';
            role_metrics << grammar << ',' << scf::abstraction_signature_name(signature) << ','
                         << result.local_relation_pairs << ',' << result.true_pairs << ','
                         << result.false_pairs << ',' << result.unknown_pairs << ','
                         << fmt6(result.precision) << ','
                         << (result.recall ? fmt6(*result.recall) : "N/A") << ','
                         << fmt6(result.mean_key_purity) << ','
                         << fmt6(result.weighted_key_purity) << ','
                         << fmt6(result.min_key_purity) << '\n';
        }
        std::cout << "v14 synthetic: " << grammar << " done\n";
    }
}

std::string yields_text(const scf::Corpus& corpus, const std::vector<scf::StringId>& yields,
                        const std::size_t cap = 12) {
    std::string text;
    for (std::size_t index = 0; index < std::min(cap, yields.size()); ++index) {
        text += (index == 0 ? "" : ", ");
        text += '"' +
                corpus.string_interner().to_string(yields[index], corpus.token_interner()) + '"';
    }
    if (yields.size() > cap) {
        text += ", ...";
    }
    return text;
}

// Multi-round replay with textual trace for recursive_context_cascade
// (context_plus_concat), plus the multi-role dump for
// ambiguous_surface_roles.
void run_v14_traces(const std::filesystem::path& directory) {
    {
        const auto dataset = scf::generate_dataset("recursive_context_cascade", 1.0, 1);
        std::ostringstream text;
        for (const auto& sentence : dataset.sentences) {
            for (std::size_t index = 0; index < sentence.tokens.size(); ++index) {
                text << (index == 0 ? "" : " ") << sentence.tokens[index];
            }
            text << '\n';
        }
        scf::Corpus corpus;
        std::istringstream input(text.str());
        corpus.load(input);
        auto trace = open_file(directory / "recursive_cascade_trace.txt");
        trace << "recursive_context_cascade (corpus: \"w a m\", \"w b m\")\n"
              << "signature = context_plus_concat (context_only saturates in one round;\n"
              << "see the one-round idempotence theorem in IMPLEMENTATION_NOTES)\n\n";
        // naive replay with class-content tracking
        const auto count = corpus.string_interner().size();
        const auto epsilon = corpus.string_interner().epsilon_id();
        std::vector<std::size_t> label(count);
        for (scf::StringId s = 0; s < count; ++s) label[s] = s;
        const auto text_of = [&](const scf::StringId s) {
            return corpus.string_interner().to_string(s, corpus.token_interner());
        };
        std::set<std::set<scf::StringId>> previous_classes;
        std::set<std::tuple<std::size_t, std::size_t, scf::StringId, scf::StringId>>
            previous_relations;
        for (std::size_t round = 0;; ++round) {
            using Key = std::pair<std::size_t, std::size_t>;
            std::map<scf::StringId, std::set<Key>> profile, decomposition;
            std::map<Key, std::set<scf::StringId>> blocks;
            for (const auto& record : corpus.context_records()) {
                const Key key{label[record.triple.left], label[record.triple.right]};
                profile[record.triple.yield].insert(key);
                blocks[key].insert(record.triple.yield);
            }
            for (const auto& triple : corpus.concat_triples()) {
                decomposition[triple.result].insert({label[triple.left], label[triple.right]});
            }
            trace << "ROUND " << round << "\n";
            // newly merged abstraction classes vs previous round
            std::map<std::size_t, std::set<scf::StringId>> parts;
            for (scf::StringId s = 0; s < count; ++s) parts[label[s]].insert(s);
            std::set<std::set<scf::StringId>> classes;
            for (const auto& [key, members] : parts) classes.insert(members);
            if (round > 0) {
                trace << "  newly merged context abstraction classes:\n";
                bool any = false;
                for (const auto& members : classes) {
                    if (members.size() >= 2 && !previous_classes.contains(members)) {
                        trace << "    { ";
                        for (const auto member : members) trace << '"' << text_of(member) << "\" ";
                        trace << "}\n";
                        any = true;
                    }
                }
                if (!any) trace << "    (none)\n";
            }
            // newly induced local relations
            std::set<std::tuple<std::size_t, std::size_t, scf::StringId, scf::StringId>>
                relations;
            for (const auto& [key, yields] : blocks) {
                for (auto a = yields.begin(); a != yields.end(); ++a) {
                    for (auto b = std::next(a); b != yields.end(); ++b) {
                        relations.insert({key.first, key.second, *a, *b});
                    }
                }
            }
            trace << "  context keys = " << blocks.size()
                  << ", local relations = " << relations.size() << "\n";
            if (round > 0) {
                trace << "  newly induced local relations:\n";
                bool any = false;
                for (const auto& [kl, kr, a, b] : relations) {
                    bool is_new = true;
                    for (const auto& [pl, pr, pa, pb] : previous_relations) {
                        if (pa == a && pb == b) {
                            is_new = false;  // same yield pair already related somewhere
                            break;
                        }
                    }
                    if (is_new) {
                        trace << "    \"" << text_of(a) << "\" ~ \"" << text_of(b) << "\"\n";
                        any = true;
                    }
                }
                if (!any) trace << "    (none)\n";
            }
            trace << "\n";
            previous_classes = classes;
            previous_relations = relations;
            // next partition (context_plus_concat)
            std::map<std::pair<std::set<Key>, std::set<Key>>, std::size_t> groups;
            std::vector<std::size_t> next(count);
            for (scf::StringId s = 0; s < count; ++s) {
                if (s == epsilon) {
                    next[s] = count + 1;
                    continue;
                }
                const auto entry =
                    groups.emplace(std::pair(profile[s], decomposition[s]), groups.size());
                next[s] = entry.first->second;
            }
            std::map<std::size_t, std::set<scf::StringId>> next_parts;
            for (scf::StringId s = 0; s < count; ++s) next_parts[next[s]].insert(s);
            std::set<std::set<scf::StringId>> next_classes;
            for (const auto& [key, members] : next_parts) next_classes.insert(members);
            if (next_classes == classes) {
                trace << "FIXED POINT after " << round << " productive rounds\n";
                break;
            }
            label = next;
        }
        std::cout << "wrote recursive_cascade_trace.txt\n";
    }
    {
        const auto dataset = scf::generate_dataset("ambiguous_surface_roles", 1.0, 1);
        std::ostringstream text;
        for (const auto& sentence : dataset.sentences) {
            for (std::size_t index = 0; index < sentence.tokens.size(); ++index) {
                text << (index == 0 ? "" : " ") << sentence.tokens[index];
            }
            text << '\n';
        }
        scf::Corpus corpus;
        std::istringstream input(text.str());
        corpus.load(input);
        scf::ContextIndexedSolver solver(corpus);
        solver.run();
        auto output = open_file(directory / "ambiguous_surface_roles.txt");
        output << "ambiguous_surface_roles: one surface form, several local roles, no split,\n"
                  "no global collapse (context_only signature, full coverage)\n\n";
        for (scf::StringId s = 1; s < corpus.string_interner().size(); ++s) {
            if (corpus.string_interner().tokens(s).size() != 1) {
                continue;
            }
            const auto keys = solver.keys_of_yield(s);
            std::size_t role_keys = 0;
            std::string detail;
            for (const auto& key : keys) {
                const auto found = std::lower_bound(
                    solver.blocks().begin(), solver.blocks().end(), key,
                    [](const scf::LocalRoleBlock& block, const scf::ContextKey& target) {
                        return block.context < target;
                    });
                if (found == solver.blocks().end() || !(found->context == key) ||
                    found->yields.size() < 2) {
                    continue;
                }
                ++role_keys;
                detail += "  key(" + std::to_string(key.left) + "," +
                          std::to_string(key.right) + ") block_size=" +
                          std::to_string(found->yields.size()) + ": " +
                          yields_text(corpus, found->yields) + "\n";
            }
            output << "surface_form = \""
                   << corpus.string_interner().to_string(s, corpus.token_interner())
                   << "\"\nlocal_role_count = " << role_keys << "\n"
                   << detail << "\n";
        }
        std::cout << "wrote ambiguous_surface_roles.txt\n";
    }
}

// Experimental shadow parser vs raw/opportunity on the v1.4 benchmark
// (spec §30-31). Never the default parser.
void run_v14_shadow(const std::filesystem::path& directory) {
    const std::vector<std::string> grammars{
        "nested_balanced", "simple_np_vp", "symmetric_abc",
        "hierarchical_correlated_balanced", "hierarchical_correlated_right",
        "hierarchical_correlated_left"};
    auto output = open_file(directory / "indexed_shadow_parse_metrics.csv");
    output << "grammar,evidence_source,gold_in_argmax_rate,unique_optimal_rate,"
              "exact_unique_match_rate,mean_argmax_size,forced_precision_observable,"
              "forced_recall_observable\n";
    for (const auto& grammar : grammars) {
        const auto dataset = scf::generate_dataset(grammar, 1.0, 1);
        std::ostringstream text;
        for (const auto& sentence : dataset.sentences) {
            for (std::size_t index = 0; index < sentence.tokens.size(); ++index) {
                text << (index == 0 ? "" : " ") << sentence.tokens[index];
            }
            text << '\n';
        }
        scf::Corpus corpus;
        std::istringstream input(text.str());
        corpus.load(input);
        const auto gold = scf::dataset_gold_trees(dataset);
        const auto observable = scf::dataset_observable_gold(dataset);
        const auto evaluate_with = [&](const std::vector<scf::SpanEvidence>& evidence,
                                       const std::string& source) {
            const auto analyses = scf::analyze_sentences(corpus, evidence);
            const auto evaluation =
                scf::evaluate_corpus(analyses, gold, evidence, {}, observable);
            output << grammar << ',' << source << ','
                   << fmt6(evaluation.gold_in_argmax_rate) << ','
                   << fmt6(evaluation.unique_optimal_rate) << ','
                   << fmt6(evaluation.exact_unique_match_rate) << ','
                   << fmt6(evaluation.mean_argmax_size) << ','
                   << fmt6(evaluation.forced_precision_observable_gold) << ','
                   << fmt6(evaluation.forced_recall_observable_gold) << '\n';
        };
        evaluate_with(scf::EvidenceBuilder(corpus).span_evidence(), "raw");
        evaluate_with(
            scf::EvidenceBuilder(corpus, scf::EvidenceObjective::OpportunityNormalized)
                .span_evidence(),
            "opportunity");
        scf::ContextIndexedSolver solver(corpus);
        solver.run();
        evaluate_with(scf::indexed_shadow_evidence(corpus, solver), "indexed-shadow");
    }
    std::cout << "wrote indexed_shadow_parse_metrics.csv\n";
}

}  // namespace

int main(int argc, char** argv) {
    scf::platform::initialize_console();
    try {
        std::optional<std::filesystem::path> output_directory;
        std::uint64_t seeds = 20;
        std::string subcommand = "v121";
        std::vector<std::size_t> cardinalities{2, 3, 4, 5};
        std::vector<scf::EvidenceObjective> objectives = scf::all_evidence_objectives();
        int index = 1;
        if (index < argc && argv[index][0] != '-') {
            subcommand = argv[index];
            ++index;
        }
        for (; index < argc; ++index) {
            const std::string_view argument(argv[index]);
            const auto value = [&]() -> std::string {
                if (index + 1 >= argc) {
                    throw std::runtime_error("missing value after " + std::string(argument));
                }
                return argv[++index];
            };
            if (argument == "--help" || argument == "-h") {
                std::cout << "Usage: scf_audit [v121|objective-bias|objective-grid|"
                             "rho-objectives|v13] --output-dir DIR\n"
                             "       [--seeds N] [--K csv] [--evidence-objectives csv] [--n 4]\n";
                return 0;
            }
            if (argument == "--output-dir") {
                output_directory = value();
            } else if (argument == "--seeds") {
                seeds = std::stoull(value());
            } else if (argument == "--K") {
                cardinalities.clear();
                std::istringstream stream(value());
                std::string part;
                while (std::getline(stream, part, ',')) {
                    cardinalities.push_back(std::stoull(part));
                }
            } else if (argument == "--n") {
                if (std::stoull(value()) != 4) {
                    throw std::runtime_error("objective-bias supports n = 4 only");
                }
            } else if (argument == "--evidence-objectives") {
                objectives.clear();
                std::istringstream stream(value());
                std::string part;
                while (std::getline(stream, part, ',')) {
                    objectives.push_back(scf::parse_evidence_objective(part));
                }
            } else {
                throw std::runtime_error("unknown argument: " + std::string(argument));
            }
        }
        if (!output_directory) {
            throw std::runtime_error("--output-dir is required");
        }
        std::filesystem::create_directories(*output_directory);
        if (subcommand == "v121") {
            write_observational_equivalence_report(*output_directory);
            run_saturation_ablation(*output_directory);
            run_span_length_bias(*output_directory);
            run_tree_shape_scores(*output_directory);
            run_audit_grid(*output_directory, seeds);
            run_rho_sweep(*output_directory);
        } else if (subcommand == "objective-bias") {
            run_objective_bias(*output_directory, cardinalities, objectives);
        } else if (subcommand == "objective-grid") {
            run_objective_grid(*output_directory, seeds, objectives);
        } else if (subcommand == "rho-objectives") {
            run_rho_by_objective(*output_directory, objectives);
        } else if (subcommand == "v13") {
            run_objective_bias(*output_directory, cardinalities, objectives);
            run_objective_grid(*output_directory, seeds, objectives);
            run_rho_by_objective(*output_directory, objectives);
        } else if (subcommand == "v14-synthetic") {
            run_v14_synthetic(*output_directory);
            run_v14_traces(*output_directory);
            run_v14_shadow(*output_directory);
        } else {
            throw std::runtime_error("unknown subcommand '" + subcommand + "'");
        }
        std::cout << "audit complete: " << output_directory->string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "scf_audit: " << error.what() << '\n';
        return 1;
    }
}
