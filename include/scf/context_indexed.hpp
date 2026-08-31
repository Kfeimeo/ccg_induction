#pragma once

#include "scf/corpus.hpp"
#include "scf/evidence_builder.hpp"  // SpanEvidence for the experimental shadow source

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace scf {

// SCF v1.4 — Context-Indexed Equivalence.
//
// Naming is deliberate and must stay separated (spec §41):
//   LegacyGlobalEClass       — the v1.1-v1.3 global DSU class (baseline/audit only)
//   ContextAbstractionClass  — A_t(x): the recursive context-indexing label;
//                              A(u) = A(v) iff the complete abstract-context
//                              profiles are equal. NOT a grammatical category
//                              and NOT an unconditional equivalence claim.
//   ContextKey               — c = (A(L), A(R)) for an occurrence's contexts
//   LocalRoleBlock           — Y(c): the yields observed under one ContextKey;
//                              u ~_c v iff both are in Y(c). Transitivity holds
//                              only inside one ContextKey; cross-context
//                              bridging never merges anything.

using ContextClassId = std::uint32_t;

struct ContextKey {
    ContextClassId left{};
    ContextClassId right{};

    auto operator<=>(const ContextKey&) const = default;
};

struct LocalRoleBlock {
    ContextKey context;
    std::vector<StringId> yields;  // sorted, distinct
};

enum class AbstractionSignature {
    ContextOnly,        // Sig_t(u) = P_t(u)                       (default)
    ContextPlusConcat,  // Sig_t(u) = (P_t(u), D_t(u))  — conservative refinement,
                        // read-only decomposition abstraction, never a positive
                        // congruence closure (spec §22-23). Not the default.
};

std::string abstraction_signature_name(AbstractionSignature signature);
AbstractionSignature parse_abstraction_signature(const std::string& name);

struct ContextIndexedRoundStats {
    std::size_t round{};
    std::size_t context_class_count{};       // #classes(A_round)
    std::size_t context_key_count{};         // distinct keys under A_round
    std::uint64_t local_relation_pair_count{};  // sum over keys of C(|Y|,2)
    std::size_t new_context_class_merges{};  // classes(A_{r-1}) - classes(A_r), 0 for round 0
    std::int64_t new_local_relation_pairs{};  // delta vs previous round (signed)
    double largest_context_class_ratio{};    // largest A_round class / (|S|-1)
    double max_local_block_ratio{};          // max |Y(c)| / (|S|-1)
};

struct ContextIndexedDiagnostics {
    std::size_t initial_context_classes{};
    std::size_t final_context_classes{};
    double context_abstraction_collapse_ratio{};  // 1 - final/initial
    std::size_t largest_context_abstraction_class{};
    double largest_context_abstraction_class_ratio{};
    std::size_t round_count{};  // productive rounds until fixed point (excl. check)
    std::size_t context_key_count{};
    double mean_local_role_block_size{};
    double median_local_role_block_size{};
    double p90_local_role_block_size{};
    double p99_local_role_block_size{};
    std::size_t max_local_role_block_size{};
    double max_local_role_block_ratio{};
    // Diagnostic-only unindexed projection graph: (u,v) edge iff exists c with
    // u ~_c v. A giant projected component is NOT a v1.4 equivalence class.
    std::size_t projected_graph_components{};
    std::size_t projected_giant_component_size{};
    double projected_giant_component_ratio{};
};

class ContextIndexedSolver {
public:
    explicit ContextIndexedSolver(
        const Corpus& corpus,
        AbstractionSignature signature = AbstractionSignature::ContextOnly);

    void run();

    // --- core relation queries (Acceptance A) ---
    [[nodiscard]] ContextClassId abstraction_class(StringId s) const {
        return class_of_.at(s);
    }
    [[nodiscard]] std::optional<ContextKey> final_key_for(StringId left, StringId right) const;
    [[nodiscard]] bool locally_related(StringId u, StringId v, ContextKey key) const;
    [[nodiscard]] bool locally_related_any(StringId u, StringId v) const;

    [[nodiscard]] const std::vector<LocalRoleBlock>& blocks() const noexcept { return blocks_; }
    // Sorted distinct final ContextKeys the yield appears under (its local roles).
    [[nodiscard]] std::span<const ContextKey> keys_of_yield(StringId u) const;
    [[nodiscard]] const std::vector<ContextIndexedRoundStats>& round_stats() const noexcept {
        return round_stats_;
    }
    [[nodiscard]] std::size_t round_count() const noexcept { return productive_rounds_; }
    [[nodiscard]] bool monotonicity_violated() const noexcept { return monotonicity_violated_; }
    [[nodiscard]] std::size_t final_class_count() const noexcept { return final_class_count_; }
    [[nodiscard]] const Corpus& corpus() const noexcept { return corpus_; }
    [[nodiscard]] AbstractionSignature signature() const noexcept { return signature_; }

    [[nodiscard]] ContextIndexedDiagnostics diagnostics() const;

    // Deterministic hashes (spec §38): canonical serializations, FNV-1a 64.
    [[nodiscard]] std::uint64_t context_partition_hash() const;
    [[nodiscard]] std::uint64_t local_relation_hash() const;
    [[nodiscard]] std::uint64_t round_trace_hash() const;

    // Recursive abstraction gain (spec §28): relations counted as canonical
    // (context_key, unordered yield pair), per round.
    [[nodiscard]] std::uint64_t relation_count_round0() const noexcept {
        return round_stats_.empty() ? 0 : round_stats_.front().local_relation_pair_count;
    }
    [[nodiscard]] std::uint64_t relation_count_final() const noexcept {
        return round_stats_.empty() ? 0 : round_stats_.back().local_relation_pair_count;
    }
    [[nodiscard]] double recursive_relation_gain() const;

private:
    const Corpus& corpus_;
    AbstractionSignature signature_;
    std::vector<ContextClassId> class_of_;
    std::vector<LocalRoleBlock> blocks_;
    std::vector<std::pair<StringId, std::vector<ContextKey>>> yield_keys_;  // sorted by yield
    std::vector<ContextIndexedRoundStats> round_stats_;
    std::size_t productive_rounds_{};
    std::size_t final_class_count_{};
    bool monotonicity_violated_{false};
    bool ran_{false};
};

// Experimental shadow evidence source (spec §30). Ranking uses Strength only
// (min coverage fraction of the final abstract context universe U_c);
// Confidence = min(|R_c(u)|, |R_c(v)|) is diagnostic. Never the default.
std::vector<SpanEvidence> indexed_shadow_evidence(const Corpus& corpus,
                                                  const ContextIndexedSolver& solver);

}  // namespace scf
