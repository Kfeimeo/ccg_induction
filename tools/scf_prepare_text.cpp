#include "scf/corpus.hpp"
#include "scf/evaluator.hpp"
#include "scf/formatter.hpp"
#include "scf/pipeline.hpp"
#include "scf/prepare_text.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void print_usage(std::ostream& output) {
    output << "Usage: scf_prepare_text --input raw.txt --output corpus.txt [options]\n"
              "  --max-len N              Keep sentences up to N tokens (default: 10)\n"
              "  --lowercase BOOL         Lowercase ASCII (default: true)\n"
              "  --strip-punctuation BOOL Replace ASCII punctuation with spaces (default: true)\n"
              "  --deduplicate BOOL       Drop duplicate sentences (default: true)\n"
              "  --drop-digits BOOL       Drop sentences containing digits or unprintable\n"
              "                           symbols (default: false)\n"
              "  --report FILE            Smoke report path (default: real_smoke_report.txt next\n"
              "                           to the output corpus)\n"
              "  --no-smoke               Skip the SCF smoke run\n"
              "\n"
              "Real data is a smoke test only: no parse accuracy is claimed without gold.\n";
}

std::string require_value(const int argc, char** argv, int& index) {
    if (index + 1 >= argc) {
        throw std::runtime_error("missing value after " + std::string(argv[index]));
    }
    ++index;
    return argv[index];
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

}  // namespace

int main(int argc, char** argv) {
    try {
        std::optional<std::string> input_path;
        std::optional<std::filesystem::path> output_path;
        std::optional<std::filesystem::path> report_path;
        scf::PrepareTextConfig config;
        bool smoke = true;
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument(argv[index]);
            if (argument == "--help" || argument == "-h") {
                print_usage(std::cout);
                return 0;
            }
            if (argument == "--input") {
                input_path = require_value(argc, argv, index);
            } else if (argument == "--output") {
                output_path = require_value(argc, argv, index);
            } else if (argument == "--max-len") {
                config.max_len = std::stoull(require_value(argc, argv, index));
            } else if (argument == "--lowercase") {
                config.lowercase = parse_bool(require_value(argc, argv, index));
            } else if (argument == "--strip-punctuation") {
                config.strip_punctuation = parse_bool(require_value(argc, argv, index));
            } else if (argument == "--deduplicate") {
                config.deduplicate = parse_bool(require_value(argc, argv, index));
            } else if (argument == "--drop-digits") {
                config.drop_digits = parse_bool(require_value(argc, argv, index));
            } else if (argument == "--report") {
                report_path = require_value(argc, argv, index);
            } else if (argument == "--no-smoke") {
                smoke = false;
            } else {
                throw std::runtime_error("unknown argument: " + std::string(argument));
            }
        }
        if (!input_path || !output_path) {
            throw std::runtime_error("--input and --output are required");
        }
        std::ifstream input(*input_path);
        if (!input) {
            throw std::runtime_error("cannot open input: " + *input_path);
        }
        const auto result = scf::prepare_text(input, config);
        if (output_path->has_parent_path()) {
            std::filesystem::create_directories(output_path->parent_path());
        }
        {
            std::ofstream output(*output_path);
            if (!output) {
                throw std::runtime_error("cannot write output: " + output_path->string());
            }
            for (const auto& sentence : result.sentences) {
                output << sentence << '\n';
            }
        }
        std::cout << "input_sentence_count = " << result.input_sentence_count << '\n'
                  << "kept_sentence_count = " << result.kept_sentence_count << '\n'
                  << "filtered_long = " << result.filtered_long << '\n'
                  << "filtered_symbols = " << result.filtered_symbols << '\n'
                  << "duplicate_sentences = " << result.duplicate_sentences << '\n'
                  << "distinct_tokens = " << result.distinct_tokens << '\n';
        if (!smoke) {
            return 0;
        }

        const auto report_file =
            report_path.value_or(output_path->parent_path() / "real_smoke_report.txt");
        scf::CorpusConfig corpus_config;
        corpus_config.max_sentence_length = config.max_len;
        scf::Corpus corpus(corpus_config);
        {
            std::ostringstream buffer;
            for (const auto& sentence : result.sentences) {
                buffer << sentence << '\n';
            }
            std::istringstream stream(buffer.str());
            corpus.load(stream);
        }
        const auto bundle = scf::analyze_corpus(corpus);
        const auto& final_stats = bundle.solver.statistics().back();
        std::size_t unique = 0;
        std::size_t ambiguous = 0;
        for (const auto& analysis : bundle.analyses) {
            unique += analysis.optimal_tree_count == 1 ? 1U : 0U;
            ambiguous += analysis.optimal_tree_count > 1 ? 1U : 0U;
        }
        const auto sentences = static_cast<double>(bundle.analyses.size());

        std::ofstream report(report_file);
        if (!report) {
            throw std::runtime_error("cannot write report: " + report_file.string());
        }
        report << "SCF real-data smoke report\n"
               << "This is a preprocessing/diagnostic smoke test only. No parse accuracy is\n"
               << "claimed: the corpus has no gold annotations.\n\n"
               << "input_sentence_count = " << result.input_sentence_count << '\n'
               << "kept_sentence_count = " << result.kept_sentence_count << '\n'
               << "filtered_long = " << result.filtered_long << '\n'
               << "filtered_symbols = " << result.filtered_symbols << '\n'
               << "duplicate_sentences = " << result.duplicate_sentences << '\n'
               << "distinct_tokens = " << result.distinct_tokens << '\n'
               << "distinct_strings = " << corpus.string_interner().size() << '\n'
               << "final_eclasses = " << final_stats.classes << '\n'
               << "collapse_ratio = " << final_stats.collapse_ratio << '\n'
               << "largest_eclass = " << final_stats.largest_class << '\n'
               << "unique_optimal_rate = "
               << (sentences > 0 ? static_cast<double>(unique) / sentences : 0.0) << '\n'
               << "ambiguous_optimal_rate = "
               << (sentences > 0 ? static_cast<double>(ambiguous) / sentences : 0.0) << "\n\n"
               << "top_eclasses:\n";
        scf::EvalConfig eval_config;
        eval_config.top_eclass_count = 10;
        scf::write_top_eclasses(report, corpus, bundle.solver, eval_config);
        report << "\nexample_trees (first 5 unique optima):\n";
        std::size_t shown = 0;
        for (std::size_t sentence = 0; sentence < bundle.analyses.size() && shown < 5; ++sentence) {
            const auto& analysis = bundle.analyses[sentence];
            if (analysis.optimal_tree_count != 1) {
                continue;
            }
            report << "  " << scf::format_unique_tree(
                                  corpus, static_cast<scf::SentenceId>(sentence), analysis)
                   << '\n';
            ++shown;
        }
        if (shown == 0) {
            report << "  (none)\n";
        }
        std::cout << "Wrote " << report_file.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "scf_prepare_text: " << error.what() << '\n';
        print_usage(std::cerr);
        return 1;
    }
}
