#include "scf/oracle_v2.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace scf::v2 {

namespace {

constexpr std::size_t kMaxTableEntries = 150'000'000;

constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

std::uint64_t fnv_bytes(std::uint64_t hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= kFnvPrime;
    }
    return hash;
}

std::uint64_t fnv_u32(std::uint64_t hash, std::uint32_t value) {
    unsigned char bytes[4];
    for (int i = 0; i < 4; ++i) {
        bytes[i] = static_cast<unsigned char>((value >> (8 * i)) & 0xffU);
    }
    return fnv_bytes(hash, bytes, 4);
}

std::uint64_t pair_count(std::uint64_t n) { return n * (n - 1) / 2; }

std::string format_double(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6f", value);
    return buffer;
}

bool filter_bit(const std::vector<std::uint64_t>& filter, std::uint64_t index) {
    return (filter[index >> 6U] >> (index & 63U)) & 1U;
}

void set_filter_bit(std::vector<std::uint64_t>& filter, std::uint64_t index) {
    filter[index >> 6U] |= std::uint64_t{1} << (index & 63U);
}

}  // namespace

// ---------------------------------------------------------------------------
// Grammar families
// ---------------------------------------------------------------------------

std::vector<std::string> oracle_grammar_names() {
    return {"simple_np_vp", "transitive", "recursive_modifier",
            "observationally_equivalent_categories"};
}

OracleGrammar make_oracle_grammar(const std::string& name) {
    OracleGrammar g;
    g.name = name;
    const auto cat = [&g](const std::string& label) {
        for (std::size_t i = 0; i < g.categories.size(); ++i) {
            if (g.categories[i] == label) {
                return i;
            }
        }
        throw std::runtime_error("unknown category " + label);
    };
    const auto define = [&g](std::vector<std::string> categories,
                             std::vector<std::string> vocabulary) {
        g.categories = std::move(categories);
        g.vocabulary = std::move(vocabulary);
        g.lexicon.assign(g.vocabulary.size(), {});
    };
    const auto lex = [&](const std::string& token, const std::string& category) {
        for (std::size_t i = 0; i < g.vocabulary.size(); ++i) {
            if (g.vocabulary[i] == token) {
                g.lexicon[i].push_back(cat(category));
                return;
            }
        }
        throw std::runtime_error("unknown token " + token);
    };
    const auto comp = [&](const std::string& a, const std::string& b, const std::string& c) {
        g.composition.push_back({cat(a), cat(b), cat(c)});
    };

    if (name == "simple_np_vp") {
        define({"D", "N", "NP", "IV", "S"}, {"the", "dog", "cat", "sleeps", "runs"});
        lex("the", "D");
        lex("dog", "N");
        lex("cat", "N");
        lex("sleeps", "IV");
        lex("runs", "IV");
        comp("D", "N", "NP");
        comp("NP", "IV", "S");
        g.accepting = {cat("S")};
    } else if (name == "transitive") {
        // "sleeps" is a lexical VP while "sees the dog" derives VP: the two
        // must land in one learned class if external equivalence recovers
        // categories. Distinguishing TV from dead strings needs an accepting
        // context of weight >= 4 ("the N _ the N"), the deliberate
        // context-depth stress of this family.
        define({"D", "N", "NP", "TV", "VP", "S"},
               {"the", "dog", "cat", "sees", "likes", "sleeps"});
        lex("the", "D");
        lex("dog", "N");
        lex("cat", "N");
        lex("sees", "TV");
        lex("likes", "TV");
        lex("sleeps", "VP");
        comp("D", "N", "NP");
        comp("TV", "NP", "VP");
        comp("NP", "VP", "S");
        g.accepting = {cat("S")};
    } else if (name == "recursive_modifier") {
        // A N -> N is recursive: adjective chains of any depth stay category N
        // material, so the bounded language contains nested modifiers.
        define({"D", "A", "N", "NP", "IV", "S"}, {"the", "big", "red", "dog", "sleeps"});
        lex("the", "D");
        lex("big", "A");
        lex("red", "A");
        lex("dog", "N");
        lex("sleeps", "IV");
        comp("A", "N", "N");
        comp("D", "N", "NP");
        comp("NP", "IV", "S");
        g.accepting = {cat("S")};
    } else if (name == "observationally_equivalent_categories") {
        // Nm and Nf are distinct gold categories with byte-identical
        // distributions: no context whatsoever separates "dog" from "cat".
        define({"D", "Nm", "Nf", "NP", "IV", "S"}, {"the", "dog", "cat", "sleeps"});
        lex("the", "D");
        lex("dog", "Nm");
        lex("cat", "Nf");
        lex("sleeps", "IV");
        comp("D", "Nm", "NP");
        comp("D", "Nf", "NP");
        comp("NP", "IV", "S");
        g.accepting = {cat("S")};
    } else {
        throw std::runtime_error("unknown oracle grammar: " + name);
    }

    if (g.categories.size() > kMaxCategories) {
        throw std::runtime_error("oracle grammar exceeds category limit");
    }
    return g;
}

// ---------------------------------------------------------------------------
// OracleParser (reference CKY)
// ---------------------------------------------------------------------------

OracleParser::OracleParser(const OracleGrammar& grammar) : rules_(grammar.composition) {
    if (grammar.categories.size() > kMaxCategories) {
        throw std::runtime_error("too many categories");
    }
    if (grammar.lexicon.size() != grammar.vocabulary.size()) {
        throw std::runtime_error("lexicon/vocabulary size mismatch");
    }
    lexical_masks_.assign(grammar.vocabulary.size(), 0);
    for (std::size_t t = 0; t < grammar.lexicon.size(); ++t) {
        for (const auto category : grammar.lexicon[t]) {
            lexical_masks_[t] |= CategoryMask{1} << category;
        }
    }
    for (const auto& rule : rules_) {
        if (rule.left >= grammar.categories.size() || rule.right >= grammar.categories.size() ||
            rule.result >= grammar.categories.size()) {
            throw std::runtime_error("composition rule out of range");
        }
    }
    for (const auto category : grammar.accepting) {
        accepting_mask_ |= CategoryMask{1} << category;
    }
    if (accepting_mask_ == 0) {
        throw std::runtime_error("empty accepting set");
    }
}

