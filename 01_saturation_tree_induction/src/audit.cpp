#include "scf/audit.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <ostream>
#include <set>
#include <sstream>

namespace scf {
namespace {

constexpr char kFieldSep = '\x1f';
constexpr char kRecordSep = '\x1e';

std::string join_tokens(const std::vector<std::string>& tokens) {
    std::string text;
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        if (index != 0) {
            text += ' ';
        }
        text += tokens[index];
    }
    return text;
}

std::string rename_text(const std::string& space_joined, const TokenRenaming* renaming) {
    if (renaming == nullptr) {
        return space_joined;
    }
    std::istringstream words(space_joined);
    std::vector<std::string> mapped;
    std::string word;
    while (words >> word) {
        const auto found = renaming->find(word);
        mapped.push_back(found != renaming->end() ? found->second : word);
    }
    return join_tokens(mapped);
}

std::uint64_t hash_sorted_records(std::vector<std::string> records) {
    std::sort(records.begin(), records.end());
    records.erase(std::unique(records.begin(), records.end()), records.end());
    std::string serialized;
    for (const auto& record : records) {
        serialized += record;
        serialized += kRecordSep;
    }
    return fnv1a(serialized);
}

std::string span_texts(const Corpus& corpus, const StringId id) {
    if (id == corpus.string_interner().epsilon_id()) {
        return "";
    }
    return corpus.string_interner().to_string(id, corpus.token_interner());
}

double mean_of(const std::vector<std::uint64_t>& values) {
    if (values.empty()) {
        return 0.0;
    }
    double total = 0.0;
    for (const auto value : values) {
        total += static_cast<double>(value);
    }
    return total / static_cast<double>(values.size());
}

double median_of(std::vector<std::uint64_t> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2;
    if (values.size() % 2 == 1) {
        return static_cast<double>(values[middle]);
    }
    return (static_cast<double>(values[middle - 1]) + static_cast<double>(values[middle])) / 2.0;
}

std::string fmt6(const double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6f", value);
    return buffer;
}

}  // namespace

std::uint64_t fnv1a(const std::string& bytes) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string hash_hex(const std::uint64_t hash) {
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(hash));
    return buffer;
}

TokenSentences sentence_tokens(const std::span<const GoldSentence> sentences) {
    TokenSentences tokens;
    tokens.reserve(sentences.size());
    for (const auto& sentence : sentences) {
        tokens.push_back(sentence.tokens);
    }
    return tokens;
}

std::uint64_t sentence_set_hash(const TokenSentences& sentences) {
    std::vector<std::string> records;
    records.reserve(sentences.size());
    for (const auto& sentence : sentences) {
        std::string record;
        for (const auto& token : sentence) {
            record += token;
            record += kFieldSep;
        }
        records.push_back(std::move(record));
    }
    return hash_sorted_records(std::move(records));
}

TokenRenaming build_canonical_renaming(const TokenSentences& sentences) {
    auto sorted = sentences;
    std::sort(sorted.begin(), sorted.end());
    TokenRenaming renaming;
    std::size_t next = 0;
    for (const auto& sentence : sorted) {
        for (const auto& token : sentence) {
            if (renaming.emplace(token, "t" + std::to_string(next)).second) {
                ++next;
            }
        }
    }
    return renaming;
}

TokenSentences apply_renaming(const TokenSentences& sentences, const TokenRenaming& renaming) {
    TokenSentences renamed;
    renamed.reserve(sentences.size());
    for (const auto& sentence : sentences) {
        std::vector<std::string> mapped;
        mapped.reserve(sentence.size());
        for (const auto& token : sentence) {
            const auto found = renaming.find(token);
            mapped.push_back(found != renaming.end() ? found->second : token);
        }
        renamed.push_back(std::move(mapped));
    }
    return renamed;
}

