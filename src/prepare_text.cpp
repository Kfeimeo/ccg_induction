#include "scf/prepare_text.hpp"

#include <algorithm>
#include <cctype>
#include <istream>
#include <set>
#include <sstream>

namespace scf {
namespace {

bool is_sentence_terminator(const char ch) {
    return ch == '.' || ch == '!' || ch == '?';
}

std::vector<std::string> split_sentences(const std::string& line) {
    std::vector<std::string> chunks;
    std::string current;
    for (const char ch : line) {
        if (is_sentence_terminator(ch)) {
            chunks.push_back(current);
            current.clear();
        } else {
            current += ch;
        }
    }
    chunks.push_back(current);
    return chunks;
}

bool has_content(const std::string& text) {
    return std::any_of(text.begin(), text.end(), [](const unsigned char ch) {
        return !std::isspace(ch);
    });
}

}  // namespace

PrepareTextResult prepare_text(std::istream& input, const PrepareTextConfig& config) {
    PrepareTextResult result;
    std::set<std::vector<std::string>> seen;
    std::set<std::string> tokens_seen;
    std::string line;
    while (std::getline(input, line)) {
        ++result.input_line_count;
        for (auto chunk : split_sentences(line)) {
            if (!has_content(chunk)) {
                continue;  // empty fragments between terminators are not sentences
            }
            ++result.input_sentence_count;
            for (auto& ch : chunk) {
                const auto uch = static_cast<unsigned char>(ch);
                if (config.strip_punctuation && std::ispunct(uch)) {
                    ch = ' ';
                } else if (config.lowercase) {
                    ch = static_cast<char>(std::tolower(uch));
                }
            }
            std::istringstream words(chunk);
            std::vector<std::string> sentence;
            std::string word;
            bool has_symbols = false;
            while (words >> word) {
                if (config.drop_digits &&
                    std::any_of(word.begin(), word.end(), [](const unsigned char ch) {
                        return std::isdigit(ch) || !std::isprint(ch);
                    })) {
                    has_symbols = true;
                }
                sentence.push_back(word);
            }
            if (sentence.empty()) {
                ++result.filtered_empty;
                continue;
            }
            if (has_symbols) {
                ++result.filtered_symbols;
                continue;
            }
            if (sentence.size() > config.max_len) {
                ++result.filtered_long;
                continue;
            }
            if (config.deduplicate && !seen.insert(sentence).second) {
                ++result.duplicate_sentences;
                continue;
            }
            for (const auto& token : sentence) {
                tokens_seen.insert(token);
            }
            std::string joined;
            for (std::size_t index = 0; index < sentence.size(); ++index) {
                if (index != 0) {
                    joined += ' ';
                }
                joined += sentence[index];
            }
            result.sentences.push_back(std::move(joined));
        }
    }
    result.kept_sentence_count = result.sentences.size();
    result.distinct_tokens = tokens_seen.size();
    return result;
}

}  // namespace scf
