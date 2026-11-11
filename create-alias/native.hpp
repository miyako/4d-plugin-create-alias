#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace alias_plugin {

struct Error {
    std::string code;
    std::string message;
    std::string option;
    std::string nativeDomain;
    std::int64_t nativeCode = 0;
};

struct Options {
    bool overwrite = false;
    std::optional<std::vector<std::string>> arguments;
    std::optional<std::string> argumentsText;
    std::optional<std::string> workingDirectory;
    std::optional<std::string> description;
    std::optional<std::string> iconPath;
    std::optional<int> iconIndex;
    std::optional<int> showCommand;
};

struct Result {
    bool ok = false;
    std::string targetPath;
    std::string aliasPath;
    std::string kind;
    std::vector<Error> errors;
};

// Paths are absolute native paths encoded as UTF-8. The 4D adapter handles conversion.
Result createAlias(const std::string& target, const std::string& destination,
                   const Options& options);

std::string quoteArguments(const std::vector<std::string>& arguments);

}
