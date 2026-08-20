#include "scf/equivalence_solver.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <queue>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace scf {
namespace {

struct CanonicalRecord {
    EClassId left{};
    EClassId right{};
    StringId value{};
    std::uint64_t record_id{};
};

bool canonical_less(const CanonicalRecord& lhs, const CanonicalRecord& rhs) {
    return std::tie(lhs.left, lhs.right, lhs.value, lhs.record_id) <
           std::tie(rhs.left, rhs.right, rhs.value, rhs.record_id);
}

}  // namespace

EquivalenceSolver::EquivalenceSolver(const std::size_t string_count,
                                     const std::span<const ContextRecord> contexts,
                                     const std::span<const ConcatTriple> concats)
    : initial_class_count_(string_count),
      contexts_(contexts.begin(), contexts.end()),
      concats_(concats.begin(), concats.end()),
      dsu_(string_count) {
    if (string_count > std::numeric_limits<StringId>::max()) {
        throw std::invalid_argument("too many strings for StringId");
    }
    const auto valid = [string_count](const StringId id) { return id < string_count; };
    for (const auto& record : contexts_) {
        if (!valid(record.triple.left) || !valid(record.triple.right) || !valid(record.triple.yield)) {
            throw std::invalid_argument("context record contains invalid StringId");
        }
    }
    for (const auto& record : concats_) {
        if (!valid(record.left) || !valid(record.right) || !valid(record.result)) {
            throw std::invalid_argument("concat record contains invalid StringId");
        }
    }
}

void EquivalenceSolver::saturate() {
    if (saturated_) {
        return;
    }
    statistics_.push_back(make_stats(0, 0, 0, false));
    std::size_t previous_classes = dsu_.class_count();
    for (RoundId round = 1;; ++round) {
        const auto context_unions = run_context_phase(round);
        const auto concat_unions = run_concat_phase(round);
        const bool fixed_point = context_unions == 0 && concat_unions == 0;
        statistics_.push_back(make_stats(round, context_unions, concat_unions, fixed_point));
#ifndef NDEBUG
        assert(dsu_.class_count() <= previous_classes);
        for (StringId id = 0; id < dsu_.size(); ++id) {
            assert(dsu_.find(id) < dsu_.size());
        }
#endif
        previous_classes = dsu_.class_count();
        if (fixed_point) {
            break;
        }
    }
    saturated_ = true;
}

std::size_t EquivalenceSolver::run_context_phase(const RoundId round) {
    std::vector<CanonicalRecord> canonical;
    canonical.reserve(contexts_.size());
    for (std::size_t index = 0; index < contexts_.size(); ++index) {
        const auto& triple = contexts_[index].triple;
        canonical.push_back(CanonicalRecord{dsu_.find(triple.left),
                                            dsu_.find(triple.right),
                                            triple.yield,
                                            static_cast<std::uint64_t>(index)});
    }
    std::sort(canonical.begin(), canonical.end(), canonical_less);

    std::size_t union_count = 0;
    std::size_t begin = 0;
    while (begin < canonical.size()) {
        std::size_t end = begin + 1;
        while (end < canonical.size() && canonical[end].left == canonical[begin].left &&
               canonical[end].right == canonical[begin].right) {
            ++end;
        }
        const auto base = canonical[begin];
        StringId previous_value = base.value;
        for (std::size_t index = begin + 1; index < end; ++index) {
            const auto& current = canonical[index];
            if (current.value == previous_value) {
                continue;
            }
            previous_value = current.value;
            if (dsu_.unite(base.value, current.value)) {
                ++union_count;
                reasons_.push_back(UnionReason{base.value,
                                               current.value,
                                               ReasonKind::ContextSubstitution,
                                               base.record_id,
                                               current.record_id,
                                               base.left,
                                               base.right,
                                               round});
            }
        }
        begin = end;
    }
    return union_count;
}

