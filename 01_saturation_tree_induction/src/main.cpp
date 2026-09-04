#include "scf/corpus.hpp"
#include "scf/platform.hpp"
#include "scf/equivalence_solver.hpp"
#include "scf/evaluator.hpp"
#include "scf/evidence_builder.hpp"
#include "scf/formatter.hpp"
#include "scf/gold.hpp"
#include "scf/pipeline.hpp"
#include "scf/tree_solver.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct CliOptions {
    std::string input_path;
    std::optional<std::string> config_path;
    std::optional<std::string> gold_path;
    std::optional<std::filesystem::path> output_directory;
    scf::CorpusConfig config;
    scf::EvidenceObjective evidence_objective{scf::EvidenceObjective::RawCount};
    bool dump_classes{false};
    bool dump_proofs{false};
    bool dump_witnesses{false};
    bool dump_evidence{false};
    bool dump_optimal_forest{false};
    bool dump_trees{false};
    bool dump_one_optimal_tree_for_debug{false};
    bool eval{false};
    bool stats{false};
};

bool parse_bool(const std::string_view text) {
    if (text == "true" || text == "1") {
        return true;
    }
    if (text == "false" || text == "0") {
        return false;
    }
    throw std::runtime_error("expected true or false, got: " + std::string(text));
}

void print_usage(std::ostream& output) {
    output << "Usage: scf_cli --input corpus.txt [options]\n"
              "  --config FILE          Read key=value configuration\n"
              "  --max-len N            Maximum sentence length (default: 10)\n"
              "  --lowercase BOOL       Lowercase ASCII tokens\n"
              "  --deduplicate BOOL     Deduplicate sentence types\n"
              "  --stats                Print every saturation round\n"
              "  --dump-classes         Print all final e-classes\n"
              "  --dump-proofs          Print every successful union reason\n"
              "  --dump-witnesses       Print direct raw surface pair witnesses\n"
              "  --dump-evidence        Print occurrence scores and witness provenance\n"
              "  --dump-optimal-forest  Print all optimal splits in each DP cell\n"
              "  --dump-trees           Print parse details (summary is always printed)\n"
              "  --evidence-objective NAME\n"
              "                         raw_count (default) | opportunity | conditional | jaccard\n"
              "  --gold FILE            Gold constituent spans (sentence_id begin end label TSV)\n"
              "  --eval                 Evaluate the optimal forest against --gold\n"
              "  --dump-one-optimal-tree-for-debug\n"
              "                         For ambiguous sentences, print one optimal tree\n"
              "                         explicitly marked as a debug sample, never a prediction\n"
              "  --output-dir DIR       Export CSV and diagnostic text files\n";
}

std::string require_value(const int argc, char** argv, int& index) {
    if (index + 1 >= argc) {
        throw std::runtime_error("missing value after " + std::string(argv[index]));
    }
    ++index;
    return argv[index];
}

CliOptions parse_arguments(const int argc, char** argv) {
    CliOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--config") {
            options.config_path = require_value(argc, argv, index);
        }
    }
    if (options.config_path) {
        options.config = scf::load_config_file(*options.config_path);
    }
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help" || argument == "-h") {
            print_usage(std::cout);
            std::exit(0);
        }
        if (argument == "--input") {
            options.input_path = require_value(argc, argv, index);
        } else if (argument == "--config") {
            (void)require_value(argc, argv, index);
        } else if (argument == "--max-len") {
            options.config.max_sentence_length =
                static_cast<std::size_t>(std::stoull(require_value(argc, argv, index)));
        } else if (argument == "--lowercase") {
            options.config.lowercase = parse_bool(require_value(argc, argv, index));
        } else if (argument == "--deduplicate") {
            options.config.deduplicate_sentence_types = parse_bool(require_value(argc, argv, index));
        } else if (argument == "--dump-classes") {
            options.dump_classes = true;
        } else if (argument == "--dump-proofs") {
            options.dump_proofs = true;
        } else if (argument == "--dump-witnesses") {
            options.dump_witnesses = true;
        } else if (argument == "--dump-evidence") {
            options.dump_evidence = true;
        } else if (argument == "--dump-optimal-forest") {
            options.dump_optimal_forest = true;
        } else if (argument == "--dump-trees") {
            options.dump_trees = true;
        } else if (argument == "--dump-one-optimal-tree-for-debug") {
            options.dump_one_optimal_tree_for_debug = true;
        } else if (argument == "--evidence-objective") {
            options.evidence_objective =
                scf::parse_evidence_objective(require_value(argc, argv, index));
        } else if (argument == "--gold") {
            options.gold_path = require_value(argc, argv, index);
        } else if (argument == "--eval") {
            options.eval = true;
        } else if (argument == "--stats") {
            options.stats = true;
        } else if (argument == "--output-dir") {
            options.output_directory = require_value(argc, argv, index);
        } else {
            throw std::runtime_error("unknown argument: " + std::string(argument));
        }
    }
    if (options.input_path.empty()) {
        throw std::runtime_error("--input is required");
    }
    if (options.eval && !options.gold_path) {
        throw std::runtime_error("--eval requires --gold FILE");
    }
    return options;
}

