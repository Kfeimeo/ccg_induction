#pragma once

// SCF v2.4 -- counterexample-guided closed-world refinement.
//
// The v2.3 rule "one shared exact context makes a merge candidate" is
// abandoned.  Instead the empirical language D (the set of observed
// sentences) defines a closed-world acceptance function
//
//     Accept_D(s) = 1  iff  s in D,   otherwise 0,
//
// and two candidate strings are equivalent iff they behave identically under
// every context of a bounded, explicitly enumerated context universe C_D:
//
//     u ==_D v  iff  for all (L, R) in C_D :  Accept_D(L u R) == Accept_D(L v R).
//
// There are no frequencies, thresholds, similarity scores, or pairwise
// candidate generation.  The partition is computed by refinement: every
// observed positive frame (L, R) is a test T_c(u) = [L u R in D]; a block B
// with B1 = {u in B : T_c(u) = 1} and B0 = B \ B1 both non-empty is split by
// the distinguishing context c.  Splitting continues to a fixed point.
//
// Semantics that this module makes explicit:
//   - "not observed" is a closed-world negative for the current corpus scale;
//   - the equivalence is over D, not over natural-language grammaticality;
//   - every scale is recomputed from scratch (old splits are not axioms);
//   - (eps, eps) is one terminal test bit (complete span or not) and can only
//     split blocks; it never creates a clique of complete-sentence spans.
//
// The module reuses the v2.3.1 structured corpus loader (scf::v231) and the
// v2.1 tokenizer; the v2.3 witness/composition tables are not built.

#include "scf/clean_corpus.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace scf::v24 {

using ObjectId = std::uint32_t;
using ContextId = std::uint32_t;
using FrameType = v231::FrameType;

// ---------------------------------------------------------------------------
// Context universe (bounded, auditable; no context abstraction)
// ---------------------------------------------------------------------------
//   all_frames      : every observed exact frame (L, R), including (eps, eps)
//                     and the one-sided sentence-boundary frames.
//   internal_only   : L != eps and R != eps.
//   boundary_frames : frames that include a sentence boundary
//                     (L == eps or R == eps; (eps, eps) included).
// all_frames = internal_only  (disjoint union)  boundary_frames.
enum class ContextUniverse : std::uint8_t {
    all_frames = 0,
    internal_only = 1,
    boundary_frames = 2,
};

inline constexpr std::array<std::string_view, 3> kUniverseNames{
    "all_frames", "internal_only", "boundary_frames"};

ContextUniverse parse_universe(std::string_view name);
bool in_universe(FrameType frame, ContextUniverse universe);

// ---------------------------------------------------------------------------
// Closed-world observation table
// ---------------------------------------------------------------------------
// Objects are the observed substrings of length 1..max_substring_length,
// contexts are the exact full-sentence frames, and the positive relation
//     R = {(c, u) : L u R in D}
// is stored as a compressed sparse index in both directions.  Nothing else
// is materialised: no witnesses, no pair table, no compositions, and no
// negative examples -- Accept_D(L u R) = 0 is answered by the absence of
// (c, u) from the index.

struct ContextExemplar {
    std::uint32_t sentence{};  // one sentence in which the frame occurs
    std::uint32_t begin{};     // L = sentence[0, begin)
    std::uint32_t end{};       // R = sentence[end, size)
};

struct ObservationTable {
    std::vector<std::string> token_text;
    std::vector<std::vector<std::uint32_t>> sentences;  // the observed prefix
    std::uint64_t token_count{};

    std::vector<std::uint32_t> object_offsets;  // CSR over object_tokens
    std::vector<std::uint32_t> object_tokens;

    std::vector<ContextExemplar> contexts;
    std::vector<FrameType> context_frame;

    std::vector<std::uint64_t> object_context_offsets;  // CSR: contexts of u
    std::vector<ContextId> object_contexts;              // sorted per object
    std::vector<std::uint64_t> context_object_offsets;  // CSR: objects of c
    std::vector<ObjectId> context_objects;               // sorted per context

