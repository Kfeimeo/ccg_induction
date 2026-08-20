#include "scf/evidence_builder.hpp"

#include <algorithm>
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

YieldPair make_pair_key(const StringId first, const StringId second) noexcept {
    return first < second ? YieldPair{first, second} : YieldPair{second, first};
}

bool pair_less(const PairWitness& witness, const YieldPair& key) noexcept {
    return YieldPair{witness.first, witness.second} < key;
}

}  // namespace

EvidenceBuilder::EvidenceBuilder(const Corpus& corpus) {
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

    std::map<YieldPair, std::vector<RawContextId>> witnesses_by_pair;
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
            for (std::size_t index = begin; index < end; ++index) {
                const auto yield = entries[index].yield;
                std::size_t best_support = 0;
                std::vector<StringId> best_alternatives;
                for (const auto alternative : yields) {
                    if (alternative == yield) {
                        continue;
                    }
                    const auto support = pair_support(yield, alternative);
                    if (support > best_support) {
                        best_support = support;
                        best_alternatives.assign(1, alternative);
                    } else if (support == best_support && support != 0) {
                        best_alternatives.push_back(alternative);
                    }
                }
                if (best_support == 0) {
                    continue;
                }
                if (best_support > std::numeric_limits<std::uint32_t>::max()) {
                    throw std::overflow_error("pair support exceeds uint32_t");
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
                        static_cast<std::uint32_t>(best_support),
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
