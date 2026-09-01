#pragma once

// SCF v2.1 — Real Corpus Scaling Experiment.
//
// Independent module (namespace scf::v21, no dependency on the v1.x core or
// the v2.0 oracle module) that measures how far exact-context substitution
// evidence converges as the amount of real English text grows, with no
// abstraction, no thresholding tricks, and no supervision:
//
//  - fixed deterministic tokenizer/normalization; compact token ids; the raw
//    corpus text is never held in memory (only the uint32 id stream);
//  - nested token-count scales; per scale, high-frequency substrings of
//    length 1..3 under a relative min-frequency floor;
//  - exact observed single-token contexts C_N(u) = {(L, R) : L u R occurs};
//    absence is never negative evidence;
//  - shared-context evidence I_N(u, v) = C_N(u) ∩ C_N(v), with full curves
//    over evidence thresholds m (no "best" threshold is selected);
//  - candidate pairs come from inverting contexts, never from an O(n^2)
//    substring-pair enumeration; contexts with more than hub_cap distinct
//    substrings are excluded from pair generation (a deterministic,
//    reported engineering cap - hub contexts and their mass are counted);
//  - no global DSU merging of substrings: union-find appears only as a
//    read-only diagnostic of the shared-context graph's component structure;
//  - a disjoint held-out shard measures whether train evidence replicates;
//  - optional POS purity diagnostics against a UD treebank (labels are
//    evaluation-only and never enter discovery).

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace scf::v21 {

// ---------------------------------------------------------------------------
// Tokenizer (fixed normalization)
// ---------------------------------------------------------------------------
// - ASCII letters are lowercased; letter runs form word tokens; a single
//   apostrophe between letters stays inside the token ("don't");
// - runs of 2+ apostrophes (wiki bold/italic markup) act as separators;
// - digit runs become the token "<num>";
// - bytes >= 0x80 are treated as letter characters (kept verbatim);
// - every other visible ASCII character is a single-character token;
// - documents are separated by the reserved sentinel token id 0 ("<doc>"),
//   which can appear in contexts but never inside a substring.
void tokenize_line(std::string_view line, const std::function<void(std::string_view)>& emit);

inline constexpr std::uint32_t kDocSentinel = 0;

struct TokenCorpus {
    std::vector<std::uint32_t> stream;        // token ids incl. <doc> sentinels
    std::vector<std::string> token_text;      // id -> surface form; [0] = "<doc>"
    std::uint64_t real_tokens{};              // non-sentinel tokens in stream
    std::uint64_t documents{};
};

// Streams the input text (one document per line), interning tokens by first
// occurrence. Stops after real_token_limit non-sentinel tokens at the next
// document boundary (0 = read everything).
TokenCorpus build_token_corpus(const std::filesystem::path& input_text,
                               std::uint64_t real_token_limit);

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

struct RealScalingConfig {
    std::filesystem::path input_text;
    std::filesystem::path output_dir;
    std::vector<std::uint64_t> scales{100'000,    300'000,    1'000'000, 3'000'000,
                                      10'000'000, 30'000'000, 100'000'000};
    std::uint64_t heldout_tokens = 20'000'000;
    // min_count(N) = max(floor, ceil(rel * N)): a constant relative frequency
    // floor keeps the substring inventory roughly comparable across scales.
    double min_count_rel = 2e-6;
    std::uint64_t min_count_floor = 8;
    std::uint32_t hub_cap = 32;
    std::vector<std::uint32_t> evidence_thresholds{1, 2, 4, 8, 16};
    std::size_t pairs_per_bucket = 2000;  // held-out sample size per bucket
    std::size_t top_neighbors = 20;
    std::filesystem::path ud_conllu;      // empty = skip the POS diagnostic
    std::size_t dump_pairs_limit = 0;     // >0: write pair_dump.txt when
                                          // distinct pairs <= limit (tests)
    std::vector<std::string> probe_words;      // empty = defaults
    std::vector<std::string> probe_bigrams;    // empty = defaults
};

std::vector<std::string> default_probe_words();
std::vector<std::string> default_probe_bigrams();

// ---------------------------------------------------------------------------
// Results
// ---------------------------------------------------------------------------

struct ThresholdStats {
    std::uint32_t m{};
    std::uint64_t pairs{};
    std::uint64_t nodes{};               // substrings with >= 1 edge at m
    std::uint64_t components{};          // components among those nodes
    std::uint64_t largest_component{};
    double largest_component_ratio{};    // largest / total frequent substrings
    double same_pos_rate{-1.0};          // POS diagnostic; -1 when skipped
    std::uint64_t same_pos_pairs{};
    std::uint64_t labeled_pairs{};
};

struct ScaleMetrics {
    std::uint64_t scale_tokens{};
    std::uint64_t stream_positions{};
    std::uint64_t min_count{};
    std::uint64_t vocab_seen{};          // distinct tokens in the prefix
    std::uint64_t frequent_tokens{};
    std::uint64_t substrings_len1{};
    std::uint64_t substrings_len2{};
    std::uint64_t substrings_len3{};
    std::uint64_t substrings_total{};
    std::uint64_t occurrence_mentions{};  // substring occurrences (pre-dedup)
    std::uint64_t context_records{};      // distinct (l, r, u)
    std::uint64_t distinct_contexts{};
    std::uint64_t singleton_contexts{};   // degree == 1
    double singleton_context_share{};
    std::uint64_t shared_contexts{};      // degree >= 2
    double records_in_shared_contexts_share{};
    std::uint64_t hub_contexts{};         // degree > hub_cap
    double hub_record_share{};
    double mean_contexts_per_substring{};
    std::uint64_t substrings_with_shared_context{};
    double substrings_with_shared_context_share{};
    std::uint64_t pair_emissions{};
    std::uint64_t distinct_pairs{};
    std::vector<ThresholdStats> thresholds;
    double mean_substitution_degree{};    // partners at m=1, over all substrings
    std::uint64_t substrings_with_partner{};
    // transition vs the previous scale (0 when first scale)
    std::uint64_t prev_scale_tokens{};
    std::uint64_t common_pairs{};
    std::uint64_t new_pairs{};
    std::uint64_t lost_pairs{};            // translated but absent now
    std::uint64_t untranslatable_pairs{};  // an endpoint left the inventory
    double common_evidence_prev_mean{};
    double common_evidence_curr_mean{};
    double probe_neighborhood_jaccard_mean{-1.0};
    double same_pos_baseline{-1.0};
    double runtime_seconds{};
    double peak_rss_mb{};
};

struct HeldoutBucketStats {
    std::uint64_t scale_tokens{};
    std::string bucket;                  // "1", "2-3", "4-7", "8-15", "16+"
    std::uint64_t sampled_pairs{};
    std::uint64_t replicated_pairs{};    // held-out shared contexts >= 1
    double replication_rate{};
    double mean_heldout_shared{};
    double median_heldout_shared{};
    double mean_train_evidence{};
};

struct RealScalingResult {
    std::vector<ScaleMetrics> scales;
    std::vector<HeldoutBucketStats> heldout;
    std::uint64_t heldout_tokens_used{};
    std::uint64_t corpus_real_tokens{};
    std::uint64_t corpus_documents{};
    std::uint64_t vocab_size{};
};

// Runs the full ladder; writes scaling_metrics.csv, pair_evidence_scaling.csv,
// heldout_replication.csv, and neighborhood_samples.txt into output_dir.
RealScalingResult run_real_scaling(const RealScalingConfig& config);

// Deterministic 64-bit mix (splitmix64 finalizer); used for the hash-order
// held-out pair sampling.
std::uint64_t mix64(std::uint64_t value);

}  // namespace scf::v21
