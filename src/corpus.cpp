#include "scf/corpus.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace scf {
namespace {

struct ContextTripleHash {
    std::size_t operator()(const ContextTriple& value) const noexcept {
        std::size_t seed = value.left;
        seed ^= static_cast<std::size_t>(value.right) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
        seed ^= static_cast<std::size_t>(value.yield) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
        return seed;
    }
};

std::string trim_copy(std::string value) {
    const auto non_space = [](const unsigned char ch) { return !std::isspace(ch); };
    const auto first = std::find_if(value.begin(), value.end(), non_space);
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if(value.rbegin(), value.rend(), non_space).base();
    return std::string(first, last);
}

bool parse_bool(const std::string_view value) {
    if (value == "true" || value == "1") {
        return true;
    }
    if (value == "false" || value == "0") {
        return false;
    }
    throw std::runtime_error("expected boolean value, got: " + std::string(value));
}

}  // namespace

Corpus::Corpus(CorpusConfig config) : config_(std::move(config)) {
    if (config_.max_sentence_length == 0 ||
        config_.max_sentence_length > std::numeric_limits<std::uint16_t>::max()) {
        throw std::invalid_argument("max_sentence_length must be in [1, 65535]");
    }
    if (config_.equivalence_mode != "strict_global") {
        throw std::invalid_argument("v1.1 supports only equivalence_mode=strict_global");
    }
    if (config_.global_occurrence_consistency) {
        throw std::invalid_argument("v1.1 intentionally requires global_occurrence_consistency=false");
    }
    if (config_.tree_objective != "max_direct_substitution_support") {
        throw std::invalid_argument(
            "v1.1 supports only tree_objective=max_direct_substitution_support");
    }
    if (config_.evidence_aggregation != "max_pair_support") {
        throw std::invalid_argument("v1.1 supports only evidence_aggregation=max_pair_support");
    }
    if (!config_.count_raw_surface_contexts) {
        throw std::invalid_argument("v1.1 requires count_raw_surface_contexts=true");
    }
    if (config_.score_leaves || config_.score_root) {
        throw std::invalid_argument("v1.1 requires score_leaves=false and score_root=false");
    }
}

void Corpus::load_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open corpus: " + path);
    }
    load(input);
}

