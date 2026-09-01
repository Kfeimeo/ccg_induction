#pragma once

// SCF v2.0 — Oracle Category Recovery.
//
// A self-contained experimental module (namespace scf::v2, no dependency on
// the v1.x core) that tests one question: do externally indistinguishable
// string equivalence classes recover the true syntactic categories of a small
// synthetic CCG-like grammar G = (E, Lex, Comp, F)?
//
// The learner only ever observes the membership oracle Accept(s); gold
// categories, gold rules, and gold trees are used exclusively for evaluation.
// All equivalences are exact signature equalities — no thresholds, no
// heuristics — and every result is deterministic.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace scf::v2 {

// Bit i set <=> category i derivable. The exhaustive CategoryTable stores one
// byte per string, so oracle grammars are limited to |E| <= 8 categories.
using CategoryMask = std::uint32_t;
inline constexpr std::size_t kMaxCategories = 8;

struct OracleCompRule {
    std::size_t left{};
    std::size_t right{};
    std::size_t result{};
};

// G = (E, Lex, Comp, F). Comp is an arbitrary partial relation over E^3; it
// is never assumed functional, total, or associative.
struct OracleGrammar {
    std::string name;
    std::vector<std::string> categories;             // E
    std::vector<std::string> vocabulary;             // terminal tokens
    std::vector<std::vector<std::size_t>> lexicon;   // token index -> category ids
    std::vector<OracleCompRule> composition;         // Comp ⊆ E×E×E
    std::vector<std::size_t> accepting;              // F ⊆ E
};

// Families required by the v2.0 spec: simple_np_vp, transitive,
// recursive_modifier, observationally_equivalent_categories.
std::vector<std::string> oracle_grammar_names();
OracleGrammar make_oracle_grammar(const std::string& name);

// Reference CKY recognizer over token-index sequences. Used directly in tests
// and to cross-validate CategoryTable; both compute the exact same set
// Cats(s) = { e in E : s derives e }.
class OracleParser {
public:
    explicit OracleParser(const OracleGrammar& grammar);

    CategoryMask categories(std::span<const std::uint8_t> tokens) const;
    bool accept(std::span<const std::uint8_t> tokens) const;
    CategoryMask accepting_mask() const { return accepting_mask_; }
    std::size_t vocabulary_size() const { return lexical_masks_.size(); }

private:
    std::vector<CategoryMask> lexical_masks_;
    std::vector<OracleCompRule> rules_;
    CategoryMask accepting_mask_{};
};

// Canonical index space over every token string of length 1..max_len over a
// fixed vocabulary: ordered by length, then lexicographically by token ids
// (token 0 first). Index arithmetic is exact; nothing is hashed.
struct StringSpace {
    std::size_t vocab{};
    std::size_t max_len{};
    std::vector<std::size_t> pow;    // pow[i] = vocab^i, i in 0..max_len
    std::vector<std::size_t> start;  // start[l] = first index of length l; start[max_len+1] = size

    static StringSpace make(std::size_t vocab, std::size_t max_len);
    std::size_t size() const { return start[max_len + 1]; }
    std::size_t index(std::size_t len, std::size_t value) const { return start[len] + value; }
    std::size_t index(std::span<const std::uint8_t> tokens) const;
    std::size_t length_of(std::size_t index) const;
    std::vector<std::uint8_t> decode(std::size_t index) const;
};

// Exhaustive Cats(s) for every string of length 1..max_total_len, built
// bottom-up over all binary splits (equivalent to CKY on each string).
class CategoryTable {
public:
    CategoryTable(const OracleGrammar& grammar, std::size_t max_total_len);

    const StringSpace& space() const { return space_; }
    CategoryMask at(std::size_t index) const { return table_[index]; }
    CategoryMask categories(std::span<const std::uint8_t> tokens) const;
    bool accept_at(std::size_t index) const { return (table_[index] & accepting_mask_) != 0; }
    CategoryMask accepting_mask() const { return accepting_mask_; }

private:
    StringSpace space_;
    std::vector<std::uint8_t> table_;
    CategoryMask accepting_mask_{};
};

// Sparse accepting-context index. For every universe string u (|u| <= max_len)
// and every context (L, R) with |L|+|R| <= max_k, exactly the pairs with
// Accept(L u R) = true are stored. Because the context universe is shared by
// every u, Sig_k(u) = Sig_k(v) (with the full triples (L, R, Accept(LuR)))
// holds iff u and v accept the same set of contexts of every weight <= k, so
// the sparse index carries the complete signature information.
struct SignatureHits {
    std::size_t max_k{};
    StringSpace universe;      // strings being classified, length 1..max_len
    StringSpace corpus_space;  // whole strings L u R, length 1..max_len+max_k
    // packed entry: (weight << 56) | (context ordinal << 32) | corpus index of LuR;
    // per string, ascending (weight, ordinal).
    std::vector<std::uint64_t> packed;
    std::vector<std::size_t> offsets;  // per universe string: [offsets[i], offsets[i+1])
};

SignatureHits compute_signature_hits(const CategoryTable& table,
                                     std::size_t max_len,
                                     std::size_t max_k);

// Class ids are canonical: numbered by first occurrence in universe index
// order, so equal partitions have equal class_of vectors.
struct Partition {
    std::vector<std::uint32_t> class_of;
    std::uint32_t num_classes{};

    bool operator==(const Partition&) const = default;
};

// Partition refinement over k = 0..max_k (result[k] is the partition induced
// by Sig_k equality). corpus_filter selects which accepted whole strings are
// visible: empty = the full oracle; otherwise a bitset over corpus_space
// (positive-only ablation: a context only registers when the whole string was
// retained — absence is never negative evidence, it is simply absence).
std::vector<Partition> refine_partitions(const SignatureHits& hits,
                                         const std::vector<std::uint64_t>& corpus_filter);

