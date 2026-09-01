// Regression tests for the SCF v2.1 real-corpus scaling module.
//
// Everything here is offline and tiny: synthetic corpora are generated
// deterministically in the test, and the pipeline's exact-context /
// shared-context definitions are verified against a naive string-based
// reimplementation that shares no code with the production path except the
// tokenizer.

#include "scf/real_scaling.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

#define CHECK(condition)                                                                            \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            throw TestFailure(std::string("CHECK failed: ") + #condition + " at " + __FILE__ + ":" + \
                              std::to_string(__LINE__));                                             \
        }                                                                                           \
    } while (false)

using namespace scf::v21;

std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> out;
    tokenize_line(text, [&](const std::string_view token) { out.emplace_back(token); });
    return out;
}

const std::filesystem::path kTmp =
    std::filesystem::temp_directory_path() / "scf_v21_tests";

void write_file(const std::filesystem::path& path, const std::vector<std::string>& lines) {
    std::ofstream out(path);
    for (const auto& line : lines) {
        out << line << "\n";
    }
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

// Deterministic synthetic English-like corpus.
std::vector<std::string> synthetic_corpus(std::size_t docs) {
    const std::vector<std::string> nouns{"dog", "cat", "bird", "house", "tree"};
    const std::vector<std::string> verbs{"sees", "likes", "finds"};
    const std::vector<std::string> adjs{"big", "red"};
    std::vector<std::string> lines;
    std::uint64_t state = 42;
    const auto next = [&state](const std::size_t bound) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::size_t>((state >> 33U) % bound);
    };
    for (std::size_t d = 0; d < docs; ++d) {
        std::ostringstream line;
        const std::size_t sentences = 1 + next(3);
        for (std::size_t s = 0; s < sentences; ++s) {
            line << "the ";
            if (next(3) == 0) {
                line << adjs[next(adjs.size())] << " ";
            }
            line << nouns[next(nouns.size())] << " " << verbs[next(verbs.size())] << " the ";
            if (next(4) == 0) {
                line << adjs[next(adjs.size())] << " ";
            }
            line << nouns[next(nouns.size())] << " . ";
        }
        lines.push_back(line.str());
    }
    return lines;
}

// ---------------------------------------------------------------------------
// Naive string-based reference model
// ---------------------------------------------------------------------------

struct NaiveScale {
    std::map<std::string, std::uint64_t> substring_counts;   // frequent only
    std::map<std::string, std::set<std::pair<std::string, std::string>>> contexts;
    std::map<std::pair<std::string, std::string>, std::set<std::string>> by_context;
    std::map<std::pair<std::string, std::string>, std::uint32_t> pair_counts;  // sorted texts
    std::uint64_t records{};
    std::uint64_t singleton_contexts{};
    std::uint64_t hub_contexts{};
    std::uint64_t pair_emissions{};
};

std::vector<std::string> naive_stream(const std::vector<std::string>& lines) {
    std::vector<std::string> stream;
    for (const auto& line : lines) {
        stream.push_back("<doc>");
        for (const auto& token : tokenize(line)) {
            stream.push_back(token);
        }
    }
    stream.push_back("<doc>");
    return stream;
}

std::size_t naive_prefix_end(const std::vector<std::string>& stream, std::uint64_t real_tokens) {
    std::uint64_t real = 0;
    for (std::size_t pos = 0; pos < stream.size(); ++pos) {
        if (stream[pos] != "<doc>") {
            if (++real == real_tokens) {
                return pos + 1;
            }
        }
    }
    throw TestFailure("stream shorter than requested prefix");
}

