#pragma once

#include "scf/dsu.hpp"
#include "scf/provenance.hpp"
#include "scf/records.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace scf {

struct SaturationRoundStats {
    RoundId round{};
    std::size_t classes{};
    std::size_t context_unions{};
    std::size_t concat_unions{};
    std::size_t largest_class{};
    double collapse_ratio{};
    bool fixed_point{};
};

class EquivalenceSolver {
public:
    EquivalenceSolver(std::size_t string_count,
                      std::span<const ContextRecord> contexts,
                      std::span<const ConcatTriple> concats);

    void saturate();

    [[nodiscard]] bool equivalent(StringId lhs, StringId rhs) const;
    [[nodiscard]] EClassId eclass(StringId id) const;
    [[nodiscard]] std::size_t class_count() const noexcept { return dsu_.class_count(); }
    [[nodiscard]] std::size_t largest_class_size() const noexcept { return dsu_.largest_class_size(); }
    [[nodiscard]] std::vector<StringId> class_members(StringId id) const { return dsu_.members(id); }
    [[nodiscard]] std::vector<std::vector<StringId>> all_classes() const;
    [[nodiscard]] const std::vector<SaturationRoundStats>& statistics() const noexcept { return statistics_; }
    [[nodiscard]] const std::vector<UnionReason>& reasons() const noexcept { return reasons_; }
    [[nodiscard]] std::vector<std::size_t> proof_chain(StringId lhs, StringId rhs) const;

private:
    std::size_t run_context_phase(RoundId round);
    std::size_t run_concat_phase(RoundId round);
    [[nodiscard]] SaturationRoundStats make_stats(RoundId round,
                                                  std::size_t context_unions,
                                                  std::size_t concat_unions,
                                                  bool fixed_point) const;

    std::size_t initial_class_count_{};
    std::vector<ContextRecord> contexts_;
    std::vector<ConcatTriple> concats_;
    DisjointSet dsu_;
    std::vector<SaturationRoundStats> statistics_;
    std::vector<UnionReason> reasons_;
    bool saturated_{false};
};

std::string reason_kind_name(ReasonKind kind);

}  // namespace scf

