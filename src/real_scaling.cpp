#include "scf/real_scaling.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include <sys/resource.h>

namespace scf::v21 {

namespace {

constexpr std::uint32_t kNoDense = 0xffffffffU;
constexpr std::uint64_t kDenseBits = 21;  // frequent tokens per scale < 2^21
constexpr std::uint64_t kDenseMask = (1ULL << kDenseBits) - 1;

double peak_rss_mb() {
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return static_cast<double>(usage.ru_maxrss) / 1024.0;  // linux: KiB
}

std::string format_double(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6f", value);
    return buffer;
}

// Open-addressing map u64 -> u32 with deterministic content; iteration is
// only ever consumed after extraction + sorting.
class HashCounter {
public:
    explicit HashCounter(std::size_t initial_capacity = 1 << 16) {
        std::size_t cap = 64;
        while (cap < initial_capacity * 2) {
            cap <<= 1U;
        }
        keys_.assign(cap, kEmpty);
        values_.assign(cap, 0);
        mask_ = cap - 1;
    }

    void add(std::uint64_t key, std::uint32_t delta) {
        std::size_t slot = probe(key);
        if (keys_[slot] == kEmpty) {
            keys_[slot] = key;
            values_[slot] = delta;
            if (++size_ * 10 >= (mask_ + 1) * 6) {
                grow();
            }
        } else {
            values_[slot] += delta;
        }
    }

    void set(std::uint64_t key, std::uint32_t value) {
        std::size_t slot = probe(key);
        if (keys_[slot] == kEmpty) {
            keys_[slot] = key;
            if (++size_ * 10 >= (mask_ + 1) * 6) {
                grow();
                slot = probe(key);
            }
        }
        values_[slot] = value;
    }

    // returns 0 for absent keys (counts are always >= 1 when present)
    std::uint32_t get(std::uint64_t key) const {
        const std::size_t slot = probe(key);
        return keys_[slot] == kEmpty ? 0 : values_[slot];
    }

    bool contains(std::uint64_t key) const { return keys_[probe(key)] != kEmpty; }

    std::size_t size() const { return size_; }

    template <typename Fn>
    void for_each(Fn&& fn) const {
        for (std::size_t i = 0; i <= mask_; ++i) {
            if (keys_[i] != kEmpty) {
                fn(keys_[i], values_[i]);
            }
        }
    }

    void clear_and_free() {
        std::vector<std::uint64_t>().swap(keys_);
        std::vector<std::uint32_t>().swap(values_);
        mask_ = 0;
        size_ = 0;
    }

private:
    static constexpr std::uint64_t kEmpty = 0xffffffffffffffffULL;

    std::size_t probe(std::uint64_t key) const {
        std::size_t slot = static_cast<std::size_t>(mix(key)) & mask_;
        while (keys_[slot] != kEmpty && keys_[slot] != key) {
            slot = (slot + 1) & mask_;
        }
        return slot;
    }

    static std::uint64_t mix(std::uint64_t x) {
        x ^= x >> 33U;
        x *= 0xff51afd7ed558ccdULL;
        x ^= x >> 33U;
        x *= 0xc4ceb9fe1a85ec53ULL;
        x ^= x >> 33U;
        return x;
    }

    void grow() {
        std::vector<std::uint64_t> old_keys = std::move(keys_);
        std::vector<std::uint32_t> old_values = std::move(values_);
        const std::size_t cap = (mask_ + 1) * 2;
        keys_.assign(cap, kEmpty);
        values_.assign(cap, 0);
        mask_ = cap - 1;
        for (std::size_t i = 0; i < old_keys.size(); ++i) {
            if (old_keys[i] != kEmpty) {
                std::size_t slot = probe(old_keys[i]);
                keys_[slot] = old_keys[i];
                values_[slot] = old_values[i];
            }
        }
    }

    std::vector<std::uint64_t> keys_;
    std::vector<std::uint32_t> values_;
    std::size_t mask_{};
    std::size_t size_{};
};

struct CtxRec {
    std::uint32_t left{};
    std::uint32_t right{};
    std::uint32_t sub{};

    bool operator<(const CtxRec& other) const {
        if (left != other.left) {
            return left < other.left;
        }
        if (right != other.right) {
            return right < other.right;
        }
        return sub < other.sub;
    }
    bool operator==(const CtxRec&) const = default;
};

// Per-scale substring inventory over dense frequent-token ids.
struct Registry {
    std::vector<std::uint32_t> dense;           // global token id -> f (or kNoDense)
    std::vector<std::uint32_t> dense_to_token;  // f -> global token id
    std::vector<std::uint64_t> bigram_keys;     // sorted (f0<<21 | f1)
    std::vector<std::uint64_t> trigram_keys;    // sorted (f0<<42 | f1<<21 | f2)
    HashCounter bigram_id{1 << 10};             // key -> u_id
    HashCounter trigram_id{1 << 10};
    std::uint64_t n1{}, n2{}, n3{};

    std::uint64_t total() const { return n1 + n2 + n3; }

    std::size_t len_of(std::uint64_t u) const { return u < n1 ? 1 : (u < n1 + n2 ? 2 : 3); }

    std::array<std::uint32_t, 3> tokens_of(std::uint64_t u) const {
        if (u < n1) {
            return {dense_to_token[u], 0, 0};
        }
        if (u < n1 + n2) {
            const std::uint64_t key = bigram_keys[u - n1];
            return {dense_to_token[(key >> kDenseBits) & kDenseMask],
                    dense_to_token[key & kDenseMask], 0};
        }
        const std::uint64_t key = trigram_keys[u - n1 - n2];
        return {dense_to_token[(key >> (2 * kDenseBits)) & kDenseMask],
                dense_to_token[(key >> kDenseBits) & kDenseMask],
                dense_to_token[key & kDenseMask]};
    }

    std::string text_of(std::uint64_t u, const std::vector<std::string>& token_text) const {
        const auto tokens = tokens_of(u);
        std::string out = token_text[tokens[0]];
        for (std::size_t i = 1; i < len_of(u); ++i) {
            out += " ";
            out += token_text[tokens[i]];
        }
        return out;
    }

