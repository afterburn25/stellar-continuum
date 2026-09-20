#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace stellar::engine::json {

// Ordered members preserve duplicates, number spelling and source locations so
// consumers can apply their own schema and error precedence after syntax checks.
struct Value {
  struct Number { std::string text; };
  struct DeferredArray {
    std::string_view source;
    std::size_t count{};
    unsigned depth{};
  };
  using Array = std::vector<Value>;
  using Object = std::vector<std::pair<std::string, Value>>;
  using Data = std::variant<std::nullptr_t, bool, Number, std::string, Array, Object, DeferredArray>;
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

struct ParseOptions {
  // Exact JSON pointers through object members (e.g. /catalog/entries).
  // Arrays below array elements are not selected. All input is still checked
  // for valid UTF-8, syntax and depth before parse returns. Selected arrays
  // borrow input bytes: keep the input alive and immutable until their last use.
  std::span<const std::string_view> deferred_array_pointers;
};

[[nodiscard]] Value parse_ordered_json(std::string_view input, ParseOptions options = {});
[[nodiscard]] std::optional<std::size_t> array_size(const Value&);
// Visits one temporary element at a time for a deferred array. Element borrows
// expire after the callback. Exceptions propagate unchanged. False = not array.
bool visit_array(const Value&, const std::function<void(const Value&, std::size_t)>&);

} // namespace stellar::engine::json
