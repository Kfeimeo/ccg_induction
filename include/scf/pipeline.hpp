#pragma once

#include "scf/corpus.hpp"
#include "scf/equivalence_solver.hpp"
#include "scf/evidence_builder.hpp"
#include "scf/tree_solver.hpp"

#include <span>
#include <vector>

namespace scf {

// The unchanged v1.1 pipeline (saturation -> evidence -> maximum-evidence
// trees) bundled for reuse by scf_cli, scf_experiment, and scf_prepare_text.
struct AnalysisBundle {
    EquivalenceSolver solver;
    EvidenceBuilder builder;
    std::vector<TreeSolveResult> analyses;
};

std::vector<TreeSolveResult> analyze_sentences(const Corpus& corpus,
                                               std::span<const SpanEvidence> evidence);

AnalysisBundle analyze_corpus(const Corpus& corpus);

}  // namespace scf