CategoryMask OracleParser::categories(const std::span<const std::uint8_t> tokens) const {
    const std::size_t n = tokens.size();
    if (n == 0) {
        return 0;
    }
    // dp[i * (n + 1) + j] = Cats(tokens[i..j))
    std::vector<CategoryMask> dp(n * (n + 1), 0);
    for (std::size_t i = 0; i < n; ++i) {
        dp[i * (n + 1) + i + 1] = lexical_masks_.at(tokens[i]);
    }
    for (std::size_t len = 2; len <= n; ++len) {
        for (std::size_t i = 0; i + len <= n; ++i) {
            const std::size_t j = i + len;
            CategoryMask mask = 0;
            for (std::size_t s = i + 1; s < j; ++s) {
                const auto left = dp[i * (n + 1) + s];
                if (left == 0) {
                    continue;
                }
                const auto right = dp[s * (n + 1) + j];
                if (right == 0) {
                    continue;
                }
                for (const auto& rule : rules_) {
                    if (((left >> rule.left) & 1U) != 0 && ((right >> rule.right) & 1U) != 0) {
                        mask |= CategoryMask{1} << rule.result;
                    }
                }
            }
            dp[i * (n + 1) + j] = mask;
        }
    }
    return dp[n];
}

bool OracleParser::accept(const std::span<const std::uint8_t> tokens) const {
    return (categories(tokens) & accepting_mask_) != 0;
}

// ---------------------------------------------------------------------------
// StringSpace
// ---------------------------------------------------------------------------

StringSpace StringSpace::make(const std::size_t vocab, const std::size_t max_len) {
    if (vocab == 0 || max_len == 0) {
        throw std::runtime_error("StringSpace requires vocab >= 1 and max_len >= 1");
    }
    StringSpace space;
    space.vocab = vocab;
    space.max_len = max_len;
    space.pow.assign(max_len + 1, 1);
    space.start.assign(max_len + 2, 0);
    for (std::size_t i = 1; i <= max_len; ++i) {
        if (space.pow[i - 1] > kMaxTableEntries / vocab) {
            throw std::runtime_error("StringSpace too large");
        }
        space.pow[i] = space.pow[i - 1] * vocab;
    }
    space.start[1] = 0;
    for (std::size_t l = 1; l <= max_len; ++l) {
        if (space.start[l] > kMaxTableEntries - space.pow[l]) {
            throw std::runtime_error("StringSpace too large");
        }
        space.start[l + 1] = space.start[l] + space.pow[l];
    }
    return space;
}

std::size_t StringSpace::index(const std::span<const std::uint8_t> tokens) const {
    if (tokens.empty() || tokens.size() > max_len) {
        throw std::runtime_error("string outside StringSpace");
    }
    std::size_t value = 0;
    for (const auto token : tokens) {
        if (token >= vocab) {
            throw std::runtime_error("token outside vocabulary");
        }
        value = value * vocab + token;
    }
    return index(tokens.size(), value);
}

std::size_t StringSpace::length_of(const std::size_t idx) const {
    for (std::size_t l = 1; l <= max_len; ++l) {
        if (idx < start[l + 1]) {
            return l;
        }
    }
    throw std::runtime_error("index outside StringSpace");
}

std::vector<std::uint8_t> StringSpace::decode(const std::size_t idx) const {
    const std::size_t len = length_of(idx);
    std::size_t value = idx - start[len];
    std::vector<std::uint8_t> tokens(len, 0);
    for (std::size_t j = len; j-- > 0;) {
        tokens[j] = static_cast<std::uint8_t>(value % vocab);
        value /= vocab;
    }
    return tokens;
}

// ---------------------------------------------------------------------------
// CategoryTable
// ---------------------------------------------------------------------------

CategoryTable::CategoryTable(const OracleGrammar& grammar, const std::size_t max_total_len)
    : space_(StringSpace::make(grammar.vocabulary.size(), max_total_len)) {
    const OracleParser parser(grammar);  // reuses grammar validation
    accepting_mask_ = parser.accepting_mask();
    table_.assign(space_.size(), 0);

    const std::size_t vocab = space_.vocab;
    for (std::size_t t = 0; t < vocab; ++t) {
        CategoryMask mask = 0;
        for (const auto category : grammar.lexicon[t]) {
            mask |= CategoryMask{1} << category;
        }
        table_[space_.index(1, t)] = static_cast<std::uint8_t>(mask);
    }

    std::vector<std::uint8_t> digits;
    std::vector<std::size_t> prefix;
    std::vector<std::size_t> suffix;
    for (std::size_t n = 2; n <= max_total_len; ++n) {
        digits.assign(n, 0);
        prefix.assign(n + 1, 0);
        suffix.assign(n + 1, 0);
        const std::size_t count = space_.pow[n];
        for (std::size_t value = 0; value < count; ++value) {
            for (std::size_t j = 0; j < n; ++j) {
                prefix[j + 1] = prefix[j] * vocab + digits[j];
            }
            for (std::size_t j = n; j-- > 0;) {
                suffix[j] = digits[j] * space_.pow[n - 1 - j] + suffix[j + 1];
            }
            CategoryMask mask = 0;
            for (std::size_t split = 1; split < n; ++split) {
                const CategoryMask left = table_[space_.index(split, prefix[split])];
                if (left == 0) {
                    continue;
                }
                const CategoryMask right = table_[space_.index(n - split, suffix[split])];
                if (right == 0) {
                    continue;
                }
                for (const auto& rule : grammar.composition) {
                    if (((left >> rule.left) & 1U) != 0 && ((right >> rule.right) & 1U) != 0) {
                        mask |= CategoryMask{1} << rule.result;
                    }
                }
            }
            table_[space_.index(n, value)] = static_cast<std::uint8_t>(mask);
            for (std::size_t d = n; d-- > 0;) {
                if (++digits[d] < vocab) {
                    break;
                }
                digits[d] = 0;
            }
        }
    }
}

