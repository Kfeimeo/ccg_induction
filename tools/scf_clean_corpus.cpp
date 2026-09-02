// SCF v2.3.1 -- clean-corpus replication driver (same v2.3 learner, new corpus).

#include "scf/clean_corpus.hpp"
#include "scf/platform.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void usage() {
    std::cout
        << "usage: scf_clean_corpus --input FILE [--output-dir DIR] [--label NAME]\n"
           "       [--preprocess clean_body|v23_condition_d] [--punctuation keep|drop]\n"
           "       [--scales 100000,1000000,10000000] [--max-substring-length 3]\n"
           "       [--examples 20] [--largest-classes 20] [--class-members 40]\n"
           "       [--hub-stats-only]   (only measure the (eps,eps) hub per scale)\n"
           "       [--probe-objects '<num>,the,a,in,to']\n"
           "clean_body input: one body paragraph per line, documents separated by an\n"
           "empty line (tools/extract_wiki_body.py --mode body).\n"
           "v23_condition_d input: one flattened document per line (the v2.3 corpus).\n";
}

}  // namespace

int main(int argc, char** argv) {
    scf::platform::initialize_console();
    scf::v231::CleanCorpusConfig config;
    bool hub_stats_only = false;
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
            } else if (arg == "--label") {
                config.corpus_label = next();
            } else if (arg == "--preprocess") {
                const auto value = next();
                if (value == "clean_body") {
                    config.read.preprocess = scf::v231::Preprocess::clean_body;
                } else if (value == "v23_condition_d") {
                    config.read.preprocess = scf::v231::Preprocess::v23_condition_d;
                } else {
                    throw std::runtime_error("unknown preprocess " + value);
                }
            } else if (arg == "--punctuation") {
                const auto value = next();
                if (value == "keep") {
                    config.read.keep_punctuation = true;
                } else if (value == "drop") {
                    config.read.keep_punctuation = false;
                } else {
                    throw std::runtime_error("unknown punctuation mode " + value);
                }
            } else if (arg == "--scales") {
                config.scales.clear();
                std::istringstream parts(next());
                std::string part;
                while (std::getline(parts, part, ',')) {
                    config.scales.push_back(std::stoull(part));
                }
            } else if (arg == "--max-substring-length") {
                config.max_substring_length = std::stoull(next());
            } else if (arg == "--examples") {
                config.example_limit = std::stoull(next());
            } else if (arg == "--largest-classes") {
                config.largest_classes = std::stoull(next());
            } else if (arg == "--class-members") {
                config.class_members_shown = std::stoull(next());
            } else if (arg == "--probe-objects") {
                config.probe_objects.clear();
                std::istringstream parts(next());
                std::string part;
                while (std::getline(parts, part, ',')) {
                    config.probe_objects.push_back(part);
                }
            } else if (arg == "--hub-stats-only") {
                hub_stats_only = true;
            } else if (arg == "--help" || arg == "-h") {
                usage();
                return 0;
            } else {
                throw std::runtime_error("unknown argument " + arg);
            }
        }
        if (config.input_text.empty()) {
            throw std::runtime_error("--input is required");
        }
        if (hub_stats_only) {
            auto read = config.read;
            std::vector<std::uint64_t> scales = config.scales;
            std::sort(scales.begin(), scales.end());
            if (read.token_budget == 0) {
                read.token_budget =
                    read.preprocess == scf::v231::Preprocess::v23_condition_d
                        ? static_cast<std::uint64_t>(static_cast<double>(scales.back()) * 1.25) +
                              2'000'000
                        : scales.back() + 1;
            }
            const auto corpus = scf::v231::read_sentence_corpus(config.input_text, read);
            const auto stats =
                scf::v231::empty_frame_hub_stats(corpus, scales, config.max_substring_length);
            std::filesystem::create_directories(config.output_dir);
            std::ofstream csv(config.output_dir / "empty_frame_hub_by_scale.csv");
            csv << "corpus,nominal_tokens,actual_tokens,sentences,short_sentence_occurrences,"
                   "distinct_complete_spans,hub_candidate_pairs\n";
            for (const auto& row : stats) {
                csv << config.corpus_label << ',' << row.nominal_tokens << ',' << row.actual_tokens
                    << ',' << row.sentences << ',' << row.short_sentence_occurrences << ','
                    << row.distinct_complete_spans << ',' << row.hub_candidate_pairs << '\n';
                std::cout << "scale " << row.nominal_tokens << ": sentences " << row.sentences
                          << ", short sentence occurrences " << row.short_sentence_occurrences
                          << ", distinct complete spans (hub) " << row.distinct_complete_spans
                          << ", hub candidate pairs " << row.hub_candidate_pairs << "\n";
            }
            std::cout << "wrote " << (config.output_dir / "empty_frame_hub_by_scale.csv").string()
                      << "\n";
            return 0;
        }
        const auto result = scf::v231::run_clean_corpus_scaling(config);
        for (const auto& scale : result.scales) {
            const auto& empty =
                scale.frames.rows[static_cast<std::size_t>(scf::v231::FrameType::empty_frame)];
            std::cout << "scale " << scale.nominal_tokens << " (actual " << scale.actual_tokens
                      << ", sentences " << scale.sentences << ", documents "
                      << scale.documents << "): objects " << scale.merge.initial_objects
                      << ", witnesses " << scale.merge.local_witnesses << ", candidates "
                      << scale.merge.merge_candidates << ", accepted "
                      << scale.merge.accepted_candidates << ", rejected "
                      << scale.merge.rejected_candidates << ", classes "
                      << scale.merge.resulting_classes << ", largest "
                      << scale.merge.largest_class << ", empty-frame objects "
                      << scale.frames.objects_with_empty_frame << ", empty-frame-only accepted "
                      << empty.accepted_only << ", runtime " << scale.runtime_seconds
                      << " s, peak RSS " << scale.peak_rss_mb << " MB\n";
        }
        std::cout << "available sentences " << result.available_sentences << ", nominal tokens "
                  << result.available_nominal_tokens << "; wrote " << config.output_dir.string()
                  << "\n";
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        usage();
        return 1;
    }
    return 0;
}
