#pragma once

#include "scf/types.hpp"

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace scf {

class DisjointSet {
public:
    explicit DisjointSet(const std::size_t count) : parent_(count), size_(count, 1), class_count_(count) {
        std::iota(parent_.begin(), parent_.end(), EClassId{0});
    }

    EClassId find(const StringId value) {
        validate(value);
        auto current = static_cast<EClassId>(value);
        while (parent_[current] != current) {
            parent_[current] = parent_[parent_[current]];
            current = parent_[current];
        }
        return current;
    }

    [[nodiscard]] EClassId find_const(const StringId value) const {
        validate(value);
        auto current = static_cast<EClassId>(value);
        while (parent_[current] != current) {
            current = parent_[current];
        }
        return current;
    }

    bool unite(const StringId lhs, const StringId rhs) {
        auto lhs_root = find(lhs);
        auto rhs_root = find(rhs);
        if (lhs_root == rhs_root) {
            return false;
        }
        if (size_[lhs_root] < size_[rhs_root]) {
            std::swap(lhs_root, rhs_root);
        }
        parent_[rhs_root] = lhs_root;
        size_[lhs_root] += size_[rhs_root];
        --class_count_;
        return true;
    }

    [[nodiscard]] std::size_t class_count() const noexcept { return class_count_; }

    [[nodiscard]] std::size_t class_size(const StringId value) const {
        return size_[find_const(value)];
    }

    [[nodiscard]] std::size_t largest_class_size() const noexcept {
        std::size_t largest = 0;
        for (std::size_t i = 0; i < parent_.size(); ++i) {
            if (parent_[i] == i) {
                largest = std::max(largest, size_[i]);
            }
        }
        return largest;
    }

    [[nodiscard]] std::vector<StringId> members(const StringId value) const {
        const auto root = find_const(value);
        std::vector<StringId> result;
        for (StringId id = 0; id < parent_.size(); ++id) {
            if (find_const(id) == root) {
                result.push_back(id);
            }
        }
        return result;
    }

    [[nodiscard]] std::size_t size() const noexcept { return parent_.size(); }

private:
    void validate(const StringId value) const {
        if (value >= parent_.size()) {
            throw std::out_of_range("invalid DSU id");
        }
    }

    std::vector<EClassId> parent_;
    std::vector<std::size_t> size_;
    std::size_t class_count_{};
};

}  // namespace scf

