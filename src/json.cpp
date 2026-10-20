#include "kiln/json.hpp"

namespace kiln::json {

void append_json_unicode(std::string& out, std::string_view in, std::size_t& i);
Value parse_json_number(std::string_view in, std::size_t& i);

namespace {

class Parser {
 public:
    explicit Parser(std::string_view in) : in_(in) {}

    Value parse_value() {
        skip();
        if (i_ >= in_.size()) {
            throw Error("SCHEMA");
        }
        const char c = in_[i_];
        if (c == 'n') {
            return literal("null", Value{});
        }
        if (c == 't') {
            Value v;
            v.type = Value::Type::Bool;
            v.b = true;
            return literal("true", v);
        }
        if (c == 'f') {
            Value v;
            v.type = Value::Type::Bool;
            v.b = false;
            return literal("false", v);
        }
        if (c == '"') {
            return parse_string();
        }
        if (c == '[') {
            return parse_array();
        }
        if (c == '{') {
            return parse_object();
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            return parse_number();
        }
        throw Error("SCHEMA");
    }

    void finish() {
        skip();
        if (i_ != in_.size()) {
            throw Error("SCHEMA");
        }
    }

 private:
    std::string_view in_;
    std::size_t i_ = 0;

    void skip() {
        while (i_ < in_.size()) {
            const char c = in_[i_];
            if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
                break;
            }
            ++i_;
        }
    }

    char get() {
        if (i_ >= in_.size()) {
            throw Error("SCHEMA");
        }
        return in_[i_++];
    }

    Value literal(std::string_view word, Value v) {
        if (in_.substr(i_, word.size()) != word) {
            throw Error("SCHEMA");
        }
        i_ += word.size();
        return v;
    }

    Value parse_string() {
        if (get() != '"') {
            throw Error("SCHEMA");
        }
        std::string out;
        while (i_ < in_.size()) {
            char c = get();
            if (c == '"') {
                Value v;
                v.type = Value::Type::String;
                v.s = std::move(out);
                return v;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (i_ >= in_.size()) {
                throw Error("SCHEMA");
            }
            char e = get();
            switch (e) {
                case '"':
                case '\\':
                case '/':
                    out.push_back(e);
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u':
                    append_json_unicode(out, in_, i_);
                    break;
                default:
                    throw Error("SCHEMA");
            }
        }
        throw Error("SCHEMA");
    }

    Value parse_array() {
        get();
        Value v;
        v.type = Value::Type::Array;
        skip();
        if (i_ < in_.size() && in_[i_] == ']') {
            ++i_;
            return v;
        }
        while (true) {
            v.a.push_back(parse_value());
            skip();
            char c = get();
            if (c == ']') {
                return v;
            }
            if (c != ',') {
                throw Error("SCHEMA");
            }
        }
    }

    Value parse_object() {
        get();
        Value v;
        v.type = Value::Type::Object;
        skip();
        if (i_ < in_.size() && in_[i_] == '}') {
            ++i_;
            return v;
        }
        while (true) {
            skip();
            Value key = parse_string();
            skip();
            if (get() != ':') {
                throw Error("SCHEMA");
            }
            Value child = parse_value();
            v.o.emplace_back(std::move(key.s), std::move(child));
            skip();
            char c = get();
            if (c == '}') {
                return v;
            }
            if (c != ',') {
                throw Error("SCHEMA");
            }
        }
    }

    Value parse_number() { return parse_json_number(in_, i_); }
};

}  // namespace

Value parse(std::string_view text) {
    Parser parser(text);
    Value v = parser.parse_value();
    parser.finish();
    return v;
}

}  // namespace kiln::json
