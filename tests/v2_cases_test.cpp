#include "support.hpp"

#include "kiln/frozen.hpp"
#include "kiln/json.hpp"
#include "kiln/schema.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

int fail(const std::string& id, const std::string& why) {
    std::cerr << id << " " << why << "\n";
    return 1;
}

int run_cases(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        return fail(path, "failed to open");
    }
    std::stringstream buffer;
    buffer << input.rdbuf();
    const kiln::json::Value doc = kiln::json::parse(buffer.str());
    const kiln::json::Value* cases = doc.find("cases");
    if (cases == nullptr || !cases->is_array()) {
        return fail(path, "missing cases");
    }
    int count = 0;
    for (const kiln::json::Value& item : cases->a) {
        ++count;
        const std::string id = item.find("id")->s;
        const std::vector<std::uint8_t> raw = kiln::test::unhex(item.find("input")->s);
        const kiln::Schema schema = kiln::schema_from_case(*item.find("schema"));
        const bool framed = item.find("schema")->is_string() && item.find("schema")->s == "frame";
        const bool reject = item.find("expect")->s == "reject";
        try {
            const kiln::Value value = framed ? kiln::decode_framed(schema, raw) : kiln::decode(schema, raw);
            if (reject) {
                return fail(id, "expected reject");
            }
            if (const kiln::json::Value* want = item.find("decoded")) {
                if (framed) {
                    const kiln::json::Value* hash = want->find("hash");
                    if (hash == nullptr || !hash->is_uint || hash->u != kiln::type_hash(schema)) {
                        return fail(id, "hash");
                    }
                }
                kiln::json::Value expect = *want;
                if (framed) {
                    expect.o.clear();
                    expect.o.emplace_back("id", *want->find("id"));
                    expect.o.emplace_back("batch", *want->find("batch"));
                }
                if (!kiln::test::same(kiln::test::project(value), expect)) {
                    return fail(id, "value mismatch");
                }
            }
            const std::vector<std::uint8_t> again = framed ? kiln::encode_framed(schema, value) : kiln::encode(schema, value);
            if (again != raw) {
                return fail(id, "bytes mismatch");
            }
        } catch (const kiln::Error& err) {
            if (!reject || item.find("error") == nullptr || err.code() != item.find("error")->s) {
                return fail(id, std::string("error ") + err.code());
            }
        }
    }
    if (count != 46) {
        return fail(path, "expected 46 cases");
    }
    std::cout << "v2 cases " << count << "\n";
    return 0;
}