void print_round(std::ostream& output, const scf::SaturationRoundStats& stats) {
    output << "round=" << stats.round << " classes=" << stats.classes
           << " context_unions=" << stats.context_unions << " concat_unions=" << stats.concat_unions
           << " largest_class=" << stats.largest_class << " collapse_ratio=" << std::fixed
           << std::setprecision(4) << stats.collapse_ratio;
    if (stats.fixed_point) {
        output << " fixed_point=true";
    }
    output << '\n';
}

void print_classes(std::ostream& output,
                   const scf::Corpus& corpus,
                   const scf::EquivalenceSolver& solver) {
    for (const auto& members : solver.all_classes()) {
        output << "EClass " << solver.eclass(members.front()) << " size=" << members.size() << '\n';
        for (const auto member : members) {
            output << "  \"" << corpus.string_interner().to_string(member, corpus.token_interner()) << "\"\n";
        }
    }
}

void print_reason(std::ostream& output,
                  const scf::Corpus& corpus,
                  const scf::UnionReason& reason,
                  const std::size_t reason_index) {
    const auto string_text = [&](const scf::StringId id) {
        return corpus.string_interner().to_string(id, corpus.token_interner());
    };
    output << "MERGE " << reason_index << '\n'
           << "lhs = \"" << string_text(reason.lhs) << "\"\n"
           << "rhs = \"" << string_text(reason.rhs) << "\"\n"
           << "round = " << reason.round << '\n'
           << "reason = " << scf::reason_kind_name(reason.kind) << '\n'
           << "canonical_key = (" << reason.left_class << ", " << reason.right_class << ")\n"
           << "source_records = (" << reason.source_record_a << ", " << reason.source_record_b << ")\n";
    if (reason.kind == scf::ReasonKind::ContextSubstitution) {
        const auto& lhs = corpus.context_records().at(static_cast<std::size_t>(reason.source_record_a)).triple;
        const auto& rhs = corpus.context_records().at(static_cast<std::size_t>(reason.source_record_b)).triple;
        output << "source_a = (\"" << string_text(lhs.left) << "\", \"" << string_text(lhs.right)
               << "\") -> \"" << string_text(lhs.yield) << "\"\n"
               << "source_b = (\"" << string_text(rhs.left) << "\", \"" << string_text(rhs.right)
               << "\") -> \"" << string_text(rhs.yield) << "\"\n";
    } else {
        const auto& lhs = corpus.concat_triples().at(static_cast<std::size_t>(reason.source_record_a));
        const auto& rhs = corpus.concat_triples().at(static_cast<std::size_t>(reason.source_record_b));
        output << "source_a = \"" << string_text(lhs.left) << "\" + \"" << string_text(lhs.right)
               << "\" -> \"" << string_text(lhs.result) << "\"\n"
               << "source_b = \"" << string_text(rhs.left) << "\" + \"" << string_text(rhs.right)
               << "\" -> \"" << string_text(rhs.result) << "\"\n";
    }
    output << '\n';
}

void print_proofs(std::ostream& output,
                  const scf::Corpus& corpus,
                  const scf::EquivalenceSolver& solver) {
    for (std::size_t index = 0; index < solver.reasons().size(); ++index) {
        print_reason(output, corpus, solver.reasons()[index], index);
    }
}

std::string surface_text(const scf::Corpus& corpus, const scf::StringId id) {
    if (id == corpus.string_interner().epsilon_id()) {
        return "<epsilon>";
    }
    return corpus.string_interner().to_string(id, corpus.token_interner());
}

void print_witnesses(std::ostream& output,
                     const scf::Corpus& corpus,
                     const scf::EvidenceBuilder& builder) {
    for (const auto& pair : builder.pairs()) {
        output << "  pair=(\"" << surface_text(corpus, pair.first) << "\", \""
               << surface_text(corpus, pair.second) << "\") support=" << pair.contexts.size() << '\n';
        for (const auto context_id : pair.contexts) {
            const auto& context = builder.raw_context(context_id);
            output << "    (\"" << surface_text(corpus, context.left) << "\", \""
                   << surface_text(corpus, context.right) << "\")\n";
        }
    }
}

