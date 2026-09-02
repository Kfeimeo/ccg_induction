#include "scf/clean_corpus.hpp"
#include "scf/platform.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace scf::v231 {
namespace {

using v23::ContextId;
using v23::ObjectId;

std::string csv_double(const double value) {
    if (value < 0.0) {
        return "-1";
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(6) << value;
    return out.str();
}

double ratio(const std::uint64_t numerator, const std::uint64_t denominator) {
    return denominator == 0 ? 0.0
                            : static_cast<double>(numerator) / static_cast<double>(denominator);
}

std::uint64_t pair_key(ObjectId first, ObjectId second) {
    if (second < first) {
        std::swap(first, second);
    }
    return (static_cast<std::uint64_t>(first) << 32U) | second;
}

// Exact reproduction of the v2.3 corpus construction (v2.2 condition D):
// see scf::v23::run_conservative_scaling.
SentenceCorpus read_v23_condition_d(const std::filesystem::path& input_text,
                                    const std::uint64_t read_limit) {
    const v21::TokenCorpus corpus = v21::build_token_corpus(input_text, read_limit);
    const auto spans = v21::segment_sentences(corpus);
    SentenceCorpus out;
    out.token_text = corpus.token_text;
    std::uint64_t count_a = 0;
    std::uint64_t count_d = 0;
    std::size_t last_document = static_cast<std::size_t>(-1);
    for (const auto& span : spans) {
        std::vector<std::uint32_t> sentence;
        for (std::size_t pos = span.begin; pos < span.end; ++pos) {
            const auto token = corpus.stream[pos];
            if (!v21::is_punctuation_token(corpus.token_text[token])) {
                sentence.push_back(token);
            } else {
                ++out.punctuation_tokens_dropped;
            }
        }
        if (span.has_final_punct) {
            ++out.terminators_consumed;
        }
        count_a += span.end - span.begin;
        count_d += sentence.size();
        if (!sentence.empty()) {
            if (span.document != last_document) {
                last_document = span.document;
                ++out.documents;
            }
            out.sentences.push_back(std::move(sentence));
            out.sentence_document.push_back(static_cast<std::uint32_t>(out.documents - 1));
            out.sentence_paragraph.push_back(static_cast<std::uint32_t>(out.documents - 1));
            out.cumulative_nominal.push_back(count_a);
            out.cumulative_actual.push_back(count_d);
        }
    }
    out.paragraphs = out.documents;
    return out;
}

SentenceCorpus read_clean_body(const std::filesystem::path& input_text,
                               const CleanCorpusReadOptions& options) {
    std::ifstream input(input_text, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open corpus text: " + input_text.string());
    }
    SentenceCorpus out;
    out.token_text.push_back("<boundary>");
    struct SvHash {
        using is_transparent = void;
        std::size_t operator()(const std::string_view text) const noexcept {
            return std::hash<std::string_view>{}(text);
        }
        std::size_t operator()(const std::string& text) const noexcept {
            return std::hash<std::string_view>{}(text);
        }
    };
    std::unordered_map<std::string, std::uint32_t, SvHash, std::equal_to<>> vocab;
    std::vector<std::uint8_t> is_punct{0};
    std::vector<std::uint8_t> is_final{0};

    std::uint64_t nominal = 0;
    bool in_document = false;
    std::uint32_t document_index = 0;
    std::uint32_t paragraph_index = 0;
    std::string line;
    std::vector<std::uint32_t> sentence;
    const auto close_sentence = [&]() {
        if (sentence.empty()) {
            return;
        }
        nominal += sentence.size();
        out.sentences.push_back(std::move(sentence));
        sentence.clear();
        out.sentence_document.push_back(document_index);
        out.sentence_paragraph.push_back(paragraph_index);
        out.cumulative_nominal.push_back(nominal);
        out.cumulative_actual.push_back(nominal);
    };
    while (std::getline(input, line)) {
        scf::platform::strip_trailing_cr(line);
        const bool blank = std::all_of(line.begin(), line.end(), [](const unsigned char c) {
            return std::isspace(c) != 0;
        });
        if (blank) {
            if (in_document) {
                in_document = false;
                if (options.token_budget != 0 && nominal >= options.token_budget) {
                    break;
                }
            }
            continue;
        }
        if (!in_document) {
            in_document = true;
            document_index = static_cast<std::uint32_t>(out.documents);
            ++out.documents;
        }
        paragraph_index = static_cast<std::uint32_t>(out.paragraphs);
        ++out.paragraphs;
        v21::tokenize_line(line, [&](const std::string_view token) {
            const auto found = vocab.find(token);
            std::uint32_t id = 0;
            if (found != vocab.end()) {
                id = found->second;
            } else {
                id = static_cast<std::uint32_t>(out.token_text.size());
                out.token_text.emplace_back(token);
                vocab.emplace(std::string(token), id);
                is_punct.push_back(v21::is_punctuation_token(token) ? 1 : 0);
                is_final.push_back(v21::is_final_punctuation_token(token) ? 1 : 0);
            }
            if (is_final[id] != 0) {
                ++out.terminators_consumed;
                close_sentence();
                return;
            }
            if (is_punct[id] != 0) {
                if (!options.keep_punctuation) {
                    ++out.punctuation_tokens_dropped;
                    return;
                }
                ++out.punctuation_tokens_kept;
            }
            sentence.push_back(id);
        });
        close_sentence();  // a paragraph boundary always ends the sentence
    }
    return out;
}

struct ClassSummary {
    std::vector<ObjectId> members;
    std::uint64_t complete_sentence_members{};
    std::uint64_t single_token_members{};
    std::uint64_t num_members{};
};

std::optional<ContextId> empty_context(const v23::ObservedDataset& data) {
    for (ContextId id = 0; id < data.context_keys.size(); ++id) {
        if (data.context_keys[id].left == 0 && data.context_keys[id].right == 0) {
            return id;
        }
    }
    return std::nullopt;
}

bool has_context(const v23::ObservedDataset& data, const ObjectId object,
                 const std::optional<ContextId> context) {
    if (!context.has_value()) {
        return false;
    }
    const auto& contexts = data.contexts_of_object[object];
    return std::binary_search(contexts.begin(), contexts.end(), *context);
}

bool contains_num(const v23::ObservedDataset& data, const ObjectId object) {
    for (const auto token : data.object_tokens[object]) {
        if (data.token_text[token] == "<num>") {
            return true;
        }
    }
    return false;
}

std::vector<ClassSummary> sorted_classes(const v23::ObservedDataset& data,
                                         const v23::ConservativeMerger& merger) {
    auto classes = merger.classes();
    std::sort(classes.begin(), classes.end(), [](const auto& a, const auto& b) {
        return a.size() > b.size() || (a.size() == b.size() && a.front() < b.front());
    });
    const auto empty = empty_context(data);
    std::vector<ClassSummary> result;
    result.reserve(classes.size());
    for (auto& cls : classes) {
        ClassSummary summary;
        for (const ObjectId member : cls) {
            if (has_context(data, member, empty)) {
                ++summary.complete_sentence_members;
            }
            if (data.object_tokens[member].size() == 1) {
                ++summary.single_token_members;
            }
            if (contains_num(data, member)) {
                ++summary.num_members;
            }
        }
        summary.members = std::move(cls);
        result.push_back(std::move(summary));
    }
    return result;
}

std::string frame_text(const v23::ObservedDataset& data, const ContextId context) {
    return "L=[" + data.left_context_text(context) + "] R=[" +
           data.right_context_text(context) + "]";
}

std::string mask_text(const std::uint8_t mask) {
    std::string out;
    for (std::size_t type = 0; type < kFrameTypes; ++type) {
        if ((mask >> type) & 1U) {
            if (!out.empty()) {
                out += '+';
            }
            out += frame_type_name(static_cast<FrameType>(type));
        }
    }
    return out;
}

void write_rejected(std::ostream& out,
                    const v23::ObservedDataset& data,
                    const v23::ConflictRecord& item,
                    const std::uint8_t mask) {
    out << "candidate " << data.object_text[item.candidate_first] << " <=> "
        << data.object_text[item.candidate_second] << " | witness "
        << frame_text(data, item.candidate_context) << " | witness_types=" << mask_text(mask)
        << "\n  conflict: ";
    if (item.merge_on_left) {
        out << "Comp(" << data.object_text[item.shared_operand_first] << ", "
            << data.object_text[item.first_source] << ", "
            << data.object_text[item.first_output] << ") vs Comp("
            << data.object_text[item.shared_operand_second] << ", "
            << data.object_text[item.second_source] << ", "
            << data.object_text[item.second_output] << ")";
    } else {
        out << "Comp(" << data.object_text[item.first_source] << ", "
            << data.object_text[item.shared_operand_first] << ", "
            << data.object_text[item.first_output] << ") vs Comp("
            << data.object_text[item.second_source] << ", "
            << data.object_text[item.shared_operand_second] << ", "
            << data.object_text[item.second_output] << ")";
    }
    out << "; outputs have different observed exact-context profiles and no direct "
           "substitution witness\n";
}

}  // namespace

std::string_view frame_type_name(const FrameType type) {
    switch (type) {
        case FrameType::empty_frame:
            return "empty_frame";
        case FrameType::left_boundary:
            return "left_boundary";
        case FrameType::right_boundary:
            return "right_boundary";
        case FrameType::internal_frame:
            return "internal_frame";
    }
    return "unknown";
}

FrameType frame_type_of(const v23::ObservedDataset& data, const ContextId context) {
    const auto key = data.context_keys.at(context);
    const bool left_empty = key.left == 0;
    const bool right_empty = key.right == 0;
    if (left_empty && right_empty) {
        return FrameType::empty_frame;
    }
    if (left_empty) {
        return FrameType::left_boundary;
    }
    if (right_empty) {
        return FrameType::right_boundary;
    }
    return FrameType::internal_frame;
}

SentenceCorpus read_sentence_corpus(const std::filesystem::path& input_text,
                                    const CleanCorpusReadOptions& options) {
    if (options.preprocess == Preprocess::v23_condition_d) {
        return read_v23_condition_d(input_text, options.token_budget);
    }
    return read_clean_body(input_text, options);
}

std::size_t prefix_sentence_limit(const SentenceCorpus& corpus, const std::uint64_t scale) {
    const auto boundary = std::lower_bound(corpus.cumulative_nominal.begin(),
                                           corpus.cumulative_nominal.end(), scale);
    if (boundary == corpus.cumulative_nominal.end()) {
        return corpus.sentences.size();
    }
    return static_cast<std::size_t>(boundary - corpus.cumulative_nominal.begin()) + 1;
}

std::vector<CandidateTypeMask> candidate_type_masks(const v23::ObservedDataset& data) {
    // data.witnesses is sorted by (first, second, context) with first < second.
    std::vector<CandidateTypeMask> masks;
    for (const auto& witness : data.witnesses) {
        const auto bit = static_cast<std::uint8_t>(
            1U << static_cast<unsigned>(frame_type_of(data, witness.context)));
        if (!masks.empty() && masks.back().first == witness.first &&
            masks.back().second == witness.second) {
            masks.back().mask |= bit;
        } else {
            masks.push_back({witness.first, witness.second, bit});
        }
    }
    return masks;
}

std::uint8_t lookup_mask(const std::vector<CandidateTypeMask>& masks,
                         ObjectId first,
                         ObjectId second) {
    if (second < first) {
        std::swap(first, second);
    }
    const auto it = std::lower_bound(
        masks.begin(), masks.end(), pair_key(first, second),
        [](const CandidateTypeMask& item, const std::uint64_t key) {
            return pair_key(item.first, item.second) < key;
        });
    if (it == masks.end() || it->first != first || it->second != second) {
        throw std::runtime_error("candidate without witnesses");
    }
    return it->mask;
}

FrameDiagnostics diagnose_frames(const v23::ObservedDataset& data,
                                 const v23::ConservativeMerger& merger) {
    FrameDiagnostics result;
    const auto empty = empty_context(data);
    for (ObjectId object = 0; object < data.object_tokens.size(); ++object) {
        if (has_context(data, object, empty)) {
            ++result.objects_with_empty_frame;
        }
    }
    for (const auto& witness : data.witnesses) {
        ++result.rows[static_cast<std::size_t>(frame_type_of(data, witness.context))]
              .witness_count;
    }
    const auto masks = candidate_type_masks(data);
    const auto only_type = [](const std::uint8_t mask) -> int {
        return std::popcount(mask) == 1 ? std::countr_zero(mask) : -1;
    };
    for (const auto& item : masks) {
        for (std::size_t type = 0; type < kFrameTypes; ++type) {
            if ((item.mask >> type) & 1U) {
                ++result.rows[type].candidate_count_any;
            }
        }
        const int only = only_type(item.mask);
        if (only >= 0) {
            ++result.rows[static_cast<std::size_t>(only)].candidate_count_only;
        } else {
            ++result.mixed_candidates;
        }
    }

    const auto classes = sorted_classes(data, merger);
    std::vector<std::uint8_t> in_largest(data.object_tokens.size(), 0);
    if (!classes.empty()) {
        const auto& largest = classes.front();
        result.largest_class_size = largest.members.size();
        result.largest_class_complete_sentence_members = largest.complete_sentence_members;
        result.largest_class_single_token_members = largest.single_token_members;
        result.largest_class_num_members = largest.num_members;
        for (const ObjectId member : largest.members) {
            in_largest[member] = 1;
        }
    }

    for (const auto& item : merger.accepted()) {
        const auto mask = lookup_mask(masks, item.first, item.second);
        for (std::size_t type = 0; type < kFrameTypes; ++type) {
            if ((mask >> type) & 1U) {
                ++result.rows[type].accepted_any;
            }
        }
        const int only = only_type(mask);
        if (only >= 0) {
            auto& row = result.rows[static_cast<std::size_t>(only)];
            ++row.accepted_only;
            row.induced_unions_only += item.induced_unions;
            if (in_largest[item.first] != 0 && in_largest[item.second] != 0) {
                ++row.accepted_only_in_largest_class;
            }
        } else {
            ++result.mixed_accepted;
        }
    }
    for (const auto& item : merger.rejected()) {
        const auto mask = lookup_mask(masks, item.candidate_first, item.candidate_second);
        for (std::size_t type = 0; type < kFrameTypes; ++type) {
            if ((mask >> type) & 1U) {
                ++result.rows[type].rejected_any;
            }
        }
        const int only = only_type(mask);
        if (only >= 0) {
            ++result.rows[static_cast<std::size_t>(only)].rejected_only;
        } else {
            ++result.mixed_rejected;
        }
    }
    for (auto& row : result.rows) {
        row.redundant_only = row.candidate_count_only - row.accepted_only - row.rejected_only;
    }
    return result;
}

std::vector<HubStats> empty_frame_hub_stats(const SentenceCorpus& corpus,
                                            const std::vector<std::uint64_t>& scales_in,
                                            const std::size_t max_substring_length) {
    auto scales = scales_in;
    std::sort(scales.begin(), scales.end());
    scales.erase(std::unique(scales.begin(), scales.end()), scales.end());
    std::vector<HubStats> result;
    std::set<std::vector<std::uint32_t>> complete;
    std::size_t cursor = 0;
    std::uint64_t short_occurrences = 0;
    for (const std::uint64_t scale : scales) {
        if (corpus.cumulative_nominal.empty() || scale > corpus.cumulative_nominal.back()) {
            break;
        }
        const std::size_t limit = prefix_sentence_limit(corpus, scale);
        for (; cursor < limit; ++cursor) {
            const auto& sentence = corpus.sentences[cursor];
            if (sentence.size() <= max_substring_length) {
                ++short_occurrences;
                complete.insert(sentence);
            }
        }
        HubStats row;
        row.nominal_tokens = scale;
        row.actual_tokens = corpus.cumulative_actual[limit - 1];
        row.sentences = limit;
        row.short_sentence_occurrences = short_occurrences;
        row.distinct_complete_spans = complete.size();
        row.hub_candidate_pairs = complete.size() < 2 ? 0 : complete.size() * (complete.size() - 1) / 2;
        result.push_back(row);
    }
    return result;
}

CleanCorpusResult run_clean_corpus_scaling(const CleanCorpusConfig& config) {
    if (config.input_text.empty()) {
        throw std::runtime_error("input_text is required");
    }
    auto scales = config.scales;
    std::sort(scales.begin(), scales.end());
    scales.erase(std::unique(scales.begin(), scales.end()), scales.end());
    if (scales.empty()) {
        throw std::runtime_error("no scales requested");
    }
    std::filesystem::create_directories(config.output_dir);

    CleanCorpusReadOptions read = config.read;
    if (read.token_budget == 0) {
        read.token_budget =
            read.preprocess == Preprocess::v23_condition_d
                ? static_cast<std::uint64_t>(static_cast<double>(scales.back()) * 1.25) +
                      2'000'000  // the v2.3 read limit
                : scales.back() + 1;
    }
    const SentenceCorpus corpus = read_sentence_corpus(config.input_text, read);
    while (!scales.empty() &&
           (corpus.cumulative_nominal.empty() || scales.back() > corpus.cumulative_nominal.back())) {
        scales.pop_back();
    }
    if (scales.empty()) {
        throw std::runtime_error("corpus smaller than the smallest requested scale");
    }

    std::ofstream csv(config.output_dir / "clean_corpus_scaling.csv");
    csv << "corpus,preprocess,nominal_tokens,actual_tokens,sentences,documents,paragraphs,"
           "initial_objects,local_witnesses,merge_candidates,accepted_merges,rejected_merges,"
           "redundant_candidates,induced_unions,resulting_classes,largest_class,"
           "largest_class_ratio,median_class_size,p95_class_size,accepted_merge_rate,"
           "rejected_merge_rate,objects_with_empty_frame,empty_frame_witnesses,"
           "empty_frame_witness_share,empty_frame_candidates_only,empty_frame_candidate_share,"
           "empty_frame_candidates_any,empty_frame_accepted_only,empty_frame_accepted_share,"
           "empty_frame_accepted_any,mixed_candidates,largest_class_complete_sentence_members,"
           "largest_class_single_token_members,largest_class_num_members,common_objects_prev,"
           "changed_pairs_prev,changed_pair_share_prev,runtime_seconds,peak_rss_mb\n";
    std::ofstream frame_csv(config.output_dir / "frame_type_metrics.csv");
    frame_csv << "corpus,nominal_tokens,frame_type,witness_count,witness_share,"
                 "candidate_count_any,candidate_count_only,candidate_only_share,accepted_any,"
                 "accepted_only,accepted_only_share,rejected_any,rejected_only,"
                 "redundant_only,induced_unions_only,accepted_only_in_largest_class\n";
    std::ofstream class_file(config.output_dir / "largest_classes.txt");
    std::ofstream accepted_file(config.output_dir / "successful_merges_by_frame_type.txt");
    std::ofstream rejected_file(config.output_dir / "rejected_merges_by_frame_type.txt");
    std::ofstream largest_file(config.output_dir / "largest_class_members.txt");
    std::ofstream probe_file(config.output_dir / "probe_object_classes.txt");
    const std::string preprocess_name =
        read.preprocess == Preprocess::v23_condition_d
            ? "v23_condition_d"
            : (read.keep_punctuation ? "clean_body_keep_punct" : "clean_body_drop_punct");
    class_file << "# SCF v2.3.1 largest learned classes -- corpus " << config.corpus_label
               << " (" << preprocess_name << ")\n"
               << "# [S] = member observed as a complete sentence ((eps,eps) frame)\n";
    accepted_file << "# SCF v2.3.1 accepted merges grouped by the boundary type of ALL their "
                     "witnesses -- corpus "
                  << config.corpus_label << " (" << preprocess_name << ")\n"
                  << "# A merge listed under empty_frame was triggered by (eps,eps) alone.\n";
    rejected_file << "# SCF v2.3.1 rejected candidates grouped by the boundary type of ALL "
                     "their witnesses -- corpus "
                  << config.corpus_label << " (" << preprocess_name << ")\n";
    largest_file << "# SCF v2.3.1 complete membership of the largest class -- corpus "
                 << config.corpus_label << " (" << preprocess_name << ")\n"
                 << "# [S] = member observed as a complete sentence ((eps,eps) frame)\n";
    probe_file << "# SCF v2.3.1 final class of each probe object -- corpus " << config.corpus_label
               << " (" << preprocess_name << ")\n"
               << "# [S] = member observed as a complete sentence ((eps,eps) frame)\n";

    CleanCorpusResult result;
    result.available_sentences = corpus.sentences.size();
    result.available_nominal_tokens =
        corpus.cumulative_nominal.empty() ? 0 : corpus.cumulative_nominal.back();
    std::unique_ptr<v23::ObservedDataset> previous_data;
    std::unique_ptr<v23::ConservativeMerger> previous_merger;
    for (const std::uint64_t scale : scales) {
        const auto start = std::chrono::steady_clock::now();
        const std::size_t sentence_limit = prefix_sentence_limit(corpus, scale);
        auto data = std::make_unique<v23::ObservedDataset>(v23::observe_sentences(
            corpus.sentences, corpus.token_text, sentence_limit, config.max_substring_length));
        auto merger = std::make_unique<v23::ConservativeMerger>(*data);
        merger->run();

        CleanScaleResult row;
        row.nominal_tokens = scale;
        row.actual_tokens = corpus.cumulative_actual[sentence_limit - 1];
        row.sentences = sentence_limit;
        row.documents = corpus.sentence_document[sentence_limit - 1] + 1ULL;
        row.paragraphs = corpus.sentence_paragraph[sentence_limit - 1] + 1ULL;
        row.merge = merger->metrics();
        row.frames = diagnose_frames(*data, *merger);
        if (previous_data) {
            row.change =
                v23::compare_partitions(*previous_data, *previous_merger, *data, *merger);
        }
        row.runtime_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        row.peak_rss_mb = scf::platform::peak_rss_mb();

        const auto& m = row.merge;
        const auto& f = row.frames;
        const auto& empty = f.rows[static_cast<std::size_t>(FrameType::empty_frame)];
        csv << config.corpus_label << ',' << preprocess_name << ',' << row.nominal_tokens << ','
            << row.actual_tokens << ',' << row.sentences << ',' << row.documents << ','
            << row.paragraphs << ',' << m.initial_objects << ',' << m.local_witnesses << ','
            << m.merge_candidates << ',' << m.accepted_candidates << ','
            << m.rejected_candidates << ',' << m.redundant_candidates << ','
            << m.induced_unions << ',' << m.resulting_classes << ',' << m.largest_class << ','
            << csv_double(m.largest_class_ratio) << ',' << m.median_class_size << ','
            << m.p95_class_size << ','
            << csv_double(ratio(m.accepted_candidates, m.merge_candidates)) << ','
            << csv_double(ratio(m.rejected_candidates, m.merge_candidates)) << ','
            << f.objects_with_empty_frame << ',' << empty.witness_count << ','
            << csv_double(ratio(empty.witness_count, m.local_witnesses)) << ','
            << empty.candidate_count_only << ','
            << csv_double(ratio(empty.candidate_count_only, m.merge_candidates)) << ','
            << empty.candidate_count_any << ',' << empty.accepted_only << ','
            << csv_double(ratio(empty.accepted_only, m.accepted_candidates)) << ','
            << empty.accepted_any << ',' << f.mixed_candidates << ','
            << f.largest_class_complete_sentence_members << ','
            << f.largest_class_single_token_members << ',' << f.largest_class_num_members
            << ',' << row.change.common_objects << ',' << row.change.changed_pairs << ','
            << csv_double(row.change.changed_pair_share) << ','
            << csv_double(row.runtime_seconds) << ',' << csv_double(row.peak_rss_mb) << '\n';
        for (std::size_t type = 0; type < kFrameTypes; ++type) {
            const auto& r = f.rows[type];
            frame_csv << config.corpus_label << ',' << scale << ','
                      << frame_type_name(static_cast<FrameType>(type)) << ','
                      << r.witness_count << ','
                      << csv_double(ratio(r.witness_count, m.local_witnesses)) << ','
                      << r.candidate_count_any << ',' << r.candidate_count_only << ','
                      << csv_double(ratio(r.candidate_count_only, m.merge_candidates)) << ','
                      << r.accepted_any << ',' << r.accepted_only << ','
                      << csv_double(ratio(r.accepted_only, m.accepted_candidates)) << ','
                      << r.rejected_any << ',' << r.rejected_only << ',' << r.redundant_only
                      << ',' << r.induced_unions_only << ','
                      << r.accepted_only_in_largest_class << '\n';
        }
        frame_csv << config.corpus_label << ',' << scale << ",mixed,0,0," << f.mixed_candidates
                  << ',' << f.mixed_candidates << ','
                  << csv_double(ratio(f.mixed_candidates, m.merge_candidates)) << ','
                  << f.mixed_accepted << ',' << f.mixed_accepted << ','
                  << csv_double(ratio(f.mixed_accepted, m.accepted_candidates)) << ','
                  << f.mixed_rejected << ',' << f.mixed_rejected << ','
                  << (f.mixed_candidates - f.mixed_accepted - f.mixed_rejected) << ",0,0\n";

        // Manual-audit files.
        const auto classes = sorted_classes(*data, *merger);
        const auto empty_ctx = empty_context(*data);
        class_file << "\n# scale " << scale << " -- top " << config.largest_classes
                   << " classes of " << m.resulting_classes << " (objects "
                   << m.initial_objects << ")\n";
        for (std::size_t i = 0; i < std::min(config.largest_classes, classes.size()); ++i) {
            const auto& cls = classes[i];
            class_file << "size=" << cls.members.size()
                       << " complete_sentence_members=" << cls.complete_sentence_members
                       << " single_token_members=" << cls.single_token_members
                       << " contains_num_members=" << cls.num_members << ": ";
            for (std::size_t j = 0;
                 j < std::min(config.class_members_shown, cls.members.size()); ++j) {
                if (j != 0) {
                    class_file << " | ";
                }
                class_file << data->object_text[cls.members[j]];
                if (has_context(*data, cls.members[j], empty_ctx)) {
                    class_file << " [S]";
                }
            }
            if (cls.members.size() > config.class_members_shown) {
                class_file << " | ... (" << cls.members.size() - config.class_members_shown
                           << " more)";
            }
            class_file << "\n";
        }

        if (!classes.empty()) {
            const auto& cls = classes.front();
            largest_file << "\n# scale " << scale << " -- size " << cls.members.size()
                         << " complete_sentence_members=" << cls.complete_sentence_members
                         << " single_token_members=" << cls.single_token_members
                         << " contains_num_members=" << cls.num_members << "\n";
            for (const ObjectId member : cls.members) {
                largest_file << data->object_text[member]
                             << (has_context(*data, member, empty_ctx) ? " [S]" : "") << "\n";
            }
        }
        probe_file << "\n# scale " << scale << "\n";
        for (const auto& probe : config.probe_objects) {
            const auto found =
                std::find(data->object_text.begin(), data->object_text.end(), probe);
            if (found == data->object_text.end()) {
                probe_file << probe << ": not an object at this scale\n";
                continue;
            }
            const auto probe_id = static_cast<ObjectId>(found - data->object_text.begin());
            const auto root = merger->class_of(probe_id);
            std::vector<ObjectId> members;
            for (ObjectId object = 0; object < data->object_text.size(); ++object) {
                if (merger->class_of(object) == root) {
                    members.push_back(object);
                }
            }
            probe_file << probe << (has_context(*data, probe_id, empty_ctx) ? " [S]" : "")
                       << ": class size " << members.size();
            if (members.size() > 1) {
                probe_file << ": ";
                for (std::size_t j = 0; j < std::min(config.class_members_shown, members.size());
                     ++j) {
                    if (j != 0) {
                        probe_file << " | ";
                    }
                    probe_file << data->object_text[members[j]]
                               << (has_context(*data, members[j], empty_ctx) ? " [S]" : "");
                }
                if (members.size() > config.class_members_shown) {
                    probe_file << " | ... (" << members.size() - config.class_members_shown
                               << " more)";
                }
            }
            probe_file << "\n";
        }

        const auto masks = candidate_type_masks(*data);
        accepted_file << "\n# scale " << scale << "\n";
        for (std::size_t type = 0; type <= kFrameTypes; ++type) {
            const bool mixed = type == kFrameTypes;
            const auto& r = f.rows[mixed ? 0 : type];
            accepted_file << "\n## "
                          << (mixed ? std::string_view("mixed")
                                    : frame_type_name(static_cast<FrameType>(type)))
                          << " (" << (mixed ? f.mixed_accepted : r.accepted_only)
                          << " accepted merges)\n";
            std::size_t shown = 0;
            for (const auto& item : merger->accepted()) {
                if (shown >= config.example_limit) {
                    break;
                }
                const auto mask = lookup_mask(masks, item.first, item.second);
                const bool is_mixed = std::popcount(mask) != 1;
                if (mixed ? !is_mixed : (is_mixed || std::countr_zero(mask) != static_cast<int>(type))) {
                    continue;
                }
                ++shown;
                accepted_file << data->object_text[item.first] << " <=> "
                              << data->object_text[item.second] << " | "
                              << frame_text(*data, item.context)
                              << " | witness_types=" << mask_text(mask)
                              << " | induced_unions=" << item.induced_unions << "\n";
            }
        }
        rejected_file << "\n# scale " << scale << "\n";
        for (std::size_t type = 0; type <= kFrameTypes; ++type) {
            const bool mixed = type == kFrameTypes;
            const auto& r = f.rows[mixed ? 0 : type];
            rejected_file << "\n## "
                          << (mixed ? std::string_view("mixed")
                                    : frame_type_name(static_cast<FrameType>(type)))
                          << " (" << (mixed ? f.mixed_rejected : r.rejected_only)
                          << " rejected candidates)\n";
            std::size_t shown = 0;
            for (const auto& item : merger->rejected()) {
                if (shown >= config.example_limit) {
                    break;
                }
                const auto mask = lookup_mask(masks, item.candidate_first, item.candidate_second);
                const bool is_mixed = std::popcount(mask) != 1;
                if (mixed ? !is_mixed : (is_mixed || std::countr_zero(mask) != static_cast<int>(type))) {
                    continue;
                }
                ++shown;
                write_rejected(rejected_file, *data, item, mask);
            }
        }

        result.scales.push_back(row);
        previous_data = std::move(data);
        previous_merger = std::move(merger);
    }
    return result;
}

}  // namespace scf::v231