std::size_t EquivalenceSolver::run_concat_phase(const RoundId round) {
    std::vector<CanonicalRecord> canonical;
    canonical.reserve(concats_.size());
    for (std::size_t index = 0; index < concats_.size(); ++index) {
        const auto& triple = concats_[index];
        canonical.push_back(CanonicalRecord{dsu_.find(triple.left),
                                            dsu_.find(triple.right),
                                            triple.result,
                                            static_cast<std::uint64_t>(index)});
    }
    std::sort(canonical.begin(), canonical.end(), canonical_less);

    std::size_t union_count = 0;
    std::size_t begin = 0;
    while (begin < canonical.size()) {
        std::size_t end = begin + 1;
        while (end < canonical.size() && canonical[end].left == canonical[begin].left &&
               canonical[end].right == canonical[begin].right) {
            ++end;
        }
        const auto base = canonical[begin];
        StringId previous_value = base.value;
        for (std::size_t index = begin + 1; index < end; ++index) {
            const auto& current = canonical[index];
            if (current.value == previous_value) {
                continue;
            }
            previous_value = current.value;
            if (dsu_.unite(base.value, current.value)) {
                ++union_count;
                reasons_.push_back(UnionReason{base.value,
                                               current.value,
                                               ReasonKind::ConcatCongruence,
                                               base.record_id,
                                               current.record_id,
                                               base.left,
                                               base.right,
                                               round});
            }
        }
        begin = end;
    }
    return union_count;
}

SaturationRoundStats EquivalenceSolver::make_stats(const RoundId round,
                                                   const std::size_t context_unions,
                                                   const std::size_t concat_unions,
                                                   const bool fixed_point) const {
    const auto classes = dsu_.class_count();
    const auto collapse = initial_class_count_ == 0
                              ? 0.0
                              : 1.0 - static_cast<double>(classes) / static_cast<double>(initial_class_count_);
    return SaturationRoundStats{round,
                                classes,
                                context_unions,
                                concat_unions,
                                dsu_.largest_class_size(),
                                collapse,
                                fixed_point};
}

bool EquivalenceSolver::equivalent(const StringId lhs, const StringId rhs) const {
    return dsu_.find_const(lhs) == dsu_.find_const(rhs);
}

EClassId EquivalenceSolver::eclass(const StringId id) const {
    return dsu_.find_const(id);
}

std::vector<std::vector<StringId>> EquivalenceSolver::all_classes() const {
    std::unordered_map<EClassId, std::size_t> positions;
    std::vector<std::vector<StringId>> result;
    for (StringId id = 0; id < dsu_.size(); ++id) {
        const auto root = dsu_.find_const(id);
        const auto [iterator, inserted] = positions.emplace(root, result.size());
        if (inserted) {
            result.emplace_back();
        }
        result[iterator->second].push_back(id);
    }
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.front() < rhs.front();
    });
    return result;
}

std::vector<std::size_t> EquivalenceSolver::proof_chain(const StringId lhs, const StringId rhs) const {
    if (!equivalent(lhs, rhs)) {
        return {};
    }
    if (lhs == rhs) {
        return {};
    }
    struct Edge {
        StringId next{};
        std::size_t reason{};
    };
    std::vector<std::vector<Edge>> graph(dsu_.size());
    for (std::size_t index = 0; index < reasons_.size(); ++index) {
        const auto& reason = reasons_[index];
        graph[reason.lhs].push_back(Edge{reason.rhs, index});
        graph[reason.rhs].push_back(Edge{reason.lhs, index});
    }
    const auto missing = std::numeric_limits<std::size_t>::max();
    std::vector<StringId> parent(dsu_.size(), std::numeric_limits<StringId>::max());
    std::vector<std::size_t> parent_reason(dsu_.size(), missing);
    std::queue<StringId> queue;
    parent[lhs] = lhs;
    queue.push(lhs);
    while (!queue.empty() && parent[rhs] == std::numeric_limits<StringId>::max()) {
        const auto current = queue.front();
        queue.pop();
        for (const auto edge : graph[current]) {
            if (parent[edge.next] != std::numeric_limits<StringId>::max()) {
                continue;
            }
            parent[edge.next] = current;
            parent_reason[edge.next] = edge.reason;
            queue.push(edge.next);
        }
    }
    if (parent[rhs] == std::numeric_limits<StringId>::max()) {
        throw std::logic_error("provenance graph does not connect an e-class");
    }
    std::vector<std::size_t> path;
    for (auto current = rhs; current != lhs; current = parent[current]) {
        path.push_back(parent_reason[current]);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

std::string reason_kind_name(const ReasonKind kind) {
    switch (kind) {
        case ReasonKind::ContextSubstitution:
            return "ContextSubstitution";
        case ReasonKind::ConcatCongruence:
            return "ConcatCongruence";
    }
    throw std::logic_error("unknown ReasonKind");
}

}  // namespace scf