CategoryMask CategoryTable::categories(const std::span<const std::uint8_t> tokens) const {
    return table_[space_.index(tokens)];
}

// ---------------------------------------------------------------------------
// Signature hits
// ---------------------------------------------------------------------------

SignatureHits compute_signature_hits(const CategoryTable& table,
                                     const std::size_t max_len,
                                     const std::size_t max_k) {
    const StringSpace& corpus = table.space();
    if (corpus.max_len < max_len + max_k) {
        throw std::runtime_error("CategoryTable shorter than max_len + max_k");
    }
    if (corpus.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("corpus space exceeds 32-bit index packing");
    }

    SignatureHits hits;
    hits.max_k = max_k;
    hits.universe = StringSpace::make(corpus.vocab, max_len);
    hits.corpus_space = corpus;
    hits.offsets.reserve(hits.universe.size() + 1);
    hits.offsets.push_back(0);

    for (std::size_t ulen = 1; ulen <= max_len; ++ulen) {
        for (std::size_t uval = 0; uval < corpus.pow[ulen]; ++uval) {
            for (std::size_t w = 0; w <= max_k; ++w) {
                std::uint32_t ordinal = 0;
                for (std::size_t llen = 0; llen <= w; ++llen) {
                    const std::size_t rlen = w - llen;
                    const std::size_t wlen = llen + ulen + rlen;
                    for (std::size_t lval = 0; lval < corpus.pow[llen]; ++lval) {
                        const std::size_t base =
                            (lval * corpus.pow[ulen] + uval) * corpus.pow[rlen];
                        const std::size_t row = corpus.start[wlen] + base;
                        for (std::size_t rval = 0; rval < corpus.pow[rlen]; ++rval) {
                            if (table.accept_at(row + rval)) {
                                hits.packed.push_back((static_cast<std::uint64_t>(w) << 56U) |
                                                      (static_cast<std::uint64_t>(ordinal) << 32U) |
                                                      static_cast<std::uint64_t>(row + rval));
                            }
                            ++ordinal;
                        }
                    }
                }
            }
            hits.offsets.push_back(hits.packed.size());
        }
    }
    return hits;
}

// ---------------------------------------------------------------------------
// Partition refinement
// ---------------------------------------------------------------------------

namespace {

struct RefineKey {
    std::uint32_t previous{};
    std::vector<std::uint32_t> ordinals;

    bool operator==(const RefineKey&) const = default;
};

struct RefineKeyHash {
    std::size_t operator()(const RefineKey& key) const noexcept {
        std::uint64_t hash = fnv_u32(kFnvOffset, key.previous);
        for (const auto ordinal : key.ordinals) {
            hash = fnv_u32(hash, ordinal);
        }
        return static_cast<std::size_t>(hash);
    }
};

}  // namespace

std::vector<Partition> refine_partitions(const SignatureHits& hits,
                                         const std::vector<std::uint64_t>& corpus_filter) {
    const std::size_t n = hits.universe.size();
    if (!corpus_filter.empty() && corpus_filter.size() * 64 < hits.corpus_space.size()) {
        throw std::runtime_error("corpus filter too small");
    }
    std::vector<Partition> result;
    result.reserve(hits.max_k + 1);
    std::vector<std::uint32_t> previous(n, 0);

    for (std::size_t w = 0; w <= hits.max_k; ++w) {
        std::unordered_map<RefineKey, std::uint32_t, RefineKeyHash> ids;
        ids.reserve(1024);
        Partition part;
        part.class_of.resize(n);
        for (std::size_t u = 0; u < n; ++u) {
            RefineKey key;
            key.previous = previous[u];
            for (std::size_t p = hits.offsets[u]; p < hits.offsets[u + 1]; ++p) {
                const std::uint64_t entry = hits.packed[p];
                const std::size_t weight = entry >> 56U;
                if (weight < w) {
                    continue;
                }
                if (weight > w) {
                    break;
                }
                if (!corpus_filter.empty() &&
                    !filter_bit(corpus_filter, entry & 0xffffffffULL)) {
                    continue;
                }
                key.ordinals.push_back(static_cast<std::uint32_t>((entry >> 32U) & 0xffffffU));
            }
            const auto found = ids.find(key);
            if (found != ids.end()) {
                part.class_of[u] = found->second;
            } else {
                const auto id = static_cast<std::uint32_t>(ids.size());
                ids.emplace(std::move(key), id);
                part.class_of[u] = id;
            }
        }
        part.num_classes = static_cast<std::uint32_t>(ids.size());
        previous = part.class_of;
        result.push_back(std::move(part));
    }
    return result;
}

Partition restrict_partition(const Partition& partition, const std::size_t prefix_size) {
    if (prefix_size > partition.class_of.size()) {
        throw std::runtime_error("restriction larger than partition");
    }
    Partition result;
    result.class_of.resize(prefix_size);
    std::vector<std::uint32_t> renumber(partition.num_classes,
                                        std::numeric_limits<std::uint32_t>::max());
    std::uint32_t next = 0;
    for (std::size_t i = 0; i < prefix_size; ++i) {
        const auto old_id = partition.class_of[i];
        if (renumber[old_id] == std::numeric_limits<std::uint32_t>::max()) {
            renumber[old_id] = next++;
        }
        result.class_of[i] = renumber[old_id];
    }
    result.num_classes = next;
    return result;
}

// ---------------------------------------------------------------------------
// Gold labeling
// ---------------------------------------------------------------------------

GoldLabeling gold_labeling(const CategoryTable& table, const std::size_t max_len) {
    if (table.space().max_len < max_len) {
        throw std::runtime_error("table shorter than requested universe");
    }
    const std::size_t n = table.space().start[max_len + 1];
    GoldLabeling gold;
    gold.mask_of.resize(n);
    gold.class_of.resize(n);
    std::unordered_map<CategoryMask, std::uint32_t> ids;
    for (std::size_t i = 0; i < n; ++i) {
        const CategoryMask mask = table.at(i);
        gold.mask_of[i] = mask;
        const auto found = ids.find(mask);
        if (found != ids.end()) {
            gold.class_of[i] = found->second;
        } else {
            const auto id = static_cast<std::uint32_t>(ids.size());
            ids.emplace(mask, id);
            gold.class_mask.push_back(mask);
            gold.class_of[i] = id;
        }
    }
    gold.num_classes = static_cast<std::uint32_t>(ids.size());
    return gold;
}

