#include "kiln/json.hpp"

#include <cstdlib>
#include <stdexcept>
#include <string>

namespace kiln::json {
namespace {

char take(std::string_view in, std::size_t& i) {
    if (i >= in.size()) {
        throw Error("SCHEMA");
    }
    return in[i++];
}

std::uint32_t hex4(std::string_view in, std::size_t& i) {
    std::uint32_t v = 0;
    for (int n = 0; n < 4; ++n) {
        const char c = take(in, i);
        v <<= 4;
        if (c >= '0' && c <= '9') {
            v |= static_cast<std::uint32_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            v |= static_cast<std::uint32_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            v |= static_cast<std::uint32_t>(c - 'A' + 10);
        } else {
            throw Error("SCHEMA");
        }
    }
    return v;
}

void push_cp(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

}  // namespace

void append_json_unicode(std::string& out, std::string_view in, std::size_t& i) {
    std::uint32_t cp = hex4(in, i);
    if (cp >= 0xD800 && cp <= 0xDBFF) {
        if (take(in, i) != '\\' || take(in, i) != 'u') {
            throw Error("SCHEMA");
        }
        const std::uint32_t lo = hex4(in, i);
        if (lo < 0xDC00 || lo > 0xDFFF) {
            throw Error("SCHEMA");
        }
        cp = 0x10000 + (((cp - 0xD800) << 10) | (lo - 0xDC00));
    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
        throw Error("SCHEMA");
    }
    push_cp(out, cp);
}

Value parse_json_number(std::string_view in, std::size_t& i) {
    const std::size_t begin = i;
    if (in[i] == '-') {
        ++i;
    }
    if (i >= in.size() || in[i] < '0' || in[i] > '9') {
        throw Error("SCHEMA");
    }
    if (in[i] == '0') {
        ++i;
    } else {
        while (i < in.size() && in[i] >= '0' && in[i] <= '9') {
            ++i;
        }
    }
    bool frac = false;
    if (i < in.size() && in[i] == '.') {
        frac = true;
        ++i;
        if (i >= in.size() || in[i] < '0' || in[i] > '9') {
            throw Error("SCHEMA");
        }
        while (i < in.size() && in[i] >= '0' && in[i] <= '9') {
            ++i;
        }
    }
    if (i < in.size() && (in[i] == 'e' || in[i] == 'E')) {
        frac = true;
        ++i;
        if (i < in.size() && (in[i] == '+' || in[i] == '-')) {
            ++i;
        }
        if (i >= in.size() || in[i] < '0' || in[i] > '9') {
            throw Error("SCHEMA");
        }
        while (i < in.size() && in[i] >= '0' && in[i] <= '9') {
            ++i;
        }
    }
    const std::string text(in.substr(begin, i - begin));
    Value v;
    v.type = Value::Type::Number;
    v.d = std::strtod(text.c_str(), nullptr);
    if (!frac) {
        try {
            std::size_t used = 0;
            if (!text.empty() && text[0] == '-') {
                const long long s = std::stoll(text, &used, 10);
                if (used == text.size()) {
                    v.is_int = true;
                    v.i = static_cast<std::int64_t>(s);
                }
            } else {
                const unsigned long long u = std::stoull(text, &used, 10);
                if (used == text.size()) {
                    v.is_uint = true;
                    v.u = static_cast<std::uint64_t>(u);
                    if (v.u <= static_cast<std::uint64_t>(INT64_MAX)) {
                        v.is_int = true;
                        v.i = static_cast<std::int64_t>(v.u);
                    }
                }
            }
        } catch (const std::exception&) {
        }
    }
    return v;
}

}  // namespace kiln::json
