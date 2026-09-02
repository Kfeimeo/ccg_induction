#include "scf/clean_corpus.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
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

std::filesystem::path temp_dir() {
    const auto path = std::filesystem::temp_directory_path() / "scf_v231_tests";
    std::filesystem::create_directories(path);
    return path;
}

std::filesystem::path write_file(const std::string& name, const std::string& content) {
    const auto path = temp_dir() / name;
    std::ofstream out(path, std::ios::binary);
    out << content;
    return path;
}

std::string sentence_text(const SentenceCorpus& corpus, const std::size_t index) {
    std::string text;
    for (std::size_t i = 0; i < corpus.sentences[index].size(); ++i) {
        if (i != 0) {
            text += ' ';
        }
        text += corpus.token_text[corpus.sentences[index][i]];
    }
    return text;
}

scf::v23::ObjectId object(const scf::v23::ObservedDataset& data, const std::string& text) {
    for (scf::v23::ObjectId id = 0; id < data.object_text.size(); ++id) {
        if (data.object_text[id] == text) {
            return id;
        }
    }
    throw Failure("missing object " + text);
}

bool has_object(const scf::v23::ObservedDataset& data, const std::string& text) {
    return std::find(data.object_text.begin(), data.object_text.end(), text) !=
           data.object_text.end();
}

// Paragraph and document boundaries end sentences; punctuation is kept;
// the terminator is absorbed into the boundary; digits fold to <num>.
void test_clean_reader_structure() {
    const auto path = write_file(
        "clean.txt",
        "The Cat sat, quietly. It slept\n"
        "Second paragraph here\n"
        "\n"
        "\n"
        "New document 2017 begins! Really?\n");
    CleanCorpusReadOptions options;
    const auto corpus = read_sentence_corpus(path, options);
    CHECK(corpus.documents == 2);
    CHECK(corpus.paragraphs == 3);
    CHECK(corpus.sentences.size() == 5);
    CHECK(sentence_text(corpus, 0) == "the cat sat , quietly");
    CHECK(sentence_text(corpus, 1) == "it slept");           // paragraph end closes it
    CHECK(sentence_text(corpus, 2) == "second paragraph here");
    CHECK(sentence_text(corpus, 3) == "new document <num> begins");
    CHECK(sentence_text(corpus, 4) == "really");
    CHECK(corpus.sentence_document[2] == 0 && corpus.sentence_document[3] == 1);
    CHECK(corpus.sentence_paragraph[1] == 0 && corpus.sentence_paragraph[2] == 1);
    CHECK(corpus.punctuation_tokens_kept == 1);
    CHECK(corpus.terminators_consumed == 3);
    CHECK(corpus.cumulative_nominal.back() == 5 + 2 + 3 + 4 + 1);
    CHECK(corpus.cumulative_actual == corpus.cumulative_nominal);

    options.keep_punctuation = false;
    const auto stripped = read_sentence_corpus(path, options);
    CHECK(sentence_text(stripped, 0) == "the cat sat quietly");
    CHECK(stripped.punctuation_tokens_dropped == 1);
}

// No substring, frame, or witness ever crosses a sentence or paragraph
// boundary: "slept second" is never an object although the tokens are
// adjacent in the file.
void test_no_cross_boundary_objects_and_frame_types() {
    const auto path = write_file(
        "frames.txt",
        "a b c\n"
        "a d c\n"
        "\n"
        "e f. g h\n"
        "e k. g h\n");
    const auto corpus = read_sentence_corpus(path, {});
    CHECK(corpus.sentences.size() == 6);
    const auto data = scf::v23::observe_sentences(corpus.sentences, corpus.token_text,
                                                  corpus.sentences.size(), 3);
    CHECK(!has_object(data, "c a"));
    CHECK(!has_object(data, "f g"));
    CHECK(!has_object(data, "h e"));
    const auto b = object(data, "b");
    const auto d = object(data, "d");
    const auto f = object(data, "f");
    const auto k = object(data, "k");
    const auto masks = candidate_type_masks(data);
    CHECK(lookup_mask(masks, b, d) == (1U << static_cast<unsigned>(FrameType::internal_frame)));
    CHECK(lookup_mask(masks, f, k) == (1U << static_cast<unsigned>(FrameType::right_boundary)));
    // "e f" / "e k" share the frame (eps, eps): a complete-sentence witness.
    CHECK(lookup_mask(masks, object(data, "e f"), object(data, "e k")) ==
          (1U << static_cast<unsigned>(FrameType::empty_frame)));
    CHECK(lookup_mask(masks, object(data, "a b"), object(data, "a d")) ==
          (1U << static_cast<unsigned>(FrameType::left_boundary)));

    scf::v23::ConservativeMerger merger(data);
    merger.run();
    const auto frames = diagnose_frames(data, merger);
    std::uint64_t witnesses = 0;
    std::uint64_t candidates_only = 0;
    for (const auto& row : frames.rows) {
        witnesses += row.witness_count;
        candidates_only += row.candidate_count_only;
    }
    CHECK(witnesses == data.witnesses.size());
    CHECK(candidates_only + frames.mixed_candidates == merger.metrics().merge_candidates);
    CHECK(frames.rows[static_cast<std::size_t>(FrameType::empty_frame)].witness_count >= 1);
    // Complete sentences: "a b c", "a d c", "e f", "e k", "g h" (deduplicated).
    CHECK(frames.objects_with_empty_frame == 5);
}

