#pragma once

#include "scf/corpus.hpp"
#include "scf/types.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace scf {

using RawContextId = std::uint32_t;

// v1.3 evidence objective laboratory. RawCount is the v1.1/v1.2 baseline and
// remains the default; the three normalized candidates exist to remove the
// opportunity-count bias support(i,j) = q^(n-len) identified by the v1.2.1
// audit. Switching the default requires benchmark evidence, not code edits.
enum class EvidenceObjective {
    RawCount,               // S = |W(u,v)|
    OpportunityNormalized,  // S = |W_g(u,v)| / |U_g| at the occurrence geometry g
    SymmetricConditional,   // S = (|W|/|C(u)| + |W|/|C(v)|) / 2
    Jaccard,                // S = |W| / |C(u) ∪ C(v)|
};

std::string evidence_objective_name(EvidenceObjective objective);
EvidenceObjective parse_evidence_objective(const std::string& name);
std::vector<EvidenceObjective> all_evidence_objectives();

// Normalized strengths live in [0, 1] as doubles. For the integer tree DP
// they are quantized to round(S * kStrengthScale); ties within 1e-12 of
// strength therefore collapse to exact integer ties (documented in README).
// RawCount keeps its exact integer semantics unscaled.
constexpr double kStrengthScale = 1e12;

struct RawContextKey {
    StringId left{};
    StringId right{};

    auto operator<=>(const RawContextKey&) const = default;
};

struct YieldPair {
    StringId first{};
    StringId second{};

    auto operator<=>(const YieldPair&) const = default;
};

struct PairWitness {
    StringId first{};
    StringId second{};
    std::vector<RawContextId> contexts;
};

// Pair-level diagnostic row (pair_evidence.tsv). strength drives tree
// ranking; confidence (= |W(u,v)|) is diagnostic only and must never be
// multiplied into the ranking by default.
struct PairEvidenceScore {
    StringId u{};
    StringId v{};
    double strength{};
    std::uint32_t shared_contexts{};       // |W(u,v)| = confidence
    std::uint32_t contexts_u{};            // |C(u)|
    std::uint32_t contexts_v{};            // |C(v)|
    std::uint32_t opportunity_contexts{};  // |U_g*| at the best geometry (opportunity only)
};

struct SpanEvidence {
    OccurrenceId occurrence{};
    Span span{};
    StringId yield{};
    // Integer DP score: |W| for RawCount, round(strength * kStrengthScale)
    // for normalized objectives.
    std::uint64_t score{};
    double strength{};          // the objective's pair strength at the argmax alternative
    std::uint32_t confidence{};  // |W(u, v*)| for the argmax alternative (diagnostic)
    std::vector<StringId> best_alternatives;
    std::vector<RawContextKey> witnesses;
};

struct EvidenceSummary {
    std::size_t raw_contexts{};
    std::size_t yield_pairs_with_support{};
    std::size_t candidate_occurrences{};
    std::uint32_t max_pair_support{};
    double mean_pair_strength{};
    double mean_pair_confidence{};
    double mean_candidate_span_score{};  // mean strength over candidate occurrences
};

class EvidenceBuilder {
public:
    explicit EvidenceBuilder(const Corpus& corpus,
                             EvidenceObjective objective = EvidenceObjective::RawCount);

    [[nodiscard]] EvidenceObjective objective() const noexcept { return objective_; }
    [[nodiscard]] std::size_t pair_support(StringId first, StringId second) const noexcept;
    [[nodiscard]] std::span<const RawContextId> pair_witnesses(StringId first,
                                                               StringId second) const noexcept;
    [[nodiscard]] const RawContextKey& raw_context(RawContextId id) const {
        return raw_contexts_.at(static_cast<std::size_t>(id));
    }
    [[nodiscard]] const std::vector<RawContextKey>& raw_contexts() const noexcept {
        return raw_contexts_;
    }
    [[nodiscard]] const std::vector<PairWitness>& pairs() const noexcept { return pairs_; }
    [[nodiscard]] const std::vector<PairEvidenceScore>& pair_scores() const noexcept {
        return pair_scores_;
    }
    [[nodiscard]] const std::vector<SpanEvidence>& span_evidence() const noexcept {
        return span_evidence_;
    }
    [[nodiscard]] std::uint32_t yield_context_count(StringId yield) const noexcept;
    [[nodiscard]] const EvidenceSummary& summary() const noexcept { return summary_; }

private:
    EvidenceObjective objective_{EvidenceObjective::RawCount};
    std::vector<RawContextKey> raw_contexts_;
    std::vector<PairWitness> pairs_;
    std::vector<PairEvidenceScore> pair_scores_;
    std::vector<SpanEvidence> span_evidence_;
    std::vector<std::pair<StringId, std::uint32_t>> yield_context_counts_;  // sorted by yield
    EvidenceSummary summary_;
};

std::vector<SpanEvidence> evidence_for_sentence(std::span<const SpanEvidence> evidence,
                                                SentenceId sentence);

}  // namespace scf
