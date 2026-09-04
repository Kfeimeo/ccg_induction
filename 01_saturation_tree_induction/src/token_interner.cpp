#include "scf/token_interner.hpp"

#include <stdexcept>

namespace scf {

TokenId TokenInterner::intern_token(const std::string_view token) {
    const auto found = ids_.find(std::string(token));
    if (found != ids_.end()) {
        return found->second;
    }
    const auto id = static_cast<TokenId>(texts_.size());
    texts_.emplace_back(token);
    ids_.emplace(texts_.back(), id);
    return id;
}

std::string_view TokenInterner::token_text(const TokenId id) const {
    if (id >= texts_.size()) {
        throw std::out_of_range("invalid TokenId");
    }
    return texts_[id];
}

}  // namespace scf