std::uint64_t raw_context_relation_hash(const Corpus& corpus, const TokenRenaming* renaming) {
    std::vector<std::string> records;
    records.reserve(corpus.context_records().size());
    for (const auto& record : corpus.context_records()) {
        std::string serialized = rename_text(span_texts(corpus, record.triple.left), renaming);
        serialized += kFieldSep;
        serialized += rename_text(span_texts(corpus, record.triple.right), renaming);
        serialized += kFieldSep;
        serialized += rename_text(span_texts(corpus, record.triple.yield), renaming);
        records.push_back(std::move(serialized));
    }
    return hash_sorted_records(std::move(records));
}

std::uint64_t raw_witness_relation_hash(const Corpus& corpus,
                                        const EvidenceBuilder& builder,
                                        const TokenRenaming* renaming) {
    std::vector<std::string> records;
    records.reserve(builder.pairs().size());
    for (const auto& pair : builder.pairs()) {
        auto first = rename_text(span_texts(corpus, pair.first), renaming);
        auto second = rename_text(span_texts(corpus, pair.second), renaming);
        if (second < first) {
            std::swap(first, second);  // canonical under renaming as well
        }
        std::vector<std::string> contexts;
        contexts.reserve(pair.contexts.size());
        for (const auto context_id : pair.contexts) {
            const auto& context = builder.raw_context(context_id);
            contexts.push_back(rename_text(span_texts(corpus, context.left), renaming) + kFieldSep +
                               rename_text(span_texts(corpus, context.right), renaming));
        }
        std::sort(contexts.begin(), contexts.end());
        std::string serialized = first + kFieldSep + second;
        for (const auto& context : contexts) {
            serialized += kRecordSep;
            serialized += context;
        }
        records.push_back(std::move(serialized));
    }
    return hash_sorted_records(std::move(records));
}

std::uint64_t gold_shape_hash(const std::span<const GoldSentence> sentences,
                              const TokenRenaming* renaming) {
    std::vector<std::string> records;
    records.reserve(sentences.size());
    for (const auto& sentence : sentences) {
        const auto tree = gold_tree_from_node(sentence.tree);
        std::string serialized = rename_text(join_tokens(sentence.tokens), renaming);
        serialized += kFieldSep;
        for (const auto& span : tree.internal_spans) {
            serialized += "[" + std::to_string(span.begin) + "," + std::to_string(span.end) + ")";
        }
        records.push_back(std::move(serialized));
    }
    return hash_sorted_records(std::move(records));
}

DatasetHashes compute_dataset_hashes(const SyntheticDataset& dataset,
                                     const Corpus& corpus,
                                     const EvidenceBuilder& builder) {
    DatasetHashes hashes;
    const auto full_language = generate_family_language(
        dataset.grammar_name, dataset.lexical_cardinality, dataset.symmetry_breaking_rate);
    hashes.surface_language = sentence_set_hash(sentence_tokens(full_language));
    hashes.sampled_corpus = sentence_set_hash(sentence_tokens(dataset.sentences));
    hashes.raw_context_relation = raw_context_relation_hash(corpus);
    hashes.raw_witness_relation = raw_witness_relation_hash(corpus, builder);
    return hashes;
}

