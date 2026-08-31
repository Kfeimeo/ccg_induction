#pragma once

#include "scf/corpus.hpp"
#include "scf/equivalence_solver.hpp"
#include "scf/evidence_builder.hpp"
#include "scf/gold.hpp"
#include "scf/tree_solver.hpp"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace scf {

// Non-identifiability is a first-class outcome, never collapsed into a plain
// failure. Ambiguous-with-gold-included is a meaningful research result.
enum class SentenceOutcome {
    UniqueCorrect,
    UniqueWrong,
    AmbiguousGoldIncluded,
    AmbiguousGoldExcluded,
    HardInconsistent,
};

std::string outcome_name(SentenceOutcome outcome);

struct EvalConfig {
    bool include_root_in_eval{false};
    bool include_leaves_in_eval{false};
    std::uint16_t brute_force_max_length{10};
    std::size_t max_failure_examples{50};
    std::size_t top_eclass_count{20};
    std::size_t top_eclass_member_cap{50};
    double collapse_ratio_threshold{0.8};
    double largest_eclass_ratio_threshold{0.25};
};

struct SentenceEvaluation {
    SentenceId sentence{};
    std::uint16_t length{};
    std::uint64_t best_score{};
    std::uint64_t gold_score{};
    // Second element of the descending multiset of all tree scores: equals
    // best_score for ambiguous optima (margin 0); absent when only one tree
    // exists or brute force is skipped (length above the cap).
    std::optional<std::uint64_t> second_best_score;
    std::optional<std::uint64_t> margin;
    std::uint64_t optimal_tree_count{};
    bool all_trees_tied{};
    bool hard_consistent{};
    bool unique_optimal{};
    bool gold_in_argmax{};
    bool exact_unique_match{};
    std::optional<double> precision;  // unique-optimal sentences only
    std::optional<double> recall;
    std::optional<double> f1;
    std::set<SpanPair> gold_spans;
    std::set<SpanPair> predicted_spans;  // populated only for unique optima
    std::vector<SpanPair> missing_gold_spans;
    std::vector<SpanPair> extra_predicted_spans;
    // v1.3 structural-invariant metrics: F_s = proper spans forced across the
    // whole optimal forest. Empty-denominator convention (documented in
    // README): |F| = 0 => precision 1; |G| = 0 => recall 1.
    std::set<SpanPair> forced_spans;
    double forced_precision{1.0};             // |F ∩ G_full| / |F|
    double forced_recall{1.0};                // |F ∩ G_full| / |G_full|
    double forced_precision_observable{1.0};  // against observable gold
    double forced_recall_observable{1.0};
    SentenceOutcome outcome{SentenceOutcome::HardInconsistent};
};

struct CorpusEvaluation {
    std::vector<SentenceEvaluation> sentences;
    std::size_t sentence_count{};
    std::size_t unique_correct{};
    std::size_t unique_wrong{};
    std::size_t ambiguous_gold_included{};
    std::size_t ambiguous_gold_excluded{};
    std::size_t hard_inconsistent{};
    double unique_optimal_rate{};
    double ambiguous_optimal_rate{};
    double exact_unique_match_rate{};  // denominator: all sentences
    std::optional<double> exact_unique_match_given_unique;
    double gold_in_argmax_rate{};
    double mean_argmax_size{};
    double median_argmax_size{};
    double mean_best_score{};
    double mean_gold_score{};
    double zero_margin_rate{};  // fraction of sentences with |argmax| > 1
    // Mean margin over sentences where a margin exists (>= 2 candidate trees);
    // ambiguous sentences contribute 0.
    std::optional<double> mean_finite_margin;
    std::optional<double> mean_unlabeled_precision_given_unique;
    std::optional<double> mean_unlabeled_recall_given_unique;
    std::optional<double> mean_unlabeled_f1_given_unique;
    // v1.3 forced-span means over all sentences.
    double forced_precision_full_gold{1.0};
    double forced_recall_full_gold{1.0};
    double forced_precision_observable_gold{1.0};
    double forced_recall_observable_gold{1.0};
};

// Proper spans of the unique optimal tree; valid only when
// analysis.optimal_tree_count == 1.
std::set<SpanPair> predicted_spans_from_unique_tree(const TreeSolveResult& analysis,
                                                    bool include_root,
                                                    bool include_leaves);

