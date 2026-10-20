#pragma once

#include <string>
#include <string_view>

namespace kiln {

std::string emit_cpp(std::string_view json_text);

}  // namespace kiln
