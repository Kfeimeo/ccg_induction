#pragma once

#include <compare>
#include <cstdint>
#include <functional>

namespace scf {

using TokenId = std::uint32_t;
using StringId = std::uint32_t;
using SentenceId = std::uint32_t;
using OccurrenceId = std::uint64_t;
using EClassId = std::uint32_t;
using RoundId = std::uint32_t;

struct Span {
    SentenceId sentence{};
    std::uint16_t begin{};
    std::uint16_t end{};

    auto operator<=>(const Span&) const = default;
};

struct SpanHash {
    std::size_t operator()(const Span& span) const noexcept {
        std::size_t seed = std::hash<SentenceId>{}(span.sentence);
        seed ^= std::hash<std::uint16_t>{}(span.begin) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
        seed ^= std::hash<std::uint16_t>{}(span.end) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
        return seed;
    }
};

}  // namespace scf

