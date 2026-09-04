#include "scf/closed_world.hpp"
#include "scf/platform.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <functional>
#include <iomanip>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace scf::v24 {
namespace {

std::uint64_t choose2(const std::uint64_t value) {
    return value < 2 ? 0 : value * (value - 1) / 2;
}

double share(const std::uint64_t part, const std::uint64_t whole) {
    return whole == 0 ? 0.0 : static_cast<double>(part) / static_cast<double>(whole);
}

std::string csv_double(const double value) {
    if (value < 0.0) {
        return "-1";
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(6) << value;
    return out.str();
}

double percentile(const std::vector<std::uint32_t>& sorted, const double p) {
    if (sorted.empty()) {
        return 0.0;
    }
    const auto index = std::min(sorted.size() - 1,
                                static_cast<std::size_t>(p * (sorted.size() - 1) + 0.5));
    return static_cast<double>(sorted[index]);
}

std::string join_tokens(std::span<const std::uint32_t> tokens,
                        const std::vector<std::string>& token_text) {
    std::string out;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (i != 0) {
            out += ' ';
        }
        out += token_text.at(tokens[i]);
    }
    return out;
}

// splitmix64 finalizer: token ids -> well-mixed 64-bit values.
std::uint64_t mix64(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

struct ContextHashKey {
    std::uint64_t left_hash{};
    std::uint64_t right_hash{};
    std::uint32_t left_length{};
    std::uint32_t right_length{};
    bool operator==(const ContextHashKey&) const = default;
};

struct ContextHashKeyHasher {
    std::size_t operator()(const ContextHashKey& key) const noexcept {
        return static_cast<std::size_t>(
            mix64(key.left_hash ^ mix64(key.right_hash + key.left_length) ^
                  (static_cast<std::uint64_t>(key.right_length) << 40U)));
    }
};

FrameType classify(const std::uint32_t left_length, const std::uint32_t right_length) {
    if (left_length == 0 && right_length == 0) {
        return FrameType::empty_frame;
    }
    if (left_length == 0) {
        return FrameType::left_boundary;
    }
    if (right_length == 0) {
        return FrameType::right_boundary;
    }
    return FrameType::internal_frame;
}

// Sorted class sizes -> summary metrics.
void fill_size_metrics(RefinementMetrics& metrics, std::vector<std::uint32_t> sizes) {
    std::sort(sizes.begin(), sizes.end());
    metrics.final_classes = sizes.size();
    metrics.singleton_classes = 0;
    metrics.nontrivial_classes = 0;
    metrics.objects_in_nontrivial_classes = 0;
    for (const auto size : sizes) {
        if (size == 1) {
            ++metrics.singleton_classes;
        } else {
            ++metrics.nontrivial_classes;
            metrics.objects_in_nontrivial_classes += size;
        }
    }
    metrics.largest_class = sizes.empty() ? 0 : sizes.back();
    metrics.largest_class_ratio = share(metrics.largest_class, metrics.initial_objects);
    metrics.median_class_size = percentile(sizes, 0.5);
    metrics.p95_class_size = percentile(sizes, 0.95);
}

}  // namespace

// ---------------------------------------------------------------------------
// Universe
// ---------------------------------------------------------------------------

ContextUniverse parse_universe(const std::string_view name) {
    for (std::size_t i = 0; i < kUniverseNames.size(); ++i) {
        if (name == kUniverseNames[i]) {
            return static_cast<ContextUniverse>(i);
        }
    }
    if (name == "all") {
        return ContextUniverse::all_frames;
    }
    if (name == "internal") {
        return ContextUniverse::internal_only;
    }
    if (name == "boundary") {
        return ContextUniverse::boundary_frames;
    }
    throw std::runtime_error("unknown context universe: " + std::string(name));
}

bool in_universe(const FrameType frame, const ContextUniverse universe) {
    switch (universe) {
        case ContextUniverse::all_frames:
            return true;
        case ContextUniverse::internal_only:
            return frame == FrameType::internal_frame;
        case ContextUniverse::boundary_frames:
            return frame != FrameType::internal_frame;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Observation table
// ---------------------------------------------------------------------------

std::span<const std::uint32_t> ObservationTable::tokens_of(const ObjectId object) const {
    return {object_tokens.data() + object_offsets.at(object),
            object_tokens.data() + object_offsets.at(object + 1)};
}

std::span<const ContextId> ObservationTable::contexts_of(const ObjectId object) const {
    return {object_contexts.data() + object_context_offsets.at(object),
            object_contexts.data() + object_context_offsets.at(object + 1)};
}

std::span<const ObjectId> ObservationTable::objects_of(const ContextId context) const {
    return {context_objects.data() + context_object_offsets.at(context),
            context_objects.data() + context_object_offsets.at(context + 1)};
}

std::span<const std::uint32_t> ObservationTable::left_of(const ContextId context) const {
    const auto& exemplar = contexts.at(context);
    const auto& sentence = sentences.at(exemplar.sentence);
    return {sentence.data(), sentence.data() + exemplar.begin};
}

std::span<const std::uint32_t> ObservationTable::right_of(const ContextId context) const {
    const auto& exemplar = contexts.at(context);
    const auto& sentence = sentences.at(exemplar.sentence);
    return {sentence.data() + exemplar.end, sentence.data() + sentence.size()};
}

bool ObservationTable::accepts(const ObjectId object, const ContextId context) const {
    const auto contexts_u = contexts_of(object);
    return std::binary_search(contexts_u.begin(), contexts_u.end(), context);
}

std::optional<ObjectId> ObservationTable::find_object(
    const std::span<const std::uint32_t> tokens) const {
    const auto found = object_index_.find(std::vector<std::uint32_t>(tokens.begin(), tokens.end()));
    if (found == object_index_.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::optional<ObjectId> ObservationTable::find_object(const std::string_view text) const {
    std::istringstream words{std::string(text)};
    std::vector<std::uint32_t> tokens;
    std::string word;
    while (words >> word) {
        const auto found = std::find(token_text.begin(), token_text.end(), word);
        if (found == token_text.end()) {
            return std::nullopt;
        }
        tokens.push_back(static_cast<std::uint32_t>(found - token_text.begin()));
    }
    if (tokens.empty()) {
        return std::nullopt;
    }
    return find_object(tokens);
}

std::optional<ContextId> ObservationTable::terminal_context() const {
    for (ContextId context = 0; context < contexts.size(); ++context) {
        if (context_frame[context] == FrameType::empty_frame) {
            return context;
        }
    }
    return std::nullopt;
}

std::string ObservationTable::object_text(const ObjectId object) const {
    return join_tokens(tokens_of(object), token_text);
}

std::string ObservationTable::left_context_text(const ContextId context) const {
    return join_tokens(left_of(context), token_text);
}

std::string ObservationTable::right_context_text(const ContextId context) const {
    return join_tokens(right_of(context), token_text);
}

std::string ObservationTable::frame_text(const ContextId context) const {
    return "L=[" + left_context_text(context) + "] R=[" + right_context_text(context) + "]";
}

ObservationTable build_observation_table(
    const std::vector<std::vector<std::uint32_t>>& sentences,
    const std::vector<std::string>& token_text,
    const std::size_t sentence_limit,
    const std::size_t max_substring_length) {
    if (max_substring_length == 0) {
        throw std::runtime_error("max_substring_length must be positive");
    }
    ObservationTable table;
    table.token_text = token_text;
    table.object_offsets.push_back(0);
    const std::size_t limit = std::min(sentence_limit, sentences.size());
    table.sentences.reserve(limit);

    // Context interning: 128-bit content hash of (L, R) plus lengths, with
    // exact verification against the exemplar so identity is exact (a hash
    // collision falls through to a chained entry, never to a wrong id).
    std::unordered_map<ContextHashKey, ContextId, ContextHashKeyHasher> context_index;
    std::vector<ContextId> next_same_hash;  // per context: chain on collision
    std::vector<std::uint64_t> records;      // (context << 32) | object
    std::vector<std::uint64_t> prefix_hash;
    std::vector<std::uint64_t> suffix_hash;
    std::vector<std::uint32_t> tokens;

    const auto same_sequence = [](std::span<const std::uint32_t> a,
                                  std::span<const std::uint32_t> b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin());
    };

    for (std::size_t sentence_index = 0; sentence_index < limit; ++sentence_index) {
        const auto& source = sentences[sentence_index];
        if (source.empty()) {
            continue;
        }
        const auto local = static_cast<std::uint32_t>(table.sentences.size());
        table.sentences.push_back(source);
        const auto& sentence = table.sentences.back();
        const std::size_t n = sentence.size();
        table.token_count += n;

        prefix_hash.assign(n + 1, 0);
        suffix_hash.assign(n + 1, 0);
        for (std::size_t i = 0; i < n; ++i) {
            prefix_hash[i + 1] = mix64(prefix_hash[i] * 0x100000001b3ULL + sentence[i] + 1);
        }
        for (std::size_t i = n; i-- > 0;) {
            suffix_hash[i] = mix64(suffix_hash[i + 1] * 0x100000001b3ULL + sentence[i] + 1);
        }

        for (std::size_t begin = 0; begin < n; ++begin) {
            const std::size_t max_len = std::min(max_substring_length, n - begin);
            tokens.clear();
            for (std::size_t len = 1; len <= max_len; ++len) {
                tokens.push_back(sentence[begin + len - 1]);
                const std::size_t end = begin + len;

                ObjectId object;
                {
                    const auto found = table.object_index_.find(tokens);
                    if (found != table.object_index_.end()) {
                        object = found->second;
                    } else {
                        object = static_cast<ObjectId>(table.object_offsets.size() - 1);
                        table.object_tokens.insert(table.object_tokens.end(), tokens.begin(),
                                                   tokens.end());
                        table.object_offsets.push_back(
                            static_cast<std::uint32_t>(table.object_tokens.size()));
                        table.object_index_.emplace(tokens, object);
                    }
                }

                const ContextHashKey key{prefix_hash[begin], suffix_hash[end],
                                         static_cast<std::uint32_t>(begin),
                                         static_cast<std::uint32_t>(n - end)};
                const ContextExemplar exemplar{local, static_cast<std::uint32_t>(begin),
                                               static_cast<std::uint32_t>(end)};
                const std::span<const std::uint32_t> left{sentence.data(),
                                                          sentence.data() + begin};
                const std::span<const std::uint32_t> right{sentence.data() + end,
                                                           sentence.data() + n};
                ContextId context;
                auto [slot, inserted] =
                    context_index.try_emplace(key, static_cast<ContextId>(table.contexts.size()));
                if (inserted) {
                    context = slot->second;
                    table.contexts.push_back(exemplar);
                    table.context_frame.push_back(classify(key.left_length, key.right_length));
                    next_same_hash.push_back(context);
                } else {
                    ContextId cursor = slot->second;
                    while (true) {
                        if (same_sequence(table.left_of(cursor), left) &&
                            same_sequence(table.right_of(cursor), right)) {
                            context = cursor;
                            break;
                        }
                        if (next_same_hash[cursor] == cursor) {
                            // genuine 128-bit collision: chain a new context
                            context = static_cast<ContextId>(table.contexts.size());
                            table.contexts.push_back(exemplar);
                            table.context_frame.push_back(
                                classify(key.left_length, key.right_length));
                            next_same_hash.push_back(context);
                            next_same_hash[cursor] = context;
                            break;
                        }
                        cursor = next_same_hash[cursor];
                    }
                }
                records.push_back((static_cast<std::uint64_t>(context) << 32U) | object);
            }
        }
    }
    std::sort(records.begin(), records.end());
    records.erase(std::unique(records.begin(), records.end()), records.end());

    const std::size_t objects = table.object_count();
    const std::size_t contexts = table.contexts.size();
    table.context_object_offsets.assign(contexts + 1, 0);
    table.object_context_offsets.assign(objects + 1, 0);
    for (const auto record : records) {
        ++table.context_object_offsets[(record >> 32U) + 1];
        ++table.object_context_offsets[(record & 0xffffffffULL) + 1];
    }
    for (std::size_t i = 0; i < contexts; ++i) {
        table.context_object_offsets[i + 1] += table.context_object_offsets[i];
    }
    for (std::size_t i = 0; i < objects; ++i) {
        table.object_context_offsets[i + 1] += table.object_context_offsets[i];
    }
    table.context_objects.resize(records.size());
    table.object_contexts.resize(records.size());
    {
        std::vector<std::uint64_t> fill_context(table.context_object_offsets.begin(),
                                                table.context_object_offsets.end() - 1);
        std::vector<std::uint64_t> fill_object(table.object_context_offsets.begin(),
                                               table.object_context_offsets.end() - 1);
        // records are sorted by (context, object): objects_of(c) come out
        // sorted; contexts_of(u) come out sorted because contexts increase.
        for (const auto record : records) {
            const auto context = static_cast<ContextId>(record >> 32U);
            const auto object = static_cast<ObjectId>(record & 0xffffffffULL);
            table.context_objects[fill_context[context]++] = object;
            table.object_contexts[fill_object[object]++] = context;
        }
    }
    return table;
}

ObservationTable table_from_lines(const std::vector<std::string>& lines,
                                  const std::size_t max_substring_length) {
    std::vector<std::string> text;
    std::unordered_map<std::string, std::uint32_t> ids;
    std::vector<std::vector<std::uint32_t>> sentences;
    for (const auto& line : lines) {
        std::istringstream words(line);
        std::vector<std::uint32_t> sentence;
        std::string word;
        while (words >> word) {
            const auto [it, inserted] = ids.try_emplace(word, static_cast<std::uint32_t>(text.size()));
            if (inserted) {
                text.push_back(word);
            }
            sentence.push_back(it->second);
        }
        sentences.push_back(std::move(sentence));
    }
    return build_observation_table(sentences, text, sentences.size(), max_substring_length);
}

// ---------------------------------------------------------------------------
// Refiner
// ---------------------------------------------------------------------------

Refiner::Refiner(const ObservationTable& table, const ContextUniverse universe)
    : table_(table), universe_(universe) {
    const auto count = static_cast<ObjectId>(table_.object_count());
    block_of_.assign(count, 0);
    position_.resize(count);
    members_.resize(count == 0 ? 0 : 1);
    if (count != 0) {
        members_[0].resize(count);
        std::iota(members_[0].begin(), members_[0].end(), 0U);
        std::iota(position_.begin(), position_.end(), 0U);
    }
}

void Refiner::run() {
    if (ran_) {
        return;
    }
    ran_ = true;
    const auto start = std::chrono::steady_clock::now();
    metrics_.initial_objects = table_.object_count();

    // Splitter order: descending positive degree, then ascending id.
    std::vector<ContextId> order;
    for (ContextId context = 0; context < table_.context_count(); ++context) {
        if (in_universe(table_.context_frame[context], universe_)) {
            order.push_back(context);
        }
    }
    std::stable_sort(order.begin(), order.end(), [&](const ContextId a, const ContextId b) {
        return table_.objects_of(a).size() > table_.objects_of(b).size();
    });
    metrics_.universe_contexts = order.size();

    std::vector<std::uint8_t> effective(table_.context_count(), 0);
    std::vector<std::uint32_t> count;       // per block: members positive for c
    std::vector<std::uint32_t> count_stamp;
    std::vector<ObjectId> target;           // per block: its B1 block for c
    std::vector<std::uint32_t> target_stamp;
    std::vector<ObjectId> touched;
    std::uint32_t stamp = 0;
    const auto ensure_block_arrays = [&]() {
        count.resize(members_.size(), 0);
        count_stamp.resize(members_.size(), 0);
        target.resize(members_.size(), 0);
        target_stamp.resize(members_.size(), 0);
    };

    for (std::uint32_t round = 1;; ++round) {
        std::uint64_t splits_this_round = 0;
        for (const ContextId context : order) {
            ++stamp;
            ensure_block_arrays();
            const auto positives = table_.objects_of(context);
            metrics_.membership_queries += positives.size();
            touched.clear();
            for (const ObjectId object : positives) {
                const ObjectId block = block_of_[object];
                if (members_[block].size() < 2) {
                    continue;
                }
                if (count_stamp[block] != stamp) {
                    count_stamp[block] = stamp;
                    count[block] = 0;
                    touched.push_back(block);
                }
                ++count[block];
            }
            metrics_.context_tests += touched.size();
            if (touched.empty()) {
                continue;
            }
            // Decide per touched block BEFORE moving anything: a block whose
            // every member accepts c is not distinguished by c (unmark it).
            bool split_any = false;
            for (const ObjectId block : touched) {
                if (count[block] == members_[block].size()) {
                    count_stamp[block] = 0;
                } else {
                    split_any = true;
                }
            }
            if (!split_any) {
                continue;
            }
            for (const ObjectId object : positives) {
                const ObjectId block = block_of_[object];
                if (count_stamp[block] != stamp) {
                    continue;
                }
                if (target_stamp[block] != stamp) {
                    target_stamp[block] = stamp;
                    target[block] = static_cast<ObjectId>(members_.size());
                    // block ids and split records advance in lockstep:
                    // block k (k >= 1) was created by splits_[k - 1].
                    splits_.push_back({context, round,
                                       static_cast<std::uint32_t>(members_[block].size()),
                                       count[block], object, 0});
                    members_.emplace_back();
                    ensure_block_arrays();
                }
                const ObjectId new_block = target[block];
                // swap-remove `object` from its old block
                auto& old_members = members_[block];
                const std::uint32_t index = position_[object];
                const ObjectId last = old_members.back();
                old_members[index] = last;
                position_[last] = index;
                old_members.pop_back();
                position_[object] = static_cast<std::uint32_t>(members_[new_block].size());
                members_[new_block].push_back(object);
                block_of_[object] = new_block;
            }
            for (const ObjectId block : touched) {
                if (target_stamp[block] == stamp) {
                    ++splits_this_round;
                    ++metrics_.block_splits;
                    splits_[target[block] - 1].out_member = members_[block].front();
                }
            }
            if (!effective[context]) {
                effective[context] = 1;
                ++metrics_.effective_splitters;
            }
        }
        metrics_.refinement_rounds = round;
        if (splits_this_round == 0) {
            break;
        }
    }

    std::vector<std::uint32_t> sizes;
    sizes.reserve(members_.size());
    for (const auto& block : members_) {
        sizes.push_back(static_cast<std::uint32_t>(block.size()));
    }
    fill_size_metrics(metrics_, std::move(sizes));
    metrics_.runtime_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

std::vector<std::vector<ObjectId>> Refiner::classes() const {
    std::vector<std::vector<ObjectId>> result = members_;
    for (auto& cls : result) {
        std::sort(cls.begin(), cls.end());
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.size() > b.size() || (a.size() == b.size() && a.front() < b.front());
    });
    return result;
}

// ---------------------------------------------------------------------------
// Brute-force references
// ---------------------------------------------------------------------------

namespace {

std::vector<ObjectId> canonical_labels(const std::vector<ObjectId>& labels) {
    std::unordered_map<ObjectId, ObjectId> relabel;
    std::vector<ObjectId> result(labels.size());
    for (std::size_t i = 0; i < labels.size(); ++i) {
        const auto [it, inserted] =
            relabel.try_emplace(labels[i], static_cast<ObjectId>(relabel.size()));
        result[i] = it->second;
    }
    return result;
}

}  // namespace

std::vector<ObjectId> signature_partition(const ObservationTable& table,
                                          const ContextUniverse universe) {
    const std::size_t objects = table.object_count();
    std::vector<std::vector<ContextId>> signatures(objects);
    for (ObjectId object = 0; object < objects; ++object) {
        for (const ContextId context : table.contexts_of(object)) {
            if (in_universe(table.context_frame[context], universe)) {
                signatures[object].push_back(context);
            }
        }
    }
    std::vector<ObjectId> order(objects);
    std::iota(order.begin(), order.end(), 0U);
    std::sort(order.begin(), order.end(), [&](const ObjectId a, const ObjectId b) {
        return signatures[a] < signatures[b] || (signatures[a] == signatures[b] && a < b);
    });
    std::vector<ObjectId> labels(objects);
    ObjectId label = 0;
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (i != 0 && signatures[order[i]] != signatures[order[i - 1]]) {
            ++label;
        }
        labels[order[i]] = label;
    }
    return labels;
}

std::vector<ObjectId> signature_partition_dense(const ObservationTable& table,
                                                const ContextUniverse universe) {
    std::vector<ContextId> universe_contexts;
    for (ContextId context = 0; context < table.context_count(); ++context) {
        if (in_universe(table.context_frame[context], universe)) {
            universe_contexts.push_back(context);
        }
    }
    const std::size_t objects = table.object_count();
    if (static_cast<std::uint64_t>(objects) * universe_contexts.size() > (1ULL << 31U)) {
        throw std::runtime_error("dense signature enumeration is limited to small tables");
    }
    const std::size_t words = (universe_contexts.size() + 63) / 64;
    std::map<std::vector<std::uint64_t>, ObjectId> classes;
    std::vector<ObjectId> labels(objects);
    for (ObjectId object = 0; object < objects; ++object) {
        std::vector<std::uint64_t> bits(words, 0);
        for (std::size_t i = 0; i < universe_contexts.size(); ++i) {
            if (table.accepts(object, universe_contexts[i])) {  // the literal test
                bits[i / 64] |= 1ULL << (i % 64);
            }
        }
        const auto [it, inserted] = classes.try_emplace(bits, static_cast<ObjectId>(classes.size()));
        labels[object] = it->second;
    }
    return labels;
}

bool same_partition(const std::vector<ObjectId>& first, const std::vector<ObjectId>& second) {
    return first.size() == second.size() && canonical_labels(first) == canonical_labels(second);
}

std::uint64_t class_count(const std::vector<ObjectId>& labels) {
    std::vector<ObjectId> sorted = labels;
    std::sort(sorted.begin(), sorted.end());
    return static_cast<std::uint64_t>(std::unique(sorted.begin(), sorted.end()) - sorted.begin());
}

std::optional<Distinction> distinguishing_context(const ObservationTable& table,
                                                  const ContextUniverse universe,
                                                  const ObjectId first,
                                                  const ObjectId second) {
    const auto a = table.contexts_of(first);
    const auto b = table.contexts_of(second);
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < a.size() || j < b.size()) {
        ContextId context;
        bool in_a;
        bool in_b;
        if (j >= b.size() || (i < a.size() && a[i] < b[j])) {
            context = a[i++];
            in_a = true;
            in_b = false;
        } else if (i >= a.size() || b[j] < a[i]) {
            context = b[j++];
            in_a = false;
            in_b = true;
        } else {
            ++i;
            ++j;
            continue;
        }
        if (in_universe(table.context_frame[context], universe)) {
            return Distinction{context, in_a, in_b};
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

TerminalDiagnostics terminal_diagnostics(const ObservationTable& table, const Refiner& refiner) {
    TerminalDiagnostics result;
    const auto terminal = table.terminal_context();
    std::unordered_map<ObjectId, std::uint64_t> per_class;
    std::unordered_map<ObjectId, std::uint64_t> class_size;
    std::unordered_map<ObjectId, std::uint64_t> class_signature_size;
    std::optional<ObjectId> empty_signature_class;
    for (ObjectId object = 0; object < table.object_count(); ++object) {
        const ObjectId cls = refiner.class_of(object);
        ++class_size[cls];
        std::uint64_t universe_contexts = 0;
        bool has_terminal = false;
        for (const ContextId context : table.contexts_of(object)) {
            if (in_universe(table.context_frame[context], refiner.universe())) {
                ++universe_contexts;
            }
            if (terminal && context == *terminal) {
                has_terminal = true;
            }
        }
        class_signature_size[cls] = universe_contexts;  // identical within a class
        if (universe_contexts == 1) {
            ++result.single_context_objects;
        }
        if (universe_contexts == 0) {
            ++result.empty_signature_objects;
            empty_signature_class = cls;
        }
        if (has_terminal) {
            ++result.terminal_objects;
            ++per_class[refiner.class_of(object)];
            if (universe_contexts == 1 &&
                in_universe(FrameType::empty_frame, refiner.universe())) {
                ++result.terminal_only_objects;
            }
        }
    }
    result.terminal_classes = per_class.size();
    for (const auto& [cls, count] : per_class) {
        static_cast<void>(cls);
        result.largest_terminal_class = std::max(result.largest_terminal_class, count);
    }
    for (const auto& [cls, size] : class_size) {
        if (empty_signature_class && cls == *empty_signature_class) {
            continue;
        }
        result.largest_class_excluding_empty_signature =
            std::max(result.largest_class_excluding_empty_signature, size);
        if (size < 2) {
            continue;
        }
        if (class_signature_size[cls] == 1) {
            ++result.nontrivial_classes_single_context;
        } else if (class_signature_size[cls] >= 2) {
            ++result.nontrivial_classes_multi_context;
            result.objects_in_multi_context_classes += size;
            result.largest_multi_context_class =
                std::max(result.largest_multi_context_class, size);
        }
    }
    return result;
}

PartitionChange compare_partitions(const ObservationTable& previous_table,
                                   const std::vector<ObjectId>& previous_labels,
                                   const ObservationTable& current_table,
                                   const std::vector<ObjectId>& current_labels) {
    std::unordered_map<std::string, ObjectId> current_by_text;
    current_by_text.reserve(current_table.object_count());
    for (ObjectId object = 0; object < current_table.object_count(); ++object) {
        current_by_text.emplace(current_table.object_text(object), object);
    }
    std::unordered_map<ObjectId, std::uint64_t> previous_counts;
    std::unordered_map<ObjectId, std::uint64_t> current_counts;
    std::map<std::pair<ObjectId, ObjectId>, std::uint64_t> joint_counts;
    PartitionChange result;
    for (ObjectId object = 0; object < previous_table.object_count(); ++object) {
        const auto found = current_by_text.find(previous_table.object_text(object));
        if (found == current_by_text.end()) {
            continue;
        }
        const ObjectId p = previous_labels[object];
        const ObjectId c = current_labels[found->second];
        ++previous_counts[p];
        ++current_counts[c];
        ++joint_counts[{p, c}];
        ++result.common_objects;
    }
    std::uint64_t same_previous = 0;
    std::uint64_t same_current = 0;
    std::uint64_t same_both = 0;
    for (const auto& [key, count] : previous_counts) {
        static_cast<void>(key);
        same_previous += choose2(count);
    }
    for (const auto& [key, count] : current_counts) {
        static_cast<void>(key);
        same_current += choose2(count);
    }
    for (const auto& [key, count] : joint_counts) {
        static_cast<void>(key);
        same_both += choose2(count);
    }
    result.pairs_split = same_previous - same_both;
    result.pairs_merged = same_current - same_both;
    result.changed_pairs = result.pairs_split + result.pairs_merged;
    const std::uint64_t all_pairs = choose2(result.common_objects);
    if (all_pairs > 0) {
        result.changed_pair_share =
            static_cast<double>(result.changed_pairs) / static_cast<double>(all_pairs);
    }
    return result;
}

PosTable load_pos_table(const std::filesystem::path& path) {
    PosTable result;
    if (path.empty()) {
        return result;
    }
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open UD file: " + path.string());
    }
    std::map<std::string, std::map<std::string, std::uint64_t>> votes;
    std::string line;
    while (std::getline(input, line)) {
        scf::platform::strip_trailing_cr(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        std::istringstream row(line);
        std::string id, form, lemma, upos;
        std::getline(row, id, '\t');
        std::getline(row, form, '\t');
        std::getline(row, lemma, '\t');
        std::getline(row, upos, '\t');
        if (id.find('-') != std::string::npos || id.find('.') != std::string::npos ||
            upos.empty() || upos == "_") {
            continue;
        }
        std::transform(form.begin(), form.end(), form.begin(), [](const unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        ++votes[form][upos];
    }
    for (const auto& [token, labels] : votes) {
        const auto best = std::max_element(
            labels.begin(), labels.end(), [](const auto& a, const auto& b) {
                return a.second < b.second || (a.second == b.second && b.first < a.first);
            });
        result.label.emplace(token, best->first);
    }
    return result;
}

PosDiagnostics evaluate_pos(const ObservationTable& table,
                            const std::vector<std::vector<ObjectId>>& classes,
                            const PosTable& pos) {
    PosDiagnostics result;
    if (pos.label.empty()) {
        return result;
    }
    for (const auto& cls : classes) {
        std::map<std::string, std::uint64_t> labels;
        for (const ObjectId object : cls) {
            if (table.tokens_of(object).size() != 1) {
                continue;
            }
            const auto found = pos.label.find(table.object_text(object));
            if (found != pos.label.end()) {
                ++labels[found->second];
                ++result.labeled_objects;
            }
        }
        std::uint64_t labeled = 0;
        std::uint64_t largest = 0;
        for (const auto& [label, count] : labels) {
            static_cast<void>(label);
            labeled += count;
            largest = std::max(largest, count);
            result.within_class_same_pos_pairs += choose2(count);
        }
        result.within_class_labeled += largest;
        result.within_class_labeled_pairs += choose2(labeled);
    }
    if (result.labeled_objects > 0) {
        result.within_class_purity = static_cast<double>(result.within_class_labeled) /
                                     static_cast<double>(result.labeled_objects);
    }
    if (result.within_class_labeled_pairs > 0) {
        result.pairwise_same_pos_precision =
            static_cast<double>(result.within_class_same_pos_pairs) /
            static_cast<double>(result.within_class_labeled_pairs);
    }
    return result;
}

V23Partition run_v23_merger(const std::vector<std::vector<std::uint32_t>>& sentences,
                            const std::vector<std::string>& token_text,
                            const std::size_t sentence_limit,
                            const std::size_t max_substring_length,
                            const ObservationTable& table) {
    V23Partition result;
    const auto start = std::chrono::steady_clock::now();
    const v23::ObservedDataset data =
        v23::observe_sentences(sentences, token_text, sentence_limit, max_substring_length);
    v23::ConservativeMerger merger(data);
    merger.run();
    result.classes = merger.metrics().resulting_classes;
    result.largest_class = merger.metrics().largest_class;

    // v23 object -> v24 object (same substrings of the same sentences).
    std::vector<ObjectId> translate(data.object_tokens.size());
    for (v23::ObjectId object = 0; object < data.object_tokens.size(); ++object) {
        const auto found = table.find_object(data.object_tokens[object]);
        if (!found) {
            throw std::runtime_error("v2.3 object missing from the v2.4 table: " +
                                     data.object_text[object]);
        }
        translate[object] = *found;
    }
    if (data.object_tokens.size() != table.object_count()) {
        throw std::runtime_error("v2.3 and v2.4 object inventories differ");
    }
    result.labels.resize(table.object_count());
    for (v23::ObjectId object = 0; object < data.object_tokens.size(); ++object) {
        result.labels[translate[object]] = translate[merger.class_of(object)];
    }
    for (const auto& record : merger.accepted()) {
        result.accepted.push_back(
            {translate[record.first], translate[record.second],
             static_cast<std::uint8_t>(v231::classify_context(data, record.context))});
    }
    result.runtime_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    return result;
}

V23Comparison compare_with_v23(const V23Partition& partition, const Refiner& refiner) {
    V23Comparison result;
    result.ran = true;
    result.v23_classes = partition.classes;
    result.v23_largest_class = partition.largest_class;
    result.v23_runtime_seconds = partition.runtime_seconds;
    std::unordered_map<ObjectId, std::uint64_t> v23_counts;
    std::unordered_map<ObjectId, std::uint64_t> v24_counts;
    std::map<std::pair<ObjectId, ObjectId>, std::uint64_t> joint;
    for (ObjectId object = 0; object < partition.labels.size(); ++object) {
        const ObjectId a = partition.labels[object];
        const ObjectId b = refiner.class_of(object);
        ++v23_counts[a];
        ++v24_counts[b];
        ++joint[{a, b}];
    }
    std::uint64_t same_both = 0;
    for (const auto& [key, count] : joint) {
        static_cast<void>(key);
        same_both += choose2(count);
    }
    for (const auto& [key, count] : v23_counts) {
        static_cast<void>(key);
        result.v23_same_class_pairs += choose2(count);
    }
    for (const auto& [key, count] : v24_counts) {
        static_cast<void>(key);
        result.v24_same_class_pairs += choose2(count);
    }
    result.v23_pairs_separated_by_v24 = result.v23_same_class_pairs - same_both;
    result.v24_pairs_separated_by_v23 = result.v24_same_class_pairs - same_both;
    for (const auto& merge : partition.accepted) {
        ++result.accepted_merges;
        ++result.accepted_by_frame[merge.frame];
        if (!refiner.same_class(merge.first, merge.second)) {
            ++result.accepted_merges_separated;
            ++result.accepted_separated_by_frame[merge.frame];
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Synthetic oracle cases
// ---------------------------------------------------------------------------

namespace {

struct OracleReport {
    std::ostringstream out;
    std::size_t failures{};

    void check(const std::string& what, const bool ok) {
        out << "  " << what << ": " << (ok ? "PASS" : "FAIL") << '\n';
        if (!ok) {
            ++failures;
        }
    }
};

ObjectId must_find(const ObservationTable& table, const std::string& text) {
    const auto found = table.find_object(text);
    if (!found) {
        throw std::runtime_error("oracle case: missing object " + text);
    }
    return *found;
}

void describe_distinction(std::ostream& out,
                          const ObservationTable& table,
                          const ContextUniverse universe,
                          const std::string& u,
                          const std::string& v) {
    const auto a = must_find(table, u);
    const auto b = must_find(table, v);
    const auto d = distinguishing_context(table, universe, a, b);
    if (!d) {
        out << "    " << u << " vs " << v << ": no distinguishing context\n";
        return;
    }
    out << "    " << u << " vs " << v << ": " << table.frame_text(d->context) << "  Accept(L "
        << u << " R)=" << d->accepts_first << "  Accept(L " << v << " R)=" << d->accepts_second
        << '\n';
}

std::vector<std::string> pseudo_random_corpus(const std::uint64_t seed, const std::size_t count) {
    // A deterministic small language with reusable slots so that both
    // indistinguishable and distinguishable pairs occur.
    const std::vector<std::string> dets{"the", "a", "every"};
    const std::vector<std::string> nouns{"dog", "cat", "bird", "fish"};
    const std::vector<std::string> verbs{"sleeps", "runs", "sings"};
    const std::vector<std::string> adverbs{"", "quietly", "now"};
    std::uint64_t state = seed;
    const auto next = [&](const std::size_t modulus) {
        state = mix64(state + 0x9e3779b97f4a7c15ULL);
        return static_cast<std::size_t>(state % modulus);
    };
    std::vector<std::string> lines;
    for (std::size_t i = 0; i < count; ++i) {
        std::string line = dets[next(dets.size())] + " " + nouns[next(nouns.size())] + " " +
                           verbs[next(verbs.size())];
        const auto& adverb = adverbs[next(adverbs.size())];
        if (!adverb.empty()) {
            line += " " + adverb;
        }
        if (next(4) == 0) {
            line = nouns[next(nouns.size())];  // a bare complete span
        }
        lines.push_back(line);
    }
    return lines;
}

}  // namespace

std::string run_oracle_cases(const std::filesystem::path& output_dir) {
    OracleReport report;
    auto& out = report.out;
    out << "# SCF v2.4 synthetic oracle cases (closed-world refinement)\n";

    // 1. dog/cat behave identically on every bounded context -> same class.
    {
        const auto table = table_from_lines({"the dog sleeps", "the cat sleeps", "a dog runs",
                                             "a cat runs", "i see the dog", "i see the cat",
                                             "dog", "cat"});
        out << "\ncase 1: dog/cat identical in every context\n";
        for (std::size_t u = 0; u < kUniverseNames.size(); ++u) {
            Refiner refiner(table, static_cast<ContextUniverse>(u));
            refiner.run();
            report.check(std::string(kUniverseNames[u]) + " dog == cat",
                         refiner.same_class(must_find(table, "dog"), must_find(table, "cat")));
        }
        Refiner all(table, ContextUniverse::all_frames);
        all.run();
        report.check("all_frames 'the dog' == 'the cat'",
                     all.same_class(must_find(table, "the dog"), must_find(table, "the cat")));
        report.check("all_frames dog != sleeps",
                     !all.same_class(must_find(table, "dog"), must_find(table, "sleeps")));
    }

    // 2. mary/swimming share a positive context but a (1,0) context exists.
    {
        const auto table = table_from_lines({"mary is fun", "swimming is fun", "mary runs",
                                             "john runs", "john is fun"});
        out << "\ncase 2: mary/swimming share (eps, is fun) but differ on (eps, runs)\n";
        Refiner refiner(table, ContextUniverse::all_frames);
        refiner.run();
        const auto mary = must_find(table, "mary");
        const auto swimming = must_find(table, "swimming");
        const auto mary_john = distinguishing_context(table, ContextUniverse::all_frames, mary,
                                                      must_find(table, "john"));
        report.check("mary and john are indistinguishable", !mary_john.has_value());
        report.check("shared positive context exists",
                     table.accepts(mary, table.contexts_of(swimming).front()));
        report.check("all_frames mary != swimming", !refiner.same_class(mary, swimming));
        const auto d = distinguishing_context(table, ContextUniverse::all_frames, mary, swimming);
        report.check("a (1,0) distinguishing context is reported",
                     d && d->accepts_first != d->accepts_second);
        describe_distinction(out, table, ContextUniverse::all_frames, "mary", "swimming");
        report.check("all_frames mary == john", refiner.same_class(mary, must_find(table, "john")));
    }

    // 3. two complete sentences (eps,eps)=1 with different behaviour elsewhere.
    {
        const auto table = table_from_lines({"introduction", "<num>", "conclusions",
                                             "the introduction is long", "<num> mice ran",
                                             "see section <num>"});
        out << "\ncase 3: complete spans share (eps,eps) only -> not merged by the terminal test\n";
        Refiner all(table, ContextUniverse::all_frames);
        all.run();
        const auto intro = must_find(table, "introduction");
        const auto num = must_find(table, "<num>");
        const auto concl = must_find(table, "conclusions");
        report.check("all_frames introduction != <num>", !all.same_class(intro, num));
        report.check("all_frames conclusions != <num>", !all.same_class(concl, num));
        report.check("all_frames conclusions != introduction", !all.same_class(concl, intro));
        describe_distinction(out, table, ContextUniverse::all_frames, "introduction", "<num>");
        describe_distinction(out, table, ContextUniverse::all_frames, "conclusions", "<num>");
        describe_distinction(out, table, ContextUniverse::all_frames, "conclusions",
                             "introduction");
        // (eps,eps) alone never produces a pairwise clique: the terminal
        // objects end in three different classes although all three accept it.
        const auto terminal = table.terminal_context();
        report.check("all three accept (eps,eps)",
                     terminal && table.accepts(intro, *terminal) &&
                         table.accepts(num, *terminal) && table.accepts(concl, *terminal));
        Refiner boundary(table, ContextUniverse::boundary_frames);
        boundary.run();
        report.check("boundary_frames introduction != <num>", !boundary.same_class(intro, num));
        report.check("boundary_frames conclusions == introduction (no other boundary frame)",
                     boundary.same_class(concl, intro));
    }

    // 4. observationally indistinguishable pair -> must merge (all universes).
    {
        const auto table = table_from_lines({"x alpha y", "x beta y", "p alpha", "p beta",
                                             "alpha q", "beta q"});
        out << "\ncase 4: observationally indistinguishable pair must merge\n";
        for (std::size_t u = 0; u < kUniverseNames.size(); ++u) {
            Refiner refiner(table, static_cast<ContextUniverse>(u));
            refiner.run();
            report.check(std::string(kUniverseNames[u]) + " alpha == beta",
                         refiner.same_class(must_find(table, "alpha"), must_find(table, "beta")));
            report.check(std::string(kUniverseNames[u]) + " 'x alpha' == 'x beta'",
                         refiner.same_class(must_find(table, "x alpha"),
                                            must_find(table, "x beta")));
        }
    }

    // 5. D_small subset D_large: closed-world false negative repaired by data.
    {
        const std::vector<std::string> small{"the dog sleeps", "the cat sleeps", "a dog runs"};
        std::vector<std::string> large = small;
        large.push_back("a cat runs");
        const auto small_table = table_from_lines(small);
        const auto large_table = table_from_lines(large);
        out << "\ncase 5: D_small splits dog/cat by a missing positive; D_large repairs it\n";
        Refiner small_ref(small_table, ContextUniverse::all_frames);
        small_ref.run();
        Refiner large_ref(large_table, ContextUniverse::all_frames);
        large_ref.run();
        report.check("small: dog != cat (closed-world negative)",
                     !small_ref.same_class(must_find(small_table, "dog"),
                                           must_find(small_table, "cat")));
        describe_distinction(out, small_table, ContextUniverse::all_frames, "dog", "cat");
        report.check("large: dog == cat",
                     large_ref.same_class(must_find(large_table, "dog"),
                                          must_find(large_table, "cat")));
        const auto change = compare_partitions(small_table, small_ref.labels(), large_table,
                                               large_ref.labels());
        out << "    common objects " << change.common_objects << ", pairs merged at the larger scale "
            << change.pairs_merged << ", pairs split " << change.pairs_split << '\n';
        report.check("partition change reports the repaired pair", change.pairs_merged >= 1);
        report.check("no previously equal pair became unequal", change.pairs_split == 0);
    }

    // 6. optimized refinement == brute-force dense signature partition.
    {
        out << "\ncase 6: refinement vs brute-force full signature enumeration\n";
        for (const std::uint64_t seed : {1ULL, 7ULL, 42ULL}) {
            const auto table = table_from_lines(pseudo_random_corpus(seed, 60));
            for (std::size_t u = 0; u < kUniverseNames.size(); ++u) {
                const auto universe = static_cast<ContextUniverse>(u);
                Refiner refiner(table, universe);
                refiner.run();
                const auto dense = signature_partition_dense(table, universe);
                const auto sparse = signature_partition(table, universe);
                const bool ok_dense = same_partition(refiner.labels(), dense);
                const bool ok_sparse = same_partition(refiner.labels(), sparse);
                std::ostringstream label;
                label << "seed " << seed << " " << kUniverseNames[u] << " (objects "
                      << table.object_count() << ", universe contexts "
                      << refiner.metrics().universe_contexts << ", classes "
                      << refiner.metrics().final_classes << ", dense classes "
                      << class_count(dense) << ")";
                report.check(label.str() + " dense identical", ok_dense);
                report.check(label.str() + " sparse identical", ok_sparse);
            }
        }
    }

    out << "\nsummary: " << (report.failures == 0 ? "all checks passed" : "FAILURES PRESENT")
        << " (failures=" << report.failures << ")\n";
    if (!output_dir.empty()) {
        std::filesystem::create_directories(output_dir);
        std::ofstream file(output_dir / "oracle_comparison.txt");
        file << out.str();
    }
    return out.str();
}

// ---------------------------------------------------------------------------
// Ladder driver
// ---------------------------------------------------------------------------

std::vector<std::pair<std::string, std::string>> default_probe_pairs() {
    return {{"<num>", "conclusions"},
            {"<num>", "introduction"},
            {"introduction", "conclusions"},
            {"introduction", "discussion"},
            {"results", "discussion"},
            {"<num>", "the"},
            {"<num>", "and"},
            {"the", "and"},
            {"the", "a"},
            {"<num>", "[ <num> ]"},
            {"<num>", "<num> . <num>"},
            {"materials and methods", "conclusions"},
            {"conflict of interest", "acknowledgments"},
            {"is", "was"},
            {"in", "of"}};
}

namespace {

void write_distinguishing_contexts(std::ostream& out,
                                   const ObservationTable& table,
                                   const Refiner& refiner,
                                   const std::uint64_t scale,
                                   const std::size_t limit,
                                   const std::vector<std::pair<std::string, std::string>>& probes) {
    const auto universe = refiner.universe();
    out << "\n# scale " << scale << " universe " << kUniverseNames[static_cast<std::size_t>(universe)]
        << "\n";
    out << "## first " << limit << " block splits in refinement order\n"
        << "u | v | L | R | Accept(LuR) | Accept(LvR) | block_size | in_size | round\n";
    std::size_t shown = 0;
    for (const auto& split : refiner.splits()) {
        if (shown++ >= limit) {
            break;
        }
        out << table.object_text(split.in_member) << " | " << table.object_text(split.out_member)
            << " | " << table.left_context_text(split.context) << " | "
            << table.right_context_text(split.context) << " | 1 | 0 | " << split.block_size
            << " | " << split.in_size << " | " << split.round << '\n';
    }
    if (shown == 0) {
        out << "(no splits)\n";
    }

    out << "## probe pairs (previously merged through (eps,eps) or boundary frames in v2.3)\n";
    for (const auto& [u, v] : probes) {
        const auto a = table.find_object(u);
        const auto b = table.find_object(v);
        if (!a || !b) {
            out << u << " vs " << v << ": not both observed at this scale\n";
            continue;
        }
        if (refiner.same_class(*a, *b)) {
            out << u << " vs " << v << ": SAME CLASS (identical signature, "
                << std::count_if(table.contexts_of(*a).begin(), table.contexts_of(*a).end(),
                                 [&](const ContextId c) {
                                     return in_universe(table.context_frame[c], universe);
                                 })
                << " universe contexts)\n";
            continue;
        }
        const auto d = distinguishing_context(table, universe, *a, *b);
        if (!d) {
            out << u << " vs " << v << ": different classes without a distinguishing context (bug)\n";
            continue;
        }
        out << u << " vs " << v << ": DIFFERENT | L=[" << table.left_context_text(d->context)
            << "] R=[" << table.right_context_text(d->context) << "] | Accept(L " << u
            << " R)=" << d->accepts_first << " Accept(L " << v << " R)=" << d->accepts_second
            << '\n';
    }

    const auto terminal = table.terminal_context();
    out << "## objects observed as complete spans ((eps,eps) = 1)\n";
    if (!terminal) {
        out << "(none)\n";
        return;
    }
    const auto terminal_objects = table.objects_of(*terminal);
    std::map<ObjectId, std::vector<ObjectId>> by_class;
    for (const ObjectId object : terminal_objects) {
        by_class[refiner.class_of(object)].push_back(object);
    }
    out << "terminal objects " << terminal_objects.size() << " in " << by_class.size()
        << " classes\n";
    shown = 0;
    for (std::size_t i = 0; i + 1 < terminal_objects.size() && shown < limit; ++i, ++shown) {
        const ObjectId u = terminal_objects[i];
        const ObjectId v = terminal_objects[i + 1];
        out << table.object_text(u) << " vs " << table.object_text(v) << ": ";
        if (refiner.same_class(u, v)) {
            out << "SAME CLASS\n";
            continue;
        }
        const auto d = distinguishing_context(table, universe, u, v);
        if (!d) {
            out << "DIFFERENT without a distinguishing context (bug)\n";
            continue;
        }
        out << "DIFFERENT | L=[" << table.left_context_text(d->context) << "] R=["
            << table.right_context_text(d->context) << "] | " << d->accepts_first << " vs "
            << d->accepts_second << '\n';
    }
    out << "largest classes among terminal objects:\n";
    std::vector<std::pair<std::size_t, ObjectId>> ranked;
    for (const auto& [cls, members] : by_class) {
        ranked.emplace_back(members.size(), cls);
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        return a.first > b.first || (a.first == b.first && a.second < b.second);
    });
    for (std::size_t i = 0; i < std::min<std::size_t>(5, ranked.size()); ++i) {
        const auto& members = by_class[ranked[i].second];
        out << "  size " << members.size() << ": ";
        for (std::size_t j = 0; j < std::min<std::size_t>(12, members.size()); ++j) {
            out << (j == 0 ? "" : " | ") << table.object_text(members[j]);
        }
        if (members.size() > 12) {
            out << " | ... (" << members.size() - 12 << " more)";
        }
        out << '\n';
    }
}

void write_class_examples(std::ostream& out,
                          const ObservationTable& table,
                          const Refiner& refiner,
                          const std::vector<std::vector<ObjectId>>& classes,
                          const std::uint64_t scale,
                          const std::size_t class_limit,
                          const PosTable& pos) {
    const auto universe = refiner.universe();
    const auto terminal = table.terminal_context();
    out << "\n# scale " << scale << " universe " << kUniverseNames[static_cast<std::size_t>(universe)]
        << " -- " << class_limit << " largest classes\n";
    for (std::size_t i = 0; i < std::min(class_limit, classes.size()); ++i) {
        const auto& cls = classes[i];
        if (cls.size() < 2) {
            break;
        }
        std::size_t lexical = 0;
        std::size_t terminal_members = 0;
        std::map<std::string, std::size_t> labels;
        for (const ObjectId object : cls) {
            if (table.tokens_of(object).size() == 1) {
                ++lexical;
                const auto found = pos.label.find(table.object_text(object));
                if (found != pos.label.end()) {
                    ++labels[found->second];
                }
            }
            if (terminal && table.accepts(object, *terminal)) {
                ++terminal_members;
            }
        }
        std::vector<ContextId> signature;
        for (const ContextId context : table.contexts_of(cls.front())) {
            if (in_universe(table.context_frame[context], universe)) {
                signature.push_back(context);
            }
        }
        out << "size=" << cls.size() << " lexical_members=" << lexical
            << " members_with_empty_frame=" << terminal_members
            << " shared_signature_contexts=" << signature.size();
        if (!labels.empty()) {
            out << " upos[";
            bool first = true;
            for (const auto& [label, count] : labels) {
                out << (first ? "" : " ") << label << "=" << count;
                first = false;
            }
            out << "]";
        }
        out << "\n  contexts: ";
        for (std::size_t j = 0; j < std::min<std::size_t>(3, signature.size()); ++j) {
            out << (j == 0 ? "" : " ; ") << table.frame_text(signature[j]);
        }
        if (signature.size() > 3) {
            out << " ; ... (" << signature.size() - 3 << " more)";
        }
        if (signature.empty()) {
            out << "(empty signature: no universe context)";
        }
        out << "\n  members: ";
        for (std::size_t j = 0; j < std::min<std::size_t>(40, cls.size()); ++j) {
            out << (j == 0 ? "" : " | ") << table.object_text(cls[j]);
        }
        if (cls.size() > 40) {
            out << " | ... (" << cls.size() - 40 << " more)";
        }
        out << "\n";
    }
    // Classes whose members agree on two or more universe contexts are the
    // only ones that can be called categories by more than one observation.
    out << "## classes with >= 2 shared contexts (first " << class_limit << " by size)\n";
    std::size_t multi_shown = 0;
    for (const auto& cls : classes) {
        if (cls.size() < 2) {
            break;
        }
        std::vector<ContextId> signature;
        for (const ContextId context : table.contexts_of(cls.front())) {
            if (in_universe(table.context_frame[context], universe)) {
                signature.push_back(context);
            }
        }
        if (signature.size() < 2) {
            continue;
        }
        if (multi_shown++ >= class_limit) {
            break;
        }
        out << "size=" << cls.size() << " shared_signature_contexts=" << signature.size()
            << "\n  members: ";
        for (std::size_t j = 0; j < std::min<std::size_t>(20, cls.size()); ++j) {
            out << (j == 0 ? "" : " | ") << table.object_text(cls[j]);
        }
        out << "\n  contexts: ";
        for (std::size_t j = 0; j < std::min<std::size_t>(4, signature.size()); ++j) {
            out << (j == 0 ? "" : " ; ") << table.frame_text(signature[j]);
        }
        if (signature.size() > 4) {
            out << " ; ... (" << signature.size() - 4 << " more)";
        }
        out << "\n";
    }
    if (multi_shown == 0) {
        out << "(none)\n";
    }
    std::map<std::size_t, std::size_t> histogram;
    for (const auto& cls : classes) {
        ++histogram[cls.size()];
    }
    out << "class size histogram (size: classes): ";
    std::size_t shown = 0;
    for (const auto& [size, count] : histogram) {
        if (shown++ >= 12) {
            out << "...";
            break;
        }
        out << size << ":" << count << " ";
    }
    out << "\n";
}

}  // namespace

ClosedWorldResult run_closed_world_scaling(const ClosedWorldConfig& config) {
    if (config.input.empty()) {
        throw std::runtime_error("input is required");
    }
    auto scales = config.scales;
    std::sort(scales.begin(), scales.end());
    scales.erase(std::unique(scales.begin(), scales.end()), scales.end());
    if (scales.empty()) {
        throw std::runtime_error("no scales requested");
    }
    if (config.universes.empty()) {
        throw std::runtime_error("no context universe requested");
    }
    std::filesystem::create_directories(config.output_dir);
    const std::uint64_t read_limit =
        static_cast<std::uint64_t>(static_cast<double>(scales.back()) * 1.25) + 2'000'000;
    const v231::SentenceCorpus corpus = v231::load_structured_corpus(config.input, read_limit);
    while (!scales.empty() && v231::prefix_sentences(corpus, scales.back()) == 0) {
        scales.pop_back();
    }
    if (scales.empty()) {
        throw std::runtime_error("corpus smaller than the smallest requested scale");
    }
    const PosTable pos = load_pos_table(config.ud_conllu);
    const auto probes = config.probe_pairs.empty() ? default_probe_pairs() : config.probe_pairs;

    std::ofstream csv(config.output_dir / "closed_world_scaling.csv");
    csv << "corpus,universe,nominal_tokens,actual_tokens,sentences,documents,initial_objects,"
           "observed_contexts,positive_records,universe_contexts,context_tests,"
           "effective_splitters,block_splits,refinement_rounds,membership_queries,"
           "final_classes,singleton_classes,nontrivial_classes,objects_in_nontrivial_classes,"
           "largest_class,largest_class_ratio,median_class_size,p95_class_size,"
           "terminal_objects,terminal_classes,largest_terminal_class,terminal_only_objects,"
           "empty_signature_objects,largest_class_excluding_empty_signature,"
           "single_context_objects,nontrivial_classes_single_context,"
           "nontrivial_classes_multi_context,objects_in_multi_context_classes,"
           "largest_multi_context_class,common_objects_prev,changed_pairs_prev,changed_pair_share_prev,"
           "pairs_split_prev,pairs_merged_prev,pos_labeled_objects,within_class_pos_purity,"
           "within_class_labeled_pairs,pairwise_same_pos_precision,oracle_identical,"
           "oracle_classes,v23_ran,v23_classes,v23_largest_class,v23_same_class_pairs,"
           "v23_pairs_separated_by_v24,v24_same_class_pairs,v24_pairs_separated_by_v23,"
           "v23_accepted_merges,v23_accepted_separated,v23_empty_frame_accepted,"
           "v23_empty_frame_accepted_separated,v23_internal_accepted,"
           "v23_internal_accepted_separated,table_build_seconds,refinement_seconds,"
           "v23_runtime_seconds,peak_rss_mb\n";
    std::ofstream contexts_file(config.output_dir / "distinguishing_contexts.txt");
    std::ofstream classes_file(config.output_dir / "class_examples.txt");
    std::ofstream oracle_file(config.output_dir / "oracle_comparison.txt");
    contexts_file << "# SCF v2.4 distinguishing contexts (" << config.corpus_label << ")\n";
    classes_file << "# SCF v2.4 class examples (" << config.corpus_label << ")\n";
    oracle_file << run_oracle_cases("") << "\n# real corpus: optimized refinement vs sparse full-"
                                          "signature partition ("
                << config.corpus_label << ")\n";

    ClosedWorldResult result;
    result.available_sentences = corpus.sentences.size();
    result.available_tokens = corpus.cumulative_actual.empty() ? 0 : corpus.cumulative_actual.back();

    // The previous scale's table is shared by all universes; only the labels
    // differ per universe.
    std::unique_ptr<ObservationTable> previous_table;
    std::map<ContextUniverse, std::vector<ObjectId>> previous_labels;

    for (const std::uint64_t scale : scales) {
        const std::size_t sentence_limit = v231::prefix_sentences(corpus, scale);
        const auto build_start = std::chrono::steady_clock::now();
        auto table = std::make_unique<ObservationTable>(build_observation_table(
            corpus.sentences, corpus.token_text, sentence_limit, config.max_substring_length));
        const double build_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - build_start).count();
        // The v2.3 merger (comparison baseline) is run once per scale and
        // compared against every universe's refinement.
        std::optional<V23Partition> v23_partition;
        if (config.compare_v23_max_scale != 0 && scale <= config.compare_v23_max_scale) {
            v23_partition = run_v23_merger(corpus.sentences, corpus.token_text, sentence_limit,
                                           config.max_substring_length, *table);
        }

        for (const ContextUniverse universe : config.universes) {
            Refiner refiner(*table, universe);
            refiner.run();
            const auto classes = refiner.classes();

            ClosedWorldScaleResult row;
            row.universe = universe;
            row.nominal_tokens = scale;
            row.actual_tokens = corpus.cumulative_actual[sentence_limit - 1];
            row.sentences = sentence_limit;
            row.documents = static_cast<std::uint64_t>(corpus.sentence_document[sentence_limit - 1]) + 1;
            row.contexts_total = table->context_count();
            row.records = table->record_count();
            row.table_build_seconds = build_seconds;
            row.metrics = refiner.metrics();
            row.terminal = terminal_diagnostics(*table, refiner);
            row.pos = evaluate_pos(*table, classes, pos);
            const auto prev = previous_labels.find(universe);
            if (prev != previous_labels.end() && previous_table) {
                row.change =
                    compare_partitions(*previous_table, prev->second, *table, refiner.labels());
            }
            if (config.oracle_check) {
                const auto reference = signature_partition(*table, universe);
                row.oracle_identical = same_partition(refiner.labels(), reference) ? 1 : 0;
                row.oracle_classes = class_count(reference);
                oracle_file << "scale " << scale << " " << kUniverseNames[static_cast<std::size_t>(universe)]
                            << ": refinement classes " << row.metrics.final_classes
                            << ", signature classes " << row.oracle_classes << ", identical: "
                            << (row.oracle_identical == 1 ? "yes" : "NO") << '\n';
            }
            if (v23_partition) {
                row.v23 = compare_with_v23(*v23_partition, refiner);
            }
            row.peak_rss_mb = scf::platform::peak_rss_mb();
            result.rows.push_back(row);

            const auto& m = row.metrics;
            const auto& t = row.terminal;
            const auto& v = row.v23;
            csv << config.corpus_label << ',' << kUniverseNames[static_cast<std::size_t>(universe)]
                << ',' << row.nominal_tokens << ',' << row.actual_tokens << ',' << row.sentences
                << ',' << row.documents << ',' << m.initial_objects << ',' << row.contexts_total
                << ',' << row.records << ',' << m.universe_contexts << ',' << m.context_tests
                << ',' << m.effective_splitters << ',' << m.block_splits << ','
                << m.refinement_rounds << ',' << m.membership_queries << ',' << m.final_classes
                << ',' << m.singleton_classes << ',' << m.nontrivial_classes << ','
                << m.objects_in_nontrivial_classes << ',' << m.largest_class << ','
                << csv_double(m.largest_class_ratio) << ',' << m.median_class_size << ','
                << m.p95_class_size << ',' << t.terminal_objects << ',' << t.terminal_classes
                << ',' << t.largest_terminal_class << ',' << t.terminal_only_objects << ','
                << t.empty_signature_objects << ',' << t.largest_class_excluding_empty_signature
                << ',' << t.single_context_objects << ',' << t.nontrivial_classes_single_context
                << ',' << t.nontrivial_classes_multi_context << ','
                << t.objects_in_multi_context_classes << ',' << t.largest_multi_context_class
                << ',' << row.change.common_objects << ',' << row.change.changed_pairs << ','
                << csv_double(row.change.changed_pair_share) << ',' << row.change.pairs_split
                << ',' << row.change.pairs_merged << ',' << row.pos.labeled_objects << ','
                << csv_double(row.pos.within_class_purity) << ','
                << row.pos.within_class_labeled_pairs << ','
                << csv_double(row.pos.pairwise_same_pos_precision) << ',' << row.oracle_identical
                << ',' << row.oracle_classes << ',' << (v.ran ? 1 : 0) << ',' << v.v23_classes
                << ',' << v.v23_largest_class << ',' << v.v23_same_class_pairs << ','
                << v.v23_pairs_separated_by_v24 << ',' << v.v24_same_class_pairs << ','
                << v.v24_pairs_separated_by_v23 << ',' << v.accepted_merges << ','
                << v.accepted_merges_separated << ',' << v.accepted_by_frame[0] << ','
                << v.accepted_separated_by_frame[0] << ',' << v.accepted_by_frame[3] << ','
                << v.accepted_separated_by_frame[3] << ',' << csv_double(row.table_build_seconds)
                << ',' << csv_double(m.runtime_seconds) << ','
                << csv_double(v.v23_runtime_seconds) << ',' << csv_double(row.peak_rss_mb) << '\n';
            csv.flush();

            write_distinguishing_contexts(contexts_file, *table, refiner, scale,
                                          config.example_limit, probes);
            write_class_examples(classes_file, *table, refiner, classes, scale,
                                 config.largest_classes, pos);
            contexts_file.flush();
            classes_file.flush();
            oracle_file.flush();
            previous_labels[universe] = refiner.labels();
        }
        previous_table = std::move(table);
    }
    return result;
}

}  // namespace scf::v24
