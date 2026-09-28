#include <stellar/engine/json_document.hpp>

#include <algorithm>
#include <cstdint>

namespace stellar::engine::json {
namespace {

void validate_utf8(std::string_view input) {
  const auto fail = [&](std::string message, std::size_t byte) -> void {
    const auto line = static_cast<std::size_t>(
        std::count(input.begin(), input.begin() + byte, '\n'));
    throw ParseFailure(std::move(message), line, byte);
  };
  std::size_t position = input.starts_with("\xEF\xBB\xBF") ? 3 : 0;
  while (position < input.size()) {
    const auto first = static_cast<unsigned char>(input[position]);
    if (first <= 0x7f) {
      ++position;
      continue;
    }

    std::size_t continuation_count{};
    std::uint32_t code_point{};
    std::uint32_t minimum{};
    if (first >= 0xc2 && first <= 0xdf) {
      continuation_count = 1;
      code_point = first & 0x1f;
      minimum = 0x80;
    } else if (first >= 0xe0 && first <= 0xef) {
      continuation_count = 2;
      code_point = first & 0x0f;
      minimum = 0x800;
    } else if (first >= 0xf0 && first <= 0xf4) {
      continuation_count = 3;
      code_point = first & 0x07;
      minimum = 0x10000;
    } else {
      fail("Invalid UTF-8 leading byte in JSON input.", position);
    }

    if (continuation_count > input.size() - position - 1)
      fail("Incomplete UTF-8 sequence in JSON input.", position);
    for (std::size_t offset = 1; offset <= continuation_count; ++offset) {
      const auto continuation =
          static_cast<unsigned char>(input[position + offset]);
      if ((continuation & 0xc0) != 0x80)
        fail("Invalid UTF-8 continuation byte in JSON input.",
             position + offset);
      code_point = (code_point << 6) | (continuation & 0x3f);
    }
    if (code_point < minimum || code_point > 0x10ffff ||
        (code_point >= 0xd800 && code_point <= 0xdfff))
      fail("Invalid UTF-8 scalar value in JSON input.", position);
    position += continuation_count + 1;
  }
}

class Parser {
public:
  explicit Parser(std::string_view input, ParseOptions options = {},
                  std::size_t base_byte = 0, std::size_t base_line = 0,
                  bool already_validated = false)
      : input_(input), options_(options), base_byte_(base_byte), line_(base_line) {
    if (!already_validated) validate_utf8(input_);
    if (input_.starts_with("\xEF\xBB\xBF"))
      position_ = 3;
  }

  Value parse() {
    whitespace();
    auto result = value(0);
    whitespace();
    if (position_ != input_.size())
      error("Additional text encountered after finished reading JSON content.");
    return result;
  }

  void visit_elements(unsigned depth,
      const std::function<void(const Value&, std::size_t)>& visitor) {
    (void)take(); // validated opening '['
    whitespace();
    if (input_[position_] == ']') return;
    std::size_t index = 0;
    while (true) {
      const auto item = value(depth + 1);
      visitor(item, index++);
      whitespace();
      if (take() == ']') return;
    }
  }

private:
  bool defer_array() const {
    return array_nesting_ == 0 &&
        std::find(options_.deferred_array_pointers.begin(),
                  options_.deferred_array_pointers.end(), pointer_) !=
            options_.deferred_array_pointers.end();
  }

  [[noreturn]] void error(std::string message) const {
    throw ParseFailure(std::move(message), line_, base_byte_ + position_);
  }

  char take() {
    if (position_ == input_.size())
      error("Expected another JSON token.");
    const auto result = input_[position_++];
    if (result == '\n')
      ++line_;
    return result;
  }

  void whitespace() {
    while (position_ != input_.size()) {
      const auto next = input_[position_];
      if (next != ' ' && next != '\t' && next != '\r' && next != '\n')
        break;
      (void)take();
    }
  }

  bool consume(std::string_view wanted) {
    if (!input_.substr(position_).starts_with(wanted))
      return false;
    for (std::size_t index = 0; index != wanted.size(); ++index)
      (void)take();
    return true;
  }