std::string mask_name(const CategoryMask mask, const OracleGrammar& grammar) {
    if (mask == 0) {
        return "{NONE}";
    }
    std::string out = "{";
    bool first = true;
    for (std::size_t i = 0; i < grammar.categories.size(); ++i) {
        if (((mask >> i) & 1U) != 0) {
            if (!first) {
                out += ",";
            }
            out += grammar.categories[i];
            first = false;
        }
    }
    out += "}";
    return out;
}

// ---------------------------------------------------------------------------
// Partition metrics
// ---------------------------------------------------------------------------

PartitionMetrics compare_partitions(const std::span<const std::uint32_t> left,
                                    const std::span<const std::uint32_t> right) {
    if (left.size() != right.size()) {
        throw std::runtime_error("partition size mismatch");
    }
    PartitionMetrics metrics;
    metrics.universe_size = left.size();
    const std::size_t n = left.size();
    if (n == 0) {
        metrics.ari = 1.0;
        metrics.nmi = 1.0;
        metrics.pairwise_precision = 1.0;
        metrics.pairwise_recall = 1.0;
        return metrics;
    }

    std::unordered_map<std::uint64_t, std::uint64_t> cells;
    std::unordered_map<std::uint32_t, std::uint64_t> rows;
    std::unordered_map<std::uint32_t, std::uint64_t> cols;
    for (std::size_t i = 0; i < n; ++i) {
        ++cells[(static_cast<std::uint64_t>(left[i]) << 32U) | right[i]];
        ++rows[left[i]];
        ++cols[right[i]];
    }
    metrics.left_classes = rows.size();
    metrics.right_classes = cols.size();

    std::uint64_t same_both = 0;
    for (const auto& cell : cells) {
        same_both += pair_count(cell.second);
    }
    std::uint64_t same_left = 0;
    for (const auto& row : rows) {
        same_left += pair_count(row.second);
    }
    std::uint64_t same_right = 0;
    for (const auto& col : cols) {
        same_right += pair_count(col.second);
    }
    const std::uint64_t total_pairs = pair_count(n);

    metrics.merge_error_pairs = same_left - same_both;
    metrics.split_error_pairs = same_right - same_both;
    metrics.pairwise_precision =
        same_left == 0 ? 1.0 : static_cast<double>(same_both) / static_cast<double>(same_left);
    metrics.pairwise_recall =
        same_right == 0 ? 1.0 : static_cast<double>(same_both) / static_cast<double>(same_right);

    const long double s = static_cast<long double>(same_both);
    const long double sa = static_cast<long double>(same_left);
    const long double sb = static_cast<long double>(same_right);
    const long double n2 = static_cast<long double>(total_pairs);
    if (n2 == 0) {
        metrics.ari = 1.0;
    } else {
        const long double expected = sa * sb / n2;
        const long double maximum = (sa + sb) / 2;
        const long double denom = maximum - expected;
        metrics.ari = denom == 0 ? 1.0 : static_cast<double>((s - expected) / denom);
    }

    const long double dn = static_cast<long double>(n);
    long double h_left = 0;
    for (const auto& row : rows) {
        const long double p = static_cast<long double>(row.second) / dn;
        h_left -= p * std::log(p);
    }
    long double h_right = 0;
    for (const auto& col : cols) {
        const long double p = static_cast<long double>(col.second) / dn;
        h_right -= p * std::log(p);
    }
    long double mutual = 0;
    for (const auto& [key, count] : cells) {
        const auto row = rows.at(static_cast<std::uint32_t>(key >> 32U));
        const auto col = cols.at(static_cast<std::uint32_t>(key & 0xffffffffULL));
        const long double p = static_cast<long double>(count) / dn;
        mutual += p * std::log(dn * static_cast<long double>(count) /
                               (static_cast<long double>(row) * static_cast<long double>(col)));
    }
    metrics.nmi = (h_left + h_right) == 0
                      ? 1.0
                      : static_cast<double>(2 * mutual / (h_left + h_right));
    return metrics;
}

ObservationalEquivalence find_observationally_equivalent_gold_classes(
    const std::span<const std::uint32_t> learned,
    const std::span<const std::uint32_t> gold) {
    if (learned.size() != gold.size()) {
        throw std::runtime_error("partition size mismatch");
    }
    ObservationalEquivalence result;
    // gold class -> (containing learned class or "split" sentinel, size)
    std::unordered_map<std::uint32_t, std::pair<std::int64_t, std::uint64_t>> containment;
    for (std::size_t i = 0; i < learned.size(); ++i) {
        auto& entry = containment.try_emplace(gold[i], static_cast<std::int64_t>(learned[i]),
                                              std::uint64_t{0})
                          .first->second;
        if (entry.first != static_cast<std::int64_t>(learned[i])) {
            entry.first = -1;  // spread over several learned classes
        }
        ++entry.second;
    }
    // learned class -> fully contained gold classes (canonical order by gold id)
    std::unordered_map<std::uint32_t, std::vector<std::pair<std::uint32_t, std::uint64_t>>> groups;
    std::vector<std::uint32_t> gold_ids;
    gold_ids.reserve(containment.size());
    for (const auto& entry : containment) {
        gold_ids.push_back(entry.first);
    }
    std::sort(gold_ids.begin(), gold_ids.end());
    for (const auto gold_id : gold_ids) {
        const auto& entry = containment.at(gold_id);
        if (entry.first >= 0) {
            groups[static_cast<std::uint32_t>(entry.first)].push_back({gold_id, entry.second});
        }
    }
    std::vector<std::uint32_t> learned_ids;
    learned_ids.reserve(groups.size());
    for (const auto& group : groups) {
        if (group.second.size() >= 2) {
            learned_ids.push_back(group.first);
        }
    }
    std::sort(learned_ids.begin(), learned_ids.end());
    for (const auto learned_id : learned_ids) {
        const auto& members = groups.at(learned_id);
        std::vector<std::uint32_t> group;
        std::uint64_t total = 0;
        std::uint64_t within = 0;
        for (const auto& [gold_id, size] : members) {
            group.push_back(gold_id);
            total += size;
            within += pair_count(size);
        }
        result.excluded_merge_pairs += pair_count(total) - within;
        result.groups.push_back(std::move(group));
    }
    return result;
}

