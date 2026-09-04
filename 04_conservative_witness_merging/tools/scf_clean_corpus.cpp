// SCF v2.3.1 -- clean-corpus replication driver (unchanged v2.3 learner).

#include "scf/clean_corpus.hpp"
#include "scf/platform.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void usage() {
    std::cout << "usage: scf_clean_corpus --input FILE [--output-dir DIR]\n"
                 "       [--corpus LABEL] [--preprocessing structured|v23d]\n"
                 "       [--scales 100000,1000000,10000000] [--max-substring-length 3]\n"
                 "       [--ud FILE] [--examples 20] [--largest-classes 20]\n";
}

}  // namespace

int main(int argc, char** argv) {
    scf::platform::initialize_console();
    scf::v231::CleanCorpusConfig config;
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
            } else if (arg == "--preprocessing") {
                config.preprocessing = next();
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
            } else if (arg == "--largest-classes") {
                config.largest_classes = std::stoull(next());
            } else if (arg == "--help" || arg == "-h") {
                usage();
                return 0;
            } else {
                throw std::runtime_error("unknown argument " + arg);
            }
        }
        if (config.input.empty()) {
            throw std::runtime_error("--input is required");
        }
        const auto result = scf::v231::run_clean_corpus_scaling(config);
        for (const auto& scale : result.scales) {
            std::cout << config.corpus_label << '/' << config.preprocessing << " scale "
                      << scale.nominal_tokens << " (actual " << scale.actual_tokens
                      << ", sentences " << scale.sentences << "): objects "
                      << scale.merge.initial_objects << ", witnesses "
                      << scale.merge.local_witnesses << ", candidates "
                      << scale.merge.merge_candidates << ", accepted "
                      << scale.merge.accepted_candidates << ", rejected "
                      << scale.merge.rejected_candidates << ", classes "
                      << scale.merge.resulting_classes << ", largest "
                      << scale.merge.largest_class << ", empty-frame objects "
                      << scale.frames.objects_with_empty_frame << ", runtime "
                      << scale.runtime_seconds << "s, peak RSS " << scale.peak_rss_mb
                      << " MB\n";
        }
        std::cout << "wrote clean_corpus_scaling.csv, frame_type_metrics.csv and diagnostics to "
                  << config.output_dir.string() << "\n";
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        usage();
        return 1;
    }
    return 0;
}
