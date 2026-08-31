// SCF v1.3 real-corpus constraint-density smoke test.
//
// Answers exactly one question: is the observable substitutional constraint
// system of a real corpus dense enough to justify the SCF hypothesis?
// It never claims parsing accuracy — there is no gold treebank here. All
// density numbers are empirical proxies, not proofs of algebraic
// overdetermination or matrix rank.

#include "scf/context_indexed.hpp"
#include "scf/corpus.hpp"
#include "scf/equivalence_solver.hpp"
#include "scf/evaluator.hpp"
#include "scf/evidence_builder.hpp"
#include "scf/pipeline.hpp"
#include "scf/synthetic.hpp"  // deterministic_shuffle

#include "scf/audit.hpp"  // hash_hex

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::string input_path;
    std::filesystem::path output_directory;
    std::vector<std::size_t> sample_sizes{100, 500, 1000, 5000, 10000};
    std::uint64_t seed{42};
    std::size_t min_len{2};
    std::size_t max_len{10};
    bool lowercase{true};
    bool strip_punctuation{true};
    bool deduplicate{true};
    bool drop_digit_tokens{false};
    bool run_saturation{true};
};

std::string fmt6(const double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6f", value);
    return buffer;
}

bool parse_bool(const std::string_view text) {
    if (text == "true" || text == "1") return true;
    if (text == "false" || text == "0") return false;
    throw std::runtime_error("expected true or false, got: " + std::string(text));
}

std::ofstream open_file(const std::filesystem::path& path) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("cannot write " + path.string());
    }
    return output;
}

double mean_of(const std::vector<double>& values) {
    if (values.empty()) return 0.0;
    double total = 0.0;
    for (const auto value : values) total += value;
    return total / static_cast<double>(values.size());
}

double percentile_of(std::vector<double> values, const double percentile) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const auto index = std::min(values.size() - 1,
                                static_cast<std::size_t>(percentile * (values.size() - 1) + 0.5));
    return values[index];
}

// Transparent, dependency-free preprocessing (whitespace tokenization only).
std::vector<std::string> preprocess(std::istream& input, const Options& options,
                                    std::size_t* input_count) {
    std::vector<std::string> kept;
    std::set<std::vector<std::string>> seen;
    std::string line;
    while (std::getline(input, line)) {
        ++*input_count;
        for (auto& ch : line) {
            const auto uch = static_cast<unsigned char>(ch);
            if (options.strip_punctuation && std::ispunct(uch)) {
                ch = ' ';
            } else if (options.lowercase) {
                ch = static_cast<char>(std::tolower(uch));
            }
        }
        std::istringstream words(line);
        std::vector<std::string> tokens;
        std::string word;
        bool drop = false;
        while (words >> word) {
            if (options.drop_digit_tokens &&
                std::any_of(word.begin(), word.end(),
                            [](const unsigned char c) { return std::isdigit(c); })) {
                drop = true;
            }
            tokens.push_back(word);
        }
        if (drop || tokens.size() < options.min_len || tokens.size() > options.max_len) {
            continue;
        }
        if (options.deduplicate && !seen.insert(tokens).second) {
            continue;
        }
        std::string joined;
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            joined += (i == 0 ? "" : " ") + tokens[i];
        }
        kept.push_back(std::move(joined));
    }
    return kept;
}

struct RegimeSignals {
    std::size_t n{};
    double fraction_contexts_ge2{};
    double fraction_yields_with_partner{};
    double fraction_spans_with_witness{};
    double constraint_density_proxy{};
    double occurrence_density_proxy{};
    double collapse_ratio{};
    double largest_eclass_ratio{};
};