NaiveScale naive_scale(const std::vector<std::string>& stream,
                       const std::size_t positions,
                       const std::uint64_t min_count,
                       const std::uint32_t hub_cap) {
    NaiveScale model;
    std::map<std::string, std::uint64_t> raw_counts;
    for (std::size_t i = 0; i < positions; ++i) {
        for (std::size_t len = 1; len <= 3 && i + len <= positions; ++len) {
            bool valid = true;
            std::string text;
            for (std::size_t k = 0; k < len; ++k) {
                if (stream[i + k] == "<doc>") {
                    valid = false;
                    break;
                }
                if (k > 0) {
                    text += " ";
                }
                text += stream[i + k];
            }
            if (valid) {
                ++raw_counts[text];
            }
        }
    }
    for (const auto& [text, count] : raw_counts) {
        if (count >= min_count) {
            model.substring_counts.emplace(text, count);
        }
    }
    for (std::size_t i = 0; i < positions; ++i) {
        for (std::size_t len = 1; len <= 3 && i + len <= positions; ++len) {
            bool valid = true;
            std::string text;
            for (std::size_t k = 0; k < len; ++k) {
                if (stream[i + k] == "<doc>") {
                    valid = false;
                    break;
                }
                if (k > 0) {
                    text += " ";
                }
                text += stream[i + k];
            }
            if (!valid || model.substring_counts.find(text) == model.substring_counts.end()) {
                continue;
            }
            const std::string left = i > 0 ? stream[i - 1] : "<doc>";
            const std::string right = i + len < positions ? stream[i + len] : "<doc>";
            model.contexts[text].insert({left, right});
            model.by_context[{left, right}].insert(text);
        }
    }
    for (const auto& [text, ctxs] : model.contexts) {
        model.records += ctxs.size();
    }
    for (const auto& [ctx, subs] : model.by_context) {
        const std::uint64_t degree = subs.size();
        if (degree == 1) {
            ++model.singleton_contexts;
        }
        if (degree > hub_cap) {
            ++model.hub_contexts;
        } else if (degree >= 2) {
            model.pair_emissions += degree * (degree - 1) / 2;
            for (auto a = subs.begin(); a != subs.end(); ++a) {
                for (auto b = std::next(a); b != subs.end(); ++b) {
                    ++model.pair_counts[{std::min(*a, *b), std::max(*a, *b)}];
                }
            }
        }
    }
    return model;
}