    [[nodiscard]] std::size_t object_count() const noexcept {
        return object_offsets.empty() ? 0 : object_offsets.size() - 1;
    }
    [[nodiscard]] std::size_t context_count() const noexcept { return contexts.size(); }
    [[nodiscard]] std::uint64_t record_count() const noexcept { return object_contexts.size(); }

    [[nodiscard]] std::span<const std::uint32_t> tokens_of(ObjectId object) const;
    [[nodiscard]] std::span<const ContextId> contexts_of(ObjectId object) const;
    [[nodiscard]] std::span<const ObjectId> objects_of(ContextId context) const;
    [[nodiscard]] std::span<const std::uint32_t> left_of(ContextId context) const;
    [[nodiscard]] std::span<const std::uint32_t> right_of(ContextId context) const;

    // The membership query Accept_D(L u R): binary search in the positive
    // index of u.  Never enumerates negatives.
    [[nodiscard]] bool accepts(ObjectId object, ContextId context) const;

    [[nodiscard]] std::optional<ObjectId> find_object(
        std::span<const std::uint32_t> tokens) const;
    // Whitespace-separated surface text -> object (tokens must exist).
    [[nodiscard]] std::optional<ObjectId> find_object(std::string_view text) const;
    [[nodiscard]] std::optional<ContextId> terminal_context() const;  // (eps, eps)

    [[nodiscard]] std::string object_text(ObjectId object) const;
    [[nodiscard]] std::string left_context_text(ContextId context) const;
    [[nodiscard]] std::string right_context_text(ContextId context) const;
    [[nodiscard]] std::string frame_text(ContextId context) const;  // L=[..] R=[..]

private:
    friend ObservationTable build_observation_table(
        const std::vector<std::vector<std::uint32_t>>&, const std::vector<std::string>&,
        std::size_t, std::size_t);
    std::map<std::vector<std::uint32_t>, ObjectId> object_index_;
};

ObservationTable build_observation_table(
    const std::vector<std::vector<std::uint32_t>>& sentences,
    const std::vector<std::string>& token_text,
    std::size_t sentence_limit,
    std::size_t max_substring_length = 3);

// Convenience for tests and oracle cases: one whitespace-tokenized sentence
// per string, interned by first occurrence.
ObservationTable table_from_lines(const std::vector<std::string>& lines,
                                  std::size_t max_substring_length = 3);

// ---------------------------------------------------------------------------
// Partition refinement
// ---------------------------------------------------------------------------

struct SplitRecord {
    ContextId context{};
    std::uint32_t round{};
    std::uint32_t block_size{};   // |B| before the split
    std::uint32_t in_size{};      // |B1| = members with Accept(L u R) = 1
    ObjectId in_member{};         // a member of B1 (Accept = 1)
    ObjectId out_member{};        // a member of B0 (Accept = 0)
};

struct RefinementMetrics {
    std::uint64_t initial_objects{};
    std::uint64_t universe_contexts{};     // |C_D| for the chosen universe
    std::uint64_t context_tests{};         // (block, context) tests evaluated
    std::uint64_t effective_splitters{};   // contexts that split >= 1 block
    std::uint64_t block_splits{};          // B -> B1 + B0 events
    std::uint64_t refinement_rounds{};     // passes until no block splits
    std::uint64_t membership_queries{};    // positive-index lookups performed
    std::uint64_t final_classes{};
    std::uint64_t singleton_classes{};
    std::uint64_t nontrivial_classes{};    // size >= 2
    std::uint64_t objects_in_nontrivial_classes{};
    std::uint64_t largest_class{};
    double largest_class_ratio{};
    double median_class_size{};
    double p95_class_size{};
    double runtime_seconds{};
};

// Splitter-driven refinement.  All objects start in one block.  Contexts of
// the universe are processed in a fixed order (descending number of positive
// objects, then ascending id -- an efficiency/ordering choice only, the fixed
// point is the same for every order); for every context, every block it
// touches with 0 < |B1| < |B| is split.  Passes repeat until no split occurs.
// Cost is O(sum over contexts of |objects_of(c)|) per pass: the members of a
// block that are NOT positive for c are never enumerated.
class Refiner {
public:
    Refiner(const ObservationTable& table, ContextUniverse universe);
    void run();