// Restrict a partition computed over a larger universe prefix (signatures do
// not depend on the universe bound, and shorter strings occupy a prefix of
// the index space) to the first `prefix_size` strings, renumbered canonically.
Partition restrict_partition(const Partition& partition, std::size_t prefix_size);

// Gold labeling: each universe string is labeled by its exact derivable
// category set Cats(u); the gold partition groups strings by that set.
// Cats(u) = ∅ (non-constituents) is a gold class like any other.
struct GoldLabeling {
    std::vector<CategoryMask> mask_of;     // per universe string
    std::vector<std::uint32_t> class_of;   // canonical gold class ids
    std::vector<CategoryMask> class_mask;  // per gold class id
    std::uint32_t num_classes{};
};

GoldLabeling gold_labeling(const CategoryTable& table, std::size_t max_len);

std::string mask_name(CategoryMask mask, const OracleGrammar& grammar);

// Exact pair-counting comparison. merge_error_pairs = pairs equal under
// `left` but not `right`; split_error_pairs = the converse. ARI is the
// adjusted Rand index; NMI uses arithmetic-mean normalization 2I/(H_l + H_r)
// with the convention NMI = 1 when both entropies are zero. Empty pair
// denominators yield precision/recall = 1.
struct PartitionMetrics {
    std::size_t universe_size{};
    std::size_t left_classes{};
    std::size_t right_classes{};
    double ari{};
    double nmi{};
    double pairwise_precision{};
    double pairwise_recall{};
    std::uint64_t merge_error_pairs{};
    std::uint64_t split_error_pairs{};
};

PartitionMetrics compare_partitions(std::span<const std::uint32_t> left,
                                    std::span<const std::uint32_t> right);

// Gold classes that are fully contained, together, inside one learned class
// are observationally equivalent under the bounded oracle (no context of
// weight <= K separates any of their members). Merge pairs between such gold
// classes are flagged, not counted as ordinary errors.
struct ObservationalEquivalence {
    std::vector<std::vector<std::uint32_t>> groups;  // gold class ids, size >= 2
    std::uint64_t excluded_merge_pairs{};
};

ObservationalEquivalence find_observationally_equivalent_gold_classes(
    std::span<const std::uint32_t> learned,
    std::span<const std::uint32_t> gold);

// Composition recovery over learned classes: Comp(A, B, C) is recorded
// whenever some u in A, v in B with |uv| <= max_len satisfies uv in C. The
// congruence audit asks whether u ≡ u', v ≡ v' always forces uv ≡ u'v'
// (comparing only strings that exist in the universe); a violation is exactly
// a learned input pair (A, B) mapping to more than one result class.
//
// Scoring against the gold rule set uses an exact labeling: a learned class
// is labelable when no member has two or more gold categories and at least
// one member is a constituent; its label is the set of its members'
// categories (an observationally equivalent class carries several). A triple
// whose three classes are labelable is correct when some compatible
// (a, b, c) is a gold rule. A gold rule is witnessable when some universe
// pair of exact single-category strings realizes it within the length bound.
struct CompositionRecovery {
    std::uint64_t composition_triples{};
    std::uint64_t labeled_triples{};
    std::uint64_t correct_labeled_triples{};
    double composition_precision{};  // over labeled triples
    std::uint64_t witnessable_gold_rules{};
    std::uint64_t recovered_gold_rules{};
    double composition_recall{};  // over witnessable gold rules
    std::uint64_t functional_input_pairs{};
    std::uint64_t nonfunctional_input_pairs{};
    std::uint64_t congruence_violations{};  // == nonfunctional_input_pairs by definition
    std::optional<std::string> first_violation_example;
    std::vector<std::string> incorrect_triple_examples;  // labeled triples outside gold Comp
};

CompositionRecovery recover_composition(const CategoryTable& table,
                                        const OracleGrammar& grammar,
                                        const Partition& partition,
                                        std::size_t max_len);

// Positive-only ablation: the retained corpus is a deterministic seeded
// sample of ceil(coverage * N) strings from the accepted language of length
// <= corpus max length (= max_len + max_k, so 100% coverage provably
// reproduces the oracle partition).
struct PositiveSample {
    std::uint64_t accepted_count{};
    std::uint64_t retained_count{};
    std::vector<std::uint64_t> filter;  // bitset over corpus_space
};

PositiveSample sample_positive(const CategoryTable& table, double coverage, std::uint64_t seed);

// Same deterministic Fisher-Yates as the v1.x generator (reimplemented here
// to keep the module independent): unbiased rejection sampling over
// std::mt19937_64, identical across platforms.
void deterministic_shuffle_v2(std::vector<std::size_t>& values, std::uint64_t seed);

// FNV-1a over the canonical class_of sequence; platform-independent.
std::uint64_t partition_hash(const Partition& partition);
std::string hash_hex(std::uint64_t value);

struct OracleExperimentConfig {
    std::vector<std::string> grammars;  // empty = all known families
    std::size_t min_len{2};
    std::size_t max_len{6};
    std::size_t max_k{4};
    std::vector<double> coverages{0.05, 0.10, 0.20, 0.40, 0.80, 1.0};
    std::vector<std::uint64_t> seeds{1, 2, 3};
};

// Runs the full sweep and writes category_recovery.csv,
// composition_recovery.csv, positive_only_recovery.csv, and
// oracle_summary.txt into output_dir.
void run_oracle_experiment(const OracleExperimentConfig& config,
                           const std::filesystem::path& output_dir);

}  // namespace scf::v2
