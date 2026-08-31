#include "scf/pipeline.hpp"

namespace scf {

std::vector<TreeSolveResult> analyze_sentences(const Corpus& corpus,
                                               const std::span<const SpanEvidence> evidence) {
    std::vector<TreeSolveResult> analyses;
    analyses.reserve(corpus.sentences().size());
    for (std::size_t sentence = 0; sentence < corpus.sentences().size(); ++sentence) {
        const auto sentence_id = static_cast<SentenceId>(sentence);
        std::vector<SpanScore> scores;
        for (const auto& item : evidence) {
            if (item.span.sentence == sentence_id) {
                scores.push_back(SpanScore{item.span, item.score});
            }
        }
        analyses.push_back(solve_maximum_evidence_trees(
            sentence_id, static_cast<std::uint16_t>(corpus.sentences()[sentence].size()), scores));
    }
    return analyses;
}

AnalysisBundle analyze_corpus(const Corpus& corpus, const EvidenceObjective objective) {
    EquivalenceSolver solver(corpus.string_interner().size(),
                             corpus.context_records(),
                             corpus.concat_triples());
    solver.saturate();
    EvidenceBuilder builder(corpus, objective);
    auto analyses = analyze_sentences(corpus, builder.span_evidence());
    return AnalysisBundle{std::move(solver), std::move(builder), std::move(analyses)};
}

}  // namespace scf
