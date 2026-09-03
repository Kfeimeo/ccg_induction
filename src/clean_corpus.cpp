#include "scf/clean_corpus.hpp"
#include "scf/platform.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace scf::v231 {
namespace {

std::uint64_t pair_key(v23::ObjectId first, v23::ObjectId second) {
    if (second < first) {
        std::swap(first, second);
    }
    return (static_cast<std::uint64_t>(first) << 32U) | second;
}

std::string csv_double(const double value) {
    if (value < 0.0) {
        return "-1";
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(6) << value;
    return out.str();
}

double share(const std::uint64_t part, const std::uint64_t whole) {
    return whole == 0 ? 0.0 : static_cast<double>(part) / static_cast<double>(whole);
}

struct Interner {
    std::vector<std::string> text;
    std::unordered_map<std::string, std::uint32_t> ids;

    std::uint32_t intern(const std::string_view token) {
        const auto found = ids.find(std::string(token));
        if (found != ids.end()) {
            return found->second;
        }
        const auto id = static_cast<std::uint32_t>(text.size());
        text.emplace_back(token);
        ids.emplace(std::string(token), id);
        return id;
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// Corpus loading
// ---------------------------------------------------------------------------

SentenceCorpus load_structured_corpus(const std::filesystem::path& input,
                                      const std::uint64_t token_limit) {
    std::ifstream stream(input, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot open structured corpus: " + input.string());
    }
    SentenceCorpus corpus;
    Interner interner;
    interner.ids.reserve(1 << 20);
    std::string line;
    std::uint64_t cumulative = 0;
    bool in_document = false;
    std::uint32_t document = 0;
    std::uint32_t paragraph = 0;
    bool limit_reached = false;
    while (std::getline(stream, line)) {
        scf::platform::strip_trailing_cr(line);
        if (line.rfind("#doc", 0) == 0) {
            if (limit_reached) {
                break;  // stop at the next document boundary
            }
            if (in_document) {
                ++document;
            }
            in_document = true;
            ++corpus.documents;
            continue;
        }
        if (line == "#par") {
            if (corpus.paragraphs != 0) {
                ++paragraph;
            }
            ++corpus.paragraphs;
            continue;
        }
        if (line.empty()) {
            continue;
        }
        std::vector<std::uint32_t> sentence;
        v21::tokenize_line(line, [&](const std::string_view token) {
            sentence.push_back(interner.intern(token));
        });
        // The sentence-final . ? ! run is the <EOS> boundary signal, not an
        // object: consume it exactly as the v2.2/v2.3 segmentation does.
        while (!sentence.empty() &&
               v21::is_final_punctuation_token(interner.text[sentence.back()])) {
            sentence.pop_back();
            ++corpus.consumed_final_punctuation;
        }
        if (sentence.empty()) {
            ++corpus.dropped_empty_sentences;
            continue;
        }
        cumulative += sentence.size();
        corpus.sentences.push_back(std::move(sentence));
        corpus.sentence_document.push_back(document);
        corpus.sentence_paragraph.push_back(paragraph);
        corpus.cumulative_nominal.push_back(cumulative);
        corpus.cumulative_actual.push_back(cumulative);
        if (token_limit != 0 && cumulative >= token_limit) {
            limit_reached = true;
        }
    }
    corpus.token_text = std::move(interner.text);
    return corpus;
}

SentenceCorpus load_condition_d_corpus(const std::filesystem::path& input,
                                       const std::uint64_t token_limit) {
    const v21::TokenCorpus base = v21::build_token_corpus(input, token_limit);
    const auto spans = v21::segment_sentences(base);
    SentenceCorpus corpus;
    corpus.token_text = base.token_text;
    corpus.documents = base.documents;
    corpus.paragraphs = base.documents;  // no paragraph structure in this mode
    std::uint64_t count_a = 0;
    std::uint64_t count_d = 0;
    for (const auto& span : spans) {
        std::vector<std::uint32_t> sentence;
        for (std::size_t pos = span.begin; pos < span.end; ++pos) {
            const auto token = base.stream[pos];
            if (!v21::is_punctuation_token(base.token_text[token])) {
                sentence.push_back(token);
            }
        }
        count_a += span.end - span.begin;
        count_d += sentence.size();
        if (span.has_final_punct) {
            ++corpus.consumed_final_punctuation;
        }
        if (sentence.empty()) {
            ++corpus.dropped_empty_sentences;
            continue;
        }
        corpus.sentences.push_back(std::move(sentence));
        corpus.sentence_document.push_back(static_cast<std::uint32_t>(span.document));
        corpus.sentence_paragraph.push_back(static_cast<std::uint32_t>(span.document));
        corpus.cumulative_nominal.push_back(count_a);
        corpus.cumulative_actual.push_back(count_d);
    }
    return corpus;
}

std::size_t prefix_sentences(const SentenceCorpus& corpus, const std::uint64_t scale) {
    if (corpus.cumulative_nominal.empty() || scale > corpus.cumulative_nominal.back()) {
        return 0;
    }
    const auto boundary = std::lower_bound(corpus.cumulative_nominal.begin(),
                                           corpus.cumulative_nominal.end(), scale);
    return static_cast<std::size_t>(boundary - corpus.cumulative_nominal.begin()) + 1;
}

// ---------------------------------------------------------------------------
// Frame types
// ---------------------------------------------------------------------------

FrameType classify_context(const v23::ObservedDataset& data, const v23::ContextId context) {
    const auto key = data.context_keys.at(context);
    const bool left_empty = key.left == 0;    // trie root 0 == epsilon
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

FrameDiagnostics compute_frame_diagnostics(const v23::ObservedDataset& data,
                                           const v23::ConservativeMerger& merger) {
    FrameDiagnostics result;
    const auto index = [](const FrameType type) { return static_cast<std::size_t>(type); };

    std::vector<std::uint8_t> context_type(data.context_keys.size());
    for (v23::ContextId context = 0; context < data.context_keys.size(); ++context) {
        context_type[context] = static_cast<std::uint8_t>(classify_context(data, context));
    }

    // Candidate table: first witness per unordered pair in witness order (the
    // same rule as ConservativeMerger::run), plus the set of frame types over
    // all witnesses of the pair.
    std::map<std::uint64_t, std::pair<v23::ContextId, std::uint8_t>> candidates;
    for (const auto& witness : data.witnesses) {
        ++result.rows[context_type[witness.context]].witness_count;
        auto [it, inserted] = candidates.try_emplace(
            pair_key(witness.first, witness.second), witness.context, std::uint8_t{0});
        static_cast<void>(inserted);
        it->second.second |= static_cast<std::uint8_t>(1U << context_type[witness.context]);
    }
    const auto is_exclusive = [&](const std::uint64_t key, const FrameType type) {
        const auto found = candidates.find(key);
        return found != candidates.end() &&
               found->second.second == static_cast<std::uint8_t>(1U << index(type));
    };
    for (const auto& [key, entry] : candidates) {
        const auto type = static_cast<FrameType>(context_type[entry.first]);
        ++result.rows[index(type)].candidate_count;
        if (is_exclusive(key, type)) {
            ++result.rows[index(type)].exclusive_candidate_count;
        }
    }

    // Largest final class.
    v23::ObjectId largest_root = 0;
    {
        std::map<v23::ObjectId, std::uint64_t> sizes;
        for (v23::ObjectId object = 0; object < data.object_text.size(); ++object) {
            ++sizes[merger.class_of(object)];
        }
        for (const auto& [root, size] : sizes) {
            if (size > result.largest_class_size) {
                result.largest_class_size = size;
                largest_root = root;
            }
        }
    }

    for (const auto& record : merger.accepted()) {
        const auto type = static_cast<FrameType>(context_type[record.context]);
        auto& row = result.rows[index(type)];
        ++row.accepted_merge_count;
        row.induced_unions += record.induced_unions;
        if (is_exclusive(pair_key(record.first, record.second), type)) {
            ++row.exclusive_accepted_count;
        }
        if (result.largest_class_size > 0 && merger.class_of(record.first) == largest_root) {
            ++row.largest_class_accepted_merges;
        }
    }
    for (const auto& record : merger.rejected()) {
        ++result.rows[index(static_cast<FrameType>(context_type[record.candidate_context]))]
              .rejected_merge_count;
    }
    for (auto& row : result.rows) {
        row.redundant_candidate_count =
            row.candidate_count - row.accepted_merge_count - row.rejected_merge_count;
    }

    // Objects observed as complete sentences: the (eps, eps) context.
    for (v23::ObjectId object = 0; object < data.contexts_of_object.size(); ++object) {
        bool has_empty = false;
        for (const auto context : data.contexts_of_object[object]) {
            if (context_type[context] == static_cast<std::uint8_t>(FrameType::empty_frame)) {
                has_empty = true;
                break;
            }
        }
        if (has_empty) {
            ++result.objects_with_empty_frame;
            if (result.largest_class_size > 0 && merger.class_of(object) == largest_root) {
                ++result.largest_class_members_with_empty_frame;
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Diagnostic files
// ---------------------------------------------------------------------------

namespace {

std::string frame_text(const v23::ObservedDataset& data, const v23::ContextId context) {
    return "L=[" + data.left_context_text(context) + "] R=[" + data.right_context_text(context) +
           "]";
}

void write_merge_examples(std::ostream& accepted,
                          std::ostream& rejected,
                          const v23::ObservedDataset& data,
                          const v23::ConservativeMerger& merger,
                          const std::uint64_t scale,
                          const std::size_t limit) {
    std::map<std::uint64_t, std::uint8_t> witness_types;
    std::map<std::uint64_t, std::uint32_t> witness_counts;
    for (const auto& witness : data.witnesses) {
        const auto key = pair_key(witness.first, witness.second);
        witness_types[key] |= static_cast<std::uint8_t>(
            1U << static_cast<unsigned>(classify_context(data, witness.context)));
        ++witness_counts[key];
    }
    const auto type_set_text = [&](const std::uint64_t key) {
        std::string text;
        const auto mask = witness_types[key];
        for (std::size_t i = 0; i < kFrameTypeNames.size(); ++i) {
            if ((mask & (1U << i)) != 0) {
                if (!text.empty()) {
                    text += '+';
                }
                text += kFrameTypeNames[i];
            }
        }
        return text;
    };

    accepted << "\n# scale " << scale << "\n";
    for (std::size_t t = 0; t < kFrameTypeNames.size(); ++t) {
        const auto type = static_cast<FrameType>(t);
        accepted << "\n## " << kFrameTypeNames[t] << " accepted merges (first " << limit
                 << " in candidate order)\n";
        std::size_t shown = 0;
        for (const auto& item : merger.accepted()) {
            if (classify_context(data, item.context) != type) {
                continue;
            }
            if (shown++ >= limit) {
                break;
            }
            const auto key = pair_key(item.first, item.second);
            accepted << data.object_text[item.first] << " <=> " << data.object_text[item.second]
                     << " | " << frame_text(data, item.context)
                     << " | induced_unions=" << item.induced_unions
                     << " | pair_witnesses=" << witness_counts[key]
                     << " | witness_types=" << type_set_text(key) << "\n";
        }
        if (shown == 0) {
            accepted << "(none)\n";
        }
    }

    rejected << "\n# scale " << scale << "\n";
    for (std::size_t t = 0; t < kFrameTypeNames.size(); ++t) {
        const auto type = static_cast<FrameType>(t);
        rejected << "\n## " << kFrameTypeNames[t] << " rejected merges (first " << limit
                 << " in candidate order)\n";
        std::size_t shown = 0;
        for (const auto& item : merger.rejected()) {
            if (classify_context(data, item.candidate_context) != type) {
                continue;
            }
            if (shown++ >= limit) {
                break;
            }
            rejected << "candidate " << data.object_text[item.candidate_first] << " <=> "
                     << data.object_text[item.candidate_second] << " | witness "
                     << frame_text(data, item.candidate_context) << " | pair_witnesses="
                     << witness_counts[pair_key(item.candidate_first, item.candidate_second)]
                     << "\n  conflict: ";
            if (item.merge_on_left) {
                rejected << "Comp(" << data.object_text[item.shared_operand_first] << ", "
                         << data.object_text[item.first_source] << ", "
                         << data.object_text[item.first_output] << ") vs Comp("
                         << data.object_text[item.shared_operand_second] << ", "
                         << data.object_text[item.second_source] << ", "
                         << data.object_text[item.second_output] << ")";
            } else {
                rejected << "Comp(" << data.object_text[item.first_source] << ", "
                         << data.object_text[item.shared_operand_first] << ", "
                         << data.object_text[item.first_output] << ") vs Comp("
                         << data.object_text[item.second_source] << ", "
                         << data.object_text[item.shared_operand_second] << ", "
                         << data.object_text[item.second_output] << ")";
            }
            rejected << "; outputs have different observed exact-context profiles and no "
                        "direct substitution witness\n";
        }
        if (shown == 0) {
            rejected << "(none)\n";
        }
    }
}

void write_largest_classes(std::ostream& output,
                           const v23::ObservedDataset& data,
                           const v23::ConservativeMerger& merger,
                           const std::uint64_t scale,
                           const std::size_t class_limit) {
    auto classes = merger.classes();
    std::sort(classes.begin(), classes.end(), [](const auto& a, const auto& b) {
        return a.size() > b.size() || (a.size() == b.size() && a.front() < b.front());
    });
    // Accepted merges grouped by final class root and frame type.
    std::map<v23::ObjectId, std::array<std::uint64_t, 4>> merges_by_root;
    for (const auto& item : merger.accepted()) {
        ++merges_by_root[merger.class_of(item.first)]
                        [static_cast<std::size_t>(classify_context(data, item.context))];
    }
    output << "\n# scale " << scale << " -- " << class_limit << " largest classes\n";
    for (std::size_t i = 0; i < std::min(class_limit, classes.size()); ++i) {
        const auto& cls = classes[i];
        std::uint64_t empty_members = 0;
        std::uint64_t lexical = 0;
        for (const auto object : cls) {
            if (classify_context(data, data.contexts_of_object[object].front()) ==
                    FrameType::empty_frame ||
                std::any_of(data.contexts_of_object[object].begin(),
                            data.contexts_of_object[object].end(), [&](const auto context) {
                                return classify_context(data, context) == FrameType::empty_frame;
                            })) {
                ++empty_members;
            }
            if (data.object_tokens[object].size() == 1) {
                ++lexical;
            }
        }
        const auto& merges = merges_by_root[merger.class_of(cls.front())];
        output << "size=" << cls.size() << " lexical_members=" << lexical
               << " members_with_empty_frame=" << empty_members << " accepted_merges["
               << kFrameTypeNames[0] << "=" << merges[0] << " " << kFrameTypeNames[1] << "="
               << merges[1] << " " << kFrameTypeNames[2] << "=" << merges[2] << " "
               << kFrameTypeNames[3] << "=" << merges[3] << "]\n  ";
        for (std::size_t j = 0; j < std::min<std::size_t>(40, cls.size()); ++j) {
            if (j != 0) {
                output << " | ";
            }
            output << data.object_text[cls[j]];
        }
        if (cls.size() > 40) {
            output << " | ... (" << cls.size() - 40 << " more)";
        }
        output << "\n";
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Ladder driver
// ---------------------------------------------------------------------------

CleanCorpusResult run_clean_corpus_scaling(const CleanCorpusConfig& config) {
    if (config.input.empty()) {
        throw std::runtime_error("input is required");
    }
    if (config.preprocessing != "structured" && config.preprocessing != "v23d") {
        throw std::runtime_error("preprocessing must be structured or v23d");
    }
    auto scales = config.scales;
    std::sort(scales.begin(), scales.end());
    scales.erase(std::unique(scales.begin(), scales.end()), scales.end());
    if (scales.empty()) {
        throw std::runtime_error("no scales requested");
    }
    std::filesystem::create_directories(config.output_dir);
    const std::uint64_t read_limit =
        static_cast<std::uint64_t>(static_cast<double>(scales.back()) * 1.25) + 2'000'000;
    const SentenceCorpus corpus = config.preprocessing == "structured"
                                      ? load_structured_corpus(config.input, read_limit)
                                      : load_condition_d_corpus(config.input, read_limit);
    while (!scales.empty() && prefix_sentences(corpus, scales.back()) == 0) {
        scales.pop_back();
    }
    if (scales.empty()) {
        throw std::runtime_error("corpus smaller than the smallest requested scale");
    }

    std::ofstream csv(config.output_dir / "clean_corpus_scaling.csv");
    csv << "corpus,preprocessing,nominal_tokens,actual_tokens,sentences,documents,paragraphs,"
           "initial_objects,local_witnesses,merge_candidates,accepted_merges,rejected_merges,"
           "redundant_candidates,induced_unions,resulting_classes,largest_class,"
           "largest_class_ratio,median_class_size,p95_class_size,accepted_merge_rate,"
           "rejected_merge_rate,objects_with_empty_frame,empty_frame_object_share,"
           "empty_frame_witness_share,empty_frame_candidate_share,"
           "empty_frame_accepted_share,empty_frame_exclusive_accepted,"
           "largest_class_members_with_empty_frame,common_objects_prev,changed_pairs_prev,"
           "changed_pair_share_prev,pos_labeled_objects,within_class_pos_purity,"
           "within_class_labeled_pairs,pairwise_same_pos_precision,runtime_seconds,"
           "peak_rss_mb\n";
    std::ofstream frames_csv(config.output_dir / "frame_type_metrics.csv");
    frames_csv << "corpus,preprocessing,nominal_tokens,frame_type,witness_count,"
                  "witness_share,candidate_count,candidate_share,accepted_merge_count,"
                  "accepted_share,rejected_merge_count,rejected_share,"
                  "redundant_candidate_count,induced_unions,exclusive_candidate_count,"
                  "exclusive_accepted_count,largest_class_accepted_merges,"
                  "largest_class_contribution_share,acceptance_rate_within_type\n";
    std::ofstream class_file(config.output_dir / "largest_classes.txt");
    std::ofstream accepted_file(config.output_dir / "successful_merges_by_frame_type.txt");
    std::ofstream rejected_file(config.output_dir / "rejected_merges_by_frame_type.txt");
    class_file << "# SCF v2.3.1 largest learned classes (" << config.corpus_label << ", "
               << config.preprocessing << ")\n";
    accepted_file << "# SCF v2.3.1 accepted merges by witness frame type ("
                  << config.corpus_label << ", " << config.preprocessing << ")\n";
    rejected_file << "# SCF v2.3.1 rejected merges by witness frame type ("
                  << config.corpus_label << ", " << config.preprocessing << ")\n";

    CleanCorpusResult result;
    result.available_sentences = corpus.sentences.size();
    result.available_actual_tokens =
        corpus.cumulative_actual.empty() ? 0 : corpus.cumulative_actual.back();
    std::unique_ptr<v23::ObservedDataset> previous_data;
    std::unique_ptr<v23::ConservativeMerger> previous_merger;
    for (const std::uint64_t scale : scales) {
        const auto start = std::chrono::steady_clock::now();
        const std::size_t sentence_limit = prefix_sentences(corpus, scale);
        auto data = std::make_unique<v23::ObservedDataset>(v23::observe_sentences(
            corpus.sentences, corpus.token_text, sentence_limit, config.max_substring_length));
        auto merger = std::make_unique<v23::ConservativeMerger>(*data);
        merger->run();

        CleanScaleResult row;
        row.nominal_tokens = scale;
        row.actual_tokens = corpus.cumulative_actual[sentence_limit - 1];
        row.sentences = sentence_limit;
        row.documents = static_cast<std::uint64_t>(corpus.sentence_document[sentence_limit - 1]) + 1;
        row.paragraphs =
            static_cast<std::uint64_t>(corpus.sentence_paragraph[sentence_limit - 1]) + 1;
        row.merge = merger->metrics();
        row.frames = compute_frame_diagnostics(*data, *merger);
        row.pos = v23::evaluate_pos(*data, *merger, config.ud_conllu);
        if (previous_data) {
            row.change =
                v23::compare_partitions(*previous_data, *previous_merger, *data, *merger);
        }
        row.runtime_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        row.peak_rss_mb = scf::platform::peak_rss_mb();
        result.scales.push_back(row);

        const auto& m = row.merge;
        const auto& f = row.frames;
        const auto& empty = f.rows[0];
        csv << config.corpus_label << ',' << config.preprocessing << ',' << row.nominal_tokens
            << ',' << row.actual_tokens << ',' << row.sentences << ',' << row.documents << ','
            << row.paragraphs << ',' << m.initial_objects << ',' << m.local_witnesses << ','
            << m.merge_candidates << ',' << m.accepted_candidates << ','
            << m.rejected_candidates << ',' << m.redundant_candidates << ','
            << m.induced_unions << ',' << m.resulting_classes << ',' << m.largest_class << ','
            << csv_double(m.largest_class_ratio) << ',' << m.median_class_size << ','
            << m.p95_class_size << ','
            << csv_double(share(m.accepted_candidates, m.merge_candidates)) << ','
            << csv_double(share(m.rejected_candidates, m.merge_candidates)) << ','
            << f.objects_with_empty_frame << ','
            << csv_double(share(f.objects_with_empty_frame, m.initial_objects)) << ','
            << csv_double(share(empty.witness_count, m.local_witnesses)) << ','
            << csv_double(share(empty.candidate_count, m.merge_candidates)) << ','
            << csv_double(share(empty.accepted_merge_count, m.accepted_candidates)) << ','
            << empty.exclusive_accepted_count << ','
            << f.largest_class_members_with_empty_frame << ',' << row.change.common_objects
            << ',' << row.change.changed_pairs << ','
            << csv_double(row.change.changed_pair_share) << ',' << row.pos.labeled_objects
            << ',' << csv_double(row.pos.within_class_purity) << ','
            << row.pos.within_class_labeled_pairs << ','
            << csv_double(row.pos.pairwise_same_pos_precision) << ','
            << csv_double(row.runtime_seconds) << ',' << csv_double(row.peak_rss_mb) << '\n';
        std::uint64_t largest_class_merges = 0;
        for (const auto& type_row : f.rows) {
            largest_class_merges += type_row.largest_class_accepted_merges;
        }
        for (std::size_t t = 0; t < kFrameTypeNames.size(); ++t) {
            const auto& r = f.rows[t];
            frames_csv << config.corpus_label << ',' << config.preprocessing << ','
                       << row.nominal_tokens << ',' << kFrameTypeNames[t] << ','
                       << r.witness_count << ','
                       << csv_double(share(r.witness_count, m.local_witnesses)) << ','
                       << r.candidate_count << ','
                       << csv_double(share(r.candidate_count, m.merge_candidates)) << ','
                       << r.accepted_merge_count << ','
                       << csv_double(share(r.accepted_merge_count, m.accepted_candidates))
                       << ',' << r.rejected_merge_count << ','
                       << csv_double(share(r.rejected_merge_count, m.rejected_candidates))
                       << ',' << r.redundant_candidate_count << ',' << r.induced_unions << ','
                       << r.exclusive_candidate_count << ',' << r.exclusive_accepted_count
                       << ',' << r.largest_class_accepted_merges << ','
                       << csv_double(share(r.largest_class_accepted_merges, largest_class_merges))
                       << ',' << csv_double(share(r.accepted_merge_count, r.candidate_count))
                       << '\n';
        }
        write_largest_classes(class_file, *data, *merger, scale, config.largest_classes);
        write_merge_examples(accepted_file, rejected_file, *data, *merger, scale,
                             config.example_limit);
        csv.flush();
        frames_csv.flush();
        class_file.flush();
        accepted_file.flush();
        rejected_file.flush();
        previous_data = std::move(data);
        previous_merger = std::move(merger);
    }
    return result;
}

}  // namespace scf::v231
