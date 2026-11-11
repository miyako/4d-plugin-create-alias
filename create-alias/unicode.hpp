#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace alias_plugin {

inline std::string toUtf8(std::u16string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        std::uint32_t c = text[i];
        if (c >= 0xd800 && c <= 0xdbff) {
            if (++i == text.size() || text[i] < 0xdc00 || text[i] > 0xdfff)
                throw std::invalid_argument("Unpaired UTF-16 surrogate");
            c = 0x10000 + ((c - 0xd800) << 10) + text[i] - 0xdc00;
        } else if (c >= 0xdc00 && c <= 0xdfff) {
            throw std::invalid_argument("Unpaired UTF-16 surrogate");
        }
        if (c < 0x80) out += static_cast<char>(c);
        else if (c < 0x800) {
            out += static_cast<char>(0xc0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 63));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xe0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 63));
            out += static_cast<char>(0x80 | (c & 63));
        } else {
            out += static_cast<char>(0xf0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 63));
            out += static_cast<char>(0x80 | ((c >> 6) & 63));
            out += static_cast<char>(0x80 | (c & 63));
        }
    }
    return out;
}

inline std::u16string fromUtf8(std::string_view text) {
    std::u16string out;
    for (std::size_t i = 0; i < text.size();) {
        auto b = static_cast<unsigned char>(text[i++]);
        std::uint32_t c = b;
        unsigned count = 0;
        if (b >= 0xc2 && b <= 0xdf) { c = b & 31; count = 1; }
        else if (b >= 0xe0 && b <= 0xef) { c = b & 15; count = 2; }
        else if (b >= 0xf0 && b <= 0xf4) { c = b & 7; count = 3; }
        else if (b >= 0x80) throw std::invalid_argument("Invalid UTF-8");
        for (unsigned j = 0; j < count; ++j) {
            if (i == text.size()) throw std::invalid_argument("Truncated UTF-8");
            auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) throw std::invalid_argument("Invalid UTF-8 continuation");
            c = (c << 6) | (next & 63);
        }
        if ((count == 1 && c < 0x80) || (count == 2 && c < 0x800) ||
            (count == 3 && c < 0x10000) || c > 0x10ffff ||
            (c >= 0xd800 && c <= 0xdfff))
            throw std::invalid_argument("Invalid UTF-8 code point");
        if (c <= 0xffff) out += static_cast<char16_t>(c);
        else {
            c -= 0x10000;
            out += static_cast<char16_t>(0xd800 | (c >> 10));
            out += static_cast<char16_t>(0xdc00 | (c & 1023));
        }
    }
    return out;
}

}
