#pragma once

#include "scf/corpus.hpp"
#include "scf/equivalence_solver.hpp"
#include "scf/tree_solver.hpp"

#include <string>

namespace scf {

std::string format_span(const Span& span);
std::string format_sentence(const Corpus& corpus, SentenceId sentence);
std::string format_span_yield(const Corpus& corpus, const Span& span);
std::string format_unique_tree(const Corpus& corpus, SentenceId sentence, const TreeSolveResult& result);

// Renders one member of the optimal forest by always taking the first optimal
// split. Debug sample only: it must never be reported as a prediction when
// the optimum is ambiguous.
std::string format_one_optimal_tree(const Corpus& corpus,
                                    SentenceId sentence,
                                    const TreeSolveResult& result);

}  // namespace scf

