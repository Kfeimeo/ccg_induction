#include "scf/synthetic.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void print_usage(std::ostream& output) {
    output << "Usage: scf_generate --grammar NAME --output-dir DIR [options]\n"
              "  --grammar NAME       Grammar family (see --list-grammars)\n"
              "  --coverage X         Fraction of the full language in (0, 1] (default: 1.0)\n"
              "  --seed N             Deterministic sampling seed (default: 1)\n"
              "  --max-sentences N    Extra cap after coverage sampling; 0 = none (default: 0)\n"
              "  --output-dir DIR     Writes corpus.txt, gold_spans.tsv, gold_brackets.txt,\n"
              "                       grammar.json\n"
              "  --list-grammars      Print available grammar families and exit\n";
}

std::string require_value(const int argc, char** argv, int& index) {
    if (index + 1 >= argc) {
        throw std::runtime_error("missing value after " + std::string(argv[index]));
    }
    ++index;
    return argv[index];
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::optional<std::string> grammar;
        std::optional<std::filesystem::path> output_directory;
        double coverage = 1.0;
        std::uint64_t seed = 1;
        std::size_t max_sentences = 0;
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument(argv[index]);
            if (argument == "--help" || argument == "-h") {
                print_usage(std::cout);
                return 0;
            }
            if (argument == "--list-grammars") {
                for (const auto& name : scf::known_grammar_names()) {
                    std::cout << name << '\n';
                }
                return 0;
            }
            if (argument == "--grammar") {
                grammar = require_value(argc, argv, index);
            } else if (argument == "--coverage") {
                coverage = std::stod(require_value(argc, argv, index));
            } else if (argument == "--seed") {
                seed = std::stoull(require_value(argc, argv, index));
            } else if (argument == "--max-sentences") {
                max_sentences = std::stoull(require_value(argc, argv, index));
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
        const auto dataset = scf::generate_dataset(*grammar, coverage, seed, max_sentences);
        scf::write_dataset(dataset, *output_directory);
        std::cout << "grammar = " << dataset.grammar_name << '\n'
                  << "seed = " << dataset.seed << '\n'
                  << "coverage = " << dataset.coverage << '\n'
                  << "full_sentence_count = " << dataset.full_sentence_count << '\n'
                  << "sampled_sentence_count = " << dataset.sentences.size() << '\n'
                  << "output_dir = " << output_directory->string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "scf_generate: " << error.what() << '\n';
        print_usage(std::cerr);
        return 1;
    }
}