// ---------------------------------------------------------------------------
// Composition recovery
// ---------------------------------------------------------------------------

namespace {

std::string string_text(const StringSpace& space,
                        const std::size_t index,
                        const OracleGrammar& grammar) {
    std::string out;
    for (const auto token : space.decode(index)) {
        if (!out.empty()) {
            out += " ";
        }
        out += grammar.vocabulary[token];
    }
    return out;
}

}  // namespace

CompositionRecovery recover_composition(const CategoryTable& table,
                                        const OracleGrammar& grammar,
                                        const Partition& partition,
                                        const std::size_t max_len) {
    const StringSpace space = StringSpace::make(grammar.vocabulary.size(), max_len);
    if (partition.class_of.size() != space.size()) {
        throw std::runtime_error("partition does not match universe of max_len");
    }
    if (partition.num_classes >= (1U << 21U)) {
        throw std::runtime_error("too many classes for triple packing");
    }

    CompositionRecovery recovery;

    // Class labels: no member with >= 2 categories, at least one constituent.
    struct ClassLabel {
        CategoryMask label{};
        bool has_constituent{};
        bool has_multi{};
    };
    std::vector<ClassLabel> labels(partition.num_classes);
    for (std::size_t i = 0; i < space.size(); ++i) {
        const CategoryMask mask = table.at(i);
        auto& entry = labels[partition.class_of[i]];
        if (std::popcount(mask) >= 2) {
            entry.has_multi = true;
        } else if (mask != 0) {
            entry.label |= mask;
            entry.has_constituent = true;
        }
    }
    const auto labelable = [&labels](const std::uint32_t class_id) {
        return labels[class_id].has_constituent && !labels[class_id].has_multi;
    };

    struct TripleWitness {
        std::size_t u{};
        std::size_t v{};
    };
    std::unordered_map<std::uint64_t, TripleWitness> triples;
    struct PairEntry {
        std::uint32_t result{};
        std::size_t u{};
        std::size_t v{};
    };
    std::unordered_map<std::uint64_t, PairEntry> first_result;
    std::unordered_set<std::uint64_t> violated;

    for (std::size_t total = 2; total <= max_len; ++total) {
        for (std::size_t ulen = 1; ulen < total; ++ulen) {
            const std::size_t vlen = total - ulen;
            for (std::size_t uval = 0; uval < space.pow[ulen]; ++uval) {
                const std::size_t uidx = space.index(ulen, uval);
                const std::uint32_t a = partition.class_of[uidx];
                for (std::size_t vval = 0; vval < space.pow[vlen]; ++vval) {
                    const std::size_t vidx = space.index(vlen, vval);
                    const std::size_t uvidx =
                        space.index(total, uval * space.pow[vlen] + vval);
                    const std::uint32_t b = partition.class_of[vidx];
                    const std::uint32_t c = partition.class_of[uvidx];
                    const std::uint64_t triple_key = (static_cast<std::uint64_t>(a) << 42U) |
                                                     (static_cast<std::uint64_t>(b) << 21U) | c;
                    triples.try_emplace(triple_key, TripleWitness{uidx, vidx});
                    const std::uint64_t pair_key = (static_cast<std::uint64_t>(a) << 32U) | b;
                    const auto found = first_result.find(pair_key);
                    if (found == first_result.end()) {
                        first_result.emplace(pair_key, PairEntry{c, uidx, vidx});
                    } else if (found->second.result != c && violated.insert(pair_key).second &&
                               !recovery.first_violation_example.has_value()) {
                        std::ostringstream example;
                        example << "u=\"" << string_text(space, found->second.u, grammar)
                                << "\" v=\"" << string_text(space, found->second.v, grammar)
                                << "\" -> class " << found->second.result << " ; u'=\""
                                << string_text(space, uidx, grammar) << "\" v'=\""
                                << string_text(space, vidx, grammar) << "\" -> class " << c
                                << " (u ≡ u', v ≡ v', but uv ≢ u'v')";
                        recovery.first_violation_example = example.str();
                    }
                }
            }
        }
    }

    recovery.composition_triples = triples.size();
    recovery.nonfunctional_input_pairs = violated.size();
    recovery.functional_input_pairs = first_result.size() - violated.size();
    recovery.congruence_violations = violated.size();

    // Deterministic iteration for scoring and examples: sort triples by key.
    std::vector<std::pair<std::uint64_t, TripleWitness>> ordered(triples.begin(), triples.end());
    std::sort(ordered.begin(), ordered.end(),
              [](const auto& x, const auto& y) { return x.first < y.first; });

    struct LabeledTriple {
        CategoryMask a{};
        CategoryMask b{};
        CategoryMask c{};
    };
    std::vector<LabeledTriple> labeled;
    for (const auto& [key, witness] : ordered) {
        const auto a = static_cast<std::uint32_t>(key >> 42U);
        const auto b = static_cast<std::uint32_t>((key >> 21U) & 0x1fffffU);
        const auto c = static_cast<std::uint32_t>(key & 0x1fffffU);
        if (!labelable(a) || !labelable(b) || !labelable(c)) {
            continue;
        }
        ++recovery.labeled_triples;
        const LabeledTriple triple{labels[a].label, labels[b].label, labels[c].label};
        labeled.push_back(triple);
        bool correct = false;
        for (const auto& rule : grammar.composition) {
            if (((triple.a >> rule.left) & 1U) != 0 && ((triple.b >> rule.right) & 1U) != 0 &&
                ((triple.c >> rule.result) & 1U) != 0) {
                correct = true;
                break;
            }
        }
        if (correct) {
            ++recovery.correct_labeled_triples;
        } else if (recovery.incorrect_triple_examples.size() < 10) {
            std::ostringstream example;
            example << "(" << mask_name(triple.a, grammar) << ", " << mask_name(triple.b, grammar)
                    << ", " << mask_name(triple.c, grammar) << ") e.g. \""
                    << string_text(space, witness.u, grammar) << "\" + \""
                    << string_text(space, witness.v, grammar) << "\"";
            recovery.incorrect_triple_examples.push_back(example.str());
        }
    }
    recovery.composition_precision =
        recovery.labeled_triples == 0
            ? 1.0
            : static_cast<double>(recovery.correct_labeled_triples) /
                  static_cast<double>(recovery.labeled_triples);

    // Witnessable gold rules: realizable by exact single-category strings
    // within the length bound.
    std::vector<std::vector<std::size_t>> singleton_strings(grammar.categories.size());
    for (std::size_t i = 0; i < space.size(); ++i) {
        const CategoryMask mask = table.at(i);
        if (std::popcount(mask) == 1) {
            singleton_strings[static_cast<std::size_t>(std::countr_zero(mask))].push_back(i);
        }
    }
    for (const auto& rule : grammar.composition) {
        bool witnessable = false;
        for (const auto uidx : singleton_strings[rule.left]) {
            const std::size_t ulen = space.length_of(uidx);
            for (const auto vidx : singleton_strings[rule.right]) {
                const std::size_t vlen = space.length_of(vidx);
                if (ulen + vlen > max_len) {
                    continue;
                }
                const std::size_t uvidx =
                    space.index(ulen + vlen, (uidx - space.start[ulen]) * space.pow[vlen] +
                                                 (vidx - space.start[vlen]));
                if (((table.at(uvidx) >> rule.result) & 1U) != 0) {
                    witnessable = true;
                    break;
                }
            }
            if (witnessable) {
                break;
            }
        }
        if (!witnessable) {
            continue;
        }
        ++recovery.witnessable_gold_rules;
        for (const auto& triple : labeled) {
            if (((triple.a >> rule.left) & 1U) != 0 && ((triple.b >> rule.right) & 1U) != 0 &&
                ((triple.c >> rule.result) & 1U) != 0) {
                ++recovery.recovered_gold_rules;
                break;
            }
        }
    }
    recovery.composition_recall =
        recovery.witnessable_gold_rules == 0
            ? 1.0
            : static_cast<double>(recovery.recovered_gold_rules) /
                  static_cast<double>(recovery.witnessable_gold_rules);
    return recovery;
}

