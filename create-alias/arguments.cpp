#include "native.hpp"

namespace alias_plugin {

std::string quoteArguments(const std::vector<std::string>& arguments) {
    std::string result;
    for (const auto& argument : arguments) {
        if (!result.empty()) result += ' ';
        result += '"';
        std::size_t backslashes = 0;
        for (char character : argument) {
            if (character == '\\') {
                ++backslashes;
            } else {
                result.append(backslashes * (character == '"' ? 2 : 1), '\\');
                if (character == '"') result += '\\';
                result += character;
                backslashes = 0;
            }
        }
        result.append(backslashes * 2, '\\');
        result += '"';
    }
    return result;
}

}
