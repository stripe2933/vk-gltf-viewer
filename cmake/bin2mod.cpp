#include <cerrno>

import std;

using namespace std::string_view_literals;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#ifdef _WIN32
#define PATH_C_STR(...) (__VA_ARGS__).string().c_str()
#else
#define PATH_C_STR(...) (__VA_ARGS__).c_str()
#endif

template <char, std::ranges::range R>
struct joiner {
    R r;
};

template <char Delimiter, std::ranges::input_range R, typename CharT>
struct std::formatter<joiner<Delimiter, R>, CharT> : range_formatter<std::ranges::range_value_t<R>, CharT> {
    static constexpr char delimiter = Delimiter;

    constexpr formatter() {
        range_formatter<std::ranges::range_value_t<R>, CharT>::set_separator(std::string_view { &delimiter, 1 });
    }

    [[nodiscard]] constexpr auto format(joiner<Delimiter, R> joiner, auto &ctx) const {
        return range_formatter<std::ranges::range_value_t<R>, CharT>::format(joiner.r, ctx);
    }
};

template <char Delimiter, std::ranges::input_range R>
[[nodiscard]] constexpr auto join(const R &r) noexcept {
    return joiner<Delimiter, std::ranges::ref_view<const R>> { std::ranges::ref_view { r } };
}

[[nodiscard]] std::vector<std::uint32_t> loadFileAsBinary(std::ifstream file) {
    file.seekg(0, std::ios::end);
    const std::size_t fileByteSize = file.tellg();
    file.seekg(0);

    std::vector<std::uint32_t> result(fileByteSize / sizeof(std::uint32_t));
    file.read(reinterpret_cast<char*>(result.data()), fileByteSize);

    return result;
}

int main(int argc, char **argv) {
    if (argc != 6) {
        std::println(std::cerr, "Usage: bin2mod --namespace <namespace> <input-file> -o <output-file>");
        return 1;
    }

    const std::filesystem::path outputPath { argv[5] };
    std::ofstream outputFile { outputPath };
    if (!outputFile) {
        std::println(std::cerr, "Failed to open output file: {}", std::strerror(errno));
        return 1;
    }

    const std::string_view namespace_ = argv[2];
    std::vector<std::string_view> modulePartitionNameTokens
        = namespace_
        | std::views::split("::"sv)
        | std::views::transform([](auto &&r) { return std::string_view { r }; })
        | std::ranges::to<std::vector>();

    std::string outputFilenameIdentifier = outputPath.stem().string();
    for (char &c : outputFilenameIdentifier) {
        if (!((c >= 'A' && c <= 'z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) {
            c = '_';
        }
    }
    if (char c = outputFilenameIdentifier.front(); c >= '0' && c <= '9') {
        outputFilenameIdentifier.insert(0, "_"sv);
    }
    modulePartitionNameTokens.push_back(outputFilenameIdentifier);

    const std::filesystem::path inputPath { argv[3] };
    std::ifstream inputFile { inputPath };
    if (!inputFile) {
        std::println(std::cerr, "Failed to open input file: {}", std::strerror(errno));
        return 1;
    }
    const std::vector<std::uint32_t> data = loadFileAsBinary(std::move(inputFile));
    std::println(
        outputFile,
        "export module {:n:s};\n"
        "\n"
        "export namespace {} {{\n"
        "    constexpr unsigned int {}[] = {{\n"
        "        {:n:}\n"
        "    }};\n"
        "}}",
        join<'.'>(modulePartitionNameTokens), namespace_, outputFilenameIdentifier, data);
}