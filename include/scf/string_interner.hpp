#pragma once

#include "scf/token_interner.hpp"
#include "scf/types.hpp"

#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace scf {

struct TokenSequenceHash {
    std::size_t operator()(const std::vector<TokenId>& tokens) const noexcept;
};

class StringInterner {
public:
    StringInterner();

    StringId intern(std::span<const TokenId> tokens);
    [[nodiscard]] std::optional<StringId> find(std::span<const TokenId> tokens) const;
    [[nodiscard]] StringId epsilon_id() const noexcept { return 0; }
    [[nodiscard]] std::span<const TokenId> tokens(StringId id) const;
    [[nodiscard]] std::string to_string(StringId id, const TokenInterner& token_interner) const;
    [[nodiscard]] std::size_t size() const noexcept { return storage_.size(); }

private:
    std::vector<std::vector<TokenId>> storage_;
    std::unordered_map<std::vector<TokenId>, StringId, TokenSequenceHash> ids_;
};

}  // namespace scf