    [[nodiscard]] ObjectId class_of(ObjectId object) const { return block_of_[object]; }
    [[nodiscard]] bool same_class(ObjectId first, ObjectId second) const {
        return block_of_[first] == block_of_[second];
    }
    [[nodiscard]] const std::vector<ObjectId>& labels() const noexcept { return block_of_; }
    // Classes sorted by size (descending) then by smallest member.
    [[nodiscard]] std::vector<std::vector<ObjectId>> classes() const;
    [[nodiscard]] const RefinementMetrics& metrics() const noexcept { return metrics_; }
    [[nodiscard]] const std::vector<SplitRecord>& splits() const noexcept { return splits_; }
    [[nodiscard]] ContextUniverse universe() const noexcept { return universe_; }

private:
    const ObservationTable& table_;
    ContextUniverse universe_;
    std::vector<ObjectId> block_of_;
    std::vector<std::uint32_t> position_;
    std::vector<std::vector<ObjectId>> members_;
    RefinementMetrics metrics_;
    std::vector<SplitRecord> splits_;
    bool ran_{};
};

// Brute-force references.  Both return class labels (one per object).
//   signature_partition       : group objects by their exact set of positive
//                               universe contexts (sparse full signature).
//   signature_partition_dense : materialise the full bit vector
//                               [Accept_D(L u R)]_{(L,R) in C_D} per object
//                               and group identical vectors.  Small inputs.
std::vector<ObjectId> signature_partition(const ObservationTable& table,
                                          ContextUniverse universe);
std::vector<ObjectId> signature_partition_dense(const ObservationTable& table,
                                                ContextUniverse universe);
// True iff the two labelings induce the same partition.
bool same_partition(const std::vector<ObjectId>& first, const std::vector<ObjectId>& second);
std::uint64_t class_count(const std::vector<ObjectId>& labels);

// First context of the universe on which u and v differ (nullopt if none).
struct Distinction {
    ContextId context{};
    bool accepts_first{};
    bool accepts_second{};
};
std::optional<Distinction> distinguishing_context(const ObservationTable& table,
                                                  ContextUniverse universe,
                                                  ObjectId first,
                                                  ObjectId second);

// ---------------------------------------------------------------------------
// Diagnostics (evaluation only; nothing here feeds the partition)
// ---------------------------------------------------------------------------

struct TerminalDiagnostics {
    std::uint64_t terminal_objects{};        // #{u : (eps, eps) observed}
    std::uint64_t terminal_classes{};        // classes containing such objects
    std::uint64_t largest_terminal_class{};  // max terminal objects in a class
    std::uint64_t terminal_only_objects{};   // signature == {(eps, eps)}
    std::uint64_t empty_signature_objects{}; // no universe context at all
    std::uint64_t largest_class_excluding_empty_signature{};
    // Signature-size structure of the partition: a non-singleton class whose
    // members share exactly one universe context consists of objects that
    // were each observed in that single frame and nowhere else.
    std::uint64_t single_context_objects{};            // |signature| == 1
    std::uint64_t nontrivial_classes_single_context{}; // size >= 2, |sig| == 1
    std::uint64_t nontrivial_classes_multi_context{};  // size >= 2, |sig| >= 2
    std::uint64_t objects_in_multi_context_classes{};
    std::uint64_t largest_multi_context_class{};
};

TerminalDiagnostics terminal_diagnostics(const ObservationTable& table, const Refiner& refiner);

struct PartitionChange {
    std::uint64_t common_objects{};
    std::uint64_t changed_pairs{};
    std::uint64_t pairs_split{};    // together before, apart now (new distinctions)
    std::uint64_t pairs_merged{};   // apart before, together now (repaired splits)
    double changed_pair_share{};
};

PartitionChange compare_partitions(const ObservationTable& previous_table,
                                   const std::vector<ObjectId>& previous_labels,
                                   const ObservationTable& current_table,
                                   const std::vector<ObjectId>& current_labels);

struct PosTable {
    std::map<std::string, std::string> label;  // lowercased form -> majority UPOS
};
PosTable load_pos_table(const std::filesystem::path& ud_conllu);

