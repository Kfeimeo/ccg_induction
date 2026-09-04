#pragma once

// SCF v2.3 -- conservative evidence-driven category merging.
//
// The discovery path in this module deliberately has no occurrence counts,
// similarity scores, thresholds, POS features, or language-model features.
// Objects start in singleton classes.  An unordered pair can be considered
// only after an exact full-sentence substitution witness has been observed.

#include "scf/real_scaling.hpp"

#include <cstddef>
#include <compare>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace scf::v23 {

using ObjectId = std::uint32_t;
using ContextId = std::uint32_t;

struct ContextKey {
    std::uint32_t left{};
    std::uint32_t right{};
    auto operator<=>(const ContextKey&) const = default;
};

struct TrieNode {
    std::uint32_t parent{};
    std::uint32_t token{};
};

struct LocalWitness {
    ObjectId first{};
    ObjectId second{};
    ContextId context{};
    auto operator<=>(const LocalWitness&) const = default;
};

struct Composition {
    ObjectId left{};
    ObjectId right{};
    ObjectId result{};
    auto operator<=>(const Composition&) const = default;
};

// A finite positive observation table.  contexts_of_object contains exact
// full-sentence frames (not immediate-token windows).  Epsilon sentence
// boundaries are represented by trie root 0 and never become objects.
struct ObservedDataset {
    std::vector<std::string> token_text;
    std::vector<std::vector<std::uint32_t>> object_tokens;
    std::vector<std::string> object_text;
    std::vector<ContextKey> context_keys;
    std::vector<TrieNode> left_trie;
    std::vector<TrieNode> right_trie;
    std::vector<std::vector<ContextId>> contexts_of_object;
    std::vector<LocalWitness> witnesses;
    std::vector<Composition> compositions;
    std::uint64_t sentence_count{};
    std::uint64_t token_count{};

    [[nodiscard]] std::string left_context_text(ContextId id) const;
    [[nodiscard]] std::string right_context_text(ContextId id) const;
};

ObservedDataset observe_sentences(
    const std::vector<std::vector<std::uint32_t>>& sentences,
    const std::vector<std::string>& token_text,
    std::size_t sentence_limit,
    std::size_t max_substring_length = 3);

struct ConflictRecord {
    ObjectId candidate_first{};
    ObjectId candidate_second{};
    ContextId candidate_context{};
    bool merge_on_left{};  // false: u*x/v*x; true: x*u/x*v
    ObjectId first_source{};
    ObjectId second_source{};
    ObjectId shared_operand_first{};
    ObjectId shared_operand_second{};
    ObjectId first_output{};
    ObjectId second_output{};
};

struct MergeRecord {
    ObjectId first{};
    ObjectId second{};
    ContextId context{};
    std::size_t induced_unions{};
};

struct MergeMetrics {
    std::uint64_t initial_objects{};
    std::uint64_t local_witnesses{};
    std::uint64_t merge_candidates{};
    std::uint64_t accepted_candidates{};
    std::uint64_t rejected_candidates{};
    std::uint64_t redundant_candidates{};
    std::uint64_t induced_unions{};
    std::uint64_t resulting_classes{};
    std::uint64_t largest_class{};
    double largest_class_ratio{};
    double median_class_size{};
    double p95_class_size{};
};

class ConservativeMerger {
public:
    explicit ConservativeMerger(const ObservedDataset& data);
    void run();

    [[nodiscard]] ObjectId class_of(ObjectId object) const;
    [[nodiscard]] bool same_class(ObjectId first, ObjectId second) const;
    [[nodiscard]] std::vector<std::vector<ObjectId>> classes() const;
    [[nodiscard]] const MergeMetrics& metrics() const noexcept { return metrics_; }
    [[nodiscard]] const std::vector<MergeRecord>& accepted() const noexcept {
        return accepted_;
    }
    [[nodiscard]] const std::vector<ConflictRecord>& rejected() const noexcept {
        return rejected_;
    }

private:
    const ObservedDataset& data_;
    std::vector<ObjectId> parent_;
    std::vector<std::uint32_t> size_;
    std::vector<std::vector<ObjectId>> members_;
    std::vector<std::vector<std::pair<ObjectId, ObjectId>>> right_behavior_;
    std::vector<std::vector<std::pair<ObjectId, ObjectId>>> left_behavior_;
    std::vector<std::vector<ObjectId>> witness_neighbors_;
    MergeMetrics metrics_;
    std::vector<MergeRecord> accepted_;
    std::vector<ConflictRecord> rejected_;
    bool ran_{};

    ObjectId find(ObjectId object) const;
};

struct PosDiagnostics {
    std::uint64_t labeled_objects{};
    std::uint64_t within_class_labeled{};
    double within_class_purity{-1.0};
    std::uint64_t within_class_labeled_pairs{};
    std::uint64_t within_class_same_pos_pairs{};
    double pairwise_same_pos_precision{-1.0};
};

PosDiagnostics evaluate_pos(const ObservedDataset& data,
                            const ConservativeMerger& merger,
                            const std::filesystem::path& ud_conllu);

struct PartitionChange {
    std::uint64_t common_objects{};
    std::uint64_t changed_pairs{};
    double changed_pair_share{};
};

PartitionChange compare_partitions(const ObservedDataset& previous_data,
                                   const ConservativeMerger& previous,
                                   const ObservedDataset& current_data,
                                   const ConservativeMerger& current);

struct ScaleResult {
    std::uint64_t nominal_tokens{};
    std::uint64_t actual_tokens{};
    MergeMetrics merge;
    PosDiagnostics pos;
    PartitionChange change;
    double runtime_seconds{};
};

struct ConservativeScalingConfig {
    std::filesystem::path input_text;
    std::filesystem::path output_dir{"04_conservative_witness_merging/results/v2_3_conservative"};
    std::vector<std::uint64_t> scales{100'000, 1'000'000, 10'000'000, 100'000'000};
    std::filesystem::path ud_conllu;
    std::size_t max_substring_length{3};
    std::size_t example_limit{20};
};

struct ConservativeScalingResult {
    std::vector<ScaleResult> scales;
    std::uint64_t available_sentences{};
    std::uint64_t available_condition_d_tokens{};
};

ConservativeScalingResult run_conservative_scaling(const ConservativeScalingConfig& config);

// Runs the required hand-auditable oracle grammar and writes
// oracle_sanity.txt.  The returned text is also embedded in the report.
std::string run_conservative_oracle_sanity(const std::filesystem::path& output_dir);

}  // namespace scf::v23
