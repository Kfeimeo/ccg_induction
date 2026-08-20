#pragma once

#include "scf/types.hpp"

#include <compare>
#include <cstdint>
#include <vector>

namespace scf {

struct Occurrence {
    SentenceId sentence{};
    std::uint16_t begin{};
    std::uint16_t end{};
    StringId yield{};
    StringId left_context{};
    StringId right_context{};
};

struct ContextTriple {
    StringId left{};
    StringId right{};
    StringId yield{};

    auto operator<=>(const ContextTriple&) const = default;
};

struct ConcatTriple {
    StringId left{};
    StringId right{};
    StringId result{};

    auto operator<=>(const ConcatTriple&) const = default;
};

struct ContextRecord {
    ContextTriple triple;
    std::vector<OccurrenceId> occurrences;
};

}  // namespace scf

