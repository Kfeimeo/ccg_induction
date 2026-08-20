#include "scf/formatter.hpp"

#include <sstream>
#include <stdexcept>

namespace scf {
namespace {

std::string render_tree(const Corpus& corpus,
                        const SentenceId sentence,
                        const std::uint16_t begin,
                        const std::uint16_t end,
                        const TreeSolveResult& result) {
    const auto& tokens = corpus.sentences().at(sentence);
    if (end == begin + 1) {
        return std::string(corpus.token_interner().token_text(tokens.at(begin)));
    }
    const auto dimension = static_cast<std::size_t>(result.sentence_length) + 1;
    const auto split = result.unique_tree_splits.at(static_cast<std::size_t>(begin) * dimension + end);
    if (split <= static_cast<std::int16_t>(begin) || split >= static_cast<std::int16_t>(end)) {
        throw std::logic_error("missing split in unique tree reconstruction");
    }
    return "(" + render_tree(corpus, sentence, begin, static_cast<std::uint16_t>(split), result) + " " +
           render_tree(corpus, sentence, static_cast<std::uint16_t>(split), end, result) + ")";
}

std::string render_first_split_tree(const Corpus& corpus,
                                    const SentenceId sentence,
                                    const std::uint16_t begin,
                                    const std::uint16_t end,
                                    const TreeSolveResult& result) {
    const auto& tokens = corpus.sentences().at(sentence);
    if (end == begin + 1) {
        return std::string(corpus.token_interner().token_text(tokens.at(begin)));
    }
    const auto dimension = static_cast<std::size_t>(result.sentence_length) + 1;
    const auto& splits = result.optimal_splits.at(static_cast<std::size_t>(begin) * dimension + end);
    if (splits.empty()) {
        throw std::logic_error("missing split in optimal forest sample");
    }
    const auto split = splits.front();
    return "(" + render_first_split_tree(corpus, sentence, begin, split, result) + " " +
           render_first_split_tree(corpus, sentence, split, end, result) + ")";
}

}  // namespace


std::string format_span(const Span& span) {
    return "[" + std::to_string(span.begin) + "," + std::to_string(span.end) + ")";
}

std::string format_sentence(const Corpus& corpus, const SentenceId sentence) {
    const auto& tokens = corpus.sentences().at(sentence);
    std::ostringstream output;
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        if (index != 0) {
            output << ' ';
        }
        output << corpus.token_interner().token_text(tokens[index]);
    }
    return output.str();
}

std::string format_span_yield(const Corpus& corpus, const Span& span) {
    const auto& sentence = corpus.sentences().at(span.sentence);
    std::ostringstream output;
    for (std::size_t index = span.begin; index < span.end; ++index) {
        if (index != span.begin) {
            output << ' ';
        }
        output << corpus.token_interner().token_text(sentence.at(index));
    }
    return output.str();
}

std::string format_unique_tree(const Corpus& corpus,
                               const SentenceId sentence,
                               const TreeSolveResult& result) {
    if (!result.hard_consistent || result.optimal_tree_count != 1 || result.unique_tree_splits.empty()) {
        return {};
    }
    return render_tree(corpus, sentence, 0, result.sentence_length, result);
}

std::string format_one_optimal_tree(const Corpus& corpus,
                                    const SentenceId sentence,
                                    const TreeSolveResult& result) {
    if (!result.hard_consistent || result.optimal_tree_count == 0) {
        return {};
    }
    return render_first_split_tree(corpus, sentence, 0, result.sentence_length, result);
}

}  // namespace scf
