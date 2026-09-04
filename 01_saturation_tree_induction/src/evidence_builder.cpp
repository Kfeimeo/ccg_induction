#include "scf/evidence_builder.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace scf {
namespace {

struct RawEntry {
    RawContextKey context;
    StringId yield{};
    std::size_t record{};
};

// Geometry g(o) = (|L| in tokens, |R| in tokens). Every context in one raw
// bucket shares the same geometry, and so do all occurrences it covers.
struct ContextGeometry {
    std::uint32_t left_tokens{};
    std::uint32_t right_tokens{};

    auto operator<=>(const ContextGeometry&) const = default;
};

YieldPair make_pair_key(const StringId first, const StringId second) noexcept {
    return first < second ? YieldPair{first, second} : YieldPair{second, first};
}

bool pair_less(const PairWitness& witness, const YieldPair& key) noexcept {
    return YieldPair{witness.first, witness.second} < key;
}

std::uint64_t quantize_strength(const double strength) {
    return static_cast<std::uint64_t>(std::llround(strength * kStrengthScale));
}

}  // namespace

std::string evidence_objective_name(const EvidenceObjective objective) {
    switch (objective) {
        case EvidenceObjective::RawCount: return "raw_count";
        case EvidenceObjective::OpportunityNormalized: return "opportunity";
        case EvidenceObjective::SymmetricConditional: return "conditional";
        case EvidenceObjective::Jaccard: return "jaccard";
    }
    throw std::logic_error("unknown evidence objective");
}

EvidenceObjective parse_evidence_objective(const std::string& name) {
    if (name == "raw_count") {
        return EvidenceObjective::RawCount;
    }
    if (name == "opportunity") {
        return EvidenceObjective::OpportunityNormalized;
    }
    if (name == "conditional") {
        return EvidenceObjective::SymmetricConditional;
    }
    if (name == "jaccard") {
        return EvidenceObjective::Jaccard;
    }
    throw std::runtime_error("unknown evidence objective '" + name +
                             "' (expected raw_count, opportunity, conditional, or jaccard)");
}

std::vector<EvidenceObjective> all_evidence_objectives() {
    return {EvidenceObjective::RawCount, EvidenceObjective::OpportunityNormalized,
            EvidenceObjective::SymmetricConditional, EvidenceObjective::Jaccard};
}

