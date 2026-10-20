#pragma once

#include "kiln/schema.hpp"

#include <string>

namespace kiln {

std::string cpp_type_name(const Schema& schema);
std::string schema_expr(const Schema& schema);
std::string member_line(const Schema& schema, const std::string& name);

}  // namespace kiln