    // -1 when the token sequence is not in this scale's inventory.
    std::int64_t lookup(const std::array<std::uint32_t, 3>& tokens, std::size_t len) const {
        std::array<std::uint64_t, 3> f{};
        for (std::size_t i = 0; i < len; ++i) {
            if (tokens[i] >= dense.size() || dense[tokens[i]] == kNoDense) {
                return -1;
            }
            f[i] = dense[tokens[i]];
        }
        if (len == 1) {
            return static_cast<std::int64_t>(f[0]);
        }
        if (len == 2) {
            const std::uint64_t key = (f[0] << kDenseBits) | f[1];
            return bigram_id.contains(key) ? static_cast<std::int64_t>(bigram_id.get(key)) : -1;
        }
        const std::uint64_t key = (f[0] << (2 * kDenseBits)) | (f[1] << kDenseBits) | f[2];
        return trigram_id.contains(key) ? static_cast<std::int64_t>(trigram_id.get(key)) : -1;
    }
};

struct Dsu {
    std::vector<std::uint32_t> parent;
    std::vector<std::uint32_t> size;

    explicit Dsu(std::size_t n) : parent(n), size(n, 1) {
        std::iota(parent.begin(), parent.end(), 0U);
    }
    std::uint32_t find(std::uint32_t x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    }
    void unite(std::uint32_t a, std::uint32_t b) {
        a = find(a);
        b = find(b);
        if (a == b) {
            return;
        }
        if (size[a] < size[b]) {
            std::swap(a, b);
        }
        parent[b] = a;
        size[a] += size[b];
    }
};

const std::array<std::string, 5> kBucketOrder{"1", "2-3", "4-7", "8-15", "16+"};

}  // namespace

std::uint64_t mix64(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

// ---------------------------------------------------------------------------
// Tokenizer
// ---------------------------------------------------------------------------

void tokenize_line(const std::string_view line,
                   const std::function<void(std::string_view)>& emit) {
    static constexpr std::string_view kNum = "<num>";
    std::string word;
    const auto flush = [&]() {
        if (!word.empty()) {
            emit(word);
            word.clear();
        }
    };
    const std::size_t n = line.size();
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned char c = static_cast<unsigned char>(line[i]);
        if (c >= 0x80) {
            word.push_back(static_cast<char>(c));
            continue;
        }
        if (std::isalpha(c) != 0) {
            word.push_back(static_cast<char>(std::tolower(c)));
            continue;
        }
        if (c == '\'') {
            // 2+ apostrophes: wiki bold/italic markup, acts as a separator.
            if (i + 1 < n && line[i + 1] == '\'') {
                flush();
                while (i + 1 < n && line[i + 1] == '\'') {
                    ++i;
                }
                continue;
            }
            // single apostrophe inside a word stays ("don't")
            if (!word.empty() && i + 1 < n &&
                (std::isalpha(static_cast<unsigned char>(line[i + 1])) != 0 ||
                 static_cast<unsigned char>(line[i + 1]) >= 0x80)) {
                word.push_back('\'');
                continue;
            }
            flush();
            emit(std::string_view(&line[i], 1));
            continue;
        }
        if (std::isdigit(c) != 0) {
            flush();
            while (i + 1 < n && std::isdigit(static_cast<unsigned char>(line[i + 1])) != 0) {
                ++i;
            }
            emit(kNum);
            continue;
        }
        flush();
        if (std::isspace(c) == 0) {
            emit(std::string_view(&line[i], 1));
        }
    }
    flush();
}

TokenCorpus build_token_corpus(const std::filesystem::path& input_text,
                               const std::uint64_t real_token_limit) {
    std::ifstream input(input_text);
    if (!input) {
        throw std::runtime_error("cannot open corpus text: " + input_text.string());
    }
    TokenCorpus corpus;
    corpus.token_text.push_back("<doc>");
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
    vocab.reserve(1 << 20);
    std::string line;
    while (std::getline(input, line)) {
        corpus.stream.push_back(kDocSentinel);
        ++corpus.documents;
        tokenize_line(line, [&](const std::string_view token) {
            const auto found = vocab.find(token);
            std::uint32_t id = 0;
            if (found != vocab.end()) {
                id = found->second;
            } else {
                id = static_cast<std::uint32_t>(corpus.token_text.size());
                corpus.token_text.emplace_back(token);
                vocab.emplace(std::string(token), id);
            }
            corpus.stream.push_back(id);
            ++corpus.real_tokens;
        });
        if (real_token_limit != 0 && corpus.real_tokens >= real_token_limit) {
            break;
        }
    }
    corpus.stream.push_back(kDocSentinel);
    return corpus;
}

std::vector<std::string> default_probe_words() {
    return {"the",   "of",    "and",   "in",   "was",        "is",     "he",    "she",
            "city",  "war",   "world", "people", "government", "university", "music", "house",
            "red",   "small", "large", "first", "new",        "said",   "made",  "found"};
}

std::vector<std::string> default_probe_bigrams() {
    return {"of the", "in the", "united states", "new york"};
}

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

namespace {

struct ProbeState {
    std::string text;                       // surface form, tokens joined
    std::vector<std::string> previous_top;  // neighbor texts at previous scale
};

struct PrevScaleState {
    std::uint64_t scale_tokens{};
    // registry snapshot: per u_id its global token sequence
    std::vector<std::array<std::uint32_t, 3>> tokens;
    std::vector<std::uint8_t> lengths;
    std::filesystem::path pair_file;  // sorted (u64 key, u32 count) records
};

struct SampledPair {
    std::uint64_t key{};
    std::uint32_t train_count{};
    std::uint8_t bucket{};
};

std::uint64_t pack_pair(std::uint64_t a, std::uint64_t b) { return (a << 32U) | b; }

}  // namespace

RealScalingResult run_real_scaling(const RealScalingConfig& config) {
    const auto ladder_start = std::chrono::steady_clock::now();
    std::filesystem::create_directories(config.output_dir);

    // ---- corpus ----------------------------------------------------------
    auto scales = config.scales;
    std::sort(scales.begin(), scales.end());
    const std::uint64_t want_tokens =
        scales.empty() ? 0 : scales.back() + config.heldout_tokens + 1'000'000;
    TokenCorpus corpus = build_token_corpus(config.input_text, want_tokens);

    RealScalingResult result;
    result.corpus_real_tokens = corpus.real_tokens;
    result.corpus_documents = corpus.documents;
    result.vocab_size = corpus.token_text.size();

    // Clamp scales the corpus cannot fill.
    while (!scales.empty() && scales.back() > corpus.real_tokens) {
        scales.pop_back();
    }
    if (scales.empty()) {
        throw std::runtime_error("corpus smaller than the smallest requested scale");
    }

    // Scale boundaries in stream positions, and the held-out span (starts at
    // the first document boundary after the largest train prefix).
    std::vector<std::size_t> scale_end(scales.size(), 0);
    std::size_t heldout_begin = corpus.stream.size();
    {
        std::uint64_t real = 0;
        std::size_t next = 0;
        for (std::size_t pos = 0; pos < corpus.stream.size() && next < scales.size(); ++pos) {
            if (corpus.stream[pos] != kDocSentinel) {
                ++real;
                if (real == scales[next]) {
                    scale_end[next] = pos + 1;
                    ++next;
                }
            }
        }
        for (std::size_t pos = scale_end.back(); pos < corpus.stream.size(); ++pos) {
            if (corpus.stream[pos] == kDocSentinel) {
                heldout_begin = pos;
                break;
            }
        }
    }
    std::size_t heldout_end = heldout_begin;
    {
        std::uint64_t real = 0;
        while (heldout_end < corpus.stream.size() && real < config.heldout_tokens) {
            if (corpus.stream[heldout_end] != kDocSentinel) {
                ++real;
            }
            ++heldout_end;
        }
        result.heldout_tokens_used = real;
    }

    // ---- optional POS labels (evaluation only) ---------------------------
    std::unordered_map<std::string, std::uint8_t> pos_of_token;
    std::vector<std::string> pos_names;
    if (!config.ud_conllu.empty()) {
        std::ifstream ud(config.ud_conllu);
        if (!ud) {
            throw std::runtime_error("cannot open UD file: " + config.ud_conllu.string());
        }
        std::unordered_map<std::string, std::map<std::string, std::uint32_t>> votes;
        std::string line;
        while (std::getline(ud, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }
            std::istringstream fields(line);
            std::string id, form, lemma, upos;
            std::getline(fields, id, '\t');
            std::getline(fields, form, '\t');
            std::getline(fields, lemma, '\t');
            std::getline(fields, upos, '\t');
            if (id.find('-') != std::string::npos || id.find('.') != std::string::npos ||
                upos.empty() || upos == "_") {
                continue;
            }
            std::string lower;
            for (const char c : form) {
                lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            ++votes[lower][upos];
        }
        std::map<std::string, std::uint8_t> name_ids;
        for (const auto& [token, dist] : votes) {
            const auto best = std::max_element(
                dist.begin(), dist.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; });
            const auto inserted =
                name_ids.try_emplace(best->first, static_cast<std::uint8_t>(name_ids.size()));
            pos_of_token[token] = inserted.first->second;
        }
        pos_names.resize(name_ids.size());
        for (const auto& [name, id] : name_ids) {
            pos_names[id] = name;
        }
    }

    // ---- probes ----------------------------------------------------------
    const auto probe_words =
        config.probe_words.empty() ? default_probe_words() : config.probe_words;
    const auto probe_bigrams =
        config.probe_bigrams.empty() ? default_probe_bigrams() : config.probe_bigrams;
    std::vector<ProbeState> probes;
    for (const auto& word : probe_words) {
        probes.push_back({word, {}});
    }
    for (const auto& bigram : probe_bigrams) {
        probes.push_back({bigram, {}});
    }
    std::unordered_map<std::string, std::uint32_t> token_id_of;
    for (std::uint32_t id = 0; id < corpus.token_text.size(); ++id) {
        token_id_of.emplace(corpus.token_text[id], id);
    }

    std::ofstream neighborhoods(config.output_dir / "neighborhood_samples.txt");
    neighborhoods << "# SCF v2.1 substitution neighborhoods (top " << config.top_neighbors
                  << " partners by |I_N(u,v)|, ties by inventory order)\n";

    PrevScaleState previous;

    // ---- ladder ----------------------------------------------------------
    for (std::size_t scale_index = 0; scale_index < scales.size(); ++scale_index) {
        const auto t_start = std::chrono::steady_clock::now();
        const std::uint64_t N = scales[scale_index];
        const std::size_t positions = scale_end[scale_index];
        const std::uint32_t* stream = corpus.stream.data();

        ScaleMetrics metrics;
        metrics.scale_tokens = N;
        metrics.stream_positions = positions;
        metrics.min_count = std::max<std::uint64_t>(
            config.min_count_floor,
            static_cast<std::uint64_t>(std::ceil(config.min_count_rel * static_cast<double>(N))));
        const std::uint64_t min_count = metrics.min_count;

        // Pass 1: unigram counts.
        std::vector<std::uint64_t> unigram(corpus.token_text.size(), 0);
        for (std::size_t i = 0; i < positions; ++i) {
            ++unigram[stream[i]];
        }
        Registry registry;
        registry.dense.assign(corpus.token_text.size(), kNoDense);
        for (std::uint32_t token = 1; token < corpus.token_text.size(); ++token) {
            if (unigram[token] >= min_count) {
                registry.dense[token] =
                    static_cast<std::uint32_t>(registry.dense_to_token.size());
                registry.dense_to_token.push_back(token);
            }
            if (unigram[token] > 0) {
                ++metrics.vocab_seen;
            }
        }
        if (registry.dense_to_token.size() >= (1ULL << kDenseBits)) {
            throw std::runtime_error("frequent-token inventory exceeds packing budget");
        }
        metrics.frequent_tokens = registry.dense_to_token.size();
        registry.n1 = registry.dense_to_token.size();

        // Pass 2: bigram counts (both parts frequent; no sentinel).
        HashCounter bigram_counts(1 << 20);
        for (std::size_t i = 0; i + 1 < positions; ++i) {
            const std::uint32_t a = registry.dense[stream[i]];
            const std::uint32_t b = registry.dense[stream[i + 1]];
            if (a != kNoDense && b != kNoDense && stream[i] != kDocSentinel &&
                stream[i + 1] != kDocSentinel) {
                bigram_counts.add((static_cast<std::uint64_t>(a) << kDenseBits) | b, 1);
            }
        }
        bigram_counts.for_each([&](const std::uint64_t key, const std::uint32_t count) {
            if (count >= min_count) {
                registry.bigram_keys.push_back(key);
            }
        });
        std::sort(registry.bigram_keys.begin(), registry.bigram_keys.end());
        registry.n2 = registry.bigram_keys.size();
        for (std::size_t i = 0; i < registry.bigram_keys.size(); ++i) {
            registry.bigram_id.set(registry.bigram_keys[i],
                                   static_cast<std::uint32_t>(registry.n1 + i));
        }

        // Pass 3: trigram counts (both sub-bigrams frequent - Apriori).
        HashCounter trigram_counts(1 << 20);
        for (std::size_t i = 0; i + 2 < positions; ++i) {
            const std::uint32_t a = registry.dense[stream[i]];
            const std::uint32_t b = registry.dense[stream[i + 1]];
            const std::uint32_t c = registry.dense[stream[i + 2]];
            if (a == kNoDense || b == kNoDense || c == kNoDense || stream[i] == kDocSentinel ||
                stream[i + 1] == kDocSentinel || stream[i + 2] == kDocSentinel) {
                continue;
            }
            const std::uint64_t left = (static_cast<std::uint64_t>(a) << kDenseBits) | b;
            const std::uint64_t right = (static_cast<std::uint64_t>(b) << kDenseBits) | c;
            if (bigram_counts.get(left) >= min_count && bigram_counts.get(right) >= min_count) {
                trigram_counts.add((left << kDenseBits) | c, 1);
            }
        }
        trigram_counts.for_each([&](const std::uint64_t key, const std::uint32_t count) {
            if (count >= min_count) {
                registry.trigram_keys.push_back(key);
            }
        });
        std::sort(registry.trigram_keys.begin(), registry.trigram_keys.end());
        registry.n3 = registry.trigram_keys.size();
        for (std::size_t i = 0; i < registry.trigram_keys.size(); ++i) {
            registry.trigram_id.set(registry.trigram_keys[i],
                                    static_cast<std::uint32_t>(registry.n1 + registry.n2 + i));
        }
        bigram_counts.clear_and_free();
        trigram_counts.clear_and_free();

        metrics.substrings_len1 = registry.n1;
        metrics.substrings_len2 = registry.n2;
        metrics.substrings_len3 = registry.n3;
        metrics.substrings_total = registry.total();
        const std::uint64_t total_subs = registry.total();

        // Pass 4: context records (l, r, u) with global context token ids.
        std::vector<CtxRec> records;
        for (std::size_t i = 0; i < positions; ++i) {
            if (stream[i] == kDocSentinel || registry.dense[stream[i]] == kNoDense) {
                continue;
            }
            const std::uint32_t left = i > 0 ? stream[i - 1] : kDocSentinel;
            const std::uint32_t f0 = registry.dense[stream[i]];
            // length 1
            {
                const std::uint32_t right = i + 1 < positions ? stream[i + 1] : kDocSentinel;
                records.push_back({left, right, f0});
                ++metrics.occurrence_mentions;
            }
            // length 2
            if (i + 1 < positions && stream[i + 1] != kDocSentinel &&
                registry.dense[stream[i + 1]] != kNoDense) {
                const std::uint64_t key =
                    (static_cast<std::uint64_t>(f0) << kDenseBits) | registry.dense[stream[i + 1]];
                if (registry.bigram_id.contains(key)) {
                    const std::uint32_t right =
                        i + 2 < positions ? stream[i + 2] : kDocSentinel;
                    records.push_back({left, right, registry.bigram_id.get(key)});
                    ++metrics.occurrence_mentions;
                }
                // length 3
                if (i + 2 < positions && stream[i + 2] != kDocSentinel &&
                    registry.dense[stream[i + 2]] != kNoDense) {
                    const std::uint64_t tri =
                        (key << kDenseBits) | registry.dense[stream[i + 2]];
                    if (registry.trigram_id.contains(tri)) {
                        const std::uint32_t right =
                            i + 3 < positions ? stream[i + 3] : kDocSentinel;
                        records.push_back({left, right, registry.trigram_id.get(tri)});
                        ++metrics.occurrence_mentions;
                    }
                }
            }
        }
        std::sort(records.begin(), records.end());
        records.erase(std::unique(records.begin(), records.end()), records.end());
        metrics.context_records = records.size();

        // Pass 5: run scan over exact contexts; degree stats and pair budget.
        std::vector<std::uint32_t> context_count(total_subs, 0);
        std::vector<std::uint32_t> shared_context_count(total_subs, 0);
        std::uint64_t records_in_shared = 0;
        std::uint64_t hub_records = 0;
        std::uint64_t pair_emissions = 0;
        for (std::size_t i = 0; i < records.size();) {
            std::size_t j = i;
            while (j < records.size() && records[j].left == records[i].left &&
                   records[j].right == records[i].right) {
                ++j;
            }
            const std::uint64_t degree = j - i;
            ++metrics.distinct_contexts;
            if (degree == 1) {
                ++metrics.singleton_contexts;
            } else {
                ++metrics.shared_contexts;
                records_in_shared += degree;
                for (std::size_t k = i; k < j; ++k) {
                    ++shared_context_count[records[k].sub];
                }
            }
            if (degree > config.hub_cap) {
                ++metrics.hub_contexts;
                hub_records += degree;
            } else if (degree >= 2) {
                pair_emissions += degree * (degree - 1) / 2;
            }
            for (std::size_t k = i; k < j; ++k) {
                ++context_count[records[k].sub];
            }
            i = j;
        }
        metrics.singleton_context_share =
            metrics.distinct_contexts == 0
                ? 0.0
                : static_cast<double>(metrics.singleton_contexts) /
                      static_cast<double>(metrics.distinct_contexts);
        metrics.records_in_shared_contexts_share =
            records.empty() ? 0.0
                            : static_cast<double>(records_in_shared) /
                                  static_cast<double>(records.size());
        metrics.hub_record_share =
            records.empty() ? 0.0
                            : static_cast<double>(hub_records) /
                                  static_cast<double>(records.size());
        metrics.mean_contexts_per_substring =
            total_subs == 0 ? 0.0
                            : static_cast<double>(records.size()) /
                                  static_cast<double>(total_subs);
        for (std::uint64_t u = 0; u < total_subs; ++u) {
            if (shared_context_count[u] > 0) {
                ++metrics.substrings_with_shared_context;
            }
        }
        metrics.substrings_with_shared_context_share =
            total_subs == 0 ? 0.0
                            : static_cast<double>(metrics.substrings_with_shared_context) /
                                  static_cast<double>(total_subs);
        metrics.pair_emissions = pair_emissions;

        // Pass 6: pair emission from non-hub shared contexts.
        std::vector<std::uint64_t> pairs;
        pairs.reserve(pair_emissions);
        for (std::size_t i = 0; i < records.size();) {
            std::size_t j = i;
            while (j < records.size() && records[j].left == records[i].left &&
                   records[j].right == records[i].right) {
                ++j;
            }
            const std::uint64_t degree = j - i;
            if (degree >= 2 && degree <= config.hub_cap) {
                for (std::size_t a = i; a < j; ++a) {
                    for (std::size_t b = a + 1; b < j; ++b) {
                        pairs.push_back(pack_pair(records[a].sub, records[b].sub));
                    }
                }
            }
            i = j;
        }
        std::vector<CtxRec>().swap(records);
        std::sort(pairs.begin(), pairs.end());

        // Collapse duplicates into (key, count).
        std::vector<std::uint64_t> pair_keys;
        std::vector<std::uint32_t> pair_counts;
        for (std::size_t i = 0; i < pairs.size();) {
            std::size_t j = i;
            while (j < pairs.size() && pairs[j] == pairs[i]) {
                ++j;
            }
            pair_keys.push_back(pairs[i]);
            pair_counts.push_back(static_cast<std::uint32_t>(j - i));
            i = j;
        }
        std::vector<std::uint64_t>().swap(pairs);
        metrics.distinct_pairs = pair_keys.size();

        // POS labels for length-1 substrings (evaluation only).
        std::vector<std::int16_t> pos_label;
        if (!pos_of_token.empty()) {
            pos_label.assign(total_subs, -1);
            std::vector<std::uint64_t> label_counts;
            for (std::uint64_t u = 0; u < registry.n1; ++u) {
                const auto found = pos_of_token.find(corpus.token_text[registry.dense_to_token[u]]);
                if (found != pos_of_token.end()) {
                    pos_label[u] = found->second;
                    if (label_counts.size() <= found->second) {
                        label_counts.resize(found->second + 1, 0);
                    }
                    ++label_counts[found->second];
                }
            }
            std::uint64_t labeled = 0;
            for (const auto count : label_counts) {
                labeled += count;
            }
            double baseline = 0.0;
            for (const auto count : label_counts) {
                const double p = labeled == 0 ? 0.0
                                              : static_cast<double>(count) /
                                                    static_cast<double>(labeled);
                baseline += p * p;
            }
            metrics.same_pos_baseline = baseline;
        }

        // Pass 7: one scan over distinct pairs - curves, components,
        // substitution degrees, probe neighborhoods, held-out sampling.
        const auto& thresholds = config.evidence_thresholds;
        std::vector<Dsu> components;
        components.reserve(thresholds.size());
        for (std::size_t t = 0; t < thresholds.size(); ++t) {
            components.emplace_back(total_subs);
        }
        std::vector<std::uint64_t> pairs_at_m(thresholds.size(), 0);
        std::vector<std::uint64_t> same_pos_at_m(thresholds.size(), 0);
        std::vector<std::uint64_t> labeled_at_m(thresholds.size(), 0);
        std::vector<std::vector<std::uint8_t>> node_seen(
            thresholds.size(), std::vector<std::uint8_t>(total_subs, 0));
        std::vector<std::uint32_t> degree1(total_subs, 0);

        std::vector<std::int32_t> probe_uid(probes.size(), -1);
        std::unordered_map<std::uint32_t, std::size_t> probe_index;
        for (std::size_t p = 0; p < probes.size(); ++p) {
            std::array<std::uint32_t, 3> seq{};
            std::size_t len = 0;
            std::istringstream words(probes[p].text);
            std::string word;
            bool known = true;
            while (words >> word && len < 3) {
                const auto found = token_id_of.find(word);
                if (found == token_id_of.end()) {
                    known = false;
                    break;
                }
                seq[len++] = found->second;
            }
            if (known && len > 0) {
                const auto uid = registry.lookup(seq, len);
                if (uid >= 0) {
                    probe_uid[p] = static_cast<std::int32_t>(uid);
                    probe_index.emplace(static_cast<std::uint32_t>(uid), p);
                }
            }
        }
        std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>> probe_partners(
            probes.size());  // (count, partner uid)

        // Held-out sampling: per bucket keep the pairs_per_bucket smallest
        // mix64(key) values (deterministic hash-order sample).
        using SampleEntry = std::pair<std::uint64_t, std::uint64_t>;  // (hash, index)
        std::array<std::priority_queue<SampleEntry>, 5> samplers;

        for (std::size_t idx = 0; idx < pair_keys.size(); ++idx) {
            const std::uint64_t key = pair_keys[idx];
            const std::uint32_t count = pair_counts[idx];
            const auto a = static_cast<std::uint32_t>(key >> 32U);
            const auto b = static_cast<std::uint32_t>(key & 0xffffffffULL);
            ++degree1[a];
            ++degree1[b];
            const bool labeled = !pos_label.empty() && pos_label[a] >= 0 && pos_label[b] >= 0;
            for (std::size_t t = 0; t < thresholds.size(); ++t) {
                if (count >= thresholds[t]) {
                    ++pairs_at_m[t];
                    components[t].unite(a, b);
                    node_seen[t][a] = 1;
                    node_seen[t][b] = 1;
                    if (labeled) {
                        ++labeled_at_m[t];
                        if (pos_label[a] == pos_label[b]) {
                            ++same_pos_at_m[t];
                        }
                    }
                }
            }
            const auto pa = probe_index.find(a);
            if (pa != probe_index.end()) {
                probe_partners[pa->second].push_back({count, b});
            }
            const auto pb = probe_index.find(b);
            if (pb != probe_index.end()) {
                probe_partners[pb->second].push_back({count, a});
            }
            const std::size_t bucket = count <= 1 ? 0 : count <= 3 ? 1 : count <= 7 ? 2
                                       : count <= 15               ? 3
                                                                   : 4;
            auto& sampler = samplers[bucket];
            const std::uint64_t hash = mix64(key);
            if (sampler.size() < config.pairs_per_bucket) {
                sampler.push({hash, idx});
            } else if (hash < sampler.top().first) {
                sampler.pop();
                sampler.push({hash, idx});
            }
        }

        for (std::size_t t = 0; t < thresholds.size(); ++t) {
            ThresholdStats stats;
            stats.m = thresholds[t];
            stats.pairs = pairs_at_m[t];
            std::unordered_map<std::uint32_t, std::uint64_t> component_sizes;
            for (std::uint32_t u = 0; u < total_subs; ++u) {
                if (node_seen[t][u] != 0) {
                    ++stats.nodes;
                    ++component_sizes[components[t].find(u)];
                }
            }
            stats.components = component_sizes.size();
            for (const auto& entry : component_sizes) {
                stats.largest_component = std::max(stats.largest_component, entry.second);
            }
            stats.largest_component_ratio =
                total_subs == 0 ? 0.0
                                : static_cast<double>(stats.largest_component) /
                                      static_cast<double>(total_subs);
            if (!pos_label.empty()) {
                stats.labeled_pairs = labeled_at_m[t];
                stats.same_pos_pairs = same_pos_at_m[t];
                stats.same_pos_rate = labeled_at_m[t] == 0
                                          ? -1.0
                                          : static_cast<double>(same_pos_at_m[t]) /
                                                static_cast<double>(labeled_at_m[t]);
            }
            metrics.thresholds.push_back(stats);
        }
        components.clear();
        node_seen.clear();

        std::uint64_t degree_sum = 0;
        for (std::uint32_t u = 0; u < total_subs; ++u) {
            degree_sum += degree1[u];
            if (degree1[u] > 0) {
                ++metrics.substrings_with_partner;
            }
        }
        metrics.mean_substitution_degree =
            total_subs == 0 ? 0.0
                            : static_cast<double>(degree_sum) / static_cast<double>(total_subs);

        // ---- neighborhood samples + stability vs previous scale ----------
        neighborhoods << "\n== scale " << N << " ==\n";
        double jaccard_sum = 0.0;
        std::size_t jaccard_terms = 0;
        for (std::size_t p = 0; p < probes.size(); ++p) {
            neighborhoods << "probe \"" << probes[p].text << "\"";
            std::vector<std::string> top_texts;
            if (probe_uid[p] < 0) {
                neighborhoods << ": not in inventory at this scale\n";
            } else {
                const auto uid = static_cast<std::uint32_t>(probe_uid[p]);
                std::uint64_t frequency = 0;
                const auto seq = registry.tokens_of(uid);
                if (registry.len_of(uid) == 1) {
                    frequency = unigram[seq[0]];
                }
                neighborhoods << " frequency=" << (frequency > 0 ? std::to_string(frequency)
                                                                 : std::string("-"))
                              << " context_count=" << context_count[uid]
                              << " shared_context_count=" << shared_context_count[uid]
                              << " substitution_degree=" << degree1[uid] << "\n";
                auto& partners = probe_partners[p];
                std::sort(partners.begin(), partners.end(),
                          [](const auto& x, const auto& y) {
                              if (x.first != y.first) {
                                  return x.first > y.first;
                              }
                              return x.second < y.second;
                          });
                const std::size_t take = std::min(partners.size(), config.top_neighbors);
                for (std::size_t i = 0; i < take; ++i) {
                    const auto partner_text =
                        registry.text_of(partners[i].second, corpus.token_text);
                    top_texts.push_back(partner_text);
                    neighborhoods << "    " << partner_text << " |I|=" << partners[i].first;
                    if (!pos_label.empty() && partners[i].second < registry.n1 &&
                        pos_label[partners[i].second] >= 0) {
                        neighborhoods << " pos="
                                      << pos_names[static_cast<std::size_t>(
                                             pos_label[partners[i].second])];
                    }
                    neighborhoods << "\n";
                }
            }
            if (!probes[p].previous_top.empty() && !top_texts.empty()) {
                std::set<std::string> prev(probes[p].previous_top.begin(),
                                           probes[p].previous_top.end());
                std::set<std::string> curr(top_texts.begin(), top_texts.end());
                std::size_t inter = 0;
                for (const auto& item : curr) {
                    inter += prev.count(item);
                }
                const std::size_t uni = prev.size() + curr.size() - inter;
                const double jaccard =
                    uni == 0 ? 0.0 : static_cast<double>(inter) / static_cast<double>(uni);
                neighborhoods << "    jaccard_vs_previous_scale=" << format_double(jaccard)
                              << "\n";
                jaccard_sum += jaccard;
                ++jaccard_terms;
            }
            probes[p].previous_top = std::move(top_texts);
        }
        if (jaccard_terms > 0) {
            metrics.probe_neighborhood_jaccard_mean = jaccard_sum /
                                                      static_cast<double>(jaccard_terms);
        }

        // ---- transition vs previous scale --------------------------------
        if (previous.scale_tokens != 0) {
            metrics.prev_scale_tokens = previous.scale_tokens;
            std::ifstream prev_pairs(previous.pair_file, std::ios::binary);
            std::vector<std::pair<std::uint64_t, std::uint32_t>> translated;
            std::uint64_t prev_key = 0;
            std::uint32_t prev_count = 0;
            while (prev_pairs.read(reinterpret_cast<char*>(&prev_key), sizeof(prev_key)) &&
                   prev_pairs.read(reinterpret_cast<char*>(&prev_count), sizeof(prev_count))) {
                const auto a = static_cast<std::uint32_t>(prev_key >> 32U);
                const auto b = static_cast<std::uint32_t>(prev_key & 0xffffffffULL);
                const auto ua = registry.lookup(previous.tokens[a], previous.lengths[a]);
                const auto ub = registry.lookup(previous.tokens[b], previous.lengths[b]);
                if (ua < 0 || ub < 0) {
                    ++metrics.untranslatable_pairs;
                    continue;
                }
                const std::uint64_t lo = static_cast<std::uint64_t>(std::min(ua, ub));
                const std::uint64_t hi = static_cast<std::uint64_t>(std::max(ua, ub));
                translated.push_back({pack_pair(lo, hi), prev_count});
            }
            std::sort(translated.begin(), translated.end());
            std::uint64_t prev_sum = 0;
            std::uint64_t curr_sum = 0;
            std::size_t cursor = 0;
            for (const auto& [key, count] : translated) {
                while (cursor < pair_keys.size() && pair_keys[cursor] < key) {
                    ++cursor;
                }
                if (cursor < pair_keys.size() && pair_keys[cursor] == key) {
                    ++metrics.common_pairs;
                    prev_sum += count;
                    curr_sum += pair_counts[cursor];
                } else {
                    ++metrics.lost_pairs;
                }
            }
            metrics.new_pairs = metrics.distinct_pairs - metrics.common_pairs;
            metrics.common_evidence_prev_mean =
                metrics.common_pairs == 0 ? 0.0
                                          : static_cast<double>(prev_sum) /
                                                static_cast<double>(metrics.common_pairs);
            metrics.common_evidence_curr_mean =
                metrics.common_pairs == 0 ? 0.0
                                          : static_cast<double>(curr_sum) /
                                                static_cast<double>(metrics.common_pairs);
            std::filesystem::remove(previous.pair_file);
        }

        // ---- persist pair file + registry snapshot for the next scale ----
        {
            previous.scale_tokens = N;
            previous.pair_file = config.output_dir / "pairs_prev.bin";
            std::ofstream out(previous.pair_file, std::ios::binary | std::ios::trunc);
            for (std::size_t i = 0; i < pair_keys.size(); ++i) {
                out.write(reinterpret_cast<const char*>(&pair_keys[i]), sizeof(pair_keys[i]));
                out.write(reinterpret_cast<const char*>(&pair_counts[i]),
                          sizeof(pair_counts[i]));
            }
            previous.tokens.assign(total_subs, {});
            previous.lengths.assign(total_subs, 0);
            for (std::uint64_t u = 0; u < total_subs; ++u) {
                previous.tokens[u] = registry.tokens_of(u);
                previous.lengths[u] = static_cast<std::uint8_t>(registry.len_of(u));
            }
        }

        if (config.dump_pairs_limit > 0 && pair_keys.size() <= config.dump_pairs_limit) {
            std::ofstream dump(config.output_dir /
                               ("pair_dump_" + std::to_string(N) + ".txt"));
            for (std::size_t i = 0; i < pair_keys.size(); ++i) {
                const auto a = static_cast<std::uint32_t>(pair_keys[i] >> 32U);
                const auto b = static_cast<std::uint32_t>(pair_keys[i] & 0xffffffffULL);
                dump << registry.text_of(a, corpus.token_text) << "\t"
                     << registry.text_of(b, corpus.token_text) << "\t" << pair_counts[i]
                     << "\n";
            }
        }

        // ---- held-out replication ----------------------------------------
        {
            std::vector<SampledPair> sampled;
            for (std::size_t bucket = 0; bucket < samplers.size(); ++bucket) {
                auto sampler = samplers[bucket];
                while (!sampler.empty()) {
                    const auto idx = sampler.top().second;
                    sampler.pop();
                    sampled.push_back({pair_keys[idx], pair_counts[idx],
                                       static_cast<std::uint8_t>(bucket)});
                }
            }
            std::sort(sampled.begin(), sampled.end(),
                      [](const SampledPair& x, const SampledPair& y) {
                          return x.key < y.key;
                      });
            std::vector<std::uint8_t> wanted(total_subs, 0);
            for (const auto& pair : sampled) {
                wanted[static_cast<std::uint32_t>(pair.key >> 32U)] = 1;
                wanted[static_cast<std::uint32_t>(pair.key & 0xffffffffULL)] = 1;
            }
            // Scan the held-out span; collect exact contexts of wanted
            // substrings under the *train* inventory.
            std::vector<CtxRec> hrecords;
            for (std::size_t i = heldout_begin; i < heldout_end; ++i) {
                if (corpus.stream[i] == kDocSentinel ||
                    registry.dense[corpus.stream[i]] == kNoDense) {
                    continue;
                }
                const std::uint32_t left = i > 0 ? corpus.stream[i - 1] : kDocSentinel;
                for (std::size_t len = 1; len <= 3 && i + len <= heldout_end; ++len) {
                    std::array<std::uint32_t, 3> seq{};
                    bool valid = true;
                    for (std::size_t k = 0; k < len; ++k) {
                        seq[k] = corpus.stream[i + k];
                        if (seq[k] == kDocSentinel) {
                            valid = false;
                            break;
                        }
                    }
                    if (!valid) {
                        break;
                    }
                    const auto uid = registry.lookup(seq, len);
                    if (uid < 0 || wanted[static_cast<std::uint64_t>(uid)] == 0) {
                        continue;
                    }
                    const std::uint32_t right =
                        i + len < heldout_end ? corpus.stream[i + len] : kDocSentinel;
                    hrecords.push_back({left, right, static_cast<std::uint32_t>(uid)});
                }
            }
            // Sort by (sub, left, right); dedupe; slice per substring.
            std::sort(hrecords.begin(), hrecords.end(), [](const CtxRec& x, const CtxRec& y) {
                if (x.sub != y.sub) {
                    return x.sub < y.sub;
                }
                if (x.left != y.left) {
                    return x.left < y.left;
                }
                return x.right < y.right;
            });
            hrecords.erase(std::unique(hrecords.begin(), hrecords.end()), hrecords.end());
            std::unordered_map<std::uint32_t, std::pair<std::size_t, std::size_t>> slices;
            for (std::size_t i = 0; i < hrecords.size();) {
                std::size_t j = i;
                while (j < hrecords.size() && hrecords[j].sub == hrecords[i].sub) {
                    ++j;
                }
                slices.emplace(hrecords[i].sub, std::make_pair(i, j));
                i = j;
            }
            const auto shared_in_heldout = [&](const std::uint32_t a,
                                               const std::uint32_t b) -> std::uint64_t {
                const auto sa = slices.find(a);
                const auto sb = slices.find(b);
                if (sa == slices.end() || sb == slices.end()) {
                    return 0;
                }
                std::size_t i = sa->second.first;
                std::size_t j = sb->second.first;
                std::uint64_t shared = 0;
                while (i < sa->second.second && j < sb->second.second) {
                    const std::uint64_t ci = (static_cast<std::uint64_t>(hrecords[i].left)
                                              << 32U) |
                                             hrecords[i].right;
                    const std::uint64_t cj = (static_cast<std::uint64_t>(hrecords[j].left)
                                              << 32U) |
                                             hrecords[j].right;
                    if (ci < cj) {
                        ++i;
                    } else if (cj < ci) {
                        ++j;
                    } else {
                        ++shared;
                        ++i;
                        ++j;
                    }
                }
                return shared;
            };
            std::array<std::vector<std::uint64_t>, 5> bucket_shared;
            std::array<std::uint64_t, 5> bucket_train_sum{};
            for (const auto& pair : sampled) {
                const auto a = static_cast<std::uint32_t>(pair.key >> 32U);
                const auto b = static_cast<std::uint32_t>(pair.key & 0xffffffffULL);
                bucket_shared[pair.bucket].push_back(shared_in_heldout(a, b));
                bucket_train_sum[pair.bucket] += pair.train_count;
            }
            for (std::size_t bucket = 0; bucket < bucket_shared.size(); ++bucket) {
                auto& values = bucket_shared[bucket];
                if (values.empty()) {
                    continue;
                }
                HeldoutBucketStats stats;
                stats.scale_tokens = N;
                stats.bucket = kBucketOrder[bucket];
                stats.sampled_pairs = values.size();
                std::uint64_t total_shared = 0;
                for (const auto value : values) {
                    total_shared += value;
                    if (value > 0) {
                        ++stats.replicated_pairs;
                    }
                }
                stats.replication_rate = static_cast<double>(stats.replicated_pairs) /
                                         static_cast<double>(values.size());
                stats.mean_heldout_shared = static_cast<double>(total_shared) /
                                            static_cast<double>(values.size());
                std::sort(values.begin(), values.end());
                stats.median_heldout_shared =
                    values.size() % 2 == 1
                        ? static_cast<double>(values[values.size() / 2])
                        : (static_cast<double>(values[values.size() / 2 - 1]) +
                           static_cast<double>(values[values.size() / 2])) /
                              2.0;
                stats.mean_train_evidence = static_cast<double>(bucket_train_sum[bucket]) /
                                            static_cast<double>(values.size());
                result.heldout.push_back(stats);
            }
        }

        metrics.runtime_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t_start).count();
        metrics.peak_rss_mb = peak_rss_mb();
        result.scales.push_back(std::move(metrics));
    }
    std::filesystem::remove(config.output_dir / "pairs_prev.bin");

