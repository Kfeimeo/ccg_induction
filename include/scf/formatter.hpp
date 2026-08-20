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

}  // namespace scf

