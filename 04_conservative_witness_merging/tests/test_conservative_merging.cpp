#include "scf/conservative_merging.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
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

using namespace scf::v23;

struct TinyCorpus {
    std::vector<std::string> text{"<unused>"};
    std::map<std::string, std::uint32_t> ids;
    std::vector<std::vector<std::uint32_t>> sentences;

    void add(const std::string& line) {
        std::istringstream words(line);
        std::vector<std::uint32_t> sentence;
        std::string word;
        while (words >> word) {
            const auto [it, inserted] =
                ids.try_emplace(word, static_cast<std::uint32_t>(text.size()));
            if (inserted) {
                text.push_back(word);
            }
            sentence.push_back(it->second);
        }
        sentences.push_back(std::move(sentence));
    }
};

ObjectId object(const ObservedDataset& data, const std::string& text) {
    for (ObjectId id = 0; id < data.object_text.size(); ++id) {
        if (data.object_text[id] == text) {
            return id;
        }
    }
    throw Failure("missing object " + text);
}

void test_exact_sentence_context_and_deduplication() {
    TinyCorpus corpus;
    corpus.add("l u r");
    corpus.add("l v r");
    corpus.add("l u r");  // occurrence frequency must not change the set
    const auto data = observe_sentences(corpus.sentences, corpus.text, 3, 3);
    const auto u = object(data, "u");
    const auto v = object(data, "v");
    std::size_t pair_witnesses = 0;
    ContextId context = 0;
    for (const auto& witness : data.witnesses) {
        if ((witness.first == u && witness.second == v) ||
            (witness.first == v && witness.second == u)) {
            ++pair_witnesses;
            context = witness.context;
        }
    }
    CHECK(pair_witnesses == 1);
    CHECK(data.left_context_text(context) == "l");
    CHECK(data.right_context_text(context) == "r");
}

void test_local_witness_is_not_automatic_union() {
    TinyCorpus corpus;
    corpus.add("a p z");
    corpus.add("a q z");
    corpus.add("l p t r");
    corpus.add("m q t s");
    const auto data = observe_sentences(corpus.sentences, corpus.text, 4, 3);
    ConservativeMerger merger(data);
    merger.run();
    CHECK(!merger.same_class(object(data, "p"), object(data, "q")));
    CHECK(merger.metrics().rejected_candidates > 0);
    CHECK(!merger.rejected().empty());
}

void test_indistinguishable_and_quotient_closure() {
    TinyCorpus corpus;
    corpus.add("x alpha y");
    corpus.add("x beta y");
    const auto data = observe_sentences(corpus.sentences, corpus.text, 2, 3);
    ConservativeMerger merger(data);
    merger.run();
    CHECK(merger.same_class(object(data, "alpha"), object(data, "beta")));
    CHECK(merger.same_class(object(data, "x alpha"), object(data, "x beta")));
    CHECK(merger.same_class(object(data, "alpha y"), object(data, "beta y")));
    CHECK(merger.metrics().induced_unions > 0);
}

void test_more_data_unlocks_a_candidate() {
    TinyCorpus corpus;
    corpus.add("frame rare1 end");
    corpus.add("frame rare2 end");
    const auto early_data = observe_sentences(corpus.sentences, corpus.text, 1, 3);
    ConservativeMerger early(early_data);
    early.run();
    CHECK(early_data.object_text.end() ==
          std::find(early_data.object_text.begin(), early_data.object_text.end(), "rare2"));
    const auto full_data = observe_sentences(corpus.sentences, corpus.text, 2, 3);
    ConservativeMerger full(full_data);
    full.run();
    CHECK(full.same_class(object(full_data, "rare1"), object(full_data, "rare2")));
}

void test_oracle_driver() {
    const auto path = std::filesystem::temp_directory_path() / "scf_v23_oracle_test";
    std::filesystem::remove_all(path);
    const auto report = run_conservative_oracle_sanity(path);
    CHECK(report.find("false friends p/q rejected: 1") != std::string::npos);
    CHECK(std::filesystem::exists(path / "oracle_sanity.txt"));
    std::filesystem::remove_all(path);
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, void (*)()>> tests{
        {"exact_sentence_context_and_deduplication",
         test_exact_sentence_context_and_deduplication},
        {"local_witness_is_not_automatic_union", test_local_witness_is_not_automatic_union},
        {"indistinguishable_and_quotient_closure",
         test_indistinguishable_and_quotient_closure},
        {"more_data_unlocks_a_candidate", test_more_data_unlocks_a_candidate},
        {"oracle_driver", test_oracle_driver},
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
