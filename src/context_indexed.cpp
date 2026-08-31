#include "scf/context_indexed.hpp"

#include "scf/audit.hpp"  // fnv1a

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>
#include <tuple>

namespace scf {
namespace {

constexpr std::uint64_t kSignatureSeparator = 0xFFFFFFFFFFFFFFFFULL;
constexpr std::uint64_t kEpsilonSentinel = 0xFFFFFFFFFFFFFFFEULL;

std::uint64_t pack_key(const ContextKey key) {
    return (static_cast<std::uint64_t>(key.left) << 32) | key.right;
}

double percentile_sorted(const std::vector<std::size_t>& sorted, const double percentile) {
    if (sorted.empty()) {
        return 0.0;
    }
    const auto index = std::min(sorted.size() - 1,
                                static_cast<std::size_t>(percentile * (sorted.size() - 1) + 0.5));
    return static_cast<double>(sorted[index]);
}

}  // namespace

std::string abstraction_signature_name(const AbstractionSignature signature) {
    return signature == AbstractionSignature::ContextOnly ? "context_only"
                                                          : "context_plus_concat";
}

AbstractionSignature parse_abstraction_signature(const std::string& name) {
    if (name == "context_only") {
        return AbstractionSignature::ContextOnly;
    }
    if (name == "context_plus_concat") {
        return AbstractionSignature::ContextPlusConcat;
    }
    throw std::runtime_error("unknown context abstraction signature '" + name + "'");
}

ContextIndexedSolver::ContextIndexedSolver(const Corpus& corpus,
                                           const AbstractionSignature signature)
    : corpus_(corpus), signature_(signature) {}

void ContextIndexedSolver::run() {
    if (ran_) {
        return;
    }
    ran_ = true;
    const auto string_count = corpus_.string_interner().size();
    const auto epsilon = corpus_.string_interner().epsilon_id();
    const auto& records = corpus_.context_records();
    const auto& concats = corpus_.concat_triples();

    // Round 0: A_0(x) = x — every observed string is its own
    // ContextAbstractionClass; epsilon stays a singleton forever.
    class_of_.resize(string_count);
    std::iota(class_of_.begin(), class_of_.end(), 0U);

    std::size_t previous_classes = string_count;
    std::uint64_t previous_relations = 0;
    std::size_t round = 0;
    while (true) {
        // --- build keys, blocks, and stats under A_round ---
        std::vector<std::pair<ContextKey, StringId>> key_yields;
        key_yields.reserve(records.size());
        for (const auto& record : records) {
            key_yields.emplace_back(
                ContextKey{class_of_[record.triple.left], class_of_[record.triple.right]},
                record.triple.yield);
        }
        std::sort(key_yields.begin(), key_yields.end());
        key_yields.erase(std::unique(key_yields.begin(), key_yields.end()), key_yields.end());

        std::vector<LocalRoleBlock> blocks;
        std::uint64_t relation_pairs = 0;
        std::size_t max_block = 0;
        for (std::size_t begin = 0; begin < key_yields.size();) {
            std::size_t end = begin + 1;
            while (end < key_yields.size() && key_yields[end].first == key_yields[begin].first) {
                ++end;
            }
            LocalRoleBlock block;
            block.context = key_yields[begin].first;
            block.yields.reserve(end - begin);
            for (std::size_t index = begin; index < end; ++index) {
                block.yields.push_back(key_yields[index].second);
            }
            const auto size = block.yields.size();
            relation_pairs += static_cast<std::uint64_t>(size) * (size - 1) / 2;
            max_block = std::max(max_block, size);
            blocks.push_back(std::move(block));
            begin = end;
        }

        std::size_t classes = 0;
        std::size_t largest_class = 0;
        {
            std::vector<std::size_t> class_sizes;
            class_sizes.resize(string_count, 0);
            std::size_t distinct = 0;
            for (StringId s = 0; s < string_count; ++s) {
                if (class_sizes[class_of_[s]]++ == 0) {
                    ++distinct;
                }
            }
            classes = distinct;
            for (const auto size : class_sizes) {
                largest_class = std::max(largest_class, size);
            }
        }
        const auto denominator =
            string_count > 1 ? static_cast<double>(string_count - 1) : 1.0;

        ContextIndexedRoundStats stats;
        stats.round = round;
        stats.context_class_count = classes;
        stats.context_key_count = blocks.size();
        stats.local_relation_pair_count = relation_pairs;
        stats.new_context_class_merges =
            round == 0 ? 0 : (previous_classes >= classes ? previous_classes - classes : 0);
        if (round > 0 && classes > previous_classes) {
            // Theory says #classes is non-increasing; a split is a bug signal.
            monotonicity_violated_ = true;
        }
        stats.new_local_relation_pairs =
            round == 0 ? 0
                       : static_cast<std::int64_t>(relation_pairs) -
                             static_cast<std::int64_t>(previous_relations);
        stats.largest_context_class_ratio = static_cast<double>(largest_class) / denominator;
        stats.max_local_block_ratio = static_cast<double>(max_block) / denominator;
        round_stats_.push_back(stats);
        previous_classes = classes;
        previous_relations = relation_pairs;

        // --- next partition: identical complete signature => same class ---
        // profile P_round per string (sorted unique packed keys)
        std::vector<std::vector<std::uint64_t>> signatures(string_count);
        {
            std::vector<std::pair<StringId, std::uint64_t>> yield_key_packed;
            yield_key_packed.reserve(records.size());
            for (const auto& record : records) {
                yield_key_packed.emplace_back(
                    record.triple.yield,
                    pack_key(ContextKey{class_of_[record.triple.left],
                                        class_of_[record.triple.right]}));
            }
            std::sort(yield_key_packed.begin(), yield_key_packed.end());
            yield_key_packed.erase(
                std::unique(yield_key_packed.begin(), yield_key_packed.end()),
                yield_key_packed.end());
            for (const auto& [yield, packed] : yield_key_packed) {
                signatures[yield].push_back(packed);
            }
        }
        if (signature_ == AbstractionSignature::ContextPlusConcat) {
            // Read-only decomposition abstraction D_t(u): never a positive
            // congruence union, only a conservative signature refinement.
            std::vector<std::pair<StringId, std::uint64_t>> decompositions;
            decompositions.reserve(concats.size());
            for (const auto& triple : concats) {
                decompositions.emplace_back(
                    triple.result,
                    pack_key(ContextKey{class_of_[triple.left], class_of_[triple.right]}));
            }
            std::sort(decompositions.begin(), decompositions.end());
            decompositions.erase(std::unique(decompositions.begin(), decompositions.end()),
                                 decompositions.end());
            for (auto& signature : signatures) {
                signature.push_back(kSignatureSeparator);
            }
            for (const auto& [result, packed] : decompositions) {
                signatures[result].push_back(packed);
            }
        }
        signatures[epsilon].assign(1, kEpsilonSentinel);  // epsilon: forever singleton

        std::map<std::vector<std::uint64_t>, ContextClassId> class_ids;
        std::vector<ContextClassId> next(string_count);
        for (StringId s = 0; s < string_count; ++s) {
            const auto entry = class_ids.emplace(
                signatures[s], static_cast<ContextClassId>(class_ids.size()));
            next[s] = entry.first->second;
        }

        if (next == class_of_) {
            // Fixed point: A_{t+1} == A_t by partition content (canonical
            // first-occurrence numbering makes identical partitions identical
            // vectors). Keep this round's blocks as the final state.
            blocks_ = std::move(blocks);
            final_class_count_ = classes;
            productive_rounds_ = round;
            break;
        }
        class_of_ = std::move(next);
        ++round;
    }

    // Final per-yield key lists (multi-role membership).
    {
        std::vector<std::pair<StringId, ContextKey>> pairs;
        for (const auto& block : blocks_) {
            for (const auto yield : block.yields) {
                pairs.emplace_back(yield, block.context);
            }
        }
        std::sort(pairs.begin(), pairs.end());
        yield_keys_.clear();
        for (std::size_t begin = 0; begin < pairs.size();) {
            std::size_t end = begin + 1;
            while (end < pairs.size() && pairs[end].first == pairs[begin].first) {
                ++end;
            }
            std::vector<ContextKey> keys;
            keys.reserve(end - begin);
            for (std::size_t index = begin; index < end; ++index) {
                keys.push_back(pairs[index].second);
            }
            yield_keys_.emplace_back(pairs[begin].first, std::move(keys));
            begin = end;
        }
    }
}

std::optional<ContextKey> ContextIndexedSolver::final_key_for(const StringId left,
                                                              const StringId right) const {
    if (left >= class_of_.size() || right >= class_of_.size()) {
        return std::nullopt;
    }
    return ContextKey{class_of_[left], class_of_[right]};
}

bool ContextIndexedSolver::locally_related(const StringId u,
                                           const StringId v,
                                           const ContextKey key) const {
    if (u == v) {
        return true;
    }
    const auto found = std::lower_bound(
        blocks_.begin(), blocks_.end(), key,
        [](const LocalRoleBlock& block, const ContextKey& target) {
            return block.context < target;
        });
    if (found == blocks_.end() || !(found->context == key)) {
        return false;
    }
    const auto has = [&](const StringId s) {
        return std::binary_search(found->yields.begin(), found->yields.end(), s);
    };
    return has(u) && has(v);
}

bool ContextIndexedSolver::locally_related_any(const StringId u, const StringId v) const {
    if (u == v) {
        return true;
    }
    const auto keys = keys_of_yield(u);
    for (const auto& key : keys) {
        if (locally_related(u, v, key)) {
            return true;
        }
    }
    return false;
}

std::span<const ContextKey> ContextIndexedSolver::keys_of_yield(const StringId u) const {
    const auto found = std::lower_bound(
        yield_keys_.begin(), yield_keys_.end(), u,
        [](const auto& entry, const StringId key) { return entry.first < key; });
    if (found == yield_keys_.end() || found->first != u) {
        return {};
    }
    return found->second;
}

ContextIndexedDiagnostics ContextIndexedSolver::diagnostics() const {
    ContextIndexedDiagnostics diagnostics;
    const auto string_count = corpus_.string_interner().size();
    const auto denominator = string_count > 1 ? static_cast<double>(string_count - 1) : 1.0;
    diagnostics.initial_context_classes = string_count;
    diagnostics.final_context_classes = final_class_count_;
    diagnostics.context_abstraction_collapse_ratio =
        string_count > 0
            ? 1.0 - static_cast<double>(final_class_count_) / static_cast<double>(string_count)
            : 0.0;
    {
        std::vector<std::size_t> sizes(string_count, 0);
        for (StringId s = 0; s < string_count; ++s) {
            ++sizes[class_of_[s]];
        }
        for (const auto size : sizes) {
            diagnostics.largest_context_abstraction_class =
                std::max(diagnostics.largest_context_abstraction_class, size);
        }
    }
    diagnostics.largest_context_abstraction_class_ratio =
        static_cast<double>(diagnostics.largest_context_abstraction_class) / denominator;
    diagnostics.round_count = productive_rounds_;
    diagnostics.context_key_count = blocks_.size();

    std::vector<std::size_t> block_sizes;
    block_sizes.reserve(blocks_.size());
    for (const auto& block : blocks_) {
        block_sizes.push_back(block.yields.size());
    }
    std::sort(block_sizes.begin(), block_sizes.end());
    if (!block_sizes.empty()) {
        diagnostics.mean_local_role_block_size =
            static_cast<double>(std::accumulate(block_sizes.begin(), block_sizes.end(),
                                                std::size_t{0})) /
            static_cast<double>(block_sizes.size());
        diagnostics.median_local_role_block_size = percentile_sorted(block_sizes, 0.5);
        diagnostics.p90_local_role_block_size = percentile_sorted(block_sizes, 0.9);
        diagnostics.p99_local_role_block_size = percentile_sorted(block_sizes, 0.99);
        diagnostics.max_local_role_block_size = block_sizes.back();
        diagnostics.max_local_role_block_ratio =
            static_cast<double>(block_sizes.back()) / denominator;
    }

    // Diagnostic-only projection graph (never an equivalence class).
    {
        std::vector<StringId> parent(string_count);
        std::iota(parent.begin(), parent.end(), 0U);
        const auto find = [&](StringId s) {
            while (parent[s] != s) {
                parent[s] = parent[parent[s]];
                s = parent[s];
            }
            return s;
        };
        for (const auto& block : blocks_) {
            for (std::size_t index = 1; index < block.yields.size(); ++index) {
                const auto a = find(block.yields[0]);
                const auto b = find(block.yields[index]);
                if (a != b) {
                    parent[b] = a;
                }
            }
        }
        std::vector<std::size_t> component_sizes(string_count, 0);
        std::size_t components = 0;
        for (StringId s = 0; s < string_count; ++s) {
            if (s == corpus_.string_interner().epsilon_id()) {
                continue;
            }
            const auto root = find(s);
            if (component_sizes[root]++ == 0) {
                ++components;
            }
        }
        diagnostics.projected_graph_components = components;
        for (const auto size : component_sizes) {
            diagnostics.projected_giant_component_size =
                std::max(diagnostics.projected_giant_component_size, size);
        }
        diagnostics.projected_giant_component_ratio =
            static_cast<double>(diagnostics.projected_giant_component_size) / denominator;
    }
    return diagnostics;
}

std::uint64_t ContextIndexedSolver::context_partition_hash() const {
    std::string serialized;
    for (StringId s = 0; s < class_of_.size(); ++s) {
        serialized += std::to_string(class_of_[s]);
        serialized += ',';
    }
    return fnv1a(serialized);
}

std::uint64_t ContextIndexedSolver::local_relation_hash() const {
    std::string serialized;
    for (const auto& block : blocks_) {
        serialized += std::to_string(block.context.left) + ':' +
                      std::to_string(block.context.right) + '=';
        for (const auto yield : block.yields) {
            serialized += std::to_string(yield);
            serialized += ' ';
        }
        serialized += ';';
    }
    return fnv1a(serialized);
}

std::uint64_t ContextIndexedSolver::round_trace_hash() const {
    std::string serialized;
    for (const auto& stats : round_stats_) {
        serialized += std::to_string(stats.round) + ',' +
                      std::to_string(stats.context_class_count) + ',' +
                      std::to_string(stats.context_key_count) + ',' +
                      std::to_string(stats.local_relation_pair_count) + ';';
    }
    return fnv1a(serialized);
}

double ContextIndexedSolver::recursive_relation_gain() const {
    const auto initial = relation_count_round0();
    const auto final_count = relation_count_final();
    return (static_cast<double>(final_count) - static_cast<double>(initial)) /
           std::max<double>(1.0, static_cast<double>(initial));
}

std::vector<SpanEvidence> indexed_shadow_evidence(const Corpus& corpus,
                                                  const ContextIndexedSolver& solver) {
    const auto& records = corpus.context_records();
    // U_c = distinct raw contexts mapped to c; R_c(u) = those containing u.
    std::map<ContextKey, std::uint32_t> universe;
    {
        std::vector<std::pair<ContextKey, RawContextKey>> contexts;
        contexts.reserve(records.size());
        for (const auto& record : records) {
            const auto key = solver.final_key_for(record.triple.left, record.triple.right);
            contexts.emplace_back(*key, RawContextKey{record.triple.left, record.triple.right});
        }
        std::sort(contexts.begin(), contexts.end());
        contexts.erase(std::unique(contexts.begin(), contexts.end()), contexts.end());
        for (const auto& [key, context] : contexts) {
            ++universe[key];
        }
    }
    std::map<std::pair<ContextKey, StringId>, std::uint32_t> yield_coverage;
    for (const auto& record : records) {
        const auto key = solver.final_key_for(record.triple.left, record.triple.right);
        ++yield_coverage[{*key, record.triple.yield}];
    }

    std::vector<SpanEvidence> evidence;
    for (std::size_t index = 0; index < records.size(); ++index) {
        const auto& record = records[index];
        const auto key = *solver.final_key_for(record.triple.left, record.triple.right);
        const auto found = std::lower_bound(
            solver.blocks().begin(), solver.blocks().end(), key,
            [](const LocalRoleBlock& block, const ContextKey& target) {
                return block.context < target;
            });
        if (found == solver.blocks().end() || !(found->context == key) ||
            found->yields.size() < 2) {
            continue;
        }
        const auto u = record.triple.yield;
        const auto coverage_u = yield_coverage.at({key, u});
        const auto total = universe.at(key);
        std::uint64_t best_fixed = 0;
        double best_strength = 0.0;
        std::uint32_t best_confidence = 0;
        std::vector<StringId> best_alternatives;
        for (const auto v : found->yields) {
            if (v == u) {
                continue;
            }
            const auto coverage_v = yield_coverage.at({key, v});
            const auto confidence = std::min(coverage_u, coverage_v);
            const auto strength =
                static_cast<double>(confidence) / static_cast<double>(total);
            const auto fixed =
                static_cast<std::uint64_t>(std::llround(strength * kStrengthScale));
            if (fixed > best_fixed) {
                best_fixed = fixed;
                best_strength = strength;
                best_confidence = confidence;
                best_alternatives.assign(1, v);
            } else if (fixed == best_fixed && fixed > 0) {
                best_alternatives.push_back(v);
                best_confidence = std::max(best_confidence, confidence);
            }
        }
        if (best_fixed == 0) {
            continue;
        }
        for (const auto occurrence_id : records[index].occurrences) {
            const auto& occurrence = corpus.occurrences().at(
                static_cast<std::size_t>(occurrence_id));
            evidence.push_back(SpanEvidence{
                occurrence_id,
                Span{occurrence.sentence, occurrence.begin, occurrence.end},
                occurrence.yield,
                best_fixed,
                best_strength,
                best_confidence,
                best_alternatives,
                {},  // witnesses: abstract contexts have no single raw witness list
            });
        }
    }
    std::sort(evidence.begin(), evidence.end(), [](const auto& lhs, const auto& rhs) {
        return std::tie(lhs.span, lhs.occurrence) < std::tie(rhs.span, rhs.occurrence);
    });
    return evidence;
}

}  // namespace scf