void Corpus::load(std::istream& input) {
    summary_ = {};
    token_interner_ = TokenInterner{};
    string_interner_ = StringInterner{};
    sentences_.clear();
    occurrences_.clear();
    context_records_.clear();
    concat_triples_.clear();

    std::unordered_set<std::vector<TokenId>, TokenSequenceHash> seen_sentences;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = trim_copy(std::move(line));
        if (line.empty()) {
            ++summary_.ignored_empty_lines;
            continue;
        }
        ++summary_.input_sentences;
        std::istringstream words(line);
        std::vector<TokenId> sentence;
        std::string word;
        while (words >> word) {
            if (config_.lowercase) {
                std::transform(word.begin(), word.end(), word.begin(), [](const unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
            }
            sentence.push_back(token_interner_.intern_token(word));
        }
        if (sentence.size() > config_.max_sentence_length) {
            throw std::runtime_error("sentence on line " + std::to_string(line_number) +
                                     " exceeds max_sentence_length");
        }
        if (config_.deduplicate_sentence_types && !seen_sentences.insert(sentence).second) {
            ++summary_.duplicate_sentences;
            continue;
        }
        sentences_.push_back(std::move(sentence));
    }

    build_records();
}

void Corpus::build_records() {
    std::unordered_map<ContextTriple, std::size_t, ContextTripleHash> context_ids;

    for (std::size_t sentence_index = 0; sentence_index < sentences_.size(); ++sentence_index) {
        const auto& sentence = sentences_[sentence_index];
        const auto sentence_id = static_cast<SentenceId>(sentence_index);
        const auto n = sentence.size();
        for (std::size_t begin = 0; begin < n; ++begin) {
            for (std::size_t end = begin + 1; end <= n; ++end) {
                const auto yield = string_interner_.intern(
                    std::span<const TokenId>(sentence).subspan(begin, end - begin));
                const auto left = string_interner_.intern(std::span<const TokenId>(sentence).first(begin));
                const auto right = string_interner_.intern(std::span<const TokenId>(sentence).subspan(end));
                const auto occurrence_id = static_cast<OccurrenceId>(occurrences_.size());
                occurrences_.push_back(Occurrence{sentence_id,
                                                  static_cast<std::uint16_t>(begin),
                                                  static_cast<std::uint16_t>(end),
                                                  yield,
                                                  left,
                                                  right});

#ifndef NDEBUG
                std::vector<TokenId> restored;
                const auto append = [&](const StringId id) {
                    const auto part = string_interner_.tokens(id);
                    restored.insert(restored.end(), part.begin(), part.end());
                };
                append(left);
                append(yield);
                append(right);
                assert(restored == sentence);
#endif

                if (left == string_interner_.epsilon_id() && right == string_interner_.epsilon_id()) {
                    continue;
                }
                const ContextTriple triple{left, right, yield};
                const auto [position, inserted] = context_ids.emplace(triple, context_records_.size());
                if (inserted) {
                    context_records_.push_back(ContextRecord{triple, {occurrence_id}});
                } else {
                    context_records_[position->second].occurrences.push_back(occurrence_id);
                }
            }
        }
    }

    for (StringId result = 1; result < string_interner_.size(); ++result) {
        const auto sequence = string_interner_.tokens(result);
        for (std::size_t split = 1; split < sequence.size(); ++split) {
            const auto left = string_interner_.find(sequence.first(split));
            const auto right = string_interner_.find(sequence.subspan(split));
            if (!left || !right) {
                throw std::logic_error("substring closure invariant was violated");
            }
            concat_triples_.push_back(ConcatTriple{*left, *right, result});
#ifndef NDEBUG
            std::vector<TokenId> restored(string_interner_.tokens(*left).begin(),
                                          string_interner_.tokens(*left).end());
            const auto right_tokens = string_interner_.tokens(*right);
            restored.insert(restored.end(), right_tokens.begin(), right_tokens.end());
            assert(restored.size() == sequence.size());
            assert(std::equal(restored.begin(), restored.end(), sequence.begin(), sequence.end()));
#endif
        }
    }
    std::sort(concat_triples_.begin(), concat_triples_.end());
    concat_triples_.erase(std::unique(concat_triples_.begin(), concat_triples_.end()), concat_triples_.end());
}

CorpusConfig load_config_file(const std::string& path, CorpusConfig base) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open config: " + path);
    }
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = trim_copy(std::move(line));
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            throw std::runtime_error("invalid config line " + std::to_string(line_number));
        }
        const auto key = trim_copy(line.substr(0, separator));
        const auto value = trim_copy(line.substr(separator + 1));
        if (key == "max_sentence_length") {
            base.max_sentence_length = static_cast<std::size_t>(std::stoull(value));
        } else if (key == "lowercase") {
            base.lowercase = parse_bool(value);
        } else if (key == "deduplicate_sentence_types") {
            base.deduplicate_sentence_types = parse_bool(value);
        } else if (key == "global_occurrence_consistency") {
            base.global_occurrence_consistency = parse_bool(value);
        } else if (key == "equivalence_mode") {
            base.equivalence_mode = value;
        } else if (key == "tree_objective") {
            base.tree_objective = value;
        } else if (key == "evidence_aggregation") {
            base.evidence_aggregation = value;
        } else if (key == "count_raw_surface_contexts") {
            base.count_raw_surface_contexts = parse_bool(value);
        } else if (key == "score_leaves") {
            base.score_leaves = parse_bool(value);
        } else if (key == "score_root") {
            base.score_root = parse_bool(value);
        } else {
            throw std::runtime_error("unknown config key: " + key);
        }
    }
    return base;
}

}  // namespace scf