std::vector<SpanLengthStats> score_by_span_length(
    const std::span<const std::uint16_t> sentence_lengths,
    const std::span<const SpanEvidence> evidence,
    const std::span<const GoldTree> gold) {
    std::map<Span, std::uint64_t> scores;
    for (const auto& item : evidence) {
        scores[item.span] = item.score;
    }
    struct Bucket {
        std::vector<std::uint64_t> all;
        std::vector<std::uint64_t> gold_scores;
        std::vector<std::uint64_t> non_gold_scores;
        std::size_t candidates{};
    };
    std::map<std::uint16_t, Bucket> buckets;
    for (std::size_t sentence = 0; sentence < sentence_lengths.size(); ++sentence) {
        const auto length = sentence_lengths[sentence];
        const auto gold_spans =
            sentence < gold.size() ? gold_scoring_spans(gold[sentence]) : std::set<SpanPair>{};
        for (std::uint16_t span_length = 2; span_length < length; ++span_length) {
            for (std::uint16_t begin = 0; begin + span_length <= length; ++begin) {
                const auto end = static_cast<std::uint16_t>(begin + span_length);
                if (begin == 0 && end == length) {
                    continue;  // root is never scored
                }
                const Span span{static_cast<SentenceId>(sentence), begin, end};
                const auto found = scores.find(span);
                const auto score = found != scores.end() ? found->second : 0;
                auto& bucket = buckets[span_length];
                bucket.all.push_back(score);
                bucket.candidates += score > 0 ? 1 : 0;
                if (gold_spans.contains({begin, end})) {
                    bucket.gold_scores.push_back(score);
                } else {
                    bucket.non_gold_scores.push_back(score);
                }
            }
        }
    }
    std::vector<SpanLengthStats> stats;
    for (const auto& [span_length, bucket] : buckets) {
        SpanLengthStats row;
        row.span_length = span_length;
        row.total_span_count = bucket.all.size();
        row.candidate_span_count = bucket.candidates;
        row.mean_score = mean_of(bucket.all);
        row.median_score = median_of(bucket.all);
        row.max_score = bucket.all.empty() ? 0 : *std::max_element(bucket.all.begin(), bucket.all.end());
        row.mean_gold_span_score = mean_of(bucket.gold_scores);
        row.mean_non_gold_span_score = mean_of(bucket.non_gold_scores);
        stats.push_back(std::move(row));
    }
    return stats;
}

void write_score_by_span_length_csv(std::ostream& output,
                                    const std::span<const SpanLengthStats> stats) {
    output << "span_length,total_span_count,candidate_span_count,mean_score,median_score,"
              "max_score,mean_gold_span_score,mean_non_gold_span_score\n";
    for (const auto& row : stats) {
        output << row.span_length << ',' << row.total_span_count << ',' << row.candidate_span_count
               << ',' << fmt6(row.mean_score) << ',' << fmt6(row.median_score) << ','
               << row.max_score << ',' << fmt6(row.mean_gold_span_score) << ','
               << fmt6(row.mean_non_gold_span_score) << '\n';
    }
}

std::vector<TreeShapeScores> tree_shape_scores(const std::span<const std::uint16_t> sentence_lengths,
                                               const std::span<const SpanEvidence> evidence) {
    std::map<Span, std::uint64_t> scores;
    for (const auto& item : evidence) {
        scores[item.span] = item.score;
    }
    const auto score_of = [&](const SentenceId sentence, const std::uint16_t begin,
                              const std::uint16_t end) -> std::uint64_t {
        const auto found = scores.find(Span{sentence, begin, end});
        return found != scores.end() ? found->second : 0;
    };
    std::vector<TreeShapeScores> rows;
    for (std::size_t sentence = 0; sentence < sentence_lengths.size(); ++sentence) {
        if (sentence_lengths[sentence] != 4) {
            continue;
        }
        const auto id = static_cast<SentenceId>(sentence);
        TreeShapeScores row;
        row.sentence = id;
        row.balanced_score = score_of(id, 0, 2) + score_of(id, 2, 4);
        row.left_score = score_of(id, 0, 2) + score_of(id, 0, 3);
        row.right_score = score_of(id, 1, 4) + score_of(id, 2, 4);
        const auto best = std::max({row.balanced_score, row.left_score, row.right_score});
        std::vector<std::string> winners;
        if (row.balanced_score == best) winners.push_back("balanced");
        if (row.left_score == best) winners.push_back("left");
        if (row.right_score == best) winners.push_back("right");
        for (std::size_t index = 0; index < winners.size(); ++index) {
            row.best_shape += (index == 0 ? "" : "|") + winners[index];
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

void write_tree_shape_scores_tsv(std::ostream& output,
                                 const std::span<const TreeShapeScores> scores) {
    output << "sentence_id\tbalanced_score\tleft_score\tright_score\tbest_shape\n";
    for (const auto& row : scores) {
        output << row.sentence << '\t' << row.balanced_score << '\t' << row.left_score << '\t'
               << row.right_score << '\t' << row.best_shape << '\n';
    }
}

}  // namespace scf
