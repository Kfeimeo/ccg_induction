#include "scf/string_interner.hpp"

#include <stdexcept>

namespace scf {

std::size_t TokenSequenceHash::operator()(const std::vector<TokenId>& tokens) const noexcept {
    std::size_t seed = tokens.size();
    for (const auto token : tokens) {
        seed ^= std::hash<TokenId>{}(token) + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    }
    return seed;
}

StringInterner::StringInterner() {
    storage_.emplace_back();
    ids_.emplace(storage_.front(), epsilon_id());
}

StringId StringInterner::intern(const std::span<const TokenId> tokens_view) {
    std::vector<TokenId> key(tokens_view.begin(), tokens_view.end());
    const auto found = ids_.find(key);
    if (found != ids_.end()) {
        return found->second;
    }
    const auto id = static_cast<StringId>(storage_.size());
    storage_.push_back(std::move(key));
    ids_.emplace(storage_.back(), id);
    return id;
}

std::optional<StringId> StringInterner::find(const std::span<const TokenId> tokens_view) const {
    const std::vector<TokenId> key(tokens_view.begin(), tokens_view.end());
    const auto found = ids_.find(key);
    if (found == ids_.end()) {
        return std::nullopt;
    }
    return found->second;
}

std::span<const TokenId> StringInterner::tokens(const StringId id) const {
    if (id >= storage_.size()) {
        throw std::out_of_range("invalid StringId");
    }
    return storage_[id];
}

std::string StringInterner::to_string(const StringId id, const TokenInterner& token_interner) const {
    const auto sequence = tokens(id);
    if (sequence.empty()) {
        return "<epsilon>";
    }
    std::string result;
    for (std::size_t i = 0; i < sequence.size(); ++i) {
        if (i != 0) {
            result.push_back(' ');
        }
        result.append(token_interner.token_text(sequence[i]));
    }
    return result;
}

}  // namespace scf

