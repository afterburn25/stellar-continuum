#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace stellar::core::json_detail {

struct Value {
  struct Number { std::string text; };
  using Array = std::vector<Value>;
  using Object = std::vector<std::pair<std::string, Value>>;
  using Data = std::variant<std::nullptr_t, bool, Number, std::string, Array, Object>;

  Data data;
  std::size_t byte{};
  std::size_t line{};
};

class ParseFailure final : public std::runtime_error {
public:
  ParseFailure(std::string message, std::size_t line, std::size_t byte)
      : std::runtime_error(std::move(message)), line(line), byte(byte) {}
  std::size_t line;
  std::size_t byte;
};

Value parse_ordered_json(std::string_view input);

} // namespace stellar::core::json_detail
