// SCF v2.0 — Oracle Category Recovery experiment driver.
//
// Runs the full deterministic sweep (grammar families x L x k, plus the
// positive-only coverage ablation) and writes category_recovery.csv,
// composition_recovery.csv, positive_only_recovery.csv, and
// oracle_summary.txt into the output directory.

#include "scf/oracle_v2.hpp"

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> split_csv(const std::string& text) {
    std::vector<std::string> parts;
    std::istringstream stream(text);
    std::string part;
    while (std::getline(stream, part, ',')) {
        if (!part.empty()) {
            parts.push_back(part);
        }
    }
    return parts;
}

void print_usage() {
    std::cout << "usage: scf_oracle_v2 [--output-dir DIR] [--grammars a,b,...]\n"
                 "                     [--min-len N] [--max-len N] [--max-k K]\n"
                 "                     [--coverages 0.05,...] [--seeds 1,2,3]\n"
                 "                     [--list-grammars]\n";
}

}  // namespace

int main(int argc, char** argv) {
    scf::v2::OracleExperimentConfig config;
    std::string output_dir = "results_v2_oracle";
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            const auto next = [&]() -> std::string {
                if (i + 1 >= argc) {
                    throw std::runtime_error("missing value for " + arg);
                }
                return argv[++i];
            };
            if (arg == "--output-dir") {
                output_dir = next();
            } else if (arg == "--grammars") {
                config.grammars = split_csv(next());
            } else if (arg == "--min-len") {
                config.min_len = std::stoul(next());
            } else if (arg == "--max-len") {
                config.max_len = std::stoul(next());
            } else if (arg == "--max-k") {
                config.max_k = std::stoul(next());
            } else if (arg == "--coverages") {
                config.coverages.clear();
                for (const auto& part : split_csv(next())) {
                    config.coverages.push_back(std::stod(part));
                }
            } else if (arg == "--seeds") {
                config.seeds.clear();
                for (const auto& part : split_csv(next())) {
                    config.seeds.push_back(std::stoull(part));
                }
            } else if (arg == "--list-grammars") {
                for (const auto& name : scf::v2::oracle_grammar_names()) {
                    std::cout << name << '\n';
                }
                return 0;
            } else if (arg == "--help" || arg == "-h") {
                print_usage();
                return 0;
            } else {
                throw std::runtime_error("unknown argument " + arg);
            }
        }
        if (config.min_len < 1 || config.min_len > config.max_len) {
            throw std::runtime_error("invalid length range");
        }
        scf::v2::run_oracle_experiment(config, output_dir);
        std::cout << "wrote category_recovery.csv, composition_recovery.csv, "
                     "positive_only_recovery.csv, oracle_summary.txt to "
                  << output_dir << '\n';
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        print_usage();
        return 1;
    }
    return 0;
}