std::map<std::pair<std::string, std::string>, std::uint32_t> read_pair_dump(
    const std::filesystem::path& path) {
    std::map<std::pair<std::string, std::string>, std::uint32_t> pairs;
    std::ifstream input(path);
    CHECK(static_cast<bool>(input));
    std::string line;
    while (std::getline(input, line)) {
        const auto tab1 = line.find('\t');
        const auto tab2 = line.find('\t', tab1 + 1);
        CHECK(tab1 != std::string::npos && tab2 != std::string::npos);
        std::string a = line.substr(0, tab1);
        std::string b = line.substr(tab1 + 1, tab2 - tab1 - 1);
        if (b < a) {
            std::swap(a, b);
        }
        pairs[{a, b}] = static_cast<std::uint32_t>(std::stoul(line.substr(tab2 + 1)));
    }
    return pairs;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void test_tokenizer() {
    CHECK(tokenize("Don't STOP") == (std::vector<std::string>{"don't", "stop"}));
    CHECK(tokenize("3.5 miles") == (std::vector<std::string>{"<num>", ".", "<num>", "miles"}));
    CHECK(tokenize("'''Anarchism''' is") == (std::vector<std::string>{"anarchism", "is"}));
    CHECK(tokenize("the cat's toy , here") ==
          (std::vector<std::string>{"the", "cat's", "toy", ",", "here"}));
    CHECK(tokenize("a-b") == (std::vector<std::string>{"a", "-", "b"}));
    CHECK(tokenize("cats' tails") == (std::vector<std::string>{"cats", "'", "tails"}));
    CHECK(tokenize("in 1984, x=2") ==
          (std::vector<std::string>{"in", "<num>", ",", "x", "=", "<num>"}));
    CHECK(tokenize("  spaced\tout  ") == (std::vector<std::string>{"spaced", "out"}));
}

void test_corpus_builder() {
    std::filesystem::create_directories(kTmp);
    const auto path = kTmp / "small.txt";
    write_file(path, {"The dog. The dog runs", "A cat"});
    const auto corpus = build_token_corpus(path, 0);
    CHECK(corpus.documents == 2);
    CHECK(corpus.real_tokens == 8);
    CHECK(corpus.token_text[0] == "<doc>");
    // first-occurrence interning: the=1 dog=2 .=3 runs=4 a=5 cat=6
    CHECK(corpus.token_text[1] == "the");
    CHECK(corpus.token_text[2] == "dog");
    CHECK((corpus.stream ==
           std::vector<std::uint32_t>{0, 1, 2, 3, 1, 2, 4, 0, 5, 6, 0}));

    const auto limited = build_token_corpus(path, 4);
    CHECK(limited.real_tokens >= 4);
    CHECK(limited.documents == 1);  // stops at the line boundary
}

RealScalingConfig base_config(const std::filesystem::path& input,
                              const std::filesystem::path& output) {
    RealScalingConfig config;
    config.input_text = input;
    config.output_dir = output;
    config.scales = {300, 600};
    config.heldout_tokens = 150;
    config.min_count_rel = 0.0;
    config.min_count_floor = 2;
    config.hub_cap = 100;
    config.pairs_per_bucket = 100000;  // sample everything: naive-comparable
    config.dump_pairs_limit = 1000000;
    return config;
}

void test_naive_reference() {
    std::filesystem::create_directories(kTmp);
    const auto input = kTmp / "synthetic.txt";
    write_file(input, synthetic_corpus(60));
    const auto output = kTmp / "naive_out";
    std::filesystem::remove_all(output);
    auto config = base_config(input, output);
    const auto result = run_real_scaling(config);
    CHECK(result.scales.size() == 2);

    const auto stream = naive_stream(synthetic_corpus(60));
    for (std::size_t index = 0; index < 2; ++index) {
        const auto& metrics = result.scales[index];
        const std::size_t positions = naive_prefix_end(stream, config.scales[index]);
        CHECK(metrics.stream_positions == positions);
        const auto naive =
            naive_scale(stream, positions, config.min_count_floor, config.hub_cap);

        std::uint64_t n1 = 0, n2 = 0, n3 = 0;
        for (const auto& [text, count] : naive.substring_counts) {
            const auto spaces = std::count(text.begin(), text.end(), ' ');
            (spaces == 0 ? n1 : spaces == 1 ? n2 : n3) += 1;
        }
        CHECK(metrics.substrings_len1 == n1);
        CHECK(metrics.substrings_len2 == n2);
        CHECK(metrics.substrings_len3 == n3);
        CHECK(metrics.context_records == naive.records);
        CHECK(metrics.distinct_contexts == naive.by_context.size());
        CHECK(metrics.singleton_contexts == naive.singleton_contexts);
        CHECK(metrics.hub_contexts == naive.hub_contexts);
        CHECK(metrics.pair_emissions == naive.pair_emissions);
        CHECK(metrics.distinct_pairs == naive.pair_counts.size());

        const auto dumped = read_pair_dump(
            output / ("pair_dump_" + std::to_string(config.scales[index]) + ".txt"));
        CHECK(dumped == naive.pair_counts);

        for (const auto& threshold : metrics.thresholds) {
            std::uint64_t expected = 0;
            for (const auto& [pair, count] : naive.pair_counts) {
                if (count >= threshold.m) {
                    ++expected;
                }
            }
            CHECK(threshold.pairs == expected);
        }

        // Component structure per threshold via a naive DSU over texts.
        for (const auto& threshold : metrics.thresholds) {
            std::map<std::string, std::string> parent;
            const std::function<std::string(const std::string&)> find =
                [&](const std::string& x) -> std::string {
                auto it = parent.find(x);
                if (it == parent.end() || it->second == x) {
                    return x;
                }
                const auto root = find(it->second);
                parent[x] = root;
                return root;
            };
            std::set<std::string> nodes;
            for (const auto& [pair, count] : naive.pair_counts) {
                if (count < threshold.m) {
                    continue;
                }
                nodes.insert(pair.first);
                nodes.insert(pair.second);
                const auto ra = find(pair.first);
                const auto rb = find(pair.second);
                if (ra != rb) {
                    parent[ra] = rb;
                }
            }
            std::map<std::string, std::uint64_t> sizes;
            for (const auto& node : nodes) {
                ++sizes[find(node)];
            }
            std::uint64_t largest = 0;
            for (const auto& [root, size] : sizes) {
                largest = std::max(largest, size);
            }
            CHECK(threshold.nodes == nodes.size());
            CHECK(threshold.components == sizes.size());
            CHECK(threshold.largest_component == largest);
        }
    }

    // Transition metrics between the two scales, naive version.
    {
        const auto& metrics = result.scales[1];
        const std::size_t small_positions = naive_prefix_end(stream, config.scales[0]);
        const std::size_t large_positions = naive_prefix_end(stream, config.scales[1]);
        const auto small = naive_scale(stream, small_positions, config.min_count_floor,
                                       config.hub_cap);
        const auto large = naive_scale(stream, large_positions, config.min_count_floor,
                                       config.hub_cap);
        std::uint64_t common = 0, lost = 0, untranslatable = 0;
        for (const auto& [pair, count] : small.pair_counts) {
            const bool a_ok =
                large.substring_counts.find(pair.first) != large.substring_counts.end();
            const bool b_ok =
                large.substring_counts.find(pair.second) != large.substring_counts.end();
            if (!a_ok || !b_ok) {
                ++untranslatable;
            } else if (large.pair_counts.find(pair) != large.pair_counts.end()) {
                ++common;
            } else {
                ++lost;
            }
        }
        CHECK(metrics.common_pairs == common);
        CHECK(metrics.lost_pairs == lost);
        CHECK(metrics.untranslatable_pairs == untranslatable);
        CHECK(metrics.new_pairs == large.pair_counts.size() - common);
    }
}

void test_heldout_replication_naive() {
    std::filesystem::create_directories(kTmp);
    const auto input = kTmp / "synthetic.txt";
    write_file(input, synthetic_corpus(60));
    const auto output = kTmp / "heldout_out";
    std::filesystem::remove_all(output);
    auto config = base_config(input, output);
    const auto result = run_real_scaling(config);

    const auto stream = naive_stream(synthetic_corpus(60));
    const std::size_t train_end = naive_prefix_end(stream, config.scales.back());
    std::size_t heldout_begin = train_end;
    while (heldout_begin < stream.size() && stream[heldout_begin] != "<doc>") {
        ++heldout_begin;
    }
    std::size_t heldout_end = heldout_begin;
    std::uint64_t real = 0;
    while (heldout_end < stream.size() && real < config.heldout_tokens) {
        if (stream[heldout_end] != "<doc>") {
            ++real;
        }
        ++heldout_end;
    }
    CHECK(result.heldout_tokens_used == real);

    // Naive: recompute every scale's held-out bucket stats over ALL train
    // pairs (pairs_per_bucket is large enough to sample everything).
    for (std::size_t index = 0; index < 2; ++index) {
        const std::size_t positions = naive_prefix_end(stream, config.scales[index]);
        const auto train =
            naive_scale(stream, positions, config.min_count_floor, config.hub_cap);
        // Held-out contexts of train-inventory substrings.
        std::map<std::string, std::set<std::pair<std::string, std::string>>> hctx;
        for (std::size_t i = heldout_begin; i < heldout_end; ++i) {
            for (std::size_t len = 1; len <= 3 && i + len <= heldout_end; ++len) {
                bool valid = true;
                std::string text;
                for (std::size_t k = 0; k < len; ++k) {
                    if (stream[i + k] == "<doc>") {
                        valid = false;
                        break;
                    }
                    if (k > 0) {
                        text += " ";
                    }
                    text += stream[i + k];
                }
                if (!valid ||
                    train.substring_counts.find(text) == train.substring_counts.end()) {
                    continue;
                }
                const std::string left = i > 0 ? stream[i - 1] : "<doc>";
                const std::string right =
                    i + len < heldout_end ? stream[i + len] : "<doc>";
                hctx[text].insert({left, right});
            }
        }
        std::map<std::string, std::vector<std::uint64_t>> bucket_values;
        const auto bucket_of = [](const std::uint32_t count) -> std::string {
            return count <= 1 ? "1" : count <= 3 ? "2-3" : count <= 7 ? "4-7"
                   : count <= 15                 ? "8-15"
                                                 : "16+";
        };
        for (const auto& [pair, count] : train.pair_counts) {
            const auto& ca = hctx[pair.first];
            const auto& cb = hctx[pair.second];
            std::uint64_t shared = 0;
            for (const auto& ctx : ca) {
                shared += cb.count(ctx);
            }
            bucket_values[bucket_of(count)].push_back(shared);
        }
        for (const auto& row : result.heldout) {
            if (row.scale_tokens != config.scales[index]) {
                continue;
            }
            const auto& values = bucket_values[row.bucket];
            CHECK(row.sampled_pairs == values.size());
            std::uint64_t replicated = 0;
            std::uint64_t total = 0;
            for (const auto value : values) {
                total += value;
                if (value > 0) {
                    ++replicated;
                }
            }
            CHECK(row.replicated_pairs == replicated);
            CHECK(std::abs(row.mean_heldout_shared -
                           static_cast<double>(total) / static_cast<double>(values.size())) <
                  1e-9);
        }
    }
}

void test_hub_cap() {
    std::filesystem::create_directories(kTmp);
    const auto input = kTmp / "hub.txt";
    // Context (a, b) hosts 6 distinct frequent middles; every line repeated so
    // everything passes min_count = 2.
    std::vector<std::string> lines;
    for (int repeat = 0; repeat < 2; ++repeat) {
        for (const char* middle : {"u", "v", "w", "x", "y", "z"}) {
            lines.push_back(std::string("a ") + middle + " b");
        }
    }
    write_file(input, lines);

    auto config = base_config(input, kTmp / "hub_big");
    config.scales = {36};
    config.heldout_tokens = 1;
    std::filesystem::remove_all(config.output_dir);
    const auto big = run_real_scaling(config);
    CHECK(big.scales[0].hub_contexts == 0);
    // Four degree-6 contexts — (a,b) middles, (<doc>,b) "a X" bigrams,
    // (a,<doc>) "X b" bigrams, (<doc>,<doc>) trigrams — 15 pairs each.
    CHECK(big.scales[0].thresholds[0].pairs == 60);

    config.hub_cap = 4;  // every degree-6 context becomes a hub
    config.output_dir = kTmp / "hub_small";
    std::filesystem::remove_all(config.output_dir);
    const auto small = run_real_scaling(config);
    CHECK(small.scales[0].hub_contexts == 4);
    CHECK(small.scales[0].thresholds[0].pairs == 0);
}

void test_pos_diagnostic() {
    std::filesystem::create_directories(kTmp);
    const auto input = kTmp / "synthetic.txt";
    write_file(input, synthetic_corpus(60));
    const auto ud = kTmp / "mini.conllu";
    {
        std::ofstream out(ud);
        const std::vector<std::pair<std::string, std::string>> entries{
            {"dog", "NOUN"}, {"cat", "NOUN"}, {"bird", "NOUN"}, {"house", "NOUN"},
            {"tree", "NOUN"}, {"sees", "VERB"}, {"likes", "VERB"}, {"finds", "VERB"},
            {"big", "ADJ"},  {"red", "ADJ"},  {"the", "DET"}};
        int id = 1;
        for (const auto& [form, upos] : entries) {
            out << id++ << "\t" << form << "\t" << form << "\t" << upos
                << "\t_\t_\t0\t_\t_\t_\n";
        }
    }
    const auto output = kTmp / "pos_out";
    std::filesystem::remove_all(output);
    auto config = base_config(input, output);
    config.ud_conllu = ud;
    const auto result = run_real_scaling(config);
    const auto& metrics = result.scales.back();
    CHECK(metrics.same_pos_baseline > 0.0 && metrics.same_pos_baseline < 1.0);
    bool saw_rate = false;
    for (const auto& threshold : metrics.thresholds) {
        if (threshold.labeled_pairs > 0) {
            saw_rate = true;
            CHECK(threshold.same_pos_rate >= 0.0 && threshold.same_pos_rate <= 1.0);
            CHECK(threshold.same_pos_pairs <= threshold.labeled_pairs);
        }
    }
    CHECK(saw_rate);
}

void test_determinism() {
    std::filesystem::create_directories(kTmp);
    const auto input = kTmp / "synthetic.txt";
    write_file(input, synthetic_corpus(60));
    const auto out1 = kTmp / "det1";
    const auto out2 = kTmp / "det2";
    std::filesystem::remove_all(out1);
    std::filesystem::remove_all(out2);
    auto config = base_config(input, out1);
    config.pairs_per_bucket = 5;  // exercise real hash-order sampling
    run_real_scaling(config);
    config.output_dir = out2;
    run_real_scaling(config);
    // Strip wall-time and RSS measurements before comparing: they are the
    // only legitimately non-deterministic output fields.
    const auto strip_timing = [](std::string text, const bool csv) {
        std::istringstream input(text);
        std::ostringstream output;
        std::string line;
        while (std::getline(input, line)) {
            if (csv) {
                // drop the last two columns (runtime_seconds, peak_rss_mb)
                auto cut = line.rfind(',');
                cut = cut == std::string::npos ? cut : line.rfind(',', cut - 1);
                output << line.substr(0, cut) << "\n";
            } else if (line.rfind("# total wall time", 0) != 0) {
                output << line << "\n";
            }
        }
        return output.str();
    };
    CHECK(strip_timing(read_file(out1 / "scaling_metrics.csv"), true) ==
          strip_timing(read_file(out2 / "scaling_metrics.csv"), true));
    CHECK(read_file(out1 / "pair_evidence_scaling.csv") ==
          read_file(out2 / "pair_evidence_scaling.csv"));
    CHECK(read_file(out1 / "heldout_replication.csv") ==
          read_file(out2 / "heldout_replication.csv"));
    CHECK(strip_timing(read_file(out1 / "neighborhood_samples.txt"), false) ==
          strip_timing(read_file(out2 / "neighborhood_samples.txt"), false));
}

void test_mix64_pinned() {
    CHECK(mix64(0) == 0xe220a8397b1dcdafULL);
    CHECK(mix64(1) == 0x910a2dec89025cc1ULL);
    CHECK(mix64(0xdeadbeefULL) != mix64(0xdeadbeeeULL));
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, void (*)()>> tests{
        {"tokenizer", test_tokenizer},
        {"corpus_builder", test_corpus_builder},
        {"naive_reference", test_naive_reference},
        {"heldout_replication_naive", test_heldout_replication_naive},
        {"hub_cap", test_hub_cap},
        {"pos_diagnostic", test_pos_diagnostic},
        {"determinism", test_determinism},
        {"mix64_pinned", test_mix64_pinned},
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
    std::filesystem::remove_all(kTmp);
    std::cout << tests.size() - failures << '/' << tests.size() << " tests passed\n";
    return failures == 0 ? 0 : 1;
}