  static void append_utf8(std::string &result, std::uint32_t code_point) {
    if (code_point <= 0x7f) {
      result.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7ff) {
      result.push_back(static_cast<char>(0xc0 | (code_point >> 6)));
      result.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
    } else if (code_point <= 0xffff) {
      result.push_back(static_cast<char>(0xe0 | (code_point >> 12)));
      result.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
      result.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
    } else {
      result.push_back(static_cast<char>(0xf0 | (code_point >> 18)));
      result.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3f)));
      result.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
      result.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
    }
  }

  std::uint32_t hex4() {
    std::uint32_t value{};
    for (int index = 0; index != 4; ++index) {
      const auto character = take();
      value <<= 4;
      if (character >= '0' && character <= '9')
        value |= static_cast<unsigned>(character - '0');
      else if (character >= 'a' && character <= 'f')
        value |= static_cast<unsigned>(character - 'a' + 10);
      else if (character >= 'A' && character <= 'F')
        value |= static_cast<unsigned>(character - 'A' + 10);
      else
        error("Invalid hexadecimal character in JSON string escape.");
    }
    return value;
  }

  std::string string(bool materialize = true) {
    if (take() != '"')
      error("Expected a JSON string.");
    std::string result;
    while (true) {
      const auto character = take();
      if (character == '"')
        return result;
      if (static_cast<unsigned char>(character) < 0x20)
        error("Control character is not permitted in a JSON string.");
      if (character != '\\') {
        if (materialize) result.push_back(character);
        continue;
      }
      switch (take()) {
      case '"': if (materialize) result.push_back('"'); break;
      case '\\': if (materialize) result.push_back('\\'); break;
      case '/': if (materialize) result.push_back('/'); break;
      case 'b': if (materialize) result.push_back('\b'); break;
      case 'f': if (materialize) result.push_back('\f'); break;
      case 'n': if (materialize) result.push_back('\n'); break;
      case 'r': if (materialize) result.push_back('\r'); break;
      case 't': if (materialize) result.push_back('\t'); break;
      case 'u': {
        auto code_point = hex4();
        if (code_point >= 0xd800 && code_point <= 0xdbff) {
          if (!consume("\\u"))
            error("A high surrogate must be followed by a low surrogate.");
          const auto low = hex4();
          if (low < 0xdc00 || low > 0xdfff)
            error("A high surrogate must be followed by a low surrogate.");
          code_point = 0x10000 + ((code_point - 0xd800) << 10) +
                       (low - 0xdc00);
        } else if (code_point >= 0xdc00 && code_point <= 0xdfff) {
          error("A low surrogate must follow a high surrogate.");
        }
        if (materialize) append_utf8(result, code_point);
        break;
      }
      default: error("Invalid escaped character in JSON string.");
      }
    }
  }

  Value number(std::size_t byte, std::size_t line, bool materialize) {
    const auto start = position_;
    if (position_ != input_.size() && input_[position_] == '-')
      ++position_;
    if (position_ == input_.size())
      error("Incomplete JSON number.");
    if (input_[position_] == '0') {
      ++position_;
    } else {
      if (input_[position_] < '1' || input_[position_] > '9')
        error("Invalid JSON number.");
      while (position_ != input_.size() && input_[position_] >= '0' &&
             input_[position_] <= '9')
        ++position_;
    }
    if (position_ != input_.size() && input_[position_] == '.') {
      ++position_;
      const auto digits = position_;
      while (position_ != input_.size() && input_[position_] >= '0' &&
             input_[position_] <= '9')
        ++position_;
      if (digits == position_)
        error("A decimal point must be followed by a digit.");
    }
    if (position_ != input_.size() &&
        (input_[position_] == 'e' || input_[position_] == 'E')) {
      ++position_;
      if (position_ != input_.size() &&
          (input_[position_] == '+' || input_[position_] == '-'))
        ++position_;
      const auto digits = position_;
      while (position_ != input_.size() && input_[position_] >= '0' &&
             input_[position_] <= '9')
        ++position_;
      if (digits == position_)
        error("A JSON exponent must contain a digit.");
    }
    if (!materialize) return {nullptr, byte, line};
    return {{Value::Number{std::string(input_.substr(start, position_ - start))}},
            byte, line};
  }

  Value value(unsigned depth, bool materialize = true) {
    whitespace();
    const auto start = position_;
    const auto byte = base_byte_ + position_;
    const auto line = line_;
    if (position_ == input_.size())
      error("Expected a JSON value.");
    if (input_[position_] == '{') {
      if (depth >= 64)
        error("The maximum configured JSON depth has been exceeded.");
      (void)take();
      Value::Object result;
      whitespace();
      if (position_ != input_.size() && input_[position_] == '}') {
        (void)take();
        return {std::move(result), byte, line};
      }
      while (true) {
        whitespace();
        if (position_ == input_.size() || input_[position_] != '"')
          error("Expected a JSON property name.");
        auto name = string(materialize);
        whitespace();
        if (take() != ':')
          error("Expected ':' after a JSON property name.");
        const auto pointer_size = pointer_.size();
        if (materialize && !options_.deferred_array_pointers.empty()) {
          pointer_ += '/';
          for (const char c : name) {
            if (c == '~') pointer_ += "~0";
            else if (c == '/') pointer_ += "~1";
            else pointer_ += c;
          }
        }
        auto child = value(depth + 1, materialize);
        pointer_.resize(pointer_size);
        if (materialize) result.emplace_back(std::move(name), std::move(child));
        whitespace();
        const auto separator = take();
        if (separator == '}')
          return {std::move(result), byte, line};
        if (separator != ',')
          error("Expected ',' or '}' in a JSON object.");
      }
    }
    if (input_[position_] == '[') {
      if (depth >= 64)
        error("The maximum configured JSON depth has been exceeded.");
      (void)take();
      const bool deferred = materialize && defer_array();
      const bool collect = materialize && !deferred;
      ++array_nesting_;
      std::size_t count = 0;
      Value::Array result;
      const auto finish = [&]() -> Value {
        --array_nesting_;
        if (deferred) return {Value::DeferredArray{input_.substr(start, position_ - start), count, depth}, byte, line};
        return {std::move(result), byte, line};
      };
      whitespace();
      if (position_ != input_.size() && input_[position_] == ']') {
        (void)take();
        return finish();
      }
      while (true) {
        auto item = value(depth + 1, collect);
        ++count;
        if (collect) result.push_back(std::move(item));
        whitespace();
        const auto separator = take();
        if (separator == ']')
          return finish();
        if (separator != ',')
          error("Expected ',' or ']' in a JSON array.");
      }
    }
    if (input_[position_] == '"')
      return {string(materialize), byte, line};
    if (consume("true"))
      return {true, byte, line};
    if (consume("false"))
      return {false, byte, line};
    if (consume("null"))
      return {nullptr, byte, line};
    if (input_[position_] == '-' ||
        (input_[position_] >= '0' && input_[position_] <= '9'))
      return number(byte, line, materialize);
    error("Invalid JSON value.");
  }

  std::string_view input_;
  ParseOptions options_;
  std::string pointer_;
  unsigned array_nesting_{};
  std::size_t base_byte_{};
  std::size_t position_{};
  std::size_t line_{};
};

} // namespace

Value parse_ordered_json(std::string_view input, ParseOptions options) {
  return Parser(input, options).parse();
}

std::optional<std::size_t> array_size(const Value& value) {
  if (const auto* items = std::get_if<Value::Array>(&value.data)) return items->size();
  if (const auto* items = std::get_if<Value::DeferredArray>(&value.data)) return items->count;
  return std::nullopt;
}

bool visit_array(const Value& value,
    const std::function<void(const Value&, std::size_t)>& visitor) {
  if (const auto* items = std::get_if<Value::Array>(&value.data)) {
    for (std::size_t i = 0; i < items->size(); ++i) visitor((*items)[i], i);
  } else if (const auto* deferred = std::get_if<Value::DeferredArray>(&value.data)) {
    Parser(deferred->source, {}, value.byte, value.line, true).visit_elements(deferred->depth, visitor);
  } else return false;
  return true;
}

} // namespace stellar::engine::json
