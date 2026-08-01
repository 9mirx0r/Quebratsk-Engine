#include "keyvalues.h"

#include <cctype>

namespace quebratsk::io {

std::vector<KVToken> tokenize_keyvalues(std::string_view text) {
    std::vector<KVToken> out;
    size_t i = 0;

    while (i < text.size()) {
        const char c = text[i];

        if (std::isspace(static_cast<unsigned char>(c))) {
            ++i;
        } else if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n') ++i;
        } else if (c == '{' || c == '}') {
            out.push_back({std::string(1, c), true});
            ++i;
        } else if (c == '"') {
            const size_t start = ++i;
            while (i < text.size() && text[i] != '"') ++i;
            out.push_back({std::string(text.substr(start, i - start)), false});
            if (i < text.size()) ++i; // closing quote
        } else {
            const size_t start = i;
            while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))
                   && text[i] != '{' && text[i] != '}' && text[i] != '"') {
                ++i;
            }
            out.push_back({std::string(text.substr(start, i - start)), false});
        }
    }
    return out;
}

} // namespace quebratsk::io
