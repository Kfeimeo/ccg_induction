#include "scf/clean_corpus.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if (!(condition)) {                                                                 \
            throw Failure(std::string("CHECK failed: ") + #condition + " at line " +      \
                          std::to_string(__LINE__));                                         \
        }                                                                                   \
    } while (false)

using namespace scf::v231;

std::filesystem::path temp_dir(const std::string& name) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

std::filesystem::path write_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
    return path;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

std::string sentence_text(const SentenceCorpus& corpus, const std::size_t index) {
    std::ostringstream out;
    for (std::size_t i = 0; i < corpus.sentences[index].size(); ++i) {
        out << (i == 0 ? "" : " ") << corpus.token_text[corpus.sentences[index][i]];
    }
    return out.str();
}

scf::v23::ObjectId object(const scf::v23::ObservedDataset& data, const std::string& text) {
    const auto found = std::find(data.object_text.begin(), data.object_text.end(), text);
    if (found == data.object_text.end()) {
        throw Failure("missing object " + text);
    }
    return static_cast<scf::v23::ObjectId>(found - data.object_text.begin());
}

void test_structured_loader_keeps_boundaries_and_consumes_final_punctuation() {
    const auto dir = temp_dir("scf_v231_loader");
    const auto path = write_file(dir / "tiny.scs",
                                 "#doc a\n#par\nThe cat, 12 mice!\nIt ran...\n#par\n"
                                 "Second paragraph.\n#doc b\n#par\n?\nOnly one; here\n");
    const auto corpus = load_structured_corpus(path, 0);
    CHECK(corpus.documents == 2);
    CHECK(corpus.paragraphs == 3);
    CHECK(corpus.sentences.size() == 4);  // "?" becomes empty and is dropped
    CHECK(corpus.dropped_empty_sentences == 1);
    CHECK(sentence_text(corpus, 0) == "the cat , <num> mice");  // punctuation kept
    CHECK(sentence_text(corpus, 1) == "it ran");                 // "..." consumed
    CHECK(sentence_text(corpus, 3) == "only one ; here");
    CHECK(corpus.consumed_final_punctuation == 1 + 3 + 1 + 1);
    CHECK(corpus.sentence_document[0] == 0 && corpus.sentence_document[3] == 1);
    CHECK(corpus.sentence_paragraph[0] == 0 && corpus.sentence_paragraph[1] == 0);
    CHECK(corpus.sentence_paragraph[2] == 1 && corpus.sentence_paragraph[3] == 2);
    CHECK(corpus.cumulative_nominal == std::vector<std::uint64_t>({5, 7, 9, 13}));
    CHECK(corpus.cumulative_actual == corpus.cumulative_nominal);
    CHECK(prefix_sentences(corpus, 1) == 1);
    CHECK(prefix_sentences(corpus, 5) == 1);
    CHECK(prefix_sentences(corpus, 6) == 2);
    CHECK(prefix_sentences(corpus, 13) == 4);
    CHECK(prefix_sentences(corpus, 14) == 0);

    // The token limit stops at the next document boundary.
    const auto limited = load_structured_corpus(path, 6);
    CHECK(limited.sentences.size() == 3);
    CHECK(limited.documents == 1);
    std::filesystem::remove_all(dir);
}

void test_condition_d_loader_matches_v23_rule() {
    const auto dir = temp_dir("scf_v231_cond_d");
    const auto path = write_file(dir / "flat.txt", "The cat, 12 mice! It ran. ...\nx y\n");
    const auto corpus = load_condition_d_corpus(path, 0);
    CHECK(corpus.sentences.size() == 3);
    CHECK(sentence_text(corpus, 0) == "the cat <num> mice");  // punctuation removed
    CHECK(sentence_text(corpus, 1) == "it ran");
    CHECK(sentence_text(corpus, 2) == "x y");
    // nominal counts condition-A tokens (the comma), actual counts condition-D.
    CHECK(corpus.cumulative_nominal == std::vector<std::uint64_t>({5, 7, 9}));
    CHECK(corpus.cumulative_actual == std::vector<std::uint64_t>({4, 6, 8}));
    std::filesystem::remove_all(dir);
}

