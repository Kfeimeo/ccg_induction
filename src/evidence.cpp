#include "scf/evidence.hpp"

#include <algorithm>

namespace scf {

std::vector<ConstituentEvidence> project_constituent_evidence(
    const Corpus& corpus,
    const EquivalenceSolver& solver) {
    (void)solver;
    return EvidenceBuilder(corpus).span_evidence();
}

std::vector<Span> evidence_spans_for_sentence(const std::span<const ConstituentEvidence> evidence,
                                              const SentenceId sentence) {
    std::vector<Span> result;
    for (const auto& item : evidence) {
        if (item.span.sentence == sentence) {
            result.push_back(item.span);
        }
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

}  // namespace scf
