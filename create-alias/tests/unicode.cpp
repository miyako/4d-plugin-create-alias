#include "unicode.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    using namespace alias_plugin;
    const std::string text = "ASCII \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e \xf0\x9f\x98\x80";
    if (toUtf8(fromUtf8(text)) != text) throw std::runtime_error("Unicode round trip");
    for (const std::string invalid : {"\xc0\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82", "\x80"}) {
        bool rejected = false;
        try { fromUtf8(invalid); } catch (const std::invalid_argument&) { rejected = true; }
        if (!rejected) throw std::runtime_error("Accepted invalid UTF-8");
    }
    for (auto invalid : {std::u16string(1, 0xd800), std::u16string(1, 0xdc00)}) {
        bool rejected = false;
        try { toUtf8(invalid); } catch (const std::invalid_argument&) { rejected = true; }
        if (!rejected) throw std::runtime_error("Accepted unpaired surrogate");
    }
    std::cout << "Unicode tests passed\n";
}
