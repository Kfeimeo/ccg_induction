#include "scf/evaluator.hpp"

#include "scf/enumerator.hpp"
#include "scf/formatter.hpp"

#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <map>
#include <ostream>
#include <sstream>
#include <stdexcept>

namespace scf {
namespace {

std::string fmt_double(const double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6f", value);
    return buffer;
}

std::string fmt_optional_double(const std::optional<double>& value) {
    return value ? fmt_double(*value) : "NA";
}

std::string fmt_optional_u64(const std::optional<std::uint64_t>& value) {
    return value ? std::to_string(*value) : "NA";
}

std::string json_number_or_null(const std::optional<double>& value) {
    return value ? fmt_double(*value) : "null";
}

std::string bool_text(const bool value) {
    return value ? "true" : "false";
}

void collect_unique_tree_spans(const TreeSolveResult& analysis,
                               const std::uint16_t begin,
                               const std::uint16_t end,
                               std::set<SpanPair>& spans) {
    if (end == begin + 1) {
        return;
    }
    spans.emplace(begin, end);
    const auto dimension = static_cast<std::size_t>(analysis.sentence_length) + 1;
    const auto split = analysis.unique_tree_splits.at(static_cast<std::size_t>(begin) * dimension + end);
    if (split <= static_cast<std::int16_t>(begin) || split >= static_cast<std::int16_t>(end)) {
        throw std::logic_error("missing split while extracting unique tree spans");
    }
    collect_unique_tree_spans(analysis, begin, static_cast<std::uint16_t>(split), spans);
    collect_unique_tree_spans(analysis, static_cast<std::uint16_t>(split), end, spans);
}

std::vector<SpanPair> sorted_difference(const std::set<SpanPair>& from, const std::set<SpanPair>& subtract) {
    std::vector<SpanPair> result;
    for (const auto& span : from) {
        if (!subtract.contains(span)) {
            result.push_back(span);
        }
    }
    return result;
}

std::vector<std::string> sentence_token_texts(const Corpus& corpus, const SentenceId sentence) {
    std::vector<std::string> tokens;
    for (const auto token : corpus.sentences().at(sentence)) {
        tokens.emplace_back(corpus.token_interner().token_text(token));
    }
    return tokens;
}

std::string tsv_safe(std::string text) {
    for (auto& ch : text) {
        if (ch == '\t' || ch == '\n') {
            ch = ' ';
        }
    }
    return text.empty() ? "-" : text;
}

}  // namespace

std::string outcome_name(const SentenceOutcome outcome) {
    switch (outcome) {
        case SentenceOutcome::UniqueCorrect: return "UNIQUE_CORRECT";
        case SentenceOutcome::UniqueWrong: return "UNIQUE_WRONG";
        case SentenceOutcome::AmbiguousGoldIncluded: return "AMBIGUOUS_GOLD_INCLUDED";
        case SentenceOutcome::AmbiguousGoldExcluded: return "AMBIGUOUS_GOLD_EXCLUDED";
        case SentenceOutcome::HardInconsistent: return "HARD_INCONSISTENT";
    }
    throw std::logic_error("unknown sentence outcome");
}

std::set<SpanPair> predicted_spans_from_unique_tree(const TreeSolveResult& analysis,
                                                    const bool include_root,
                                                    const bool include_leaves) {
    if (analysis.optimal_tree_count != 1 || analysis.unique_tree_splits.empty()) {
        if (analysis.sentence_length > 1) {
            throw std::logic_error("predicted spans require a unique optimal tree");
        }
    }
    std::set<SpanPair> spans;
    if (analysis.sentence_length >= 2) {
        collect_unique_tree_spans(analysis, 0, analysis.sentence_length, spans);
        if (!include_root) {
            spans.erase({0, analysis.sentence_length});
        }
    }
    if (include_leaves) {
        for (std::uint16_t index = 0; index < analysis.sentence_length; ++index) {
            spans.emplace(index, static_cast<std::uint16_t>(index + 1));
        }
    }
    return spans;
}

std::uint64_t gold_tree_score(const GoldTree& gold, const std::span<const SpanScore> evidence) {
    // Exactly the parser objective: evidence over proper nontrivial spans,
    // leaf and root contributions always excluded.
    std::map<SpanPair, std::uint64_t> scores;
    for (const auto& item : evidence) {
        scores[{item.span.begin, item.span.end}] = item.score;
    }
    std::uint64_t total = 0;
    for (const auto& span : gold_scoring_spans(gold)) {
        const auto found = scores.find(span);
        if (found != scores.end()) {
            total += found->second;
        }
    }
    return total;
}

std::vector<SpanScore> span_scores_for_sentence(const std::span<const SpanEvidence> evidence,
                                                const SentenceId sentence) {
    std::vector<SpanScore> scores;
    for (const auto& item : evidence) {
        if (item.span.sentence == sentence) {
            scores.push_back(SpanScore{item.span, item.score});
        }
    }
    return scores;
}

SentenceEvaluation evaluate_sentence(const SentenceId sentence,
                                     const TreeSolveResult& analysis,
                                     const GoldTree& gold,
                                     const std::span<const SpanScore> evidence,
                                     const EvalConfig& config) {
    if (gold.length != analysis.sentence_length) {
        throw std::runtime_error("gold tree length does not match sentence " +
                                 std::to_string(sentence));
    }
    SentenceEvaluation result;
    result.sentence = sentence;
    result.length = analysis.sentence_length;
    result.best_score = analysis.best_score;
    result.gold_score = gold_tree_score(gold, evidence);
    result.optimal_tree_count = analysis.optimal_tree_count;
    result.hard_consistent = analysis.hard_consistent;
    result.unique_optimal = analysis.optimal_tree_count == 1;
    result.gold_spans = gold_eval_spans(gold, config.include_root_in_eval,
                                        config.include_leaves_in_eval);

    if (!analysis.hard_consistent || analysis.optimal_tree_count == 0) {
        result.outcome = SentenceOutcome::HardInconsistent;
        return result;
    }

    result.gold_in_argmax = result.gold_score == result.best_score;

    if (result.length <= config.brute_force_max_length) {
        const auto brute = brute_force_tree_scores(result.length, evidence);
        if (brute.best_score != analysis.best_score) {
            throw std::logic_error("brute-force best score disagrees with the DP solver");
        }
        result.second_best_score = brute.second_best_score;
        result.margin = brute.margin;
        result.all_trees_tied = brute.all_trees_tied;
    }

    if (result.unique_optimal) {
        result.predicted_spans = predicted_spans_from_unique_tree(
            analysis, config.include_root_in_eval, config.include_leaves_in_eval);
        result.exact_unique_match = result.predicted_spans == result.gold_spans;
        result.missing_gold_spans = sorted_difference(result.gold_spans, result.predicted_spans);
        result.extra_predicted_spans = sorted_difference(result.predicted_spans, result.gold_spans);

        std::size_t intersection = 0;
        for (const auto& span : result.predicted_spans) {
            intersection += result.gold_spans.contains(span) ? 1 : 0;
        }
        if (result.predicted_spans.empty() && result.gold_spans.empty()) {
            // Length <= 2 has no proper nontrivial spans; defined as perfect.
            result.precision = 1.0;
            result.recall = 1.0;
            result.f1 = 1.0;
        } else {
            const double precision = result.predicted_spans.empty()
                                         ? 0.0
                                         : static_cast<double>(intersection) /
                                               static_cast<double>(result.predicted_spans.size());
            const double recall = result.gold_spans.empty()
                                      ? 0.0
                                      : static_cast<double>(intersection) /
                                            static_cast<double>(result.gold_spans.size());
            result.precision = precision;
            result.recall = recall;
            result.f1 = precision + recall > 0.0 ? 2.0 * precision * recall / (precision + recall)
                                                 : 0.0;
        }
        result.outcome = result.exact_unique_match ? SentenceOutcome::UniqueCorrect
                                                   : SentenceOutcome::UniqueWrong;
    } else {
        result.outcome = result.gold_in_argmax ? SentenceOutcome::AmbiguousGoldIncluded
                                               : SentenceOutcome::AmbiguousGoldExcluded;
    }
    return result;
}

CorpusEvaluation evaluate_corpus(const std::span<const TreeSolveResult> analyses,
                                 const std::span<const GoldTree> gold,
                                 const std::span<const SpanEvidence> evidence,
                                 const EvalConfig& config) {
    if (analyses.size() != gold.size()) {
        throw std::runtime_error("corpus has " + std::to_string(analyses.size()) +
                                 " sentences but the gold file describes " +
                                 std::to_string(gold.size()));
    }
    CorpusEvaluation evaluation;
    evaluation.sentence_count = analyses.size();
    for (std::size_t sentence = 0; sentence < analyses.size(); ++sentence) {
        const auto sentence_id = static_cast<SentenceId>(sentence);
        const auto scores = span_scores_for_sentence(evidence, sentence_id);
        evaluation.sentences.push_back(
            evaluate_sentence(sentence_id, analyses[sentence], gold[sentence], scores, config));
    }

    const auto total = static_cast<double>(evaluation.sentence_count);
    if (evaluation.sentence_count == 0) {
        return evaluation;
    }

    std::size_t unique = 0;
    std::size_t ambiguous = 0;
    std::size_t exact = 0;
    std::size_t gold_in_argmax = 0;
    double argmax_sum = 0.0;
    double best_sum = 0.0;
    double gold_sum = 0.0;
    double margin_sum = 0.0;
    std::size_t margin_count = 0;
    double precision_sum = 0.0;
    double recall_sum = 0.0;
    double f1_sum = 0.0;
    std::vector<double> argmax_sizes;
    for (const auto& sentence : evaluation.sentences) {
        switch (sentence.outcome) {
            case SentenceOutcome::UniqueCorrect: ++evaluation.unique_correct; break;
            case SentenceOutcome::UniqueWrong: ++evaluation.unique_wrong; break;
            case SentenceOutcome::AmbiguousGoldIncluded: ++evaluation.ambiguous_gold_included; break;
            case SentenceOutcome::AmbiguousGoldExcluded: ++evaluation.ambiguous_gold_excluded; break;
            case SentenceOutcome::HardInconsistent: ++evaluation.hard_inconsistent; break;
        }
        unique += sentence.unique_optimal ? 1 : 0;
        ambiguous += sentence.optimal_tree_count > 1 ? 1 : 0;
        exact += sentence.unique_optimal && sentence.exact_unique_match ? 1 : 0;
        gold_in_argmax += sentence.gold_in_argmax ? 1 : 0;
        argmax_sum += static_cast<double>(sentence.optimal_tree_count);
        argmax_sizes.push_back(static_cast<double>(sentence.optimal_tree_count));
        best_sum += static_cast<double>(sentence.best_score);
        gold_sum += static_cast<double>(sentence.gold_score);
        if (sentence.margin) {
            margin_sum += static_cast<double>(*sentence.margin);
            ++margin_count;
        }
        if (sentence.unique_optimal && sentence.f1) {
            precision_sum += *sentence.precision;
            recall_sum += *sentence.recall;
            f1_sum += *sentence.f1;
        }
    }

    evaluation.unique_optimal_rate = static_cast<double>(unique) / total;
    evaluation.ambiguous_optimal_rate = static_cast<double>(ambiguous) / total;
    evaluation.exact_unique_match_rate = static_cast<double>(exact) / total;
    if (unique > 0) {
        evaluation.exact_unique_match_given_unique =
            static_cast<double>(exact) / static_cast<double>(unique);
        evaluation.mean_unlabeled_precision_given_unique =
            precision_sum / static_cast<double>(unique);
        evaluation.mean_unlabeled_recall_given_unique = recall_sum / static_cast<double>(unique);
        evaluation.mean_unlabeled_f1_given_unique = f1_sum / static_cast<double>(unique);
    }
    evaluation.gold_in_argmax_rate = static_cast<double>(gold_in_argmax) / total;
    evaluation.mean_argmax_size = argmax_sum / total;
    std::sort(argmax_sizes.begin(), argmax_sizes.end());
    const auto middle = argmax_sizes.size() / 2;
    evaluation.median_argmax_size =
        argmax_sizes.size() % 2 == 1
            ? argmax_sizes[middle]
            : (argmax_sizes[middle - 1] + argmax_sizes[middle]) / 2.0;
    evaluation.mean_best_score = best_sum / total;
    evaluation.mean_gold_score = gold_sum / total;
    evaluation.zero_margin_rate = static_cast<double>(ambiguous) / total;
    if (margin_count > 0) {
        evaluation.mean_finite_margin = margin_sum / static_cast<double>(margin_count);
    }
    return evaluation;
}

CollapseDiagnostics collapse_diagnostics(const Corpus& corpus,
                                         const EquivalenceSolver& solver,
                                         const EvalConfig& config) {
    CollapseDiagnostics diagnostics;
    const auto& final_stats = solver.statistics().back();
    diagnostics.final_eclasses = final_stats.classes;
    diagnostics.collapse_ratio = final_stats.collapse_ratio;
    diagnostics.largest_eclass = final_stats.largest_class;
    const auto strings = corpus.string_interner().size();
    diagnostics.largest_eclass_ratio =
        strings == 0 ? 0.0
                     : static_cast<double>(final_stats.largest_class) / static_cast<double>(strings);
    diagnostics.suspicious_collapse =
        diagnostics.collapse_ratio > config.collapse_ratio_threshold ||
        diagnostics.largest_eclass_ratio > config.largest_eclass_ratio_threshold;
    diagnostics.successful_unions = solver.reasons().size();
    return diagnostics;
}

void write_metrics_json(std::ostream& output,
                        const RunInfo& info,
                        const Corpus& corpus,
                        const CollapseDiagnostics& diagnostics,
                        const CorpusEvaluation& evaluation) {
    output << "{\n";
    output << "  \"grammar\": "
           << (info.grammar ? "\"" + *info.grammar + "\"" : std::string("null")) << ",\n";
    output << "  \"seed\": " << (info.seed ? std::to_string(*info.seed) : "null") << ",\n";
    output << "  \"coverage\": "
           << (info.coverage ? fmt_double(*info.coverage) : std::string("null")) << ",\n";
    output << "  \"requested_coverage\": "
           << (info.coverage ? fmt_double(*info.coverage) : std::string("null")) << ",\n";
    output << "  \"effective_coverage\": " << json_number_or_null(info.effective_coverage())
           << ",\n";
    output << "  \"lexical_cardinality\": "
           << (info.lexical_cardinality ? std::to_string(*info.lexical_cardinality) : "null")
           << ",\n";
    output << "  \"symmetry_breaking_rate\": "
           << (info.symmetry_breaking_rate ? fmt_double(*info.symmetry_breaking_rate)
                                           : std::string("null"))
           << ",\n";
    output << "  \"hashes\": {\n";
    const auto hash_or_null = [](const std::optional<std::string>& value) {
        return value ? "\"" + *value + "\"" : std::string("null");
    };
    output << "    \"surface_language_hash\": " << hash_or_null(info.surface_language_hash)
           << ",\n";
    output << "    \"sampled_corpus_hash\": " << hash_or_null(info.sampled_corpus_hash) << ",\n";
    output << "    \"raw_context_relation_hash\": "
           << hash_or_null(info.raw_context_relation_hash) << ",\n";
    output << "    \"raw_witness_relation_hash\": "
           << hash_or_null(info.raw_witness_relation_hash) << "\n";
    output << "  },\n";
    output << "  \"corpus\": {\n";
    output << "    \"full_sentence_count\": "
           << (info.full_sentence_count ? std::to_string(*info.full_sentence_count) : "null")
           << ",\n";
    output << "    \"sampled_sentence_count\": "
           << (info.sampled_sentence_count ? std::to_string(*info.sampled_sentence_count) : "null")
           << ",\n";
    output << "    \"input_sentences\": " << corpus.summary().input_sentences << ",\n";
    output << "    \"sentence_types\": " << corpus.sentences().size() << ",\n";
    output << "    \"tokens\": " << corpus.token_interner().size() << ",\n";
    output << "    \"distinct_strings\": " << corpus.string_interner().size() << ",\n";
    output << "    \"context_triples\": " << corpus.context_records().size() << ",\n";
    output << "    \"concat_triples\": " << corpus.concat_triples().size() << "\n";
    output << "  },\n";
    output << "  \"saturation\": {\n";
    output << "    \"final_eclasses\": " << diagnostics.final_eclasses << ",\n";
    output << "    \"collapse_ratio\": " << fmt_double(diagnostics.collapse_ratio) << ",\n";
    output << "    \"largest_eclass\": " << diagnostics.largest_eclass << ",\n";
    output << "    \"largest_eclass_ratio\": " << fmt_double(diagnostics.largest_eclass_ratio)
           << ",\n";
    output << "    \"suspicious_collapse\": " << bool_text(diagnostics.suspicious_collapse)
           << ",\n";
    output << "    \"successful_unions\": " << diagnostics.successful_unions << "\n";
    output << "  },\n";
    output << "  \"parsing\": {\n";
    output << "    \"sentences\": " << evaluation.sentence_count << ",\n";
    output << "    \"unique_optimal_rate\": " << fmt_double(evaluation.unique_optimal_rate)
           << ",\n";
    output << "    \"ambiguous_optimal_rate\": " << fmt_double(evaluation.ambiguous_optimal_rate)
           << ",\n";
    output << "    \"exact_unique_match_rate\": " << fmt_double(evaluation.exact_unique_match_rate)
           << ",\n";
    output << "    \"exact_unique_match_given_unique\": "
           << json_number_or_null(evaluation.exact_unique_match_given_unique) << ",\n";
    output << "    \"gold_in_argmax_rate\": " << fmt_double(evaluation.gold_in_argmax_rate)
           << ",\n";
    output << "    \"mean_argmax_size\": " << fmt_double(evaluation.mean_argmax_size) << ",\n";
    output << "    \"median_argmax_size\": " << fmt_double(evaluation.median_argmax_size) << ",\n";
    output << "    \"mean_best_score\": " << fmt_double(evaluation.mean_best_score) << ",\n";
    output << "    \"mean_gold_score\": " << fmt_double(evaluation.mean_gold_score) << ",\n";
    output << "    \"zero_margin_rate\": " << fmt_double(evaluation.zero_margin_rate) << ",\n";
    output << "    \"mean_finite_margin\": " << json_number_or_null(evaluation.mean_finite_margin)
           << ",\n";
    output << "    \"mean_unlabeled_precision_given_unique\": "
           << json_number_or_null(evaluation.mean_unlabeled_precision_given_unique) << ",\n";
    output << "    \"mean_unlabeled_recall_given_unique\": "
           << json_number_or_null(evaluation.mean_unlabeled_recall_given_unique) << ",\n";
    output << "    \"mean_unlabeled_f1_given_unique\": "
           << json_number_or_null(evaluation.mean_unlabeled_f1_given_unique) << ",\n";
    output << "    \"unique_correct\": " << evaluation.unique_correct << ",\n";
    output << "    \"unique_wrong\": " << evaluation.unique_wrong << ",\n";
    output << "    \"ambiguous_gold_included\": " << evaluation.ambiguous_gold_included << ",\n";
    output << "    \"ambiguous_gold_excluded\": " << evaluation.ambiguous_gold_excluded << ",\n";
    output << "    \"hard_inconsistent\": " << evaluation.hard_inconsistent << "\n";
    output << "  }\n";
    output << "}\n";
}

void write_sentence_metrics_tsv(std::ostream& output,
                                const Corpus& corpus,
                                const std::span<const GoldTree> gold,
                                const std::span<const TreeSolveResult> analyses,
                                const CorpusEvaluation& evaluation) {
    output << "sentence_id\tsentence\tlength\tgold_tree\tpredicted_tree\toutcome\tbest_score\t"
              "gold_score\tsecond_best_score\tmargin\toptimal_tree_count\tall_trees_tied\t"
              "gold_in_argmax\tunique_optimal\texact_unique_match\tprecision\trecall\tf1\t"
              "gold_spans\tforced_optimal_spans\tmissing_gold_spans\textra_predicted_spans\n";
    for (const auto& sentence : evaluation.sentences) {
        const auto& analysis = analyses[sentence.sentence];
        const auto tokens = sentence_token_texts(corpus, sentence.sentence);
        std::string predicted = "<ambiguous>";
        if (sentence.unique_optimal) {
            predicted = format_unique_tree(corpus, sentence.sentence, analysis);
            if (predicted.empty()) {
                predicted = format_sentence(corpus, sentence.sentence);
            }
        } else if (!sentence.hard_consistent) {
            predicted = "<hard-inconsistent>";
        }
        std::vector<SpanPair> gold_list(sentence.gold_spans.begin(), sentence.gold_spans.end());
        std::vector<SpanPair> forced_list;
        for (const auto& span : analysis.forced_spans) {
            if (span.end > span.begin + 1 &&
                !(span.begin == 0 && span.end == analysis.sentence_length)) {
                forced_list.emplace_back(span.begin, span.end);
            }
        }
        output << sentence.sentence << '\t' << tsv_safe(format_sentence(corpus, sentence.sentence))
               << '\t' << sentence.length << '\t'
               << tsv_safe(bracket_from_gold_tree(gold[sentence.sentence], tokens)) << '\t'
               << tsv_safe(predicted) << '\t' << outcome_name(sentence.outcome) << '\t'
               << sentence.best_score << '\t' << sentence.gold_score << '\t'
               << fmt_optional_u64(sentence.second_best_score) << '\t'
               << fmt_optional_u64(sentence.margin) << '\t' << sentence.optimal_tree_count << '\t'
               << bool_text(sentence.all_trees_tied) << '\t' << bool_text(sentence.gold_in_argmax)
               << '\t' << bool_text(sentence.unique_optimal) << '\t'
               << bool_text(sentence.exact_unique_match) << '\t'
               << fmt_optional_double(sentence.precision) << '\t'
               << fmt_optional_double(sentence.recall) << '\t' << fmt_optional_double(sentence.f1)
               << '\t' << format_span_pairs(gold_list) << '\t' << format_span_pairs(forced_list)
               << '\t' << format_span_pairs(sentence.missing_gold_spans) << '\t'
               << format_span_pairs(sentence.extra_predicted_spans) << '\n';
    }
}

void write_failure_examples(std::ostream& output,
                            const Corpus& corpus,
                            const std::span<const GoldTree> gold,
                            const std::span<const TreeSolveResult> analyses,
                            const std::span<const SpanEvidence> evidence,
                            const CorpusEvaluation& evaluation,
                            const EvalConfig& config) {
    std::size_t written = 0;
    for (const auto& sentence : evaluation.sentences) {
        const bool failure = sentence.outcome == SentenceOutcome::UniqueWrong ||
                             sentence.outcome == SentenceOutcome::AmbiguousGoldExcluded ||
                             sentence.outcome == SentenceOutcome::HardInconsistent;
        if (!failure) {
            continue;
        }
        if (written == config.max_failure_examples) {
            output << "... more failures omitted (cap " << config.max_failure_examples << ")\n";
            break;
        }
        ++written;
        const auto& analysis = analyses[sentence.sentence];
        const auto tokens = sentence_token_texts(corpus, sentence.sentence);
        output << "FAILURE " << written << '\n';
        output << "sentence_id = " << sentence.sentence << '\n';
        output << "sentence = " << format_sentence(corpus, sentence.sentence) << '\n';
        output << "outcome = " << outcome_name(sentence.outcome) << '\n';
        output << "gold_tree = " << bracket_from_gold_tree(gold[sentence.sentence], tokens) << '\n';
        if (sentence.unique_optimal) {
            output << "predicted_tree = " << format_unique_tree(corpus, sentence.sentence, analysis)
                   << '\n';
        } else {
            output << "predicted_tree = <ambiguous; no tie-break>\n";
        }
        output << "gold_score = " << sentence.gold_score << '\n';
        output << "best_score = " << sentence.best_score << '\n';
        output << "optimal_tree_count = " << sentence.optimal_tree_count << '\n';
        output << "missing_gold_spans = " << format_span_pairs(sentence.missing_gold_spans) << '\n';
        output << "extra_predicted_spans = " << format_span_pairs(sentence.extra_predicted_spans)
               << '\n';
        output << "span_evidence_table:\n";
        for (const auto& item : evidence) {
            if (item.span.sentence != sentence.sentence) {
                continue;
            }
            output << "  " << format_span(item.span) << " \""
                   << format_span_yield(corpus, item.span) << "\" score=" << item.score << '\n';
        }
        output << '\n';
    }
    if (written == 0) {
        output << "no failures: every sentence is UNIQUE_CORRECT or AMBIGUOUS_GOLD_INCLUDED\n";
    }
}

void write_top_eclasses(std::ostream& output,
                        const Corpus& corpus,
                        const EquivalenceSolver& solver,
                        const EvalConfig& config) {
    auto classes = solver.all_classes();
    std::sort(classes.begin(), classes.end(), [&](const auto& lhs, const auto& rhs) {
        if (lhs.size() != rhs.size()) {
            return lhs.size() > rhs.size();
        }
        return lhs.front() < rhs.front();
    });
    const auto count = std::min(config.top_eclass_count, classes.size());
    for (std::size_t index = 0; index < count; ++index) {
        const auto& members = classes[index];
        output << "EClass " << solver.eclass(members.front()) << " size=" << members.size() << '\n';
        const auto member_cap = std::min(config.top_eclass_member_cap, members.size());
        for (std::size_t member = 0; member < member_cap; ++member) {
            output << "  \""
                   << corpus.string_interner().to_string(members[member], corpus.token_interner())
                   << "\"\n";
        }
        if (member_cap < members.size()) {
            output << "  ... " << members.size() - member_cap << " more members omitted\n";
        }
    }
}

void write_saturation_csv(std::ostream& output, const EquivalenceSolver& solver) {
    output << "round,classes,context_unions,concat_unions,largest_class,collapse_ratio\n";
    for (const auto& stats : solver.statistics()) {
        output << stats.round << ',' << stats.classes << ',' << stats.context_unions << ','
               << stats.concat_unions << ',' << stats.largest_class << ','
               << fmt_double(stats.collapse_ratio) << '\n';
    }
}

std::string summary_csv_header() {
    return "grammar,seed,coverage,full_sentence_count,sampled_sentence_count,distinct_strings,"
           "context_triples,concat_triples,final_eclasses,collapse_ratio,largest_eclass,"
           "largest_eclass_ratio,suspicious_collapse,successful_unions,unique_optimal_rate,"
           "ambiguous_optimal_rate,exact_unique_match_rate,exact_unique_match_given_unique,"
           "gold_in_argmax_rate,mean_argmax_size,median_argmax_size,mean_best_score,"
           "mean_gold_score,zero_margin_rate,mean_finite_margin,"
           "mean_unlabeled_precision_given_unique,mean_unlabeled_recall_given_unique,"
           "mean_unlabeled_f1_given_unique,unique_correct,unique_wrong,ambiguous_gold_included,"
           "ambiguous_gold_excluded,hard_inconsistent,requested_coverage,effective_coverage,"
           "lexical_cardinality,symmetry_breaking_rate,surface_language_hash,"
           "sampled_corpus_hash,raw_context_relation_hash,raw_witness_relation_hash";
}

std::string summary_csv_row(const RunInfo& info,
                            const Corpus& corpus,
                            const CollapseDiagnostics& diagnostics,
                            const CorpusEvaluation& evaluation) {
    std::ostringstream row;
    row << (info.grammar ? *info.grammar : "NA") << ','
        << (info.seed ? std::to_string(*info.seed) : "NA") << ','
        << (info.coverage ? fmt_double(*info.coverage) : "NA") << ','
        << (info.full_sentence_count ? std::to_string(*info.full_sentence_count) : "NA") << ','
        << (info.sampled_sentence_count ? std::to_string(*info.sampled_sentence_count) : "NA")
        << ',' << corpus.string_interner().size() << ',' << corpus.context_records().size() << ','
        << corpus.concat_triples().size() << ',' << diagnostics.final_eclasses << ','
        << fmt_double(diagnostics.collapse_ratio) << ',' << diagnostics.largest_eclass << ','
        << fmt_double(diagnostics.largest_eclass_ratio) << ','
        << bool_text(diagnostics.suspicious_collapse) << ',' << diagnostics.successful_unions
        << ',' << fmt_double(evaluation.unique_optimal_rate) << ','
        << fmt_double(evaluation.ambiguous_optimal_rate) << ','
        << fmt_double(evaluation.exact_unique_match_rate) << ','
        << fmt_optional_double(evaluation.exact_unique_match_given_unique) << ','
        << fmt_double(evaluation.gold_in_argmax_rate) << ','
        << fmt_double(evaluation.mean_argmax_size) << ','
        << fmt_double(evaluation.median_argmax_size) << ','
        << fmt_double(evaluation.mean_best_score) << ','
        << fmt_double(evaluation.mean_gold_score) << ','
        << fmt_double(evaluation.zero_margin_rate) << ','
        << fmt_optional_double(evaluation.mean_finite_margin) << ','
        << fmt_optional_double(evaluation.mean_unlabeled_precision_given_unique) << ','
        << fmt_optional_double(evaluation.mean_unlabeled_recall_given_unique) << ','
        << fmt_optional_double(evaluation.mean_unlabeled_f1_given_unique) << ','
        << evaluation.unique_correct << ',' << evaluation.unique_wrong << ','
        << evaluation.ambiguous_gold_included << ',' << evaluation.ambiguous_gold_excluded << ','
        << evaluation.hard_inconsistent << ','
        << (info.coverage ? fmt_double(*info.coverage) : "NA") << ','
        << fmt_optional_double(info.effective_coverage()) << ','
        << (info.lexical_cardinality ? std::to_string(*info.lexical_cardinality) : "NA") << ','
        << (info.symmetry_breaking_rate ? fmt_double(*info.symmetry_breaking_rate) : "NA") << ','
        << info.surface_language_hash.value_or("NA") << ','
        << info.sampled_corpus_hash.value_or("NA") << ','
        << info.raw_context_relation_hash.value_or("NA") << ','
        << info.raw_witness_relation_hash.value_or("NA");
    return row.str();
}

void print_evaluation_summary(std::ostream& output, const CorpusEvaluation& evaluation) {
    output << "Evaluation:\n"
           << "  sentences = " << evaluation.sentence_count << '\n'
           << "  unique_optimal_rate = " << fmt_double(evaluation.unique_optimal_rate) << '\n'
           << "  ambiguous_optimal_rate = " << fmt_double(evaluation.ambiguous_optimal_rate) << '\n'
           << "  exact_unique_match_rate = " << fmt_double(evaluation.exact_unique_match_rate)
           << '\n'
           << "  exact_unique_match_given_unique = "
           << fmt_optional_double(evaluation.exact_unique_match_given_unique) << '\n'
           << "  gold_in_argmax_rate = " << fmt_double(evaluation.gold_in_argmax_rate) << '\n'
           << "  mean_argmax_size = " << fmt_double(evaluation.mean_argmax_size) << '\n'
           << "  median_argmax_size = " << fmt_double(evaluation.median_argmax_size) << '\n'
           << "  mean_best_score = " << fmt_double(evaluation.mean_best_score) << '\n'
           << "  mean_gold_score = " << fmt_double(evaluation.mean_gold_score) << '\n'
           << "  zero_margin_rate = " << fmt_double(evaluation.zero_margin_rate) << '\n'
           << "  mean_finite_margin = " << fmt_optional_double(evaluation.mean_finite_margin)
           << '\n'
           << "  mean_unlabeled_precision_given_unique = "
           << fmt_optional_double(evaluation.mean_unlabeled_precision_given_unique) << '\n'
           << "  mean_unlabeled_recall_given_unique = "
           << fmt_optional_double(evaluation.mean_unlabeled_recall_given_unique) << '\n'
           << "  mean_unlabeled_f1_given_unique = "
           << fmt_optional_double(evaluation.mean_unlabeled_f1_given_unique) << '\n'
           << "  unique_correct = " << evaluation.unique_correct << '\n'
           << "  unique_wrong = " << evaluation.unique_wrong << '\n'
           << "  ambiguous_gold_included = " << evaluation.ambiguous_gold_included << '\n'
           << "  ambiguous_gold_excluded = " << evaluation.ambiguous_gold_excluded << '\n'
           << "  hard_inconsistent = " << evaluation.hard_inconsistent << '\n';
}

std::vector<std::uint16_t> corpus_sentence_lengths(const Corpus& corpus) {
    std::vector<std::uint16_t> lengths;
    lengths.reserve(corpus.sentences().size());
    for (const auto& sentence : corpus.sentences()) {
        lengths.push_back(static_cast<std::uint16_t>(sentence.size()));
    }
    return lengths;
}

}  // namespace scf