// The v2.3 condition-D reader reproduces the v2.3 sentence construction:
// punctuation dropped, prefixes measured in condition-A tokens.
void test_v23_reader_matches_condition_d() {
    const auto path = write_file("legacy.txt", "Title . The cat , sat . It ran !\nNext doc here .\n");
    CleanCorpusReadOptions options;
    options.preprocess = Preprocess::v23_condition_d;
    const auto corpus = read_sentence_corpus(path, options);
    CHECK(corpus.sentences.size() == 4);
    CHECK(sentence_text(corpus, 1) == "the cat sat");
    CHECK(corpus.cumulative_nominal[1] == 1 + 4);   // "title" + "the cat , sat"
    CHECK(corpus.cumulative_actual[1] == 1 + 3);
    CHECK(corpus.documents == 2);
    CHECK(prefix_sentence_limit(corpus, 5) == 2);
    CHECK(prefix_sentence_limit(corpus, 6) == 3);
    CHECK(prefix_sentence_limit(corpus, 1000) == corpus.sentences.size());
}

// Nested prefixes: the prefix at a smaller scale is a prefix of the larger one
// and the ladder driver writes every required file deterministically.
void test_ladder_outputs_are_deterministic() {
    std::ostringstream text;
    for (int i = 0; i < 40; ++i) {
        text << "the dog runs " << i << " times. the cat runs " << i << " times\n"
             << "a big house. a small house\n\n";
    }
    const auto path = write_file("ladder.txt", text.str());
    const auto run = [&](const std::string& name) {
        CleanCorpusConfig config;
        config.input_text = path;
        config.output_dir = temp_dir() / name;
        config.scales = {50, 200};
        std::filesystem::remove_all(config.output_dir);
        const auto result = run_clean_corpus_scaling(config);
        CHECK(result.scales.size() == 2);
        CHECK(result.scales[0].sentences < result.scales[1].sentences);
        CHECK(result.scales[0].actual_tokens >= 50);
        CHECK(result.scales[1].actual_tokens >= 200);
        for (const char* file : {"clean_corpus_scaling.csv", "frame_type_metrics.csv",
                                 "largest_classes.txt", "successful_merges_by_frame_type.txt",
                                 "rejected_merges_by_frame_type.txt",
                                 "largest_class_members.txt", "probe_object_classes.txt"}) {
            CHECK(std::filesystem::exists(config.output_dir / file));
        }
        std::ifstream csv(config.output_dir / "clean_corpus_scaling.csv");
        std::stringstream buffer;
        buffer << csv.rdbuf();
        return buffer.str();
    };
    const auto first = run("run_a");
    const auto second = run("run_b");
    // runtime/RSS columns differ between runs; compare everything before them.
    const auto strip = [](const std::string& csv) {
        std::istringstream lines(csv);
        std::string line;
        std::string out;
        while (std::getline(lines, line)) {
            std::size_t commas = 0;
            std::size_t cut = line.size();
            for (std::size_t i = 0; i < line.size(); ++i) {
                if (line[i] == ',' && ++commas == 37) {
                    cut = i;
                    break;
                }
            }
            out += line.substr(0, cut) + '\n';
        }
        return out;
    };
    CHECK(strip(first) == strip(second));
    CHECK(first.find("clean_wiki_body,clean_body_keep_punct,50,") != std::string::npos);
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, void (*)()>> tests{
        {"clean_reader_structure", test_clean_reader_structure},
        {"no_cross_boundary_objects_and_frame_types",
         test_no_cross_boundary_objects_and_frame_types},
        {"v23_reader_matches_condition_d", test_v23_reader_matches_condition_d},
        {"ladder_outputs_are_deterministic", test_ladder_outputs_are_deterministic},
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
