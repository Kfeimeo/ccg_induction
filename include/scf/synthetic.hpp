#pragma once

#include "scf/gold.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace scf {

struct Rule {
    std::string lhs;
    std::vector<std::string> rhs;
};

struct Grammar {
    std::string name;
    std::string start_symbol;
    std::vector<Rule> rules;
};

struct GoldSentence {
    std::vector<std::string> tokens;
    GoldNode tree;
};

struct SyntheticDataset {
    std::string grammar_name;
    Grammar grammar;              // CFG rules, or the CCG-lite lexicon written as rules
    std::uint64_t seed{1};
    double coverage{1.0};
    std::size_t max_sentences{};  // 0 disables the cap
    std::size_t full_sentence_count{};
    std::vector<GoldSentence> sentences;  // sampled subset, in canonical generation order
};

// Grammar families available to scf_generate / scf_experiment. "ccg_lite" is
// an auxiliary application-only fragment, not part of the CFG-style mainline.
std::vector<std::string> known_grammar_names();

Grammar make_grammar(const std::string& name);

// Expands the full finite language of a CFG-style grammar. Unary chains are
// collapsed before emitting gold trees; non-binary structure is rejected.
// Duplicate token sequences with an identical tree shape are deduplicated;
// duplicates with different shapes are a structural ambiguity error.
std::vector<GoldSentence> generate_full_language(const Grammar& grammar);

// Application-only CCG-lite auxiliary generator (forward/backward application
// over a fixed small lexicon). Derivations are projected to plain binary
// brackets. This is a bracketing sanity check, not CCG induction.
std::vector<GoldSentence> generate_ccg_lite_language();
Grammar ccg_lite_lexicon_grammar();

// Deterministic Fisher-Yates shuffle driven by std::mt19937_64, independent of
// standard-library std::shuffle/std::uniform_int_distribution implementations.
void deterministic_shuffle(std::vector<std::size_t>& values, std::uint64_t seed);

// Full language -> seeded shuffle -> first ceil(coverage * N) sentences ->
// optional max_sentences truncation -> canonical order restored.
SyntheticDataset generate_dataset(const std::string& grammar_name,
                                  double coverage,
                                  std::uint64_t seed,
                                  std::size_t max_sentences = 0);

std::string grammar_json(const SyntheticDataset& dataset);

// Writes corpus.txt, gold_spans.tsv, gold_brackets.txt, and grammar.json.
void write_dataset(const SyntheticDataset& dataset, const std::filesystem::path& directory);

std::vector<GoldTree> dataset_gold_trees(const SyntheticDataset& dataset);

}  // namespace scf
