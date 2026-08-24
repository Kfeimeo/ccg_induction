#include "scf/audit.hpp"
#include "scf/corpus.hpp"
#include "scf/equivalence_solver.hpp"
#include "scf/evaluator.hpp"
#include "scf/pipeline.hpp"
#include "scf/synthetic.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void print_usage(std::ostream& output) {
    output << "Usage: scf_experiment --grammar NAME --output-dir DIR [options]\n"
              "  --grammar NAME        Grammar family (see scf_generate --list-grammars)\n"
              "  --coverage-grid CSV   Coverage values (default: "
              "0.05,0.10,0.20,0.40,0.60,0.80,1.00)\n"
              "  --seeds CSV           Seeds (default: 1,2,3,4,5)\n"
              "  --max-sentences N     Extra cap after coverage sampling; 0 = none (default: 0)\n"
              "  --lexical-cardinality K\n"
              "                        Tokens per lexical class, 2..5; 0 = family default\n"
              "  --symmetry-breaking-rate RHO\n"
              "                        symmetric_abc only: marker-sentence rate in [0, 1]\n"
              "  --run-saturation BOOL Run equivalence saturation (default: true). Parsing uses\n"
              "                        raw witnesses either way; false skips the diagnostic\n"
              "                        saturation engine entirely (v1.2.1 ablation aid)\n"
              "  --output-dir DIR      Per-run directories cov_X_seed_Y plus summary.csv\n";
}

std::string require_value(const int argc, char** argv, int& index) {
    if (index + 1 >= argc) {
        throw std::runtime_error("missing value after " + std::string(argv[index]));
    }
    ++index;
    return argv[index];
}

std::vector<std::string> split_csv(const std::string& text) {
    std::vector<std::string> parts;
    std::string part;
    std::istringstream stream(text);
    while (std::getline(stream, part, ',')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    if (parts.empty()) {
        throw std::runtime_error("empty comma-separated list: " + text);
    }
    return parts;
}

std::string coverage_tag(const double coverage) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.2f", coverage);
    return buffer;
}

bool parse_bool(const std::string_view text) {
    if (text == "true" || text == "1") {
        return true;
    }
    if (text == "false" || text == "0") {
        return false;
    }
    throw std::runtime_error("expected true or false, got: " + std::string(text));
}