int run_reference() {
    const kiln::Schema point = kiln::struct_of({{"x", kiln::scalar(kiln::Kind::I32)}, {"y", kiln::scalar(kiln::Kind::I32)}});
    const kiln::Value point_value = kiln::Value::dict({{"x", kiln::Value::i64(9)}, {"y", kiln::Value::i64(-4)}});
    const auto point_raw = kiln::encode(point, point_value);
    if (kiln::test::hex_of(point_raw.data(), point_raw.size()) != "09000000fcffffff") {
        return fail("point", "encode");
    }
    if (kiln::decode(point, point_raw).find("x")->as_i64() != 9) {
        return fail("point", "decode");
    }
    const auto point_frame = kiln::encode_framed(point, point_value);
    if (kiln::test::hex_of(point_frame.data(), point_frame.size()) != "4b4c4e30000000002fc7036138c06ae609000000fcffffff") {
        return fail("point", "frame");
    }
    if (kiln::decode_framed(point, point_frame).find("y")->as_i64() != -4) {
        return fail("point", "framed decode");
    }
    const kiln::Schema message = kiln::struct_of({
        {"f_bool", kiln::scalar(kiln::Kind::Bool)}, {"f_int32", kiln::scalar(kiln::Kind::I32)},
        {"f_int64", kiln::scalar(kiln::Kind::I64)}, {"f_float64", kiln::scalar(kiln::Kind::F64)},
        {"f_string", kiln::scalar(kiln::Kind::Str)}, {"f_bool_2", kiln::scalar(kiln::Kind::Bool)},
        {"f_int32_2", kiln::scalar(kiln::Kind::I32)}, {"f_string_2", kiln::scalar(kiln::Kind::Str)},
    });
    const kiln::Value message_value = kiln::Value::dict({
        {"f_bool", kiln::Value::boolean(true)}, {"f_int32", kiln::Value::i64(-2)}, {"f_int64", kiln::Value::i64(99)},
        {"f_float64", kiln::Value::f64(1.5)}, {"f_string", kiln::Value::str("hello")}, {"f_bool_2", kiln::Value::boolean(false)},
        {"f_int32_2", kiln::Value::i64(7)}, {"f_string_2", kiln::Value::str("world")},
    });
    const auto message_raw = kiln::encode(message, message_value);
    const char* message_hex = "01feffffff6300000000000000000000000000f83f0500000068656c6c6f000700000005000000776f726c64";
    if (kiln::test::hex_of(message_raw.data(), message_raw.size()) != message_hex) {
        return fail("message", "encode");
    }
    if (kiln::decode(message, message_raw).find("f_string")->as_str() != "hello") {
        return fail("message", "decode");
    }
    if (kiln::decode_framed(message, kiln::encode_framed(message, message_value)).find("f_int64")->as_i64() != 99) {
        return fail("message", "frame");
    }
    const kiln::Schema dict = kiln::dict_of(kiln::scalar(kiln::Kind::I32));
    const auto dict_raw = kiln::encode(dict, kiln::Value::dict({{"b", kiln::Value::i64(2)}, {"a", kiln::Value::i64(1)}}));
    if (kiln::test::hex_of(dict_raw.data(), dict_raw.size()) != "02000000010000006101000000010000006202000000") {
        return fail("dict", "order");
    }
    const auto unicode = kiln::encode(dict, kiln::Value::dict({{"é", kiln::Value::i64(2)}, {"a", kiln::Value::i64(1)}}));
    if (kiln::test::hex_of(unicode.data(), unicode.size()) != "0200000001000000610100000002000000c3a902000000") {
        return fail("dict", "utf8 order");
    }
    const kiln::Schema opt = kiln::opt_of(kiln::scalar(kiln::Kind::Str));
    if (!kiln::decode(opt, kiln::encode(opt, kiln::Value::null())).is_null()) {
        return fail("opt", "none");
    }
    if (kiln::decode(opt, kiln::encode(opt, kiln::Value::str("z"))).as_str() != "z") {
        return fail("opt", "z");
    }
    const kiln::Schema list = kiln::list_of(kiln::scalar(kiln::Kind::F64));
    const auto nan = kiln::test::unhex("020000000000000000000080010000000000f87f");
    const kiln::Value decoded = kiln::decode(list, nan);
    if ((decoded.as_list()[0].f64_bits() >> 63) == 0 || decoded.as_list()[1].f64_bits() != 0x7ff8000000000001ull) {
        return fail("f64", "sign or nan");
    }
    if (kiln::encode(list, decoded) != nan) {
        return fail("f64", "reencode");
    }
    const kiln::Schema hostile = kiln::list_of(kiln::struct_of({{"a", kiln::scalar(kiln::Kind::I64)}}));
    try {
        kiln::decode(hostile, kiln::test::unhex("00000001"));
        return fail("hostile", "missing error");
    } catch (const kiln::Error& err) {
        if (std::string(err.code()) != "EOF") {
            return fail("hostile", err.code());
        }
    }
    const kiln::Schema text = kiln::scalar(kiln::Kind::Str);
    try {
        kiln::decode(text, kiln::test::unhex("01000000ff"));
        return fail("utf8", "missing error");
    } catch (const kiln::Error& err) {
        if (std::string(err.code()) != "UTF8") {
            return fail("utf8", err.code());
        }
    }
    kiln::ReadOpts trusted;
    trusted.trusted = true;
    if (kiln::decode(text, kiln::test::unhex("01000000ff"), trusted).as_str() != "\xff") {
        return fail("trusted", "utf8");
    }
    std::cout << "reference ok\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "testdata/kiln-v2-cases.json";
    if (const int status = run_cases(path)) {
        return status;
    }
    return run_reference();
}
