#include "scf/conservative_merging.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace scf::v23 {
namespace {

std::uint64_t pair_key(ObjectId first, ObjectId second) {
    if (second < first) {
        std::swap(first, second);
    }
    return (static_cast<std::uint64_t>(first) << 32U) | second;
}

std::uint64_t choose2(const std::uint64_t value) {
    return value < 2 ? 0 : value * (value - 1) / 2;
}

std::string join_tokens(const std::vector<std::uint32_t>& tokens,
                        const std::vector<std::string>& token_text) {
    std::ostringstream out;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (i != 0) {
            out << ' ';
        }
        out << token_text.at(tokens[i]);
    }
    return out.str();
}

class TrieBuilder {
public:
    TrieBuilder() { nodes_.push_back({0, 0}); }

    std::uint32_t step(const std::uint32_t parent, const std::uint32_t token) {
        const auto key = std::make_pair(parent, token);
        const auto found = transitions_.find(key);
        if (found != transitions_.end()) {
            return found->second;
        }
        const auto id = static_cast<std::uint32_t>(nodes_.size());
        nodes_.push_back({parent, token});
        transitions_.emplace(key, id);
        return id;
    }

    std::vector<TrieNode> take_nodes() { return std::move(nodes_); }

private:
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> transitions_;
    std::vector<TrieNode> nodes_;
};

std::string trie_text(const std::vector<TrieNode>& nodes,
                      std::uint32_t node,
                      const std::vector<std::string>& token_text,
                      const bool reverse) {
    std::vector<std::uint32_t> tokens;
    while (node != 0) {
        tokens.push_back(nodes.at(node).token);
        node = nodes.at(node).parent;
    }
    if (reverse) {
        std::reverse(tokens.begin(), tokens.end());
    }
    return join_tokens(tokens, token_text);
}

double percentile(const std::vector<std::size_t>& sorted, const double p) {
    if (sorted.empty()) {
        return 0.0;
    }
    const auto index = std::min(sorted.size() - 1,
                                static_cast<std::size_t>(p * (sorted.size() - 1) + 0.5));
    return static_cast<double>(sorted[index]);
}

