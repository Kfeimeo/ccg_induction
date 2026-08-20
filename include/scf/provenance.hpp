#pragma once

#include "scf/types.hpp"

#include <cstdint>

namespace scf {

enum class ReasonKind : std::uint8_t {
    ContextSubstitution,
    ConcatCongruence,
};

struct UnionReason {
    StringId lhs{};
    StringId rhs{};
    ReasonKind kind{};
    std::uint64_t source_record_a{};
    std::uint64_t source_record_b{};
    EClassId left_class{};
    EClassId right_class{};
    RoundId round{};
};

}  // namespace scf

