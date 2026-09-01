// SCF v2.1 — Real Corpus Scaling Experiment driver.
//
// Streams a real English corpus (one document per line; see
// tools/fetch_wiki_corpus.py), runs the nested token-count ladder, and writes
// scaling_metrics.csv, pair_evidence_scaling.csv, heldout_replication.csv,
// and neighborhood_samples.txt into the output directory.

#include "scf/real_scaling.hpp"

#include <iostream>
#include <sstream>
#include <string>

namespace {

void print_usage() {
    std::cout << "usage: scf_real_scaling --input FILE [--output-dir DIR]\n"
                 "                        [--scales 100000,300000,...]\n"
                 "                        [--heldout-tokens N] [--hub-cap D]\n"
                 "                        [--min-count-floor F] [--min-count-rel R]\n"
                 "                        [--ud CONLLU] [--pairs-per-bucket K]\n"
                 "                        [--dump-pairs-limit N]\n";
}

}  // namespace

int main(int argc, char** argv) {
    scf::v21::RealScalingConfig config;
    config.output_dir = "results_v2_1_real";
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
            } else if (arg == "--hub-cap") {
                config.hub_cap = static_cast<std::uint32_t>(std::stoul(next()));
            } else if (arg == "--min-count-floor") {
                config.min_count_floor = std::stoull(next());
            } else if (arg == "--min-count-rel") {
                config.min_count_rel = std::stod(next());
            } else if (arg == "--ud") {
                config.ud_conllu = next();
            } else if (arg == "--pairs-per-bucket") {
                config.pairs_per_bucket = std::stoull(next());
            } else if (arg == "--dump-pairs-limit") {
                config.dump_pairs_limit = std::stoull(next());
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
        const auto result = scf::v21::run_real_scaling(config);
        std::cout << "corpus: " << result.corpus_real_tokens << " tokens in "
                  << result.corpus_documents << " documents, vocab " << result.vocab_size
                  << ", held-out " << result.heldout_tokens_used << " tokens\n";
        for (const auto& scale : result.scales) {
            std::cout << "scale " << scale.scale_tokens << ": substrings "
                      << scale.substrings_total << ", context records "
                      << scale.context_records << ", distinct pairs " << scale.distinct_pairs
                      << ", " << scale.runtime_seconds << " s, peak RSS "
                      << scale.peak_rss_mb << " MB\n";
        }
        std::cout << "wrote scaling_metrics.csv, pair_evidence_scaling.csv, "
                     "heldout_replication.csv, neighborhood_samples.txt to "
                  << config.output_dir.string() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        print_usage();
        return 1;
    }
    return 0;
}
