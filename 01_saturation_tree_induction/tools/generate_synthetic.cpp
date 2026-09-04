#include "scf/platform.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void write_simple(const std::filesystem::path& directory) {
    std::ofstream output(directory / "simple.txt");
    output << "the dog runs\n"
              "a cat runs\n"
              "the dog sleeps\n"
              "a cat sleeps\n";
}

void write_cartesian(const std::filesystem::path& directory) {
    std::ofstream output(directory / "cartesian.txt");
    const std::vector<std::string> a{"a1", "a2", "a3"};
    const std::vector<std::string> b{"b1", "b2", "b3"};
    for (const auto& left : a) {
        for (const auto& right : b) {
            output << left << ' ' << right << '\n';
        }
    }
}

void write_deep(const std::filesystem::path& directory) {
    std::ofstream output(directory / "deep.txt");
    const std::vector<std::string> c{"c1", "c2"};
    const std::vector<std::string> d{"d1", "d2"};
    const std::vector<std::string> b{"b1", "b2"};
    for (const auto& first : c) {
        for (const auto& second : d) {
            for (const auto& third : b) {
                output << first << ' ' << second << ' ' << third << '\n';
            }
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    scf::platform::initialize_console();
    try {
        const std::filesystem::path output_directory = argc > 1 ? argv[1] : "01_saturation_tree_induction/data/synthetic";
        std::filesystem::create_directories(output_directory);
        write_simple(output_directory);
        write_cartesian(output_directory);
        write_deep(output_directory);
        std::cout << "Generated synthetic corpora in " << output_directory.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "scf_synthetic_generator: " << error.what() << '\n';
        return 1;
    }
}