void print_sentence_analysis(std::ostream& output,
                             const scf::Corpus& corpus,
                             const std::span<const scf::SpanEvidence> evidence,
                             const std::span<const scf::TreeSolveResult> analyses,
                             const bool dump_evidence,
                             const bool dump_optimal_forest,
                             const bool dump_one_optimal_tree_for_debug) {
    for (std::size_t sentence = 0; sentence < analyses.size(); ++sentence) {
        const auto sentence_id = static_cast<scf::SentenceId>(sentence);
        const auto& analysis = analyses[sentence];
        output << "sentence " << sentence << ": " << scf::format_sentence(corpus, sentence_id) << '\n';
        output << "  EVIDENCE:\n";
        for (const auto& item : evidence) {
            if (item.span.sentence != sentence_id) {
                continue;
            }
            output << "    " << scf::format_span(item.span) << " \""
                   << scf::format_span_yield(corpus, item.span) << "\" score=" << item.score;
            if (dump_evidence) {
                output << " best_alternatives={";
                for (std::size_t index = 0; index < item.best_alternatives.size(); ++index) {
                    if (index != 0) {
                        output << ", ";
                    }
                    output << '\"' << surface_text(corpus, item.best_alternatives[index]) << '\"';
                }
                output << "} witnesses={";
                for (std::size_t index = 0; index < item.witnesses.size(); ++index) {
                    if (index != 0) {
                        output << ", ";
                    }
                    output << "(\"" << surface_text(corpus, item.witnesses[index].left) << "\", \""
                           << surface_text(corpus, item.witnesses[index].right) << "\")";
                }
                output << '}';
            }
            output << '\n';
        }
        output << "  FORCED_OPTIMAL:\n";
        for (const auto& span : analysis.forced_spans) {
            if (span.end == span.begin + 1 || (span.begin == 0 && span.end == analysis.sentence_length)) {
                continue;
            }
            output << "    " << scf::format_span(span) << " \"" << scf::format_span_yield(corpus, span)
                   << "\"\n";
        }
        output << "  hard_consistent=" << (analysis.hard_consistent ? "true" : "false") << '\n';
        for (const auto& [lhs, rhs] : analysis.crossing_conflicts) {
            output << "    hard_conflict: " << scf::format_span(lhs) << " x "
                   << scf::format_span(rhs) << '\n';
        }
        output << "  best_score=" << analysis.best_score << '\n';
        output << "  optimal_tree_count=" << analysis.optimal_tree_count;
        if (analysis.overflowed) {
            output << " (saturated: uint64 overflow)";
        }
        output << '\n';
        if (dump_optimal_forest && analysis.hard_consistent) {
            const auto dimension = static_cast<std::size_t>(analysis.sentence_length) + 1;
            output << "  OPTIMAL_FOREST:\n";
            for (std::uint16_t length = 2; length <= analysis.sentence_length; ++length) {
                for (std::uint16_t begin = 0; begin + length <= analysis.sentence_length; ++begin) {
                    const auto end = static_cast<std::uint16_t>(begin + length);
                    const auto& splits = analysis.optimal_splits[static_cast<std::size_t>(begin) * dimension + end];
                    output << "    [" << begin << ',' << end << ") splits={";
                    for (std::size_t index = 0; index < splits.size(); ++index) {
                        if (index != 0) {
                            output << ',';
                        }
                        output << splits[index];
                    }
                    output << "}\n";
                }
            }
        }
        if (analysis.optimal_tree_count == 1) {
            output << "  tree=" << scf::format_unique_tree(corpus, sentence_id, analysis) << '\n';
        } else if (analysis.optimal_tree_count > 1) {
            output << "  tree=<ambiguous; no tie-break>\n";
            if (dump_one_optimal_tree_for_debug) {
                output << "  debug_tree=" << scf::format_one_optimal_tree(corpus, sentence_id, analysis)
                       << "  (debug sample of the optimal forest; NOT a prediction)\n";
            }
        }
        output << '\n';
    }
}

std::string csv_quote(std::string value) {
    std::size_t position = 0;
    while ((position = value.find('\"', position)) != std::string::npos) {
        value.insert(position, 1, '\"');
        position += 2;
    }
    return "\"" + value + "\"";
}