EvidenceBuilder::EvidenceBuilder(const Corpus& corpus, const EvidenceObjective objective)
    : objective_(objective) {
    const auto& records = corpus.context_records();
    std::vector<RawEntry> entries;
    entries.reserve(records.size());
    for (std::size_t index = 0; index < records.size(); ++index) {
        const auto& triple = records[index].triple;
        entries.push_back(RawEntry{{triple.left, triple.right}, triple.yield, index});
    }
    std::sort(entries.begin(), entries.end(), [](const RawEntry& lhs, const RawEntry& rhs) {
        return std::tie(lhs.context, lhs.yield, lhs.record) <
               std::tie(rhs.context, rhs.yield, rhs.record);
    });

    // |C(u)|: each context record is one distinct (L, R, yield) triple.
    {
        std::map<StringId, std::uint32_t> counts;
        for (const auto& record : records) {
            ++counts[record.triple.yield];
        }
        yield_context_counts_.assign(counts.begin(), counts.end());
    }

    // Raw buckets, geometry table, and |U_g| (distinct contexts per geometry).
    std::vector<ContextGeometry> context_geometries;
    std::map<ContextGeometry, std::uint32_t> geometry_context_counts;
    std::map<YieldPair, std::vector<RawContextId>> witnesses_by_pair;
    const auto geometry_of = [&](const RawContextKey& key) {
        return ContextGeometry{
            static_cast<std::uint32_t>(corpus.string_interner().tokens(key.left).size()),
            static_cast<std::uint32_t>(corpus.string_interner().tokens(key.right).size())};
    };
    std::size_t begin = 0;
    while (begin < entries.size()) {
        std::size_t end = begin + 1;
        while (end < entries.size() && entries[end].context == entries[begin].context) {
            ++end;
        }
        if (raw_contexts_.size() > std::numeric_limits<RawContextId>::max()) {
            throw std::overflow_error("raw context count exceeds RawContextId");
        }
        const auto context_id = static_cast<RawContextId>(raw_contexts_.size());
        raw_contexts_.push_back(entries[begin].context);
        const auto geometry = geometry_of(entries[begin].context);
        context_geometries.push_back(geometry);
        ++geometry_context_counts[geometry];

        std::vector<StringId> yields;
        yields.reserve(end - begin);
        for (std::size_t index = begin; index < end; ++index) {
            if (yields.empty() || yields.back() != entries[index].yield) {
                yields.push_back(entries[index].yield);
            }
        }
        for (std::size_t first = 0; first < yields.size(); ++first) {
            for (std::size_t second = first + 1; second < yields.size(); ++second) {
                witnesses_by_pair[YieldPair{yields[first], yields[second]}].push_back(context_id);
            }
        }
        begin = end;
    }

    pairs_.reserve(witnesses_by_pair.size());
    for (auto& [pair, contexts] : witnesses_by_pair) {
        std::sort(contexts.begin(), contexts.end());
        contexts.erase(std::unique(contexts.begin(), contexts.end()), contexts.end());
        pairs_.push_back(PairWitness{pair.first, pair.second, std::move(contexts)});
    }

    // Pair strength under the selected objective. For OpportunityNormalized
    // the strength is geometry-dependent; the pair table reports the best
    // geometry (occurrence scoring below always uses the occurrence's own
    // geometry).
    const auto witness_count_at_geometry = [&](const std::span<const RawContextId> contexts,
                                               const ContextGeometry geometry) {
        std::uint32_t count = 0;
        for (const auto context_id : contexts) {
            if (context_geometries[context_id] == geometry) {
                ++count;
            }
        }
        return count;
    };
    const auto pair_strength_at = [&](const StringId u, const StringId v,
                                      const std::uint32_t shared,
                                      const ContextGeometry* geometry,
                                      std::uint32_t* opportunity_out) -> double {
        const auto cu = yield_context_count(u);
        const auto cv = yield_context_count(v);
        switch (objective_) {
            case EvidenceObjective::RawCount:
                return static_cast<double>(shared);
            case EvidenceObjective::SymmetricConditional:
                return 0.5 * (static_cast<double>(shared) / static_cast<double>(cu) +
                              static_cast<double>(shared) / static_cast<double>(cv));
            case EvidenceObjective::Jaccard:
                return static_cast<double>(shared) /
                       static_cast<double>(cu + cv - shared);
            case EvidenceObjective::OpportunityNormalized: {
                const auto witnesses = pair_witnesses(u, v);
                if (geometry != nullptr) {
                    const auto in_geometry = witness_count_at_geometry(witnesses, *geometry);
                    const auto universe = geometry_context_counts.at(*geometry);
                    if (opportunity_out != nullptr) {
                        *opportunity_out = universe;
                    }
                    return static_cast<double>(in_geometry) / static_cast<double>(universe);
                }
                // Pair table: best over the geometries this pair witnesses.
                double best = 0.0;
                std::uint32_t best_universe = 0;
                std::map<ContextGeometry, std::uint32_t> per_geometry;
                for (const auto context_id : witnesses) {
                    ++per_geometry[context_geometries[context_id]];
                }
                for (const auto& [g, in_geometry] : per_geometry) {
                    const auto universe = geometry_context_counts.at(g);
                    const auto value =
                        static_cast<double>(in_geometry) / static_cast<double>(universe);
                    if (value > best) {
                        best = value;
                        best_universe = universe;
                    }
                }
                if (opportunity_out != nullptr) {
                    *opportunity_out = best_universe;
                }
                return best;
            }
        }
        throw std::logic_error("unknown evidence objective");
    };

    pair_scores_.reserve(pairs_.size());
    double strength_sum = 0.0;
    double confidence_sum = 0.0;
    for (const auto& pair : pairs_) {
        PairEvidenceScore row;
        row.u = pair.first;
        row.v = pair.second;
        row.shared_contexts = static_cast<std::uint32_t>(pair.contexts.size());
        row.contexts_u = yield_context_count(pair.first);
        row.contexts_v = yield_context_count(pair.second);
        row.strength = pair_strength_at(pair.first, pair.second, row.shared_contexts, nullptr,
                                        &row.opportunity_contexts);
        strength_sum += row.strength;
        confidence_sum += static_cast<double>(row.shared_contexts);
        pair_scores_.push_back(row);
    }

    // Occurrence scoring: score(o) = max over v != u with c_o in W(u, v) of
    // S(u, v) (geometry-local for OpportunityNormalized). The candidate
    // condition itself is unchanged from v1.1: the occurrence's exact raw
    // bucket must contain at least one alternative yield with a witness.
    begin = 0;
    while (begin < entries.size()) {
        std::size_t end = begin + 1;
        while (end < entries.size() && entries[end].context == entries[begin].context) {
            ++end;
        }
        std::vector<StringId> yields;
        yields.reserve(end - begin);
        for (std::size_t index = begin; index < end; ++index) {
            if (yields.empty() || yields.back() != entries[index].yield) {
                yields.push_back(entries[index].yield);
            }
        }

        if (yields.size() >= 2) {
            const auto geometry = geometry_of(entries[begin].context);
            for (std::size_t index = begin; index < end; ++index) {
                const auto yield = entries[index].yield;
                std::uint64_t best_fixed = 0;
                double best_strength = 0.0;
                std::uint32_t best_confidence = 0;
                std::vector<StringId> best_alternatives;
                for (const auto alternative : yields) {
                    if (alternative == yield) {
                        continue;
                    }
                    const auto shared =
                        static_cast<std::uint32_t>(pair_support(yield, alternative));
                    if (shared == 0) {
                        continue;
                    }
                    const auto strength =
                        pair_strength_at(yield, alternative, shared, &geometry, nullptr);
                    const auto fixed = objective_ == EvidenceObjective::RawCount
                                           ? static_cast<std::uint64_t>(shared)
                                           : quantize_strength(strength);
                    if (fixed == 0) {
                        continue;
                    }
                    if (fixed > best_fixed) {
                        best_fixed = fixed;
                        best_strength = strength;
                        best_confidence = shared;
                        best_alternatives.assign(1, alternative);
                    } else if (fixed == best_fixed) {
                        best_alternatives.push_back(alternative);
                        best_confidence = std::max(best_confidence, shared);
                    }
                }
                if (best_fixed == 0) {
                    continue;
                }

                std::vector<RawContextKey> supporting_contexts;
                for (const auto alternative : best_alternatives) {
                    for (const auto context_id : pair_witnesses(yield, alternative)) {
                        supporting_contexts.push_back(raw_context(context_id));
                    }
                }
                std::sort(supporting_contexts.begin(), supporting_contexts.end());
                supporting_contexts.erase(
                    std::unique(supporting_contexts.begin(), supporting_contexts.end()),
                    supporting_contexts.end());

                for (const auto occurrence_id : records[entries[index].record].occurrences) {
                    const auto occurrence_index = static_cast<std::size_t>(occurrence_id);
                    const auto& occurrence = corpus.occurrences().at(occurrence_index);
                    span_evidence_.push_back(SpanEvidence{
                        occurrence_id,
                        Span{occurrence.sentence, occurrence.begin, occurrence.end},
                        occurrence.yield,
                        best_fixed,
                        best_strength,
                        best_confidence,
                        best_alternatives,
                        supporting_contexts,
                    });
                }
            }
        }
        begin = end;
    }

    std::sort(span_evidence_.begin(), span_evidence_.end(), [](const auto& lhs, const auto& rhs) {
        return std::tie(lhs.span, lhs.occurrence) < std::tie(rhs.span, rhs.occurrence);
    });

    summary_.raw_contexts = raw_contexts_.size();
    summary_.yield_pairs_with_support = pairs_.size();
    summary_.candidate_occurrences = span_evidence_.size();
    for (const auto& pair : pairs_) {
        summary_.max_pair_support =
            std::max(summary_.max_pair_support, static_cast<std::uint32_t>(pair.contexts.size()));
    }
    if (!pairs_.empty()) {
        summary_.mean_pair_strength = strength_sum / static_cast<double>(pairs_.size());
        summary_.mean_pair_confidence = confidence_sum / static_cast<double>(pairs_.size());
    }
    if (!span_evidence_.empty()) {
        double span_strength_sum = 0.0;
        for (const auto& item : span_evidence_) {
            span_strength_sum += item.strength;
        }
        summary_.mean_candidate_span_score =
            span_strength_sum / static_cast<double>(span_evidence_.size());
    }
}

