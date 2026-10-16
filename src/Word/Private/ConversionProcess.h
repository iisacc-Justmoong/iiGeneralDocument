#pragma once
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>
namespace ii::document::detail {
struct ConversionProcessResult {
    bool started{};
    bool timedOut{};
    int exitCode{-1};
    std::string output, error;
};
ConversionProcessResult runConversionProcess(const std::filesystem::path &executable,
                                             const std::vector<std::string> &arguments,
                                             const std::filesystem::path &directory,
                                             std::chrono::milliseconds timeout);
std::string pathUtf8(const std::filesystem::path &path);
std::string localFileUri(const std::filesystem::path &path);
} // namespace ii::document::detail
