// SCF v2.4 -- counterexample-guided closed-world refinement driver.

#include "scf/closed_world.hpp"
#include "scf/platform.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void usage() {
    std::cout << "usage: scf_closed_world --oracle-only [--output-dir DIR]\n"
                 "       scf_closed_world --input FILE.scs [--output-dir DIR] [--corpus LABEL]\n"
                 "       [--scales 100000,200000,400000,1000000]\n"
                 "       [--universes all,internal,boundary] [--ud FILE]\n"
                 "       [--max-substring-length 3] [--examples 20] [--largest-classes 20]\n"
                 "       [--compare-v23-max-scale N] [--no-oracle-check]\n"
                 "       [--probe \"u|v\" ...]\n";
}

}  // namespace

int main(int argc, char** argv) {
    scf::platform::initialize_console();
    scf::v24::ClosedWorldConfig config;
    bool oracle_only = false;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            const auto next = [&]() {
                if (i + 1 >= argc) {
                    throw std::runtime_error("missing value for " + arg);
                }
                return std::string(argv[++i]);
            };
            if (arg == "--input") {
                config.input = next();
            } else if (arg == "--output-dir") {
                config.output_dir = next();
            } else if (arg == "--corpus") {
                config.corpus_label = next();
            } else if (arg == "--scales") {
                config.scales.clear();
                std::istringstream parts(next());
                std::string part;
                while (std::getline(parts, part, ',')) {
                    config.scales.push_back(std::stoull(part));
                }
            } else if (arg == "--universes") {
                config.universes.clear();
                std::istringstream parts(next());
                std::string part;
                while (std::getline(parts, part, ',')) {
                    config.universes.push_back(scf::v24::parse_universe(part));
                }
            } else if (arg == "--ud") {
                config.ud_conllu = next();
            } else if (arg == "--max-substring-length") {
                config.max_substring_length = std::stoull(next());
            } else if (arg == "--examples") {
                config.example_limit = std::stoull(next());
            } else if (arg == "--largest-classes") {
                config.largest_classes = std::stoull(next());
            } else if (arg == "--compare-v23-max-scale") {
                config.compare_v23_max_scale = std::stoull(next());
            } else if (arg == "--no-oracle-check") {
                config.oracle_check = false;
            } else if (arg == "--probe") {
                const auto value = next();
                const auto bar = value.find('|');
                if (bar == std::string::npos) {
                    throw std::runtime_error("--probe expects \"u|v\"");
                }
                config.probe_pairs.emplace_back(value.substr(0, bar), value.substr(bar + 1));
            } else if (arg == "--oracle-only") {
                oracle_only = true;
            } else if (arg == "--help" || arg == "-h") {
                usage();
                return 0;
            } else {
                throw std::runtime_error("unknown argument " + arg);
            }
        }
        if (oracle_only) {
            const auto report = scf::v24::run_oracle_cases(config.output_dir);
            std::cout << report;
            return report.find("FAIL") == std::string::npos ? 0 : 1;
        }
        if (config.input.empty()) {
            throw std::runtime_error("--input is required (or --oracle-only)");
        }
        const auto result = scf::v24::run_closed_world_scaling(config);
        for (const auto& row : result.rows) {
            const auto& m = row.metrics;
            std::cout << config.corpus_label << '/'
                      << scf::v24::kUniverseNames[static_cast<std::size_t>(row.universe)]
                      << " scale " << row.nominal_tokens << " (actual " << row.actual_tokens
                      << ", sentences " << row.sentences << "): objects " << m.initial_objects
                      << ", universe contexts " << m.universe_contexts << ", effective splitters "
                      << m.effective_splitters << ", rounds " << m.refinement_rounds
                      << ", classes " << m.final_classes << ", largest " << m.largest_class
                      << ", nontrivial " << m.nontrivial_classes << ", terminal-only "
                      << row.terminal.terminal_only_objects << ", queries "
                      << m.membership_queries << ", oracle "
                      << (row.oracle_identical == 1 ? "identical"
                                                     : row.oracle_identical == 0 ? "DIFFERS"
                                                                                  : "unchecked")
                      << ", build " << row.table_build_seconds << "s, refine "
                      << m.runtime_seconds << "s, peak RSS " << row.peak_rss_mb << " MB\n";
        }
        std::cout << "wrote closed_world_scaling.csv, distinguishing_contexts.txt, "
                     "class_examples.txt and oracle_comparison.txt to "
                  << config.output_dir.string() << "\n";
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        usage();
        return 1;
    }
    return 0;
}
