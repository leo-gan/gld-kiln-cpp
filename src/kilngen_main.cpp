#include "kiln/codegen.hpp"

#include "kiln/error.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void usage() {
    std::cerr << "usage: gld-kilngen-cpp <schema.json> -o <output.hpp>\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string schema_path;
    std::string output_path;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) {
                usage();
                return 2;
            }
            output_path = argv[++i];
        } else if (!arg.empty() && arg[0] == '-') {
            usage();
            return 2;
        } else {
            schema_path = arg;
        }
    }
    if (schema_path.empty() || output_path.empty()) {
        usage();
        return 2;
    }
    std::ifstream input(schema_path);
    if (!input) {
        std::cerr << "failed to open " << schema_path << "\n";
        return 1;
    }
    std::stringstream buffer;
    buffer << input.rdbuf();
    std::string text;
    try {
        text = kiln::emit_cpp(buffer.str());
    } catch (const std::runtime_error& err) {
        std::cerr << err.what() << "\n";
        return 1;
    }
    std::ofstream output(output_path);
    if (!output) {
        std::cerr << "failed to open " << output_path << "\n";
        return 1;
    }
    output << text;
    return output ? 0 : 1;
}