// ---------------------------------------------------------------------------
// Positive-only ablation
// ---------------------------------------------------------------------------

void deterministic_shuffle_v2(std::vector<std::size_t>& values, const std::uint64_t seed) {
    std::mt19937_64 engine(seed);
    const auto bounded = [&engine](const std::uint64_t bound) {
        // Unbiased rejection sampling; mt19937_64 output is fully specified by
        // the standard, so results are identical across platforms.
        std::uint64_t value = 0;
        std::uint64_t remainder = 0;
        do {
            value = engine();
            remainder = value % bound;
        } while (value - remainder > std::numeric_limits<std::uint64_t>::max() - (bound - 1));
        return remainder;
    };
    for (std::size_t index = values.size(); index > 1; --index) {
        const auto other = static_cast<std::size_t>(bounded(static_cast<std::uint64_t>(index)));
        std::swap(values[index - 1], values[other]);
    }
}

PositiveSample sample_positive(const CategoryTable& table,
                               const double coverage,
                               const std::uint64_t seed) {
    if (coverage <= 0.0 || coverage > 1.0) {
        throw std::runtime_error("coverage must lie in (0, 1]");
    }
    std::vector<std::size_t> accepted;
    for (std::size_t i = 0; i < table.space().size(); ++i) {
        if (table.accept_at(i)) {
            accepted.push_back(i);
        }
    }
    PositiveSample sample;
    sample.accepted_count = accepted.size();
    deterministic_shuffle_v2(accepted, seed);
    // ceil(coverage * N) with a tiny guard against binary rounding artifacts.
    auto take = static_cast<std::size_t>(
        std::ceil(coverage * static_cast<double>(accepted.size()) - 1e-9));
    take = std::min(std::max<std::size_t>(take, accepted.empty() ? 0 : 1), accepted.size());
    sample.retained_count = take;
    sample.filter.assign((table.space().size() + 63) / 64, 0);
    for (std::size_t i = 0; i < take; ++i) {
        set_filter_bit(sample.filter, accepted[i]);
    }
    return sample;
}

// ---------------------------------------------------------------------------
// Hashing
// ---------------------------------------------------------------------------

std::uint64_t partition_hash(const Partition& partition) {
    std::uint64_t hash = fnv_u32(kFnvOffset, partition.num_classes);
    for (const auto class_id : partition.class_of) {
        hash = fnv_u32(hash, class_id);
    }
    return hash;
}

std::string hash_hex(const std::uint64_t value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%016llx",
                  static_cast<unsigned long long>(value));
    return buffer;
}

// ---------------------------------------------------------------------------
// Experiment driver
// ---------------------------------------------------------------------------

namespace {

// Canonically renumbered projection of `partition` onto a subset of universe
// indices (e.g. the constituent sub-universe).
std::vector<std::uint32_t> project_classes(const std::vector<std::uint32_t>& class_of,
                                           const std::vector<std::size_t>& subset) {
    std::vector<std::uint32_t> projected;
    projected.reserve(subset.size());
    std::unordered_map<std::uint32_t, std::uint32_t> renumber;
    for (const auto index : subset) {
        const auto old_id = class_of[index];
        const auto found = renumber.find(old_id);
        if (found != renumber.end()) {
            projected.push_back(found->second);
        } else {
            const auto id = static_cast<std::uint32_t>(renumber.size());
            renumber.emplace(old_id, id);
            projected.push_back(id);
        }
    }
    return projected;
}

std::uint64_t class_vector_hash(const std::vector<std::uint32_t>& class_of) {
    Partition partition;
    partition.class_of = class_of;
    std::uint32_t max_id = 0;
    for (const auto id : class_of) {
        max_id = std::max(max_id, id + 1);
    }
    partition.num_classes = max_id;
    return partition_hash(partition);
}

}  // namespace

