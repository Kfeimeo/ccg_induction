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
    // v1.3: proper gold spans that are actually observable given the
    // generator's correlations. Empty means "identical to the full latent
    // gold". Only the hierarchical_correlated_right/left chains restrict it:
    // their internally frozen blocks carry no observable internal evidence,
    // so the evaluator must not demand a unique internal bracket there.
    std::vector<SpanPair> observable_spans;
};

struct SyntheticDataset {
    std::string grammar_name;
    Grammar grammar;              // CFG rules, or the CCG-lite lexicon written as rules
    std::uint64_t seed{1};
    double coverage{1.0};
    std::size_t max_sentences{};  // 0 disables the cap
    std::size_t lexical_cardinality{};   // resolved per-family K
    double symmetry_breaking_rate{0.0};  // rho; only symmetric_abc supports > 0
    std::size_t full_sentence_count{};
    std::vector<GoldSentence> sentences;  // sampled subset, in canonical generation order
};

// Grammar families available to scf_generate / scf_experiment. "ccg_lite" is
// an auxiliary application-only fragment, not part of the CFG-style mainline.
// The hierarchical_correlated_* families (v1.2.1) correlate tokens inside
// latent blocks so that different bracketings produce genuinely different
// surface languages, unlike the full-factorial Cartesian families.
std::vector<std::string> known_grammar_names();

// K = 0 selects the family default (ab_cartesian: 3, hierarchical_*: 3,
// everything else: 2). ccg_lite has a fixed lexicon and accepts only the
// default.
std::size_t resolve_lexical_cardinality(const std::string& name, std::size_t k);

Grammar make_grammar(const std::string& name, std::size_t lexical_cardinality = 0);

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

// Full language of a family under the resolved K and rho. For symmetric_abc
// with rho > 0, ceil(rho * K^2) marker sentences "a_i b_j p" (gold
// ((a_i b_j) p)) are appended in canonical (i, j) order, giving the AB block
// observable block-level contexts.
std::vector<GoldSentence> generate_family_language(const std::string& grammar_name,
                                                   std::size_t lexical_cardinality = 0,
                                                   double symmetry_breaking_rate = 0.0);

// Full language -> seeded shuffle -> first ceil(coverage * N) sentences ->
// optional max_sentences truncation -> canonical order restored.
SyntheticDataset generate_dataset(const std::string& grammar_name,
                                  double coverage,
                                  std::uint64_t seed,
                                  std::size_t max_sentences = 0,
                                  std::size_t lexical_cardinality = 0,
                                  double symmetry_breaking_rate = 0.0);

std::string grammar_json(const SyntheticDataset& dataset);

// Writes corpus.txt, gold_spans.tsv, gold_brackets.txt, and grammar.json.
void write_dataset(const SyntheticDataset& dataset, const std::filesystem::path& directory);

std::vector<GoldTree> dataset_gold_trees(const SyntheticDataset& dataset);

// Observable gold span sets per sentence (full latent proper gold when the
// sentence's observable_spans is empty).
std::vector<std::set<SpanPair>> dataset_observable_gold(const SyntheticDataset& dataset);

}  // namespace scf
