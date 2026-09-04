#pragma once

#include "scf/evidence_builder.hpp"
#include "scf/equivalence_solver.hpp"

namespace scf {

using ConstituentEvidence = SpanEvidence;

std::vector<ConstituentEvidence> project_constituent_evidence(
    const Corpus& corpus,
    const EquivalenceSolver& solver);

std::vector<Span> evidence_spans_for_sentence(std::span<const ConstituentEvidence> evidence,
                                              SentenceId sentence);

}  // namespace scf
