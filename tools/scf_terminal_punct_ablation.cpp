// SCF v2.2 — Terminal x Punctuation ablation driver.
//
// Runs conditions A-E over the same corpus and the same sentence-level
// train/held-out split, reusing the v2.1 evidence machinery verbatim, and
// writes terminal_punctuation_ablation.csv (per-condition rows plus
// delta_terminal / delta_punct rows) and ablation_neighborhood_samples.txt.

#include "scf/real_scaling.hpp"

#include <iostream>
#include <sstream>
#include <string>

namespace {

void print_usage() {
    std::cout << "usage: scf_terminal_punct_ablation --input FILE [--output-dir DIR]\n"
                 "                        [--scales 100000,1000000,...] [--heldout-tokens N]\n"
                 "                        [--conditions ABCDE] [--ud CONLLU]\n"
                 "                        [--hub-cap D] [--min-count-floor F]\n"
                 "                        [--min-count-rel R] [--pairs-per-bucket K]\n";
}

}  // namespace

int main(int argc, char** argv) {
    scf::v21::AblationConfig config;
    config.output_dir = "results_v2_2_ablation";
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            const auto next = [&]() -> std::string {
                if (i + 1 >= argc) {
                    throw std::runtime_error("missing value for " + arg);
                }
                return argv[++i];
            };
            if (arg == "--input") {
                config.input_text = next();
            } else if (arg == "--output-dir") {
                config.output_dir = next();
            } else if (arg == "--scales") {
                config.scales.clear();
                std::istringstream parts(next());
                std::string part;
                while (std::getline(parts, part, ',')) {
                    config.scales.push_back(std::stoull(part));
                }
            } else if (arg == "--heldout-tokens") {
                config.heldout_tokens = std::stoull(next());
            } else if (arg == "--conditions") {
                config.conditions = next();
            } else if (arg == "--ud") {
                config.base.ud_conllu = next();
            } else if (arg == "--hub-cap") {
                config.base.hub_cap = static_cast<std::uint32_t>(std::stoul(next()));
            } else if (arg == "--min-count-floor") {
                config.base.min_count_floor = std::stoull(next());
            } else if (arg == "--min-count-rel") {
                config.base.min_count_rel = std::stod(next());
            } else if (arg == "--pairs-per-bucket") {
                config.base.pairs_per_bucket = std::stoull(next());
            } else if (arg == "--help" || arg == "-h") {
                print_usage();
                return 0;
            } else {
                throw std::runtime_error("unknown argument " + arg);
            }
        }
        if (config.input_text.empty()) {
            throw std::runtime_error("--input is required");
        }
        const auto result = scf::v21::run_terminal_punct_ablation(config);
        std::cout << "sentences: " << result.sentences << " (held-out "
                  << result.heldout_sentences << ")\n";
        for (const auto& entry : result.conditions) {
            std::cout << "condition " << entry.condition << ":";
            for (std::size_t s = 0; s < entry.scales.size(); ++s) {
                std::cout << " scale " << entry.scales[s].scale_tokens << " -> "
                          << entry.actual_tokens[s] << " tokens, "
                          << entry.scales[s].distinct_pairs << " pairs ("
                          << entry.scales[s].runtime_seconds << " s)";
            }
            std::cout << "\n";
        }
        std::cout << "wrote terminal_punctuation_ablation.csv, "
                     "ablation_neighborhood_samples.txt to "
                  << config.output_dir.string() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        print_usage();
        return 1;
    }
    return 0;
}