void write_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("cannot write " + path.string());
    }
    output << content;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::optional<std::string> grammar;
        std::optional<std::filesystem::path> output_directory;
        std::string coverage_grid = "0.05,0.10,0.20,0.40,0.60,0.80,1.00";
        std::string seeds_csv = "1,2,3,4,5";
        std::size_t max_sentences = 0;
        std::size_t lexical_cardinality = 0;
        double symmetry_breaking_rate = 0.0;
        bool run_saturation = true;
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument(argv[index]);
            if (argument == "--help" || argument == "-h") {
                print_usage(std::cout);
                return 0;
            }
            if (argument == "--grammar") {
                grammar = require_value(argc, argv, index);
            } else if (argument == "--coverage-grid") {
                coverage_grid = require_value(argc, argv, index);
            } else if (argument == "--seeds") {
                seeds_csv = require_value(argc, argv, index);
            } else if (argument == "--max-sentences") {
                max_sentences = std::stoull(require_value(argc, argv, index));
            } else if (argument == "--lexical-cardinality") {
                lexical_cardinality = std::stoull(require_value(argc, argv, index));
            } else if (argument == "--symmetry-breaking-rate") {
                symmetry_breaking_rate = std::stod(require_value(argc, argv, index));
            } else if (argument == "--run-saturation") {
                run_saturation = parse_bool(require_value(argc, argv, index));
            } else if (argument == "--output-dir") {
                output_directory = require_value(argc, argv, index);
            } else {
                throw std::runtime_error("unknown argument: " + std::string(argument));
            }
        }
        if (!grammar) {
            throw std::runtime_error("--grammar is required");
        }
        if (!output_directory) {
            throw std::runtime_error("--output-dir is required");
        }
        std::vector<double> coverages;
        for (const auto& part : split_csv(coverage_grid)) {
            coverages.push_back(std::stod(part));
        }
        std::vector<std::uint64_t> seeds;
        for (const auto& part : split_csv(seeds_csv)) {
            seeds.push_back(std::stoull(part));
        }

        std::filesystem::create_directories(*output_directory);
        std::ofstream summary(*output_directory / "summary.csv");
        if (!summary) {
            throw std::runtime_error("cannot write summary.csv");
        }
        summary << scf::summary_csv_header() << '\n';

        const scf::EvalConfig eval_config;
        for (const auto coverage : coverages) {
            for (const auto seed : seeds) {
                const auto dataset = scf::generate_dataset(*grammar, coverage, seed, max_sentences,
                                                           lexical_cardinality,
                                                           symmetry_breaking_rate);
                const auto run_directory =
                    *output_directory /
                    ("cov_" + coverage_tag(coverage) + "_seed_" + std::to_string(seed));
                scf::write_dataset(dataset, run_directory);

                scf::Corpus corpus;
                corpus.load_file((run_directory / "corpus.txt").string());
                // Tree evidence always comes from exact raw surface contexts;
                // the saturation engine is a structural diagnostic on top.
                const scf::EvidenceBuilder builder(corpus);
                const auto analyses = scf::analyze_sentences(corpus, builder.span_evidence());
                std::optional<scf::EquivalenceSolver> solver;
                if (run_saturation) {
                    solver.emplace(corpus.string_interner().size(), corpus.context_records(),
                                   corpus.concat_triples());
                    solver->saturate();
                }
                const auto gold = scf::dataset_gold_trees(dataset);
                const auto evaluation =
                    scf::evaluate_corpus(analyses, gold, builder.span_evidence(), eval_config);
                scf::CollapseDiagnostics diagnostics;
                if (solver) {
                    diagnostics = scf::collapse_diagnostics(corpus, *solver, eval_config);
                } else {
                    diagnostics.final_eclasses = corpus.string_interner().size();
                    diagnostics.largest_eclass = corpus.string_interner().size() > 0 ? 1 : 0;
                    diagnostics.largest_eclass_ratio =
                        corpus.string_interner().size() > 0
                            ? 1.0 / static_cast<double>(corpus.string_interner().size())
                            : 0.0;
                }
                const auto hashes = scf::compute_dataset_hashes(dataset, corpus, builder);

                scf::RunInfo info;
                info.grammar = dataset.grammar_name;
                info.seed = dataset.seed;
                info.coverage = dataset.coverage;
                info.full_sentence_count = dataset.full_sentence_count;
                info.sampled_sentence_count = dataset.sentences.size();
                info.lexical_cardinality = dataset.lexical_cardinality;
                info.symmetry_breaking_rate = dataset.symmetry_breaking_rate;
                info.surface_language_hash = scf::hash_hex(hashes.surface_language);
                info.sampled_corpus_hash = scf::hash_hex(hashes.sampled_corpus);
                info.raw_context_relation_hash = scf::hash_hex(hashes.raw_context_relation);
                info.raw_witness_relation_hash = scf::hash_hex(hashes.raw_witness_relation);

                {
                    std::ofstream output(run_directory / "metrics.json");
                    scf::write_metrics_json(output, info, corpus, diagnostics, evaluation);
                }
                {
                    std::ofstream output(run_directory / "sentence_metrics.tsv");
                    scf::write_sentence_metrics_tsv(output, corpus, gold, analyses, evaluation);
                }
                {
                    const auto lengths = scf::corpus_sentence_lengths(corpus);
                    const auto stats =
                        scf::score_by_span_length(lengths, builder.span_evidence(), gold);
                    std::ofstream output(run_directory / "score_by_span_length.csv");
                    scf::write_score_by_span_length_csv(output, stats);
                }
                if (solver) {
                    {
                        std::ofstream output(run_directory / "saturation.csv");
                        scf::write_saturation_csv(output, *solver);
                    }
                    std::ofstream output(run_directory / "top_eclasses.txt");
                    scf::write_top_eclasses(output, corpus, *solver, eval_config);
                }
                {
                    std::ofstream output(run_directory / "failure_examples.txt");
                    scf::write_failure_examples(output, corpus, gold, analyses,
                                                builder.span_evidence(), evaluation, eval_config);
                }
                {
                    std::ostringstream text;
                    text << "grammar = " << dataset.grammar_name << '\n'
                         << "seed = " << dataset.seed << '\n'
                         << "coverage = " << coverage_tag(dataset.coverage) << '\n'
                         << "lexical_cardinality = " << dataset.lexical_cardinality << '\n'
                         << "symmetry_breaking_rate = " << dataset.symmetry_breaking_rate << '\n'
                         << "run_saturation = " << (run_saturation ? "true" : "false") << '\n'
                         << "full_sentence_count = " << dataset.full_sentence_count << '\n'
                         << "sampled_sentence_count = " << dataset.sentences.size() << '\n'
                         << "distinct_strings = " << corpus.string_interner().size() << '\n'
                         << "context_triples = " << corpus.context_records().size() << '\n'
                         << "concat_triples = " << corpus.concat_triples().size() << '\n';
                    if (solver) {
                        const auto& final_stats = solver->statistics().back();
                        text << "final_eclasses = " << final_stats.classes << '\n'
                             << "largest_eclass = " << final_stats.largest_class << '\n'
                             << "successful_unions = " << solver->reasons().size() << '\n';
                    } else {
                        text << "saturation = skipped (--run-saturation false)\n";
                    }
                    text << '\n';
                    std::ostringstream evaluation_text;
                    scf::print_evaluation_summary(evaluation_text, evaluation);
                    write_file(run_directory / "scf_output.txt",
                               text.str() + evaluation_text.str());
                }

                summary << scf::summary_csv_row(info, corpus, diagnostics, evaluation) << '\n';
                std::cout << "run cov=" << coverage_tag(coverage) << " seed=" << seed
                          << " sentences=" << dataset.sentences.size()
                          << " unique_optimal_rate=" << evaluation.unique_optimal_rate
                          << " gold_in_argmax_rate=" << evaluation.gold_in_argmax_rate << '\n';
            }
        }
        std::cout << "Wrote " << (*output_directory / "summary.csv").string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "scf_experiment: " << error.what() << '\n';
        print_usage(std::cerr);
        return 1;
    }
}