void test_frame_types_are_classified_from_the_epsilon_root() {
    std::vector<std::string> text;
    std::vector<std::vector<std::uint32_t>> sentences;
    const auto add = [&](const std::string& line) {
        std::istringstream words(line);
        std::vector<std::uint32_t> sentence;
        std::string word;
        while (words >> word) {
            const auto found = std::find(text.begin(), text.end(), word);
            if (found == text.end()) {
                text.push_back(word);
                sentence.push_back(static_cast<std::uint32_t>(text.size() - 1));
            } else {
                sentence.push_back(static_cast<std::uint32_t>(found - text.begin()));
            }
        }
        sentences.push_back(sentence);
    };
    add("a");           // complete-sentence spans: a, b share (eps, eps)
    add("b");
    add("a x y");       // left boundary: a/b share (eps, x y); right boundary x y? no
    add("b x y");
    add("k a z");       // internal: a/b share (k, z)
    add("k b z");
    add("q r a");       // right boundary: a/b share (q r, eps)
    add("q r b");
    const auto data = scf::v23::observe_sentences(sentences, text, sentences.size(), 3);
    std::array<std::uint64_t, 4> counts{};
    for (const auto& witness : data.witnesses) {
        if ((witness.first == object(data, "a") && witness.second == object(data, "b")) ||
            (witness.first == object(data, "b") && witness.second == object(data, "a"))) {
            ++counts[static_cast<std::size_t>(classify_context(data, witness.context))];
        }
    }
    CHECK(counts[0] == 1 && counts[1] == 1 && counts[2] == 1 && counts[3] == 1);

    scf::v23::ConservativeMerger merger(data);
    merger.run();
    const auto frames = compute_frame_diagnostics(data, merger);
    std::uint64_t witnesses = 0;
    std::uint64_t candidates = 0;
    std::uint64_t accepted = 0;
    std::uint64_t rejected = 0;
    std::uint64_t redundant = 0;
    for (const auto& row : frames.rows) {
        witnesses += row.witness_count;
        candidates += row.candidate_count;
        accepted += row.accepted_merge_count;
        rejected += row.rejected_merge_count;
        redundant += row.redundant_candidate_count;
    }
    CHECK(witnesses == merger.metrics().local_witnesses);
    CHECK(candidates == merger.metrics().merge_candidates);
    CHECK(accepted == merger.metrics().accepted_candidates);
    CHECK(rejected == merger.metrics().rejected_candidates);
    CHECK(redundant == merger.metrics().redundant_candidates);
    CHECK(frames.largest_class_size == merger.metrics().largest_class);
    // Objects observed as complete sentences: a, b, "a x y", "b x y", "k a z",
    // "k b z", "q r a", "q r b".
    CHECK(frames.objects_with_empty_frame == 8);
    // The a/b pair has witnesses of every type, so it is exclusive to none.
    CHECK(frames.rows[0].exclusive_candidate_count < frames.rows[0].candidate_count);
}

void test_ladder_is_deterministic_and_writes_all_outputs() {
    const auto dir = temp_dir("scf_v231_ladder");
    std::ostringstream corpus;
    corpus << "#doc 1\n#par\n";
    for (int i = 0; i < 40; ++i) {
        corpus << "The dog sleeps, quietly.\nThe cat sleeps.\nA dog runs!\nA cat runs.\n"
               << "Rare" << i << " things happen.\nIntroduction\n";
    }
    const auto path = write_file(dir / "ladder.scs", corpus.str());
    CleanCorpusConfig config;
    config.input = path;
    config.corpus_label = "tiny";
    config.scales = {50, 400};
    const auto run = [&](const std::string& name) {
        config.output_dir = dir / name;
        return run_clean_corpus_scaling(config);
    };
    const auto first = run("first");
    const auto second = run("second");
    CHECK(first.scales.size() == 2);
    CHECK(first.scales[0].nominal_tokens == 50 && first.scales[0].actual_tokens >= 50);
    CHECK(first.scales[1].sentences > first.scales[0].sentences);
    for (const char* file : {"clean_corpus_scaling.csv", "frame_type_metrics.csv",
                             "largest_classes.txt", "successful_merges_by_frame_type.txt",
                             "rejected_merges_by_frame_type.txt"}) {
        CHECK(std::filesystem::exists(dir / "first" / file));
        const auto a = read_file(dir / "first" / file);
        const auto b = read_file(dir / "second" / file);
        if (std::string(file).find(".csv") != std::string::npos) {
            // Runtime / RSS columns differ; compare the deterministic prefix.
            std::istringstream sa(a), sb(b);
            std::string la, lb;
            while (std::getline(sa, la) && std::getline(sb, lb)) {
                CHECK(la.substr(0, la.rfind(',', la.rfind(',') - 1)) ==
                      lb.substr(0, lb.rfind(',', lb.rfind(',') - 1)));
            }
        } else {
            CHECK(a == b);
        }
    }
    const auto frames = read_file(dir / "first" / "frame_type_metrics.csv");
    CHECK(frames.find("empty_frame") != std::string::npos);
    CHECK(frames.find("internal_frame") != std::string::npos);
    CHECK(first.scales[1].frames.objects_with_empty_frame >= 1);  // "introduction"
    std::filesystem::remove_all(dir);
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, void (*)()>> tests{
        {"structured_loader_keeps_boundaries_and_consumes_final_punctuation",
         test_structured_loader_keeps_boundaries_and_consumes_final_punctuation},
        {"condition_d_loader_matches_v23_rule", test_condition_d_loader_matches_v23_rule},
        {"frame_types_are_classified_from_the_epsilon_root",
         test_frame_types_are_classified_from_the_epsilon_root},
        {"ladder_is_deterministic_and_writes_all_outputs",
         test_ladder_is_deterministic_and_writes_all_outputs},
    };
    std::size_t failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << tests.size() - failures << '/' << tests.size() << " tests passed\n";
    return failures == 0 ? 0 : 1;
}