std::uint64_t gold_tree_score(const GoldTree& gold, std::span<const SpanScore> evidence);

SentenceEvaluation evaluate_sentence(SentenceId sentence,
                                     const TreeSolveResult& analysis,
                                     const GoldTree& gold,
                                     std::span<const SpanScore> evidence,
                                     const EvalConfig& config = {},
                                     const std::set<SpanPair>* observable_gold = nullptr);

CorpusEvaluation evaluate_corpus(std::span<const TreeSolveResult> analyses,
                                 std::span<const GoldTree> gold,
                                 std::span<const SpanEvidence> evidence,
                                 const EvalConfig& config = {},
                                 std::span<const std::set<SpanPair>> observable_gold = {});

std::vector<SpanScore> span_scores_for_sentence(std::span<const SpanEvidence> evidence,
                                                SentenceId sentence);

struct RunInfo {
    std::optional<std::string> grammar;
    std::optional<std::uint64_t> seed;
    std::optional<double> coverage;  // requested coverage
    std::optional<std::size_t> full_sentence_count;
    std::optional<std::size_t> sampled_sentence_count;
    std::optional<std::size_t> lexical_cardinality;
    std::optional<double> symmetry_breaking_rate;
    // v1.3: evidence objective and evidence-level diagnostics.
    std::optional<std::string> evidence_objective;
    std::optional<double> mean_pair_strength;
    std::optional<double> mean_pair_confidence;
    std::optional<double> mean_candidate_span_score;
    // v1.2.1 observational-equivalence hashes (hex strings).
    std::optional<std::string> surface_language_hash;
    std::optional<std::string> sampled_corpus_hash;
    std::optional<std::string> raw_context_relation_hash;
    std::optional<std::string> raw_witness_relation_hash;

    // effective_coverage = sampled_sentence_count / full_sentence_count.
    [[nodiscard]] std::optional<double> effective_coverage() const {
        if (full_sentence_count && sampled_sentence_count && *full_sentence_count > 0) {
            return static_cast<double>(*sampled_sentence_count) /
                   static_cast<double>(*full_sentence_count);
        }
        return std::nullopt;
    }
};

struct CollapseDiagnostics {
    std::size_t final_eclasses{};
    double collapse_ratio{};
    std::size_t largest_eclass{};
    double largest_eclass_ratio{};
    bool suspicious_collapse{};
    std::size_t successful_unions{};
};

CollapseDiagnostics collapse_diagnostics(const Corpus& corpus,
                                         const EquivalenceSolver& solver,
                                         const EvalConfig& config = {});

void write_metrics_json(std::ostream& output,
                        const RunInfo& info,
                        const Corpus& corpus,
                        const CollapseDiagnostics& diagnostics,
                        const CorpusEvaluation& evaluation);

void write_sentence_metrics_tsv(std::ostream& output,
                                const Corpus& corpus,
                                std::span<const GoldTree> gold,
                                std::span<const TreeSolveResult> analyses,
                                const CorpusEvaluation& evaluation);

void write_failure_examples(std::ostream& output,
                            const Corpus& corpus,
                            std::span<const GoldTree> gold,
                            std::span<const TreeSolveResult> analyses,
                            std::span<const SpanEvidence> evidence,
                            const CorpusEvaluation& evaluation,
                            const EvalConfig& config = {});

void write_top_eclasses(std::ostream& output,
                        const Corpus& corpus,
                        const EquivalenceSolver& solver,
                        const EvalConfig& config = {});

void write_saturation_csv(std::ostream& output, const EquivalenceSolver& solver);

void write_pair_evidence_tsv(std::ostream& output,
                             const Corpus& corpus,
                             const EvidenceBuilder& builder);

void write_forced_span_metrics_csv(std::ostream& output, const CorpusEvaluation& evaluation);

std::string summary_csv_header();
std::string summary_csv_row(const RunInfo& info,
                            const Corpus& corpus,
                            const CollapseDiagnostics& diagnostics,
                            const CorpusEvaluation& evaluation);

void print_evaluation_summary(std::ostream& output, const CorpusEvaluation& evaluation);

std::vector<std::uint16_t> corpus_sentence_lengths(const Corpus& corpus);

}  // namespace scf