std::string csv_double(const double value) {
    if (value < 0.0) {
        return "-1";
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(6) << value;
    return out.str();
}

struct PosTable {
    std::map<std::string, std::string> label;
};

PosTable load_pos(const std::filesystem::path& path) {
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

}  // namespace

std::string ObservedDataset::left_context_text(const ContextId id) const {
    return trie_text(left_trie, context_keys.at(id).left, token_text, true);
}

std::string ObservedDataset::right_context_text(const ContextId id) const {
    return trie_text(right_trie, context_keys.at(id).right, token_text, false);
}

ObservedDataset observe_sentences(
    const std::vector<std::vector<std::uint32_t>>& sentences,
    const std::vector<std::string>& token_text,
    const std::size_t sentence_limit,
    const std::size_t max_substring_length) {
    if (max_substring_length == 0) {
        throw std::runtime_error("max_substring_length must be positive");
    }
    ObservedDataset data;
    data.token_text = token_text;
    TrieBuilder left_trie;
    TrieBuilder right_trie;
    std::map<std::vector<std::uint32_t>, ObjectId> object_ids;
    std::map<ContextKey, ContextId> context_ids;
    std::vector<std::pair<ContextId, ObjectId>> records;
    const auto intern_object = [&](const std::vector<std::uint32_t>& tokens) {
        const auto found = object_ids.find(tokens);
        if (found != object_ids.end()) {
            return found->second;
        }
        const auto id = static_cast<ObjectId>(data.object_tokens.size());
        object_ids.emplace(tokens, id);
        data.object_tokens.push_back(tokens);
        data.object_text.push_back(join_tokens(tokens, token_text));
        return id;
    };
    const auto intern_context = [&](const ContextKey key) {
        const auto found = context_ids.find(key);
        if (found != context_ids.end()) {
            return found->second;
        }
        const auto id = static_cast<ContextId>(data.context_keys.size());
        context_ids.emplace(key, id);
        data.context_keys.push_back(key);
        return id;
    };

    const std::size_t limit = std::min(sentence_limit, sentences.size());
    for (std::size_t sentence_index = 0; sentence_index < limit; ++sentence_index) {
        const auto& sentence = sentences[sentence_index];
        if (sentence.empty()) {
            continue;
        }
        ++data.sentence_count;
        data.token_count += sentence.size();
        std::vector<std::uint32_t> prefix(sentence.size() + 1, 0);
        std::vector<std::uint32_t> suffix(sentence.size() + 1, 0);
        for (std::size_t i = 0; i < sentence.size(); ++i) {
            prefix[i + 1] = left_trie.step(prefix[i], sentence[i]);
        }
        for (std::size_t i = sentence.size(); i-- > 0;) {
            suffix[i] = right_trie.step(suffix[i + 1], sentence[i]);
        }

        std::vector<std::vector<ObjectId>> span_ids(sentence.size());
        for (std::size_t begin = 0; begin < sentence.size(); ++begin) {
            const std::size_t max_len =
                std::min(max_substring_length, sentence.size() - begin);
            span_ids[begin].reserve(max_len);
            std::vector<std::uint32_t> tokens;
            tokens.reserve(max_len);
            for (std::size_t len = 1; len <= max_len; ++len) {
                tokens.push_back(sentence[begin + len - 1]);
                const ObjectId object = intern_object(tokens);
                span_ids[begin].push_back(object);
                const ContextId context =
                    intern_context({prefix[begin], suffix[begin + len]});
                records.emplace_back(context, object);
            }
        }
        for (std::size_t begin = 0; begin < sentence.size(); ++begin) {
            for (std::size_t len = 2; len <= span_ids[begin].size(); ++len) {
                const ObjectId result = span_ids[begin][len - 1];
                for (std::size_t split = 1; split < len; ++split) {
                    const ObjectId left = span_ids[begin][split - 1];
                    const ObjectId right = span_ids[begin + split][len - split - 1];
                    data.compositions.push_back({left, right, result});
                }
            }
        }
    }

    std::sort(records.begin(), records.end());
    records.erase(std::unique(records.begin(), records.end()), records.end());
    data.contexts_of_object.resize(data.object_tokens.size());
    for (const auto& [context, object] : records) {
        data.contexts_of_object[object].push_back(context);
    }
    for (auto& contexts : data.contexts_of_object) {
        std::sort(contexts.begin(), contexts.end());
    }
    for (std::size_t begin = 0; begin < records.size();) {
        std::size_t end = begin + 1;
        while (end < records.size() && records[end].first == records[begin].first) {
            ++end;
        }
        for (std::size_t i = begin; i < end; ++i) {
            for (std::size_t j = i + 1; j < end; ++j) {
                ObjectId first = records[i].second;
                ObjectId second = records[j].second;
                if (second < first) {
                    std::swap(first, second);
                }
                data.witnesses.push_back({first, second, records[begin].first});
            }
        }
        begin = end;
    }
    std::sort(data.witnesses.begin(), data.witnesses.end());
    data.witnesses.erase(std::unique(data.witnesses.begin(), data.witnesses.end()),
                         data.witnesses.end());
    std::sort(data.compositions.begin(), data.compositions.end());
    data.compositions.erase(
        std::unique(data.compositions.begin(), data.compositions.end()),
        data.compositions.end());
    data.left_trie = left_trie.take_nodes();
    data.right_trie = right_trie.take_nodes();
    return data;
}

ConservativeMerger::ConservativeMerger(const ObservedDataset& data) : data_(data) {
    const auto count = data_.object_text.size();
    parent_.resize(count);
    std::iota(parent_.begin(), parent_.end(), 0U);
    size_.assign(count, 1);
    members_.resize(count);
    right_behavior_.resize(count);
    left_behavior_.resize(count);
    witness_neighbors_.resize(count);
    for (ObjectId object = 0; object < count; ++object) {
        members_[object].push_back(object);
    }
    for (const auto& comp : data_.compositions) {
        right_behavior_[comp.left].emplace_back(comp.right, comp.result);
        left_behavior_[comp.right].emplace_back(comp.left, comp.result);
    }
    for (const auto& witness : data_.witnesses) {
        witness_neighbors_[witness.first].push_back(witness.second);
        witness_neighbors_[witness.second].push_back(witness.first);
    }
    for (auto& neighbors : witness_neighbors_) {
        std::sort(neighbors.begin(), neighbors.end());
        neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    }
}

ObjectId ConservativeMerger::find(ObjectId object) const {
    while (parent_[object] != object) {
        object = parent_[object];
    }
    return object;
}

ObjectId ConservativeMerger::class_of(const ObjectId object) const { return find(object); }

bool ConservativeMerger::same_class(const ObjectId first, const ObjectId second) const {
    return find(first) == find(second);
}

void ConservativeMerger::run() {
    if (ran_) {
        return;
    }
    ran_ = true;
    metrics_.initial_objects = data_.object_text.size();
    metrics_.local_witnesses = data_.witnesses.size();

    std::map<std::uint64_t, ContextId> candidate_context;
    for (const auto& witness : data_.witnesses) {
        candidate_context.try_emplace(pair_key(witness.first, witness.second), witness.context);
    }
    metrics_.merge_candidates = candidate_context.size();

    struct Undo {
        ObjectId root{};
        ObjectId child{};
        std::uint32_t root_size{};
        std::size_t root_members{};
    };
    std::vector<Undo> undo;
    const auto rollback = [&](const std::size_t checkpoint) {
        while (undo.size() > checkpoint) {
            const Undo entry = undo.back();
            undo.pop_back();
            parent_[entry.child] = entry.child;
            size_[entry.root] = entry.root_size;
            members_[entry.root].resize(entry.root_members);
        }
    };
    const auto unite = [&](ObjectId first, ObjectId second) {
        first = find(first);
        second = find(second);
        if (first == second) {
            return false;
        }
        if (size_[first] < size_[second]) {
            std::swap(first, second);
        }
        undo.push_back({first, second, size_[first], members_[first].size()});
        parent_[second] = first;
        size_[first] += size_[second];
        members_[first].insert(members_[first].end(), members_[second].begin(),
                               members_[second].end());
        return true;
    };
    const auto class_contexts = [&](const ObjectId root) {
        std::vector<ContextId> contexts;
        for (const ObjectId member : members_[find(root)]) {
            const auto& source = data_.contexts_of_object[member];
            contexts.insert(contexts.end(), source.begin(), source.end());
        }
        std::sort(contexts.begin(), contexts.end());
        contexts.erase(std::unique(contexts.begin(), contexts.end()), contexts.end());
        return contexts;
    };
    const auto indistinguishable = [&](const ObjectId first, const ObjectId second) {
        return class_contexts(first) == class_contexts(second);
    };
    const auto direct_witness_between = [&](const ObjectId first_root,
                                            const ObjectId second_root)
        -> std::optional<std::pair<ObjectId, ObjectId>> {
        const ObjectId first = find(first_root);
        const ObjectId second = find(second_root);
        const auto& scan = members_[first].size() <= members_[second].size()
                               ? members_[first]
                               : members_[second];
        const ObjectId target = &scan == &members_[first] ? second : first;
        for (const ObjectId member : scan) {
            for (const ObjectId neighbor : witness_neighbors_[member]) {
                if (find(neighbor) == target) {
                    return std::make_pair(member, neighbor);
                }
            }
        }
        return std::nullopt;
    };

    struct BehaviorObservation {
        ObjectId source{};
        ObjectId partner{};
        ObjectId output{};
    };
    struct DeferredConflict {
        bool merge_on_left{};
        BehaviorObservation first;
        BehaviorObservation second;
    };

    for (const auto& [packed, context] : candidate_context) {
        const ObjectId candidate_first = static_cast<ObjectId>(packed >> 32U);
        const ObjectId candidate_second = static_cast<ObjectId>(packed & 0xffffffffULL);
        if (same_class(candidate_first, candidate_second)) {
            ++metrics_.redundant_candidates;
            continue;
        }
        const std::size_t checkpoint = undo.size();
        std::vector<std::pair<ObjectId, ObjectId>> queue{{candidate_first, candidate_second}};
        std::size_t cursor = 0;
        std::size_t transaction_unions = 0;
        bool failed = false;
        DeferredConflict failing;
        std::vector<DeferredConflict> deferred;

        const auto compare_behavior = [&](const ObjectId first_root,
                                          const ObjectId second_root,
                                          const bool merge_on_left) {
            std::map<ObjectId, std::vector<BehaviorObservation>> first_by_partner;
            std::map<ObjectId, std::vector<BehaviorObservation>> second_by_partner;
            const auto gather = [&](const ObjectId root, auto& destination) {
                for (const ObjectId member : members_[find(root)]) {
                    const auto& behavior =
                        merge_on_left ? left_behavior_[member] : right_behavior_[member];
                    for (const auto& [partner, output] : behavior) {
                        destination[find(partner)].push_back({member, partner, output});
                    }
                }
            };
            gather(first_root, first_by_partner);
            gather(second_root, second_by_partner);
            for (const auto& [partner, first_observations] : first_by_partner) {
                const auto found = second_by_partner.find(partner);
                if (found == second_by_partner.end()) {
                    continue;  // unobserved behavior is not negative evidence
                }
                for (const auto& first_obs : first_observations) {
                    for (const auto& second_obs : found->second) {
                        if (find(first_obs.output) == find(second_obs.output)) {
                            continue;
                        }
                        const auto witness =
                            direct_witness_between(first_obs.output, second_obs.output);
                        if (witness.has_value()) {
                            queue.push_back(*witness);  // quotient propagation
                        } else {
                            deferred.push_back(
                                {merge_on_left, first_obs, second_obs});
                        }
                    }
                }
            }
        };

        while (!failed) {
            while (cursor < queue.size()) {
                ObjectId first = find(queue[cursor].first);
                ObjectId second = find(queue[cursor].second);
                ++cursor;
                if (first == second) {
                    continue;
                }
                // A first-argument quotient collision: u*x and v*x.
                compare_behavior(first, second, false);
                // A second-argument quotient collision: x*u and x*v.
                compare_behavior(first, second, true);
                if (unite(first, second)) {
                    ++transaction_unions;
                }
            }

            bool added = false;
            for (const auto& item : deferred) {
                if (find(item.first.output) == find(item.second.output) ||
                    indistinguishable(item.first.output, item.second.output)) {
                    continue;  // retain a relational multi-output when unseparated
                }
                const auto witness =
                    direct_witness_between(item.first.output, item.second.output);
                if (witness.has_value()) {
                    queue.push_back(*witness);
                    added = true;
                    break;
                }
                failed = true;
                failing = item;
                break;
            }
            if (failed || !added) {
                break;
            }
        }

        if (failed) {
            rollback(checkpoint);
            ++metrics_.rejected_candidates;
            rejected_.push_back({candidate_first,
                                 candidate_second,
                                 context,
                                 failing.merge_on_left,
                                 failing.first.source,
                                 failing.second.source,
                                 failing.first.partner,
                                 failing.second.partner,
                                 failing.first.output,
                                 failing.second.output});
        } else {
            ++metrics_.accepted_candidates;
            const std::size_t induced = transaction_unions == 0 ? 0 : transaction_unions - 1;
            metrics_.induced_unions += induced;
            accepted_.push_back({candidate_first, candidate_second, context, induced});
        }
    }

    const auto final_classes = classes();
    metrics_.resulting_classes = final_classes.size();
    std::vector<std::size_t> sizes;
    sizes.reserve(final_classes.size());
    for (const auto& cls : final_classes) {
        sizes.push_back(cls.size());
        metrics_.largest_class = std::max<std::uint64_t>(metrics_.largest_class, cls.size());
    }
    std::sort(sizes.begin(), sizes.end());
    metrics_.largest_class_ratio =
        metrics_.initial_objects == 0
            ? 0.0
            : static_cast<double>(metrics_.largest_class) /
                  static_cast<double>(metrics_.initial_objects);
    metrics_.median_class_size = percentile(sizes, 0.5);
    metrics_.p95_class_size = percentile(sizes, 0.95);
}

std::vector<std::vector<ObjectId>> ConservativeMerger::classes() const {
    std::map<ObjectId, std::vector<ObjectId>> grouped;
    for (ObjectId object = 0; object < parent_.size(); ++object) {
        grouped[find(object)].push_back(object);
    }
    std::vector<std::vector<ObjectId>> result;
    result.reserve(grouped.size());
    for (auto& [root, members] : grouped) {
        static_cast<void>(root);
        result.push_back(std::move(members));
    }
    return result;
}

PosDiagnostics evaluate_pos(const ObservedDataset& data,
                            const ConservativeMerger& merger,
                            const std::filesystem::path& ud_conllu) {
    const PosTable table = load_pos(ud_conllu);
    PosDiagnostics result;
    if (table.label.empty()) {
        return result;
    }
    for (const auto& cls : merger.classes()) {
        std::map<std::string, std::uint64_t> labels;
        for (const ObjectId object : cls) {
            if (data.object_tokens[object].size() != 1) {
                continue;
            }
            const auto found = table.label.find(data.object_text[object]);
            if (found != table.label.end()) {
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

PartitionChange compare_partitions(const ObservedDataset& previous_data,
                                   const ConservativeMerger& previous,
                                   const ObservedDataset& current_data,
                                   const ConservativeMerger& current) {
    std::map<std::string, ObjectId> current_by_text;
    for (ObjectId object = 0; object < current_data.object_text.size(); ++object) {
        current_by_text.emplace(current_data.object_text[object], object);
    }
    std::map<ObjectId, std::uint64_t> previous_counts;
    std::map<ObjectId, std::uint64_t> current_counts;
    std::map<std::pair<ObjectId, ObjectId>, std::uint64_t> joint_counts;
    PartitionChange result;
    for (ObjectId object = 0; object < previous_data.object_text.size(); ++object) {
        const auto found = current_by_text.find(previous_data.object_text[object]);
        if (found == current_by_text.end()) {
            continue;
        }
        const ObjectId p = previous.class_of(object);
        const ObjectId c = current.class_of(found->second);
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
    result.changed_pairs = same_previous + same_current - 2 * same_both;
    const std::uint64_t all_pairs = choose2(result.common_objects);
    if (all_pairs > 0) {
        result.changed_pair_share =
            static_cast<double>(result.changed_pairs) / static_cast<double>(all_pairs);
    }
    return result;
}

namespace {

void write_merge_examples(std::ostream& accepted,
                          std::ostream& rejected,
                          const ObservedDataset& data,
                          const ConservativeMerger& merger,
                          const std::uint64_t scale,
                          const std::size_t limit) {
    accepted << "\n# scale " << scale << "\n";
    for (std::size_t i = 0; i < std::min(limit, merger.accepted().size()); ++i) {
        const auto& item = merger.accepted()[i];
        accepted << data.object_text[item.first] << " <=> " << data.object_text[item.second]
                 << " | L=[" << data.left_context_text(item.context) << "] R=["
                 << data.right_context_text(item.context) << "] | induced_unions="
                 << item.induced_unions << "\n";
    }
    rejected << "\n# scale " << scale << "\n";
    for (std::size_t i = 0; i < std::min(limit, merger.rejected().size()); ++i) {
        const auto& item = merger.rejected()[i];
        rejected << "candidate " << data.object_text[item.candidate_first] << " <=> "
                 << data.object_text[item.candidate_second] << " | witness L=["
                 << data.left_context_text(item.candidate_context) << "] R=["
                 << data.right_context_text(item.candidate_context) << "]\n  conflict: ";
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
        rejected << "; outputs have different observed exact-context profiles and no direct "
                    "substitution witness\n";
    }
}

void write_class_examples(std::ostream& output,
                          const ObservedDataset& data,
                          const ConservativeMerger& merger,
                          const std::uint64_t scale) {
    auto classes = merger.classes();
    std::sort(classes.begin(), classes.end(), [](const auto& a, const auto& b) {
        return a.size() > b.size() || (a.size() == b.size() && a.front() < b.front());
    });
    output << "\n# scale " << scale << " -- largest classes\n";
    for (std::size_t i = 0; i < std::min<std::size_t>(20, classes.size()); ++i) {
        output << "size=" << classes[i].size() << ": ";
        for (std::size_t j = 0; j < std::min<std::size_t>(30, classes[i].size()); ++j) {
            if (j != 0) {
                output << " | ";
            }
            output << data.object_text[classes[i][j]];
        }
        output << "\n";
    }
}

}  // namespace

ConservativeScalingResult run_conservative_scaling(const ConservativeScalingConfig& config) {
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
    const std::uint64_t read_limit =
        static_cast<std::uint64_t>(static_cast<double>(scales.back()) * 1.25) + 2'000'000;
    const v21::TokenCorpus corpus = v21::build_token_corpus(config.input_text, read_limit);
    const auto spans = v21::segment_sentences(corpus);
    std::vector<std::vector<std::uint32_t>> sentences;
    std::vector<std::uint64_t> cumulative_a;
    std::vector<std::uint64_t> cumulative_d;
    std::uint64_t count_a = 0;
    std::uint64_t count_d = 0;
    for (const auto& span : spans) {
        std::vector<std::uint32_t> sentence;
        for (std::size_t pos = span.begin; pos < span.end; ++pos) {
            const auto token = corpus.stream[pos];
            if (!v21::is_punctuation_token(corpus.token_text[token])) {
                sentence.push_back(token);
            }
        }
        count_a += span.end - span.begin;
        count_d += sentence.size();
        if (!sentence.empty()) {
            sentences.push_back(std::move(sentence));
            cumulative_a.push_back(count_a);
            cumulative_d.push_back(count_d);
        }
    }
    while (!scales.empty() && (cumulative_a.empty() || scales.back() > cumulative_a.back())) {
        scales.pop_back();
    }
    if (scales.empty()) {
        throw std::runtime_error("corpus smaller than the smallest requested scale");
    }

    std::ofstream csv(config.output_dir / "conservative_scaling.csv");
    csv << "nominal_tokens,actual_condition_d_tokens,initial_objects,local_witnesses,"
           "merge_candidates,accepted_merges,rejected_merges,redundant_candidates,"
           "induced_unions,resulting_eclasses,largest_eclass,largest_eclass_ratio,"
           "median_class_size,p95_class_size,common_objects_prev,changed_pairs_prev,"
           "changed_pair_share_prev,pos_labeled_objects,within_class_pos_purity,"
           "within_class_labeled_pairs,pairwise_same_pos_precision,runtime_seconds\n";
    std::ofstream accepted_file(config.output_dir / "successful_merges.txt");
    std::ofstream rejected_file(config.output_dir / "rejected_merges.txt");
    std::ofstream class_file(config.output_dir / "class_examples.txt");
    accepted_file << "# Direct-witness candidates accepted by transactional closure\n";
    rejected_file << "# Candidates rolled back by exact composition conflicts\n";
    class_file << "# SCF v2.3 learned class examples\n";

    ConservativeScalingResult result;
    result.available_sentences = sentences.size();
    result.available_condition_d_tokens = count_d;
    std::unique_ptr<ObservedDataset> previous_data;
    std::unique_ptr<ConservativeMerger> previous_merger;
    for (const std::uint64_t scale : scales) {
        const auto start = std::chrono::steady_clock::now();
        const auto boundary = std::lower_bound(cumulative_a.begin(), cumulative_a.end(), scale);
        const std::size_t sentence_limit =
            static_cast<std::size_t>(boundary - cumulative_a.begin()) + 1;
        auto data = std::make_unique<ObservedDataset>(observe_sentences(
            sentences, corpus.token_text, sentence_limit, config.max_substring_length));
        auto merger = std::make_unique<ConservativeMerger>(*data);
        merger->run();
        ScaleResult row;
        row.nominal_tokens = scale;
        row.actual_tokens = cumulative_d[sentence_limit - 1];
        row.merge = merger->metrics();
        row.pos = evaluate_pos(*data, *merger, config.ud_conllu);
        if (previous_data) {
            row.change = compare_partitions(*previous_data, *previous_merger, *data, *merger);
        }
        row.runtime_seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        result.scales.push_back(row);

        const auto& m = row.merge;
        csv << row.nominal_tokens << ',' << row.actual_tokens << ',' << m.initial_objects << ','
            << m.local_witnesses << ',' << m.merge_candidates << ','
            << m.accepted_candidates << ',' << m.rejected_candidates << ','
            << m.redundant_candidates << ',' << m.induced_unions << ','
            << m.resulting_classes << ',' << m.largest_class << ','
            << csv_double(m.largest_class_ratio) << ',' << m.median_class_size << ','
            << m.p95_class_size << ',' << row.change.common_objects << ','
            << row.change.changed_pairs << ',' << csv_double(row.change.changed_pair_share)
            << ',' << row.pos.labeled_objects << ','
            << csv_double(row.pos.within_class_purity) << ','
            << row.pos.within_class_labeled_pairs << ','
            << csv_double(row.pos.pairwise_same_pos_precision) << ','
            << csv_double(row.runtime_seconds) << '\n';
        write_merge_examples(accepted_file, rejected_file, *data, *merger, scale,
                             config.example_limit);
        write_class_examples(class_file, *data, *merger, scale);
        previous_data = std::move(data);
        previous_merger = std::move(merger);
    }
    return result;
}

std::string run_conservative_oracle_sanity(const std::filesystem::path& output_dir) {
    std::filesystem::create_directories(output_dir);
    std::vector<std::string> token_text{"<unused>"};
    std::map<std::string, std::uint32_t> token_id;
    const auto sentence = [&](const std::string& text) {
        std::istringstream words(text);
        std::vector<std::uint32_t> result;
        std::string word;
        while (words >> word) {
            const auto [it, inserted] = token_id.try_emplace(
                word, static_cast<std::uint32_t>(token_text.size()));
            if (inserted) {
                token_text.push_back(word);
            }
            result.push_back(it->second);
        }
        return result;
    };
    std::vector<std::vector<std::uint32_t>> sentences;
    for (const char* text : {"x alpha y",       "x beta y",       "the dog sleeps",
                             "the cat sleeps",  "the dog runs",   "the cat runs",
                             "a p z",           "a q z",          "l p t r",
                             "m q t s",         "other rare2 place", "frame rare1 end",
                             "frame rare2 end"}) {
        sentences.push_back(sentence(text));
    }

    const auto early_data =
        observe_sentences(sentences, token_text, sentences.size() - 1, 3);
    ConservativeMerger early(early_data);
    early.run();
    const auto full_data = observe_sentences(sentences, token_text, sentences.size(), 3);
    ConservativeMerger full(full_data);
    full.run();
    const auto object = [](const ObservedDataset& data, const std::string& text) {
        const auto found = std::find(data.object_text.begin(), data.object_text.end(), text);
        if (found == data.object_text.end()) {
            throw std::runtime_error("oracle object missing: " + text);
        }
        return static_cast<ObjectId>(found - data.object_text.begin());
    };
    const bool same_alpha =
        full.same_class(object(full_data, "alpha"), object(full_data, "beta"));
    const bool same_nouns =
        full.same_class(object(full_data, "dog"), object(full_data, "cat"));
    const bool false_friend_separate =
        !full.same_class(object(full_data, "p"), object(full_data, "q"));
    const bool rare_early_separate =
        !early.same_class(object(early_data, "rare1"), object(early_data, "rare2"));
    const bool rare_late_merged =
        full.same_class(object(full_data, "rare1"), object(full_data, "rare2"));

    // External-equivalence oracle over the lexical inventory.  The grammar
    // declares four non-singleton lexical classes; every other token is a
    // singleton.  This is evaluation-only and is never visible to merging.
    std::map<std::string, std::string> oracle_class;
    for (const auto& name : token_text) {
        oracle_class[name] = name;
    }
    for (const char* name : {"dog", "cat"}) {
        oracle_class[name] = "N";
    }
    for (const char* name : {"sleeps", "runs"}) {
        oracle_class[name] = "IV";
    }
    for (const char* name : {"alpha", "beta"}) {
        oracle_class[name] = "OBS_EQ";
    }
    for (const char* name : {"rare1", "rare2"}) {
        oracle_class[name] = "RARE";
    }
    std::vector<ObjectId> lexical;
    for (ObjectId id = 0; id < full_data.object_tokens.size(); ++id) {
        if (full_data.object_tokens[id].size() == 1) {
            lexical.push_back(id);
        }
    }
    std::uint64_t true_positive = 0;
    std::uint64_t false_positive = 0;
    std::uint64_t false_negative = 0;
    for (std::size_t i = 0; i < lexical.size(); ++i) {
        for (std::size_t j = i + 1; j < lexical.size(); ++j) {
            const bool gold_same =
                oracle_class.at(full_data.object_text[lexical[i]]) ==
                oracle_class.at(full_data.object_text[lexical[j]]);
            const bool learned_same = full.same_class(lexical[i], lexical[j]);
            if (gold_same && learned_same) {
                ++true_positive;
            } else if (!gold_same && learned_same) {
                ++false_positive;
            } else if (gold_same && !learned_same) {
                ++false_negative;
            }
        }
    }
    const double oracle_precision =
        true_positive + false_positive == 0
            ? 1.0
            : static_cast<double>(true_positive) /
                  static_cast<double>(true_positive + false_positive);
    const double oracle_recall =
        true_positive + false_negative == 0
            ? 1.0
            : static_cast<double>(true_positive) /
                  static_cast<double>(true_positive + false_negative);

    std::ostringstream report;
    report << "SCF v2.3 synthetic oracle sanity\n"
           << "================================\n"
           << "same-class lexical items dog/cat: " << same_nouns << "\n"
           << "observationally indistinguishable alpha/beta merged: " << same_alpha << "\n"
           << "single-context false friends p/q rejected: " << false_friend_separate << "\n"
           << "low-frequency rare1/rare2 separate before second witness: "
           << rare_early_separate << "\n"
           << "low-frequency rare1/rare2 merged after more data: " << rare_late_merged << "\n"
           << "learned-vs-oracle lexical pair precision: " << oracle_precision << "\n"
           << "learned-vs-oracle lexical pair recall: " << oracle_recall << "\n"
           << "learned-vs-oracle TP/FP/FN: " << true_positive << '/' << false_positive
           << '/' << false_negative << "\n"
           << "objects=" << full.metrics().initial_objects
           << " witnesses=" << full.metrics().local_witnesses
           << " candidates=" << full.metrics().merge_candidates
           << " accepted=" << full.metrics().accepted_candidates
           << " rejected=" << full.metrics().rejected_candidates
           << " classes=" << full.metrics().resulting_classes << "\n";
    if (!(same_alpha && same_nouns && false_friend_separate && rare_early_separate &&
          rare_late_merged)) {
        throw std::runtime_error("conservative oracle sanity check failed");
    }
    std::ofstream output(output_dir / "oracle_sanity.txt");
    output << report.str();
    return report.str();
}

}  // namespace scf::v23
