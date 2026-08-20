#pragma once

#include "scf/corpus.hpp"
#include "scf/types.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace scf {

using RawContextId = std::uint32_t;

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

struct SpanEvidence {
    OccurrenceId occurrence{};
    Span span{};
    StringId yield{};
    std::uint32_t score{};
    std::vector<StringId> best_alternatives;
    std::vector<RawContextKey> witnesses;
};

struct EvidenceSummary {
    std::size_t raw_contexts{};
    std::size_t yield_pairs_with_support{};
    std::size_t candidate_occurrences{};
    std::uint32_t max_pair_support{};
};

class EvidenceBuilder {
public:
    explicit EvidenceBuilder(const Corpus& corpus);

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
    [[nodiscard]] const std::vector<SpanEvidence>& span_evidence() const noexcept {
        return span_evidence_;
    }
    [[nodiscard]] const EvidenceSummary& summary() const noexcept { return summary_; }

private:
    std::vector<RawContextKey> raw_contexts_;
    std::vector<PairWitness> pairs_;
    std::vector<SpanEvidence> span_evidence_;
    EvidenceSummary summary_;
};

std::vector<SpanEvidence> evidence_for_sentence(std::span<const SpanEvidence> evidence,
                                                SentenceId sentence);

}  // namespace scf