std::uint32_t EvidenceBuilder::yield_context_count(const StringId yield) const noexcept {
    const auto found = std::lower_bound(
        yield_context_counts_.begin(), yield_context_counts_.end(), yield,
        [](const auto& entry, const StringId key) { return entry.first < key; });
    if (found == yield_context_counts_.end() || found->first != yield) {
        return 0;
    }
    return found->second;
}

std::size_t EvidenceBuilder::pair_support(const StringId first, const StringId second) const noexcept {
    return pair_witnesses(first, second).size();
}

std::span<const RawContextId> EvidenceBuilder::pair_witnesses(const StringId first,
                                                              const StringId second) const noexcept {
    if (first == second) {
        return {};
    }
    const auto key = make_pair_key(first, second);
    const auto found = std::lower_bound(pairs_.begin(), pairs_.end(), key, pair_less);
    if (found == pairs_.end() || found->first != key.first || found->second != key.second) {
        return {};
    }
    return found->contexts;
}

std::vector<SpanEvidence> evidence_for_sentence(const std::span<const SpanEvidence> evidence,
                                                const SentenceId sentence) {
    std::vector<SpanEvidence> result;
    for (const auto& item : evidence) {
        if (item.span.sentence == sentence) {
            result.push_back(item);
        }
    }
    return result;
}

}  // namespace scf