void export_diagnostics(const std::filesystem::path& directory,
                        const scf::Corpus& corpus,
                        const scf::EquivalenceSolver& solver,
                        const scf::EvidenceBuilder& builder,
                        const std::span<const scf::TreeSolveResult> analyses) {
    std::filesystem::create_directories(directory);
    {
        std::ofstream output(directory / "saturation.csv");
        if (!output) {
            throw std::runtime_error("cannot write saturation.csv");
        }
        output << "round,classes,context_unions,concat_unions,largest_class,collapse_ratio\n";
        for (const auto& stats : solver.statistics()) {
            output << stats.round << ',' << stats.classes << ',' << stats.context_unions << ','
                   << stats.concat_unions << ',' << stats.largest_class << ',' << std::fixed
                   << std::setprecision(6) << stats.collapse_ratio << '\n';
        }
    }
    {
        std::ofstream output(directory / "pair_witnesses.csv");
        if (!output) {
            throw std::runtime_error("cannot write pair_witnesses.csv");
        }
        output << "yield_a,yield_b,support\n";
        for (const auto& pair : builder.pairs()) {
            output << csv_quote(surface_text(corpus, pair.first)) << ','
                   << csv_quote(surface_text(corpus, pair.second)) << ',' << pair.contexts.size() << '\n';
        }
    }
    {
        std::ofstream output(directory / "span_evidence.csv");
        if (!output) {
            throw std::runtime_error("cannot write span_evidence.csv");
        }
        output << "sentence_id,begin,end,yield,score,best_alternative_count\n";
        for (const auto& item : builder.span_evidence()) {
            output << item.span.sentence << ',' << item.span.begin << ',' << item.span.end << ','
                   << csv_quote(surface_text(corpus, item.yield)) << ',' << item.score << ','
                   << item.best_alternatives.size() << '\n';
        }
    }
    {
        std::ofstream output(directory / "sentence_analysis.csv");
        if (!output) {
            throw std::runtime_error("cannot write sentence_analysis.csv");
        }
        output << "sentence_id,length,best_score,optimal_tree_count,candidate_span_count,"
                  "forced_optimal_span_count,hard_consistent,unique_optimal\n";
        for (std::size_t sentence = 0; sentence < analyses.size(); ++sentence) {
            const auto& analysis = analyses[sentence];
            const auto sentence_id = static_cast<scf::SentenceId>(sentence);
            const auto candidate_count = static_cast<std::size_t>(std::count_if(
                builder.span_evidence().begin(), builder.span_evidence().end(),
                [sentence_id](const auto& item) { return item.span.sentence == sentence_id; }));
            const auto forced_count = static_cast<std::size_t>(std::count_if(
                analysis.forced_spans.begin(), analysis.forced_spans.end(), [&](const auto& span) {
                    return span.end > span.begin + 1 &&
                           !(span.begin == 0 && span.end == analysis.sentence_length);
                }));
            output << sentence << ',' << analysis.sentence_length << ',' << analysis.best_score << ','
                   << analysis.optimal_tree_count << ',' << candidate_count << ',' << forced_count << ','
                   << (analysis.hard_consistent ? "true" : "false") << ','
                   << (analysis.optimal_tree_count == 1 ? "true" : "false") << '\n';
        }
    }
    {
        std::ofstream output(directory / "eclasses.txt");
        if (!output) {
            throw std::runtime_error("cannot write eclasses.txt");
        }
        print_classes(output, corpus, solver);
    }
    {
        std::ofstream output(directory / "proofs.txt");
        if (!output) {
            throw std::runtime_error("cannot write proofs.txt");
        }
        print_proofs(output, corpus, solver);
    }
}

}  // namespace

