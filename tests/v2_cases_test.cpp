#include "support.hpp"

#include "kiln/frozen.hpp"
#include "kiln/json.hpp"
#include "kiln/schema.hpp"

#include <cstdint>
#include <cstring>
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
    const auto f64s = decoded.as_f64s();
    std::uint64_t b0 = 0;
    std::uint64_t b1 = 0;
    if (f64s.size() != 2) {
        return fail("f64", "count");
    }
    std::memcpy(&b0, &f64s[0], 8);
    std::memcpy(&b1, &f64s[1], 8);
    if ((b0 >> 63) == 0 || b1 != 0x7ff8000000000001ull) {
        return fail("f64", "sign or nan");
    }
    if (kiln::encode(list, decoded) != nan) {
        return fail("f64", "reencode");
    }
    std::uint32_t neg0 = 0x80000000u;
    std::uint32_t qnan = 0x7fc00000u;
    float neg_f = 0;
    float nan_f = 0;
    std::memcpy(&neg_f, &neg0, 4);
    std::memcpy(&nan_f, &qnan, 4);
    const float samples[3] = {neg_f, nan_f, 1.5f};
    const kiln::Schema f32_schema = kiln::list_of(kiln::scalar(kiln::Kind::F32));
    kiln::Value::List boxed;
    boxed.push_back(kiln::Value::f32_bits(neg0));
    boxed.push_back(kiln::Value::f32_bits(qnan));
    const auto boxed_raw = kiln::encode(f32_schema, kiln::Value::list(std::move(boxed)));
    if (kiln::test::hex_of(boxed_raw.data(), boxed_raw.size()) != "02000000000000800000c07f") {
        return fail("f32", "boxed");
    }
    const auto f32_raw = kiln::encode(f32_schema, kiln::Value::f32_view(samples, 3));
    if (f32_raw.size() != 16 || kiln::test::hex_of(f32_raw.data() + 4, 8) != "000000800000c07f") {
        return fail("f32", "wire");
    }
    kiln::Value slot;
    kiln::decode_into(f32_schema, f32_raw, slot);
    const float* stable = slot.as_f32s().data();
    kiln::decode_into(f32_schema, f32_raw, slot);
    const auto got_f32 = slot.as_f32s();
    std::uint32_t g0 = 0;
    std::uint32_t g1 = 0;
    if (got_f32.size() != 3 || got_f32.data() != stable) {
        return fail("f32", "reuse");
    }
    std::memcpy(&g0, &got_f32[0], 4);
    std::memcpy(&g1, &got_f32[1], 4);
    if (g0 != neg0 || g1 != qnan || got_f32[2] != 1.5f) {
        return fail("f32", "bits");
    }
    const std::int32_t i32_in[2] = {-1, 2};
    const kiln::Schema i32_schema = kiln::list_of(kiln::scalar(kiln::Kind::I32));
    const auto i32_raw = kiln::encode(i32_schema, kiln::Value::i32_view(i32_in, 2));
    if (kiln::test::hex_of(i32_raw.data(), i32_raw.size()) != "02000000ffffffff02000000") {
        return fail("i32", "wire");
    }
    if (kiln::decode(i32_schema, i32_raw).as_i32s()[1] != 2) {
        return fail("i32", "decode");
    }
    const std::int64_t i64_in[2] = {-1, 2};
    try {
        kiln::encode(i32_schema, kiln::Value::i64_view(i64_in, 2));
        return fail("i32", "wide");
    } catch (const kiln::Error& err) {
        if (std::string(err.code()) != "TYPE") {
            return fail("i32", err.code());
        }
    }
    const auto i64_raw = kiln::encode(kiln::list_of(kiln::scalar(kiln::Kind::Int)), kiln::Value::i64_view(i64_in, 2));
    if (i64_raw.size() != 20) {
        return fail("i64", "width");
    }
    kiln::Value::List narrow;
    narrow.push_back(kiln::Value::i64(-1));
    narrow.push_back(kiln::Value::i64(2));
    const auto i8_raw = kiln::encode(kiln::list_of(kiln::scalar(kiln::Kind::I8)), kiln::Value::list(narrow));
    if (kiln::test::hex_of(i8_raw.data(), i8_raw.size()) != "02000000ff02") {
        return fail("i8", "wire");
    }
    const auto u32_raw = kiln::encode(kiln::list_of(kiln::scalar(kiln::Kind::U32)),
                                      kiln::Value::list({kiln::Value::u64(1), kiln::Value::u64(2)}));
    if (u32_raw.size() != 12 || kiln::test::hex_of(u32_raw.data(), u32_raw.size()) != "020000000100000002000000") {
        return fail("u32", "wire");
    }
    const kiln::Schema matrix = kiln::struct_of({
        {"rows", kiln::scalar(kiln::Kind::I32)},
        {"cols", kiln::scalar(kiln::Kind::I32)},
        {"values", kiln::list_of(kiln::scalar(kiln::Kind::F32))},
    });
    const auto matrix_raw = kiln::encode(matrix, kiln::Value::dict({
        {"rows", kiln::Value::i64(1)},
        {"cols", kiln::Value::i64(3)},
        {"values", kiln::Value::f32_view(samples, 3)},
    }));
    kiln::Value matrix_slot;
    kiln::decode_into(matrix, matrix_raw, matrix_slot);
    const float* matrix_ptr = matrix_slot.find("values")->as_f32s().data();
    kiln::decode_into(matrix, matrix_raw, matrix_slot);
    if (matrix_slot.find("rows")->as_i64() != 1 || matrix_slot.find("values")->as_f32s().data() != matrix_ptr) {
        return fail("struct", "reuse");
    }
    std::memcpy(&g0, &matrix_slot.find("values")->as_f32s()[0], 4);
    if (g0 != neg0) {
        return fail("struct", "negzero");
    }
    const kiln::Value copied = matrix_slot;
    if (copied.find("values")->as_f32s()[2] != 1.5f) {
        return fail("copy", "f32");
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