void run_oracle_experiment(const OracleExperimentConfig& config,
                           const std::filesystem::path& output_dir) {
    std::filesystem::create_directories(output_dir);
    std::ofstream category_csv(output_dir / "category_recovery.csv");
    std::ofstream composition_csv(output_dir / "composition_recovery.csv");
    std::ofstream positive_csv(output_dir / "positive_only_recovery.csv");
    std::ofstream summary(output_dir / "oracle_summary.txt");
    if (!category_csv || !composition_csv || !positive_csv || !summary) {
        throw std::runtime_error("cannot open output files in " + output_dir.string());
    }

    category_csv << "grammar,L,k,scope,universe_size,num_learned_classes,num_gold_classes,"
                    "ARI,NMI,pairwise_precision,pairwise_recall,merge_errors,split_errors,"
                    "merge_errors_excl_obs_equiv,observationally_equivalent_gold_category_groups,"
                    "refined_from_previous_k,partition_hash\n";
    composition_csv << "grammar,L,k,num_learned_classes,composition_triples,labeled_triples,"
                       "composition_precision,composition_recall,functional_input_pairs,"
                       "nonfunctional_input_pairs,congruence_violations,witnessable_gold_rules,"
                       "recovered_gold_rules\n";
    positive_csv << "grammar,L,k,coverage,seed,corpus_max_len,accepted_count,retained_count,"
                    "num_learned_classes,oracle_num_classes,ARI_vs_oracle,NMI_vs_oracle,"
                    "pairwise_precision_vs_oracle,pairwise_recall_vs_oracle,matches_oracle,"
                    "partition_hash\n";

    const auto grammars = config.grammars.empty() ? oracle_grammar_names() : config.grammars;
    for (const auto& grammar_name : grammars) {
        const OracleGrammar grammar = make_oracle_grammar(grammar_name);
        const CategoryTable table(grammar, config.max_len + config.max_k);
        const SignatureHits hits = compute_signature_hits(table, config.max_len, config.max_k);
        const std::vector<std::uint64_t> no_filter;
        const auto oracle_parts = refine_partitions(hits, no_filter);
        const GoldLabeling gold = gold_labeling(table, config.max_len);

        summary << "== grammar " << grammar.name << " ==\n";
        summary << "categories:";
        for (const auto& category : grammar.categories) {
            summary << " " << category;
        }
        summary << "\nvocabulary:";
        for (const auto& token : grammar.vocabulary) {
            summary << " " << token;
        }
        summary << "\nlexicon:\n";
        for (std::size_t t = 0; t < grammar.vocabulary.size(); ++t) {
            summary << "  " << grammar.vocabulary[t] << " ->";
            for (const auto category : grammar.lexicon[t]) {
                summary << " " << grammar.categories[category];
            }
            summary << "\n";
        }
        summary << "composition:";
        for (const auto& rule : grammar.composition) {
            summary << " (" << grammar.categories[rule.left] << ","
                    << grammar.categories[rule.right] << "->"
                    << grammar.categories[rule.result] << ")";
        }
        summary << "\naccepting:";
        for (const auto category : grammar.accepting) {
            summary << " " << grammar.categories[category];
        }
        std::uint64_t accepted_total = 0;
        for (std::size_t i = 0; i < table.space().size(); ++i) {
            if (table.accept_at(i)) {
                ++accepted_total;
            }
        }
        summary << "\naccepted strings with length <= " << (config.max_len + config.max_k)
                << ": " << accepted_total << "\n";

        for (std::size_t max_len = config.min_len; max_len <= config.max_len; ++max_len) {
            const std::size_t universe_size = hits.universe.start[max_len + 1];
            std::vector<Partition> parts;
            parts.reserve(config.max_k + 1);
            for (std::size_t k = 0; k <= config.max_k; ++k) {
                parts.push_back(restrict_partition(oracle_parts[k], universe_size));
            }
            std::size_t first_stable_k = config.max_k;
            while (first_stable_k > 0 &&
                   parts[first_stable_k - 1] == parts[config.max_k]) {
                --first_stable_k;
            }
            summary << "L=" << max_len << ": universe=" << universe_size
                    << " first_stable_k=" << first_stable_k
                    << " final_classes=" << parts[config.max_k].num_classes << "\n";

            struct Scope {
                std::string name;
                std::vector<std::size_t> subset;
            };
            std::vector<Scope> scopes(2);
            scopes[0].name = "full";
            scopes[1].name = "constituents";
            for (std::size_t i = 0; i < universe_size; ++i) {
                scopes[0].subset.push_back(i);
                if (gold.mask_of[i] != 0) {
                    scopes[1].subset.push_back(i);
                }
            }

            for (const auto& scope : scopes) {
                const auto gold_projected = project_classes(gold.class_of, scope.subset);
                const auto final_learned =
                    project_classes(parts[config.max_k].class_of, scope.subset);
                const auto obs_equiv =
                    find_observationally_equivalent_gold_classes(final_learned, gold_projected);
                if (max_len == config.max_len && !obs_equiv.groups.empty()) {
                    summary << "observationally_equivalent_gold_categories (L=" << max_len
                            << ", k=" << config.max_k << ", scope=" << scope.name << "):";
                    // Projected gold ids are canonical over the subset; map
                    // back to masks via first member with that projected id.
                    for (const auto& group : obs_equiv.groups) {
                        summary << " [";
                        bool first = true;
                        for (const auto gold_id : group) {
                            for (std::size_t j = 0; j < scope.subset.size(); ++j) {
                                if (gold_projected[j] == gold_id) {
                                    if (!first) {
                                        summary << " ~ ";
                                    }
                                    summary << mask_name(gold.mask_of[scope.subset[j]], grammar);
                                    first = false;
                                    break;
                                }
                            }
                        }
                        summary << "]";
                    }
                    summary << "\n";
                }
                std::size_t previous_classes = 1;
                for (std::size_t k = 0; k <= config.max_k; ++k) {
                    const auto learned = project_classes(parts[k].class_of, scope.subset);
                    const auto metrics = compare_partitions(learned, gold_projected);
                    const bool refined = metrics.left_classes != previous_classes;
                    previous_classes = metrics.left_classes;
                    category_csv << grammar.name << "," << max_len << "," << k << ","
                                 << scope.name << "," << scope.subset.size() << ","
                                 << metrics.left_classes << "," << metrics.right_classes << ","
                                 << format_double(metrics.ari) << ","
                                 << format_double(metrics.nmi) << ","
                                 << format_double(metrics.pairwise_precision) << ","
                                 << format_double(metrics.pairwise_recall) << ","
                                 << metrics.merge_error_pairs << ","
                                 << metrics.split_error_pairs << ","
                                 << (metrics.merge_error_pairs - obs_equiv.excluded_merge_pairs)
                                 << "," << obs_equiv.groups.size() << ","
                                 << (refined ? 1 : 0) << ","
                                 << hash_hex(class_vector_hash(learned)) << "\n";
                }
            }

            for (std::size_t k = 0; k <= config.max_k; ++k) {
                const auto recovery = recover_composition(table, grammar, parts[k], max_len);
                composition_csv << grammar.name << "," << max_len << "," << k << ","
                                << parts[k].num_classes << "," << recovery.composition_triples
                                << "," << recovery.labeled_triples << ","
                                << format_double(recovery.composition_precision) << ","
                                << format_double(recovery.composition_recall) << ","
                                << recovery.functional_input_pairs << ","
                                << recovery.nonfunctional_input_pairs << ","
                                << recovery.congruence_violations << ","
                                << recovery.witnessable_gold_rules << ","
                                << recovery.recovered_gold_rules << "\n";
                if (max_len == config.max_len && recovery.first_violation_example.has_value() &&
                    (k == 0 || k == config.max_k)) {
                    summary << "congruence violation example (L=" << max_len << ", k=" << k
                            << "): " << *recovery.first_violation_example << "\n";
                }
                if (max_len == config.max_len && k == config.max_k) {
                    for (const auto& example : recovery.incorrect_triple_examples) {
                        summary << "labeled triple outside gold Comp (L=" << max_len
                                << ", k=" << k << "): " << example << "\n";
                    }
                }
            }
        }

        // Learned class inventory and lexicon at (max_len, max_k).
        {
            const auto& final_part = oracle_parts[config.max_k];
            const std::size_t universe_size = hits.universe.size();
            std::vector<std::vector<std::size_t>> members(final_part.num_classes);
            for (std::size_t i = 0; i < universe_size; ++i) {
                members[final_part.class_of[i]].push_back(i);
            }
            std::size_t mixing = 0;
            for (const auto& class_members : members) {
                bool has_constituent = false;
                bool has_non_constituent = false;
                for (const auto index : class_members) {
                    (gold.mask_of[index] != 0 ? has_constituent : has_non_constituent) = true;
                }
                if (has_constituent && has_non_constituent) {
                    ++mixing;
                }
            }
            summary << "classes mixing constituents and non-constituents (L="
                    << config.max_len << ", k=" << config.max_k << "): " << mixing << "\n";
            summary << "learned class inventory (L=" << config.max_len << ", k=" << config.max_k
                    << ", " << final_part.num_classes << " classes";
            const std::size_t listed = std::min<std::size_t>(members.size(), 60);
            if (listed < members.size()) {
                summary << ", first " << listed << " shown";
            }
            summary << "):\n";
            for (std::size_t c = 0; c < listed; ++c) {
                summary << "  class " << c << " size=" << members[c].size() << " gold={";
                std::vector<CategoryMask> masks;
                for (const auto index : members[c]) {
                    if (std::find(masks.begin(), masks.end(), gold.mask_of[index]) ==
                        masks.end()) {
                        masks.push_back(gold.mask_of[index]);
                    }
                }
                for (std::size_t m = 0; m < masks.size(); ++m) {
                    summary << (m == 0 ? "" : " ") << mask_name(masks[m], grammar);
                }
                summary << "} e.g.";
                for (std::size_t m = 0; m < std::min<std::size_t>(members[c].size(), 5); ++m) {
                    summary << " \"" << string_text(hits.universe, members[c][m], grammar)
                            << "\"";
                }
                summary << "\n";
            }
            summary << "lexicon recovery (L=" << config.max_len << ", k=" << config.max_k
                    << "):\n";
            for (std::size_t t = 0; t < grammar.vocabulary.size(); ++t) {
                summary << "  " << grammar.vocabulary[t] << " -> class "
                        << final_part.class_of[hits.universe.index(1, t)] << " gold="
                        << mask_name(gold.mask_of[hits.universe.index(1, t)], grammar) << "\n";
            }
        }

        // Positive-only ablation at L = config.max_len.
        for (const auto coverage : config.coverages) {
            for (const auto seed : config.seeds) {
                const auto sample = sample_positive(table, coverage, seed);
                const auto positive_parts = refine_partitions(hits, sample.filter);
                for (std::size_t k = 0; k <= config.max_k; ++k) {
                    const auto metrics = compare_partitions(positive_parts[k].class_of,
                                                            oracle_parts[k].class_of);
                    positive_csv << grammar.name << "," << config.max_len << "," << k << ","
                                 << format_double(coverage) << "," << seed << ","
                                 << (config.max_len + config.max_k) << ","
                                 << sample.accepted_count << "," << sample.retained_count << ","
                                 << positive_parts[k].num_classes << ","
                                 << oracle_parts[k].num_classes << ","
                                 << format_double(metrics.ari) << ","
                                 << format_double(metrics.nmi) << ","
                                 << format_double(metrics.pairwise_precision) << ","
                                 << format_double(metrics.pairwise_recall) << ","
                                 << (positive_parts[k] == oracle_parts[k] ? 1 : 0) << ","
                                 << hash_hex(partition_hash(positive_parts[k])) << "\n";
                }
            }
        }
        summary << "\n";
    }
}

}  // namespace scf::v2
