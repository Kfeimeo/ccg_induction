// SCF v2.3 -- conservative evidence-driven merging driver.

#include "scf/conservative_merging.hpp"
#include "scf/platform.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void usage() {
    std::cout << "usage: scf_conservative_merging --input FILE [--output-dir DIR]\n"
                 "       [--scales 100000,1000000,10000000,100000000]\n"
                 "       [--max-substring-length 3] [--ud FILE] [--examples 20]\n"
                 "       [--oracle-only]\n";
}

}  // namespace

int main(int argc, char** argv) {
    scf::platform::initialize_console();
    scf::v23::ConservativeScalingConfig config;
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
            } else if (arg == "--max-substring-length") {
                config.max_substring_length = std::stoull(next());
            } else if (arg == "--ud") {
                config.ud_conllu = next();
            } else if (arg == "--examples") {
                config.example_limit = std::stoull(next());
            } else if (arg == "--oracle-only") {
                oracle_only = true;
            } else if (arg == "--help" || arg == "-h") {
                usage();
                return 0;
            } else {
                throw std::runtime_error("unknown argument " + arg);
            }
        }
        const auto oracle = scf::v23::run_conservative_oracle_sanity(config.output_dir);
        std::cout << oracle;
        if (oracle_only) {
            return 0;
        }
        if (config.input_text.empty()) {
            throw std::runtime_error("--input is required unless --oracle-only is used");
        }
        const auto result = scf::v23::run_conservative_scaling(config);
        for (const auto& scale : result.scales) {
            std::cout << "scale " << scale.nominal_tokens << " (condition D actual "
                      << scale.actual_tokens << "): objects "
                      << scale.merge.initial_objects << ", candidates "
                      << scale.merge.merge_candidates << ", accepted "
                      << scale.merge.accepted_candidates << ", rejected "
                      << scale.merge.rejected_candidates << ", classes "
                      << scale.merge.resulting_classes << "\n";
        }
        std::cout << "wrote conservative_scaling.csv and diagnostics to "
                  << config.output_dir.string() << "\n";
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        usage();
        return 1;
    }
    return 0;
}
