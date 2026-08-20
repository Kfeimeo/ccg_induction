#pragma once

#include "scf/records.hpp"
#include "scf/string_interner.hpp"
#include "scf/token_interner.hpp"

#include <cstddef>
#include <istream>
#include <string>
#include <vector>

namespace scf {

struct CorpusConfig {
    std::size_t max_sentence_length{10};
    bool lowercase{false};
    bool deduplicate_sentence_types{true};
    bool global_occurrence_consistency{false};
    std::string equivalence_mode{"strict_global"};
    std::string tree_objective{"max_direct_substitution_support"};
    std::string evidence_aggregation{"max_pair_support"};
    bool count_raw_surface_contexts{true};
    bool score_leaves{false};
    bool score_root{false};
};

struct CorpusSummary {
    std::size_t input_sentences{};
    std::size_t ignored_empty_lines{};
    std::size_t duplicate_sentences{};
};

class Corpus {
public:
    explicit Corpus(CorpusConfig config = {});

    void load(std::istream& input);
    void load_file(const std::string& path);

    [[nodiscard]] const CorpusConfig& config() const noexcept { return config_; }
    [[nodiscard]] const CorpusSummary& summary() const noexcept { return summary_; }
    [[nodiscard]] const TokenInterner& token_interner() const noexcept { return token_interner_; }
    [[nodiscard]] const StringInterner& string_interner() const noexcept { return string_interner_; }
    [[nodiscard]] const std::vector<std::vector<TokenId>>& sentences() const noexcept { return sentences_; }
    [[nodiscard]] const std::vector<Occurrence>& occurrences() const noexcept { return occurrences_; }
    [[nodiscard]] const std::vector<ContextRecord>& context_records() const noexcept { return context_records_; }
    [[nodiscard]] const std::vector<ConcatTriple>& concat_triples() const noexcept { return concat_triples_; }

private:
    void build_records();

    CorpusConfig config_;
    CorpusSummary summary_;
    TokenInterner token_interner_;
    StringInterner string_interner_;
    std::vector<std::vector<TokenId>> sentences_;
    std::vector<Occurrence> occurrences_;
    std::vector<ContextRecord> context_records_;
    std::vector<ConcatTriple> concat_triples_;
};

CorpusConfig load_config_file(const std::string& path, CorpusConfig base = {});

}  // namespace scf
