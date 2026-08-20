#pragma once

#include "scf/types.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace scf {

class TokenInterner {
public:
    TokenId intern_token(std::string_view token);
    [[nodiscard]] std::string_view token_text(TokenId id) const;
    [[nodiscard]] std::size_t size() const noexcept { return texts_.size(); }

private:
    std::vector<std::string> texts_;
    std::unordered_map<std::string, TokenId> ids_;
};

}  // namespace scf

