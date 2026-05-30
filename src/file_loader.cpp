#include <expected>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

std::expected<std::string, std::string>
load_file(const std::filesystem::path &filepath) {
    std::ifstream ifs(filepath, std::ios::binary);
    if (!ifs) {
        return std::unexpected("Failed to open file: " + filepath.string());
    }

    ifs.seekg(0, std::ios::end);
    const std::streamsize size = ifs.tellg();
    if (size < 0) {
        return std::unexpected("Failed to determine file size: " + filepath.string());
    }

    std::string content(static_cast<size_t>(size), '\0');
    ifs.seekg(0, std::ios::beg);

    if (!ifs.read(content.data(), size)) {
        return std::unexpected("Failed to read file: " + filepath.string());
    }

    return content;
}

std::optional<std::string>
load_file_as_string(std::string_view filepath) {
    std::ifstream file(filepath.data());
    if (!file.is_open()) {
        return std::nullopt;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    return content;
}