struct PosDiagnostics {
    std::uint64_t labeled_objects{};
    std::uint64_t within_class_labeled{};
    double within_class_purity{-1.0};
    std::uint64_t within_class_labeled_pairs{};
    std::uint64_t within_class_same_pos_pairs{};
    double pairwise_same_pos_precision{-1.0};
};

PosDiagnostics evaluate_pos(const ObservationTable& table,
                            const std::vector<std::vector<ObjectId>>& classes,
                            const PosTable& pos);

// Comparison with the v2.3 conservative merger on the same sentence prefix
// (the superseded mechanism).  Pairs are counted over objects common to both
// tables (they coincide: same substrings of the same sentences).
struct V23Comparison {
    bool ran{};
    std::uint64_t v23_classes{};
    std::uint64_t v23_largest_class{};
    std::uint64_t v23_same_class_pairs{};
    std::uint64_t v23_pairs_separated_by_v24{};
    std::uint64_t v24_same_class_pairs{};
    std::uint64_t v24_pairs_separated_by_v23{};
    std::uint64_t accepted_merges{};
    std::uint64_t accepted_merges_separated{};
    std::array<std::uint64_t, 4> accepted_by_frame{};
    std::array<std::uint64_t, 4> accepted_separated_by_frame{};
    double v23_runtime_seconds{};
};

V23Comparison compare_with_v23(const std::vector<std::vector<std::uint32_t>>& sentences,
                               const std::vector<std::string>& token_text,
                               std::size_t sentence_limit,
                               std::size_t max_substring_length,
                               const ObservationTable& table,
                               const Refiner& refiner);

// ---------------------------------------------------------------------------
// Synthetic oracle cases (deterministic)
// ---------------------------------------------------------------------------
// Runs the six required cases and returns a report; every line that ends
// with PASS/FAIL is a checked expectation.  Also writes oracle_comparison.txt
// (synthetic section) into output_dir when it is non-empty.
std::string run_oracle_cases(const std::filesystem::path& output_dir);

// ---------------------------------------------------------------------------
// Ladder driver
// ---------------------------------------------------------------------------

struct ClosedWorldConfig {
    std::filesystem::path input;   // .scs structured corpus (v2.3.1 format)
    std::filesystem::path output_dir{"05_closed_world_refinement/results/pes2o_structured"};
    std::string corpus_label{"pes2o"};
    std::vector<std::uint64_t> scales{100'000, 200'000, 400'000, 1'000'000};
    std::vector<ContextUniverse> universes{ContextUniverse::all_frames,
                                           ContextUniverse::internal_only,
                                           ContextUniverse::boundary_frames};
    std::filesystem::path ud_conllu;
    std::size_t max_substring_length{3};
    std::size_t example_limit{20};
    std::size_t largest_classes{20};
    std::uint64_t compare_v23_max_scale{0};  // run the v2.3 merger up to here
    bool oracle_check{true};                 // sparse signature comparison
    std::vector<std::pair<std::string, std::string>> probe_pairs;  // empty = default
};

std::vector<std::pair<std::string, std::string>> default_probe_pairs();

struct ClosedWorldScaleResult {
    ContextUniverse universe{};
    std::uint64_t nominal_tokens{};
    std::uint64_t actual_tokens{};
    std::uint64_t sentences{};
    std::uint64_t documents{};
    std::uint64_t contexts_total{};
    std::uint64_t records{};
    double table_build_seconds{};
    RefinementMetrics metrics;
    TerminalDiagnostics terminal;
    PartitionChange change;
    PosDiagnostics pos;
    int oracle_identical{-1};  // -1 not checked, 0 differs, 1 identical
    std::uint64_t oracle_classes{};
    V23Comparison v23;
    double peak_rss_mb{};
};

struct ClosedWorldResult {
    std::vector<ClosedWorldScaleResult> rows;
    std::uint64_t available_sentences{};
    std::uint64_t available_tokens{};
};

// Runs the nested ladder for every universe and writes closed_world_scaling.csv,
// distinguishing_contexts.txt, class_examples.txt and oracle_comparison.txt.
ClosedWorldResult run_closed_world_scaling(const ClosedWorldConfig& config);

}  // namespace scf::v24