    // ---- CSV outputs -----------------------------------------------------
    {
        std::ofstream csv(config.output_dir / "scaling_metrics.csv");
        csv << "scale_tokens,stream_positions,min_count,vocab_seen,frequent_tokens,"
               "substrings_len1,substrings_len2,substrings_len3,substrings_total,"
               "occurrence_mentions,context_records,distinct_contexts,singleton_contexts,"
               "singleton_context_share,shared_contexts,records_in_shared_contexts_share,"
               "hub_contexts,hub_record_share,mean_contexts_per_substring,"
               "substrings_with_shared_context,substrings_with_shared_context_share,"
               "pair_emissions,distinct_pairs,mean_substitution_degree,"
               "substrings_with_partner,prev_scale_tokens,common_pairs,new_pairs,lost_pairs,"
               "untranslatable_pairs,common_evidence_prev_mean,common_evidence_curr_mean,"
               "probe_neighborhood_jaccard_mean,same_pos_baseline,runtime_seconds,"
               "peak_rss_mb\n";
        for (const auto& m : result.scales) {
            csv << m.scale_tokens << "," << m.stream_positions << "," << m.min_count << ","
                << m.vocab_seen << "," << m.frequent_tokens << "," << m.substrings_len1 << ","
                << m.substrings_len2 << "," << m.substrings_len3 << "," << m.substrings_total
                << "," << m.occurrence_mentions << "," << m.context_records << ","
                << m.distinct_contexts << "," << m.singleton_contexts << ","
                << format_double(m.singleton_context_share) << "," << m.shared_contexts << ","
                << format_double(m.records_in_shared_contexts_share) << "," << m.hub_contexts
                << "," << format_double(m.hub_record_share) << ","
                << format_double(m.mean_contexts_per_substring) << ","
                << m.substrings_with_shared_context << ","
                << format_double(m.substrings_with_shared_context_share) << ","
                << m.pair_emissions << "," << m.distinct_pairs << ","
                << format_double(m.mean_substitution_degree) << ","
                << m.substrings_with_partner << "," << m.prev_scale_tokens << ","
                << m.common_pairs << "," << m.new_pairs << "," << m.lost_pairs << ","
                << m.untranslatable_pairs << ","
                << format_double(m.common_evidence_prev_mean) << ","
                << format_double(m.common_evidence_curr_mean) << ","
                << format_double(m.probe_neighborhood_jaccard_mean) << ","
                << format_double(m.same_pos_baseline) << ","
                << format_double(m.runtime_seconds) << "," << format_double(m.peak_rss_mb)
                << "\n";
        }
    }
    {
        std::ofstream csv(config.output_dir / "pair_evidence_scaling.csv");
        csv << "scale_tokens,m,pairs,nodes,components,largest_component,"
               "largest_component_ratio,labeled_pairs,same_pos_pairs,same_pos_rate\n";
        for (const auto& m : result.scales) {
            for (const auto& t : m.thresholds) {
                csv << m.scale_tokens << "," << t.m << "," << t.pairs << "," << t.nodes << ","
                    << t.components << "," << t.largest_component << ","
                    << format_double(t.largest_component_ratio) << "," << t.labeled_pairs
                    << "," << t.same_pos_pairs << "," << format_double(t.same_pos_rate)
                    << "\n";
            }
        }
    }
    {
        std::ofstream csv(config.output_dir / "heldout_replication.csv");
        csv << "scale_tokens,bucket,sampled_pairs,replicated_pairs,replication_rate,"
               "mean_heldout_shared,median_heldout_shared,mean_train_evidence\n";
        for (const auto& h : result.heldout) {
            csv << h.scale_tokens << "," << h.bucket << "," << h.sampled_pairs << ","
                << h.replicated_pairs << "," << format_double(h.replication_rate) << ","
                << format_double(h.mean_heldout_shared) << ","
                << format_double(h.median_heldout_shared) << ","
                << format_double(h.mean_train_evidence) << "\n";
        }
    }
    neighborhoods << "\n# total wall time: "
                  << format_double(std::chrono::duration<double>(
                                       std::chrono::steady_clock::now() - ladder_start)
                                       .count())
                  << " s, peak RSS " << format_double(peak_rss_mb()) << " MB\n";
    return result;
}

}  // namespace scf::v21
