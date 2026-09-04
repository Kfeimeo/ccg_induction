#pragma once

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

namespace scf {

// Low-priority real-data preprocessing for smoke tests only. No parse
// accuracy is claimed for real corpora without gold annotations.
struct PrepareTextConfig {
    std::size_t max_len{10};
    bool lowercase{true};
    bool strip_punctuation{true};
    bool deduplicate{true};
    bool drop_digits{false};
};

struct PrepareTextResult {
    std::vector<std::string> sentences;
    std::size_t input_line_count{};
    std::size_t input_sentence_count{};
    std::size_t kept_sentence_count{};
    std::size_t filtered_long{};
    std::size_t filtered_symbols{};
    std::size_t filtered_empty{};
    std::size_t duplicate_sentences{};
    std::size_t distinct_tokens{};
};

PrepareTextResult prepare_text(std::istream& input, const PrepareTextConfig& config = {});

}  // namespace scf