int main(int argc, char** argv) {
    scf::platform::initialize_console();
    try {
        const auto options = parse_arguments(argc, argv);
        scf::Corpus corpus(options.config);
        corpus.load_file(options.input_path);

        auto bundle = scf::analyze_corpus(corpus, options.evidence_objective);
        const auto& solver = bundle.solver;
        const auto& evidence_builder = bundle.builder;
        const auto& evidence = evidence_builder.span_evidence();
        const auto& analyses = bundle.analyses;

        std::cout << "Corpus:\n"
                  << "  input_sentences = " << corpus.summary().input_sentences << '\n'
                  << "  sentence_types = " << corpus.sentences().size() << '\n'
                  << "  duplicate_sentences = " << corpus.summary().duplicate_sentences << '\n'
                  << "  tokens = " << corpus.token_interner().size() << '\n'
                  << "  distinct_strings = " << corpus.string_interner().size() << '\n'
                  << "  context_triples = " << corpus.context_records().size() << '\n'
                  << "  concat_triples = " << corpus.concat_triples().size() << "\n\n";

        std::cout << "Saturation:\n";
        if (options.stats) {
            for (const auto& stats : solver.statistics()) {
                std::cout << "  ";
                print_round(std::cout, stats);
            }
        } else {
            std::cout << "  rounds = " << (solver.statistics().size() - 1) << " (including fixed-point check)\n";
        }
        const auto& final_stats = solver.statistics().back();
        std::cout << "\nFinal:\n"
                  << "  eclasses = " << final_stats.classes << '\n'
                  << "  largest_eclass = " << final_stats.largest_class << '\n'
                  << "  collapse_ratio = " << std::fixed << std::setprecision(4) << final_stats.collapse_ratio
                  << '\n'
                  << "  successful_unions = " << solver.reasons().size() << "\n\n";

        if (options.dump_classes) {
            std::cout << "EClasses:\n";
            print_classes(std::cout, corpus, solver);
            std::cout << '\n';
        }
        if (options.dump_proofs) {
            std::cout << "Proofs:\n";
            print_proofs(std::cout, corpus, solver);
        }
        std::cout << "Evidence:\n"
                  << "  raw_contexts = " << evidence_builder.summary().raw_contexts << '\n'
                  << "  yield_pairs_with_support = "
                  << evidence_builder.summary().yield_pairs_with_support << '\n'
                  << "  candidate_occurrences = "
                  << evidence_builder.summary().candidate_occurrences << '\n'
                  << "  max_pair_support = " << evidence_builder.summary().max_pair_support << "\n\n";
        if (options.dump_witnesses) {
            std::cout << "Witnesses:\n";
            print_witnesses(std::cout, corpus, evidence_builder);
            std::cout << '\n';
        }

        std::size_t unique = 0;
        std::size_t ambiguous = 0;
        std::size_t conflicted = 0;
        for (const auto& analysis : analyses) {
            unique += analysis.optimal_tree_count == 1 ? 1U : 0U;
            ambiguous += analysis.optimal_tree_count > 1 ? 1U : 0U;
            conflicted += !analysis.crossing_conflicts.empty() ? 1U : 0U;
        }
        std::cout << "Parsing:\n"
                  << "  sentences = " << analyses.size() << '\n'
                  << "  unique_optimal = " << unique << '\n'
                  << "  ambiguous_optimal = " << ambiguous << '\n'
                  << "  hard_conflicted = " << conflicted << "\n\n";
        if (options.dump_trees || options.dump_evidence || options.dump_optimal_forest) {
            print_sentence_analysis(std::cout,
                                    corpus,
                                    evidence,
                                    analyses,
                                    options.dump_evidence,
                                    options.dump_optimal_forest,
                                    options.dump_one_optimal_tree_for_debug);
        }

        std::optional<scf::CorpusEvaluation> evaluation;
        std::vector<scf::GoldTree> gold_trees;
        if (options.eval) {
            const auto rows = scf::read_gold_span_file(*options.gold_path);
            const auto lengths = scf::corpus_sentence_lengths(corpus);
            gold_trees = scf::assemble_gold_trees(rows, lengths);
            evaluation = scf::evaluate_corpus(analyses, gold_trees, evidence);
            scf::print_evaluation_summary(std::cout, *evaluation);
            std::cout << '\n';
        }

        if (options.output_directory) {
            export_diagnostics(*options.output_directory, corpus, solver, evidence_builder, analyses);
            if (evaluation) {
                const scf::EvalConfig eval_config;
                const auto diagnostics = scf::collapse_diagnostics(corpus, solver, eval_config);
                const auto open = [&](const char* name) {
                    std::ofstream output(*options.output_directory / name);
                    if (!output) {
                        throw std::runtime_error(std::string("cannot write ") + name);
                    }
                    return output;
                };
                {
                    auto output = open("metrics.json");
                    scf::write_metrics_json(output, scf::RunInfo{}, corpus, diagnostics, *evaluation);
                }
                {
                    auto output = open("sentence_metrics.tsv");
                    scf::write_sentence_metrics_tsv(output, corpus, gold_trees, analyses, *evaluation);
                }
                {
                    auto output = open("failure_examples.txt");
                    scf::write_failure_examples(output, corpus, gold_trees, analyses, evidence,
                                                *evaluation, eval_config);
                }
                {
                    auto output = open("top_eclasses.txt");
                    scf::write_top_eclasses(output, corpus, solver, eval_config);
                }
            }
            std::cout << "Exported diagnostics to " << options.output_directory->string() << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "scf_cli: " << error.what() << '\n';
        print_usage(std::cerr);
        return 1;
    }
}