// Giant connected-component ratio of the raw direct substitution graph
// (edges = yield pairs sharing an exact raw context), Stage A of the v1.4
// collapse attribution.
double raw_direct_giant_ratio(const scf::Corpus& corpus, const scf::EvidenceBuilder& builder) {
    const auto count = corpus.string_interner().size();
    std::vector<scf::StringId> parent(count);
    for (scf::StringId s = 0; s < count; ++s) parent[s] = s;
    const auto find = [&](scf::StringId s) {
        while (parent[s] != s) {
            parent[s] = parent[parent[s]];
            s = parent[s];
        }
        return s;
    };
    for (const auto& pair : builder.pairs()) {
        const auto a = find(pair.first);
        const auto b = find(pair.second);
        if (a != b) parent[b] = a;
    }
    std::vector<std::size_t> sizes(count, 0);
    std::size_t giant = 0;
    for (scf::StringId s = 1; s < count; ++s) {
        giant = std::max(giant, ++sizes[find(s)]);
    }
    return count > 1 ? static_cast<double>(giant) / static_cast<double>(count - 1) : 0.0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Options options;
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument(argv[index]);
            const auto value = [&]() -> std::string {
                if (index + 1 >= argc) {
                    throw std::runtime_error("missing value after " + std::string(argument));
                }
                return argv[++index];
            };
            if (argument == "--help" || argument == "-h") {
                std::cout
                    << "Usage: scf_real_audit --input FILE --output-dir DIR [options]\n"
                       "  --sample-sizes CSV     (default: 100,500,1000,5000,10000)\n"
                       "  --seed N               (default: 42)\n"
                       "  --min-len N --max-len N  (defaults: 2, 10)\n"
                       "  --lowercase/--strip-punctuation/--deduplicate/--drop-digit-tokens BOOL\n"
                       "  --run-saturation BOOL  (default: true)\n"
                       "Constraint geometry audit only; no parsing accuracy is reported.\n";
                return 0;
            }
            if (argument == "--input") {
                options.input_path = value();
            } else if (argument == "--output-dir") {
                options.output_directory = value();
            } else if (argument == "--sample-sizes") {
                options.sample_sizes.clear();
                std::istringstream stream(value());
                std::string part;
                while (std::getline(stream, part, ',')) {
                    options.sample_sizes.push_back(std::stoull(part));
                }
            } else if (argument == "--seed") {
                options.seed = std::stoull(value());
            } else if (argument == "--min-len") {
                options.min_len = std::stoull(value());
            } else if (argument == "--max-len") {
                options.max_len = std::stoull(value());
            } else if (argument == "--lowercase") {
                options.lowercase = parse_bool(value());
            } else if (argument == "--strip-punctuation") {
                options.strip_punctuation = parse_bool(value());
            } else if (argument == "--deduplicate") {
                options.deduplicate = parse_bool(value());
            } else if (argument == "--drop-digit-tokens") {
                options.drop_digit_tokens = parse_bool(value());
            } else if (argument == "--run-saturation") {
                options.run_saturation = parse_bool(value());
            } else {
                throw std::runtime_error("unknown argument: " + std::string(argument));
            }
        }
        if (options.input_path.empty() || options.output_directory.empty()) {
            throw std::runtime_error("--input and --output-dir are required");
        }
        std::filesystem::create_directories(options.output_directory);

        std::ifstream input(options.input_path);
        if (!input) {
            throw std::runtime_error("cannot open " + options.input_path);
        }
        std::size_t input_count = 0;
        const auto pool = preprocess(input, options, &input_count);
        std::cout << "input sentences = " << input_count << ", kept = " << pool.size() << '\n';

        // Sample sizes clipped to the pool (spec: use all available sizes,
        // never download data automatically).
        std::vector<std::size_t> sizes;
        for (const auto n : options.sample_sizes) {
            sizes.push_back(std::min(n, pool.size()));
        }
        sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());

        auto summary = open_file(options.output_directory / "real_audit_summary.csv");
        summary << "N,sentences_input,sentences_kept,sentence_types,token_types,total_tokens,"
                   "distinct_strings,occurrence_count,context_triples,concat_triples,"
                   "contexts_total,contexts_degree_1,contexts_degree_ge_2,contexts_degree_ge_3,"
                   "contexts_degree_ge_5,fraction_contexts_degree_ge_2,mean_context_degree,"
                   "median_context_degree,max_context_degree,singleton_context_ratio,"
                   "repeat_context_ratio,mean_repeat_count,mean_yield_context_degree,"
                   "median_yield_context_degree,p90_yield_context_degree,"
                   "p99_yield_context_degree,max_yield_context_degree,"
                   "mean_substitution_degree,median_substitution_degree,"
                   "p90_substitution_degree,max_substitution_degree,"
                   "fraction_yields_with_substitution_partner,proper_span_occurrences,"
                   "proper_spans_with_any_raw_witness,proper_spans_with_confidence_ge_2,"
                   "proper_spans_with_confidence_ge_3,fraction_proper_spans_with_any_witness,"
                   "direct_witness_pairs,nontrivial_yields,constraint_density_proxy,"
                   "occurrence_density_proxy,mean_left_context_diversity,"
                   "mean_right_context_diversity,mean_full_context_diversity,"
                   "initial_eclasses,final_eclasses,collapse_ratio,largest_eclass,"
                   "largest_eclass_ratio,round_count,successful_unions\n";
        auto degree_by_n = open_file(options.output_directory / "context_degree_by_N.csv");
        degree_by_n << "N,sentence_length,contexts,degree_1,degree_ge_2,degree_ge_3,degree_ge_5,"
                       "fraction_degree_ge_2,mean_degree\n";
        auto span_coverage = open_file(options.output_directory / "span_evidence_coverage.csv");
        span_coverage << "N,span_length,occurrences,with_evidence,fraction_with_evidence,"
                         "mean_confidence,mean_strength_raw_count,mean_strength_opportunity,"
                         "mean_strength_conditional,mean_strength_jaccard\n";
        auto saturation_csv = open_file(options.output_directory / "saturation_real.csv");
        saturation_csv << "N,initial_eclasses,final_eclasses,collapse_ratio,largest_eclass,"
                          "largest_eclass_ratio,round_count,successful_unions\n";
        // --- v1.4 context-indexed outputs ---
        auto indexed_rounds = open_file(options.output_directory / "context_indexed_rounds.csv");
        indexed_rounds << "N,round,context_class_count,context_key_count,"
                          "local_relation_pair_count,new_context_class_merges,"
                          "new_local_relation_pairs,largest_context_class_ratio,"
                          "max_local_block_ratio\n";
        auto attribution = open_file(options.output_directory / "collapse_attribution.csv");
        attribution << "N,raw_direct_giant_ratio,indexed_projection_giant_ratio,"
                       "legacy_global_dsu_giant_ratio,indexed_max_local_block_ratio,"
                       "indexed_context_class_largest_ratio\n";
        auto versus = open_file(options.output_directory / "global_vs_indexed.csv");
        versus << "N,legacy_global_final_classes,legacy_global_largest_ratio,"
                  "indexed_final_context_classes,indexed_largest_context_class_ratio,"
                  "indexed_max_local_block_ratio,indexed_projection_giant_ratio\n";
        auto indexed_metrics = open_file(options.output_directory / "real_indexed_metrics.csv");
        indexed_metrics
            << "N,context_partition_rounds,initial_context_classes,final_context_classes,"
               "context_abstraction_collapse_ratio,largest_context_abstraction_class_ratio,"
               "local_context_key_count,mean_local_role_block_size,max_local_role_block_size,"
               "max_local_role_block_ratio,raw_direct_giant_ratio,"
               "indexed_projection_giant_ratio,legacy_global_dsu_giant_ratio,"
               "raw_evidence_coverage,indexed_evidence_coverage,coverage_gain,"
               "surface_forms_with_multiple_local_roles,mean_local_roles_per_surface,"
               "p95_local_roles_per_surface,max_local_roles_per_surface,"
               "context_partition_hash,local_relation_hash,round_trace_hash,runtime_ms\n";

        std::vector<RegimeSignals> signals;
        const auto largest_n = sizes.empty() ? 0 : *std::max_element(sizes.begin(), sizes.end());

        for (const auto n : sizes) {
            // Deterministic sample: shuffle indices with the fixed seed, take
            // the first n, restore corpus order.
            std::vector<std::size_t> order(pool.size());
            for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
            scf::deterministic_shuffle(order, options.seed);
            order.resize(n);
            std::sort(order.begin(), order.end());
            std::ostringstream text;
            for (const auto index : order) {
                text << pool[index] << '\n';
            }
            scf::CorpusConfig config;
            config.max_sentence_length = options.max_len;
            scf::Corpus corpus(config);
            {
                std::istringstream stream(text.str());
                corpus.load(stream);
            }
            const scf::EvidenceBuilder builder(corpus);  // raw_count structure

            // --- context degree (deg(c) = distinct yields in the raw bucket)
            std::map<scf::RawContextKey, std::set<scf::StringId>> bucket_yields;
            for (const auto& record : corpus.context_records()) {
                bucket_yields[{record.triple.left, record.triple.right}].insert(
                    record.triple.yield);
            }
            std::vector<double> degrees;
            std::size_t deg1 = 0, deg2 = 0, deg3 = 0, deg5 = 0;
            double degree_sum = 0.0, repeat_sum = 0.0;
            double max_degree = 0.0;
            for (const auto& [context, yields] : bucket_yields) {
                const auto degree = static_cast<double>(yields.size());
                degrees.push_back(degree);
                degree_sum += degree;
                max_degree = std::max(max_degree, degree);
                if (yields.size() == 1) ++deg1;
                if (yields.size() >= 2) { ++deg2; repeat_sum += degree; }
                if (yields.size() >= 3) ++deg3;
                if (yields.size() >= 5) ++deg5;
            }
            const auto contexts_total = bucket_yields.size();
            const auto fraction_ge2 =
                contexts_total > 0 ? static_cast<double>(deg2) / contexts_total : 0.0;

            // context degree bucketed by containing sentence length
            {
                std::map<std::size_t, std::map<scf::RawContextKey, std::size_t>> by_length;
                for (const auto& [context, yields] : bucket_yields) {
                    const auto external =
                        corpus.string_interner().tokens(context.left).size() +
                        corpus.string_interner().tokens(context.right).size();
                    for (const auto yield : yields) {
                        const auto length =
                            external + corpus.string_interner().tokens(yield).size();
                        auto& cell = by_length[length][context];
                        cell = std::max(cell, yields.size());
                    }
                }
                for (const auto& [length, contexts] : by_length) {
                    std::size_t b1 = 0, b2 = 0, b3 = 0, b5 = 0;
                    double sum = 0.0;
                    for (const auto& [context, degree] : contexts) {
                        sum += static_cast<double>(degree);
                        if (degree == 1) ++b1;
                        if (degree >= 2) ++b2;
                        if (degree >= 3) ++b3;
                        if (degree >= 5) ++b5;
                    }
                    degree_by_n << n << ',' << length << ',' << contexts.size() << ',' << b1
                                << ',' << b2 << ',' << b3 << ',' << b5 << ','
                                << fmt6(contexts.empty() ? 0.0
                                                         : static_cast<double>(b2) /
                                                               contexts.size())
                                << ',' << fmt6(contexts.empty() ? 0.0 : sum / contexts.size())
                                << '\n';
                }
            }

            // --- yield context degree, substitution degree, diversity
            std::map<scf::StringId, std::set<scf::StringId>> left_div, right_div;
            std::map<scf::StringId, std::size_t> full_div;
            for (const auto& record : corpus.context_records()) {
                left_div[record.triple.yield].insert(record.triple.left);
                right_div[record.triple.yield].insert(record.triple.right);
                ++full_div[record.triple.yield];
            }
            std::vector<double> yield_degrees;
            std::vector<double> left_values, right_values, full_values;
            for (const auto& [yield, count] : full_div) {
                yield_degrees.push_back(static_cast<double>(count));
                left_values.push_back(static_cast<double>(left_div[yield].size()));
                right_values.push_back(static_cast<double>(right_div[yield].size()));
                full_values.push_back(static_cast<double>(count));
            }
            std::map<scf::StringId, std::size_t> partners;
            std::map<scf::StringId, std::set<scf::RawContextId>> partner_contexts;
            for (const auto& pair : builder.pairs()) {
                ++partners[pair.first];
                ++partners[pair.second];
                for (const auto context : pair.contexts) {
                    partner_contexts[pair.first].insert(context);
                    partner_contexts[pair.second].insert(context);
                }
            }
            std::vector<double> substitution_degrees;
            std::size_t yields_with_partner = 0;
            for (const auto& [yield, count] : full_div) {
                const auto found = partners.find(yield);
                const auto degree = found == partners.end() ? 0.0
                                                            : static_cast<double>(found->second);
                substitution_degrees.push_back(degree);
                yields_with_partner += degree > 0 ? 1 : 0;
            }

            // --- proper span occurrence coverage (per objective strengths)
            std::vector<scf::EvidenceBuilder> builders;
            builders.reserve(3);
            for (const auto objective :
                 {scf::EvidenceObjective::OpportunityNormalized,
                  scf::EvidenceObjective::SymmetricConditional, scf::EvidenceObjective::Jaccard}) {
                builders.emplace_back(corpus, objective);
            }
            const auto strength_map = [](const scf::EvidenceBuilder& b) {
                std::map<scf::Span, double> strengths;
                for (const auto& item : b.span_evidence()) {
                    strengths[item.span] = item.strength;
                }
                return strengths;
            };
            const auto raw_map = strength_map(builder);
            const auto opp_map = strength_map(builders[0]);
            const auto cond_map = strength_map(builders[1]);
            const auto jac_map = strength_map(builders[2]);
            std::map<scf::Span, std::uint32_t> confidence_map;
            for (const auto& item : builder.span_evidence()) {
                confidence_map[item.span] = item.confidence;
            }
            struct SpanBucket {
                std::size_t occurrences{}, with_evidence{}, conf2{}, conf3{};
                double confidence_sum{}, raw{}, opp{}, cond{}, jac{};
            };
            std::map<std::uint16_t, SpanBucket> span_buckets;
            std::size_t proper_occurrences = 0, with_witness = 0, conf_ge2 = 0, conf_ge3 = 0;
            for (std::size_t sentence = 0; sentence < corpus.sentences().size(); ++sentence) {
                const auto length =
                    static_cast<std::uint16_t>(corpus.sentences()[sentence].size());
                for (std::uint16_t span_length = 2; span_length < length; ++span_length) {
                    for (std::uint16_t begin = 0; begin + span_length <= length; ++begin) {
                        const auto end = static_cast<std::uint16_t>(begin + span_length);
                        if (begin == 0 && end == length) continue;
                        const scf::Span span{static_cast<scf::SentenceId>(sentence), begin, end};
                        auto& bucket = span_buckets[span_length];
                        ++bucket.occurrences;
                        ++proper_occurrences;
                        const auto found = confidence_map.find(span);
                        if (found != confidence_map.end()) {
                            ++bucket.with_evidence;
                            ++with_witness;
                            bucket.confidence_sum += found->second;
                            if (found->second >= 2) { ++bucket.conf2; ++conf_ge2; }
                            if (found->second >= 3) { ++bucket.conf3; ++conf_ge3; }
                            bucket.raw += raw_map.at(span);
                            bucket.opp += opp_map.at(span);
                            bucket.cond += cond_map.at(span);
                            bucket.jac += jac_map.at(span);
                        }
                    }
                }
            }
            for (const auto& [span_length, bucket] : span_buckets) {
                const auto with = static_cast<double>(bucket.with_evidence);
                span_coverage << n << ',' << span_length << ',' << bucket.occurrences << ','
                              << bucket.with_evidence << ','
                              << fmt6(bucket.occurrences > 0
                                          ? with / bucket.occurrences
                                          : 0.0)
                              << ',' << fmt6(with > 0 ? bucket.confidence_sum / with : 0.0) << ','
                              << fmt6(with > 0 ? bucket.raw / with : 0.0) << ','
                              << fmt6(with > 0 ? bucket.opp / with : 0.0) << ','
                              << fmt6(with > 0 ? bucket.cond / with : 0.0) << ','
                              << fmt6(with > 0 ? bucket.jac / with : 0.0) << '\n';
            }

            // --- density proxies (explicitly proxies, not rank arguments)
            std::size_t nontrivial_yields = 0;
            for (scf::StringId id = 1; id < corpus.string_interner().size(); ++id) {
                nontrivial_yields +=
                    corpus.string_interner().tokens(id).size() >= 2 ? 1 : 0;
            }
            const auto density_proxy =
                nontrivial_yields > 0
                    ? static_cast<double>(builder.pairs().size()) / nontrivial_yields
                    : 0.0;
            const auto occurrence_proxy =
                proper_occurrences > 0 ? static_cast<double>(with_witness) / proper_occurrences
                                       : 0.0;

            // --- saturation diagnostics
            double collapse_ratio = 0.0, largest_ratio = 0.0;
            std::size_t final_classes = corpus.string_interner().size(), largest = 1,
                        rounds = 0, unions = 0;
            if (options.run_saturation) {
                scf::EquivalenceSolver solver(corpus.string_interner().size(),
                                              corpus.context_records(), corpus.concat_triples());
                solver.saturate();
                const auto& stats = solver.statistics().back();
                collapse_ratio = stats.collapse_ratio;
                final_classes = stats.classes;
                largest = stats.largest_class;
                largest_ratio = corpus.string_interner().size() > 0
                                    ? static_cast<double>(largest) /
                                          corpus.string_interner().size()
                                    : 0.0;
                rounds = solver.statistics().size() - 1;
                unions = solver.reasons().size();
                saturation_csv << n << ',' << corpus.string_interner().size() << ','
                               << final_classes << ',' << fmt6(collapse_ratio) << ',' << largest
                               << ',' << fmt6(largest_ratio) << ',' << rounds << ',' << unions
                               << '\n';
                if (n == largest_n) {
                    auto top = open_file(options.output_directory / "top_eclasses.txt");
                    scf::write_top_eclasses(top, corpus, solver);
                }
            }

            // --- polysemy pressure (diagnostic only; no split is performed)
            if (n == largest_n) {
                struct Pressure {
                    std::string text;
                    std::size_t contexts{}, partners{};
                    double overlap{};
                };
                std::vector<Pressure> rows;
                for (const auto& [yield, count] : full_div) {
                    if (count < 5) continue;  // need enough contexts to be meaningful
                    const auto found = partner_contexts.find(yield);
                    const auto shared =
                        found == partner_contexts.end() ? 0 : found->second.size();
                    Pressure row;
                    row.text = corpus.string_interner().to_string(yield,
                                                                  corpus.token_interner());
                    row.contexts = count;
                    row.partners = partners.contains(yield) ? partners[yield] : 0;
                    row.overlap = static_cast<double>(shared) / static_cast<double>(count);
                    rows.push_back(std::move(row));
                }
                std::sort(rows.begin(), rows.end(), [](const auto& lhs, const auto& rhs) {
                    if (lhs.overlap != rhs.overlap) return lhs.overlap < rhs.overlap;
                    return lhs.contexts > rhs.contexts;
                });
                auto pressure = open_file(options.output_directory /
                                          "top_polysemy_pressure_yields.tsv");
                pressure << "yield\tcontexts_total\tsubstitution_partners_total\t"
                            "partner_overlap_ratio\n";
                for (std::size_t i = 0; i < std::min<std::size_t>(50, rows.size()); ++i) {
                    pressure << rows[i].text << '\t' << rows[i].contexts << '\t'
                             << rows[i].partners << '\t' << fmt6(rows[i].overlap) << '\n';
                }
                auto by_length = open_file(options.output_directory /
                                           "yield_degree_by_length.csv");
                by_length << "yield_length,yield_count,mean_context_degree,"
                             "median_context_degree,mean_substitution_degree,"
                             "mean_left_diversity,mean_right_diversity\n";
                std::map<std::size_t, std::vector<std::size_t>> ids_by_length;
                for (const auto& [yield, count] : full_div) {
                    ids_by_length[corpus.string_interner().tokens(yield).size()].push_back(yield);
                }
                for (const auto& [length, ids] : ids_by_length) {
                    std::vector<double> ctx, sub, left_d, right_d;
                    for (const auto yield : ids) {
                        ctx.push_back(static_cast<double>(full_div[yield]));
                        sub.push_back(partners.contains(yield)
                                          ? static_cast<double>(partners[yield])
                                          : 0.0);
                        left_d.push_back(static_cast<double>(left_div[yield].size()));
                        right_d.push_back(static_cast<double>(right_div[yield].size()));
                    }
                    by_length << length << ',' << ids.size() << ',' << fmt6(mean_of(ctx)) << ','
                              << fmt6(percentile_of(ctx, 0.5)) << ',' << fmt6(mean_of(sub))
                              << ',' << fmt6(mean_of(left_d)) << ',' << fmt6(mean_of(right_d))
                              << '\n';
                }
            }

            // --- v1.4: context-indexed equivalence on real data ---
            {
                const auto start = std::chrono::steady_clock::now();
                scf::ContextIndexedSolver indexed(corpus);
                indexed.run();
                const auto runtime_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now() - start)
                                            .count();
                const auto diagnostics = indexed.diagnostics();
                for (const auto& stats : indexed.round_stats()) {
                    indexed_rounds << n << ',' << stats.round << ','
                                   << stats.context_class_count << ','
                                   << stats.context_key_count << ','
                                   << stats.local_relation_pair_count << ','
                                   << stats.new_context_class_merges << ','
                                   << stats.new_local_relation_pairs << ','
                                   << fmt6(stats.largest_context_class_ratio) << ','
                                   << fmt6(stats.max_local_block_ratio) << '\n';
                }
                const auto raw_giant = raw_direct_giant_ratio(corpus, builder);
                attribution << n << ',' << fmt6(raw_giant) << ','
                            << fmt6(diagnostics.projected_giant_component_ratio) << ','
                            << fmt6(largest_ratio) << ','
                            << fmt6(diagnostics.max_local_role_block_ratio) << ','
                            << fmt6(diagnostics.largest_context_abstraction_class_ratio)
                            << '\n';
                versus << n << ',' << final_classes << ',' << fmt6(largest_ratio) << ','
                       << diagnostics.final_context_classes << ','
                       << fmt6(diagnostics.largest_context_abstraction_class_ratio) << ','
                       << fmt6(diagnostics.max_local_role_block_ratio) << ','
                       << fmt6(diagnostics.projected_giant_component_ratio) << '\n';

                // indexed evidence coverage over proper span occurrences
                std::map<scf::Span, bool> indexed_span;
                for (const auto& record : corpus.context_records()) {
                    const auto key =
                        *indexed.final_key_for(record.triple.left, record.triple.right);
                    const auto& blocks = indexed.blocks();
                    const auto found = std::lower_bound(
                        blocks.begin(), blocks.end(), key,
                        [](const scf::LocalRoleBlock& block, const scf::ContextKey& target) {
                            return block.context < target;
                        });
                    const bool hit = found != blocks.end() && found->context == key &&
                                     found->yields.size() >= 2;
                    for (const auto occurrence_id : record.occurrences) {
                        const auto& occurrence = corpus.occurrences().at(
                            static_cast<std::size_t>(occurrence_id));
                        indexed_span[scf::Span{occurrence.sentence, occurrence.begin,
                                               occurrence.end}] = hit;
                    }
                }
                std::size_t indexed_hits = 0;
                for (std::size_t sentence = 0; sentence < corpus.sentences().size();
                     ++sentence) {
                    const auto length =
                        static_cast<std::uint16_t>(corpus.sentences()[sentence].size());
                    for (std::uint16_t span_length = 2; span_length < length; ++span_length) {
                        for (std::uint16_t begin = 0; begin + span_length <= length; ++begin) {
                            const auto end = static_cast<std::uint16_t>(begin + span_length);
                            if (begin == 0 && end == length) continue;
                            const auto found = indexed_span.find(scf::Span{
                                static_cast<scf::SentenceId>(sentence), begin, end});
                            indexed_hits +=
                                found != indexed_span.end() && found->second ? 1 : 0;
                        }
                    }
                }
                const auto indexed_coverage =
                    proper_occurrences > 0
                        ? static_cast<double>(indexed_hits) / proper_occurrences
                        : 0.0;

                // multi-role surfaces (final keys with block >= 2 per yield)
                std::map<scf::StringId, std::size_t> role_keys;
                for (const auto& block : indexed.blocks()) {
                    if (block.yields.size() < 2) continue;
                    for (const auto yield : block.yields) ++role_keys[yield];
                }
                std::vector<double> role_counts;
                std::size_t multi_role = 0;
                std::size_t max_roles = 0;
                for (const auto& [yield, count] : role_keys) {
                    role_counts.push_back(static_cast<double>(count));
                    multi_role += count >= 2 ? 1 : 0;
                    max_roles = std::max(max_roles, count);
                }
                indexed_metrics
                    << n << ',' << diagnostics.round_count << ','
                    << diagnostics.initial_context_classes << ','
                    << diagnostics.final_context_classes << ','
                    << fmt6(diagnostics.context_abstraction_collapse_ratio) << ','
                    << fmt6(diagnostics.largest_context_abstraction_class_ratio) << ','
                    << diagnostics.context_key_count << ','
                    << fmt6(diagnostics.mean_local_role_block_size) << ','
                    << diagnostics.max_local_role_block_size << ','
                    << fmt6(diagnostics.max_local_role_block_ratio) << ','
                    << fmt6(raw_giant) << ','
                    << fmt6(diagnostics.projected_giant_component_ratio) << ','
                    << fmt6(largest_ratio) << ',' << fmt6(occurrence_proxy) << ','
                    << fmt6(indexed_coverage) << ','
                    << fmt6(indexed_coverage - occurrence_proxy) << ',' << multi_role << ','
                    << fmt6(mean_of(role_counts)) << ','
                    << fmt6(percentile_of(role_counts, 0.95)) << ',' << max_roles << ','
                    << scf::hash_hex(indexed.context_partition_hash()) << ','
                    << scf::hash_hex(indexed.local_relation_hash()) << ','
                    << scf::hash_hex(indexed.round_trace_hash()) << ',' << runtime_ms << '\n';
                std::cout << "  v1.4 indexed: rounds=" << diagnostics.round_count
                          << " classes=" << diagnostics.final_context_classes
                          << " max_block_ratio=" << fmt6(diagnostics.max_local_role_block_ratio)
                          << " proj_giant=" << fmt6(diagnostics.projected_giant_component_ratio)
                          << " cov_gain=" << fmt6(indexed_coverage - occurrence_proxy) << '\n';
            }

            std::size_t total_tokens = 0;
            for (const auto& sentence : corpus.sentences()) {
                total_tokens += sentence.size();
            }
            summary << n << ',' << input_count << ',' << pool.size() << ','
                    << corpus.sentences().size() << ',' << corpus.token_interner().size() << ','
                    << total_tokens << ',' << corpus.string_interner().size() << ','
                    << corpus.occurrences().size() << ',' << corpus.context_records().size()
                    << ',' << corpus.concat_triples().size() << ',' << contexts_total << ','
                    << deg1 << ',' << deg2 << ',' << deg3 << ',' << deg5 << ','
                    << fmt6(fraction_ge2) << ','
                    << fmt6(contexts_total > 0 ? degree_sum / contexts_total : 0.0) << ','
                    << fmt6(percentile_of(degrees, 0.5)) << ',' << max_degree << ','
                    << fmt6(contexts_total > 0 ? static_cast<double>(deg1) / contexts_total : 0.0)
                    << ',' << fmt6(fraction_ge2) << ','
                    << fmt6(deg2 > 0 ? repeat_sum / deg2 : 0.0) << ','
                    << fmt6(mean_of(yield_degrees)) << ','
                    << fmt6(percentile_of(yield_degrees, 0.5)) << ','
                    << fmt6(percentile_of(yield_degrees, 0.9)) << ','
                    << fmt6(percentile_of(yield_degrees, 0.99)) << ','
                    << fmt6(percentile_of(yield_degrees, 1.0)) << ','
                    << fmt6(mean_of(substitution_degrees)) << ','
                    << fmt6(percentile_of(substitution_degrees, 0.5)) << ','
                    << fmt6(percentile_of(substitution_degrees, 0.9)) << ','
                    << fmt6(percentile_of(substitution_degrees, 1.0)) << ','
                    << fmt6(!full_div.empty()
                                ? static_cast<double>(yields_with_partner) / full_div.size()
                                : 0.0)
                    << ',' << proper_occurrences << ',' << with_witness << ',' << conf_ge2 << ','
                    << conf_ge3 << ',' << fmt6(occurrence_proxy) << ','
                    << builder.pairs().size() << ',' << nontrivial_yields << ','
                    << fmt6(density_proxy) << ',' << fmt6(occurrence_proxy) << ','
                    << fmt6(mean_of(left_values)) << ',' << fmt6(mean_of(right_values)) << ','
                    << fmt6(mean_of(full_values)) << ',' << corpus.string_interner().size() << ','
                    << final_classes << ',' << fmt6(collapse_ratio) << ',' << largest << ','
                    << fmt6(largest_ratio) << ',' << rounds << ',' << unions << '\n';

            RegimeSignals signal;
            signal.n = n;
            signal.fraction_contexts_ge2 = fraction_ge2;
            signal.fraction_yields_with_partner =
                !full_div.empty() ? static_cast<double>(yields_with_partner) / full_div.size()
                                  : 0.0;
            signal.fraction_spans_with_witness = occurrence_proxy;
            signal.constraint_density_proxy = density_proxy;
            signal.occurrence_density_proxy = occurrence_proxy;
            signal.collapse_ratio = collapse_ratio;
            signal.largest_eclass_ratio = largest_ratio;
            signals.push_back(signal);
            std::cout << "N=" << n << " contexts_ge2=" << fmt6(fraction_ge2)
                      << " span_witness=" << fmt6(occurrence_proxy)
                      << " collapse=" << fmt6(collapse_ratio)
                      << " largest_ratio=" << fmt6(largest_ratio) << '\n';
        }

        // --- regime classification (rule-based, spec v1.3 §32)
        const auto& last = signals.back();
        const auto& first = signals.front();
        const bool density_rises =
            signals.size() > 1 &&
            last.fraction_contexts_ge2 > first.fraction_contexts_ge2 &&
            last.fraction_spans_with_witness > first.fraction_spans_with_witness;
        const bool sparse = last.fraction_contexts_ge2 < 0.05 ||
                            last.fraction_spans_with_witness < 0.10;
        const bool collapse_pathological =
            last.collapse_ratio > 0.5 || last.largest_eclass_ratio > 0.25;
        std::string regime;
        if (collapse_pathological && density_rises) {
            regime = "collapse-dominated";
        } else if (sparse && !collapse_pathological) {
            regime = "exact-context-sparse";
        } else if (density_rises && !sparse && !collapse_pathological) {
            regime = "constraint-rich";
        } else {
            regime = "mixed";
        }

        auto report = open_file(options.output_directory / "REAL_CONSTRAINT_AUDIT.md");
        report << "# Real-corpus constraint audit\n\n"
               << "Constraint geometry audit only. **No parsing accuracy is claimed or "
                  "measurable here** — the corpus has no gold treebank. Density numbers are "
                  "empirical proxies, not a proof of algebraic overdetermination or matrix "
                  "rank.\n\n"
               << "- input: `" << options.input_path << "` (" << input_count
               << " input lines, " << pool.size() << " kept after preprocessing)\n"
               << "- preprocessing: lowercase=" << (options.lowercase ? "true" : "false")
               << ", strip_punctuation=" << (options.strip_punctuation ? "true" : "false")
               << ", deduplicate=" << (options.deduplicate ? "true" : "false")
               << ", min_len=" << options.min_len << ", max_len=" << options.max_len
               << ", drop_digit_tokens=" << (options.drop_digit_tokens ? "true" : "false")
               << ", seed=" << options.seed << "\n\n"
               << "## Scale sweep\n\n"
               << "| N | contexts deg>=2 | yields w/ partner | proper spans w/ witness | "
                  "density proxy | collapse | largest e-class ratio |\n"
               << "|---|---|---|---|---|---|---|\n";
        for (const auto& signal : signals) {
            report << "| " << signal.n << " | " << fmt6(signal.fraction_contexts_ge2) << " | "
                   << fmt6(signal.fraction_yields_with_partner) << " | "
                   << fmt6(signal.fraction_spans_with_witness) << " | "
                   << fmt6(signal.constraint_density_proxy) << " | "
                   << fmt6(signal.collapse_ratio) << " | "
                   << fmt6(signal.largest_eclass_ratio) << " |\n";
        }
        report << "\n## Regime\n\n```text\n" << regime << "\n```\n\n"
               << "Rules: collapse-dominated when collapse_ratio > 0.5 or largest e-class "
                  "ratio > 0.25 at the largest N while density rises; exact-context-sparse "
                  "when contexts deg>=2 < 5% or span witness coverage < 10%; "
                  "constraint-rich when density rises without pathological collapse; "
                  "otherwise mixed.\n";
        std::cout << "regime = " << regime << '\n'
                  << "wrote " << (options.output_directory / "REAL_CONSTRAINT_AUDIT.md").string()
                  << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "scf_real_audit: " << error.what() << '\n';
        return 1;
    }
}
