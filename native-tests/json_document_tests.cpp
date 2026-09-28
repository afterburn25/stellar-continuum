#include <stellar/engine/json_document.hpp>

#include <array>
#include <iostream>

using namespace stellar::engine::json;
namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
void same(const Value& eager, const Value& deferred) {
  require(eager.byte == deferred.byte && eager.line == deferred.line, "Changed source location");
  if (const auto count = array_size(eager)) {
    require(array_size(deferred) == count, "Changed array count");
    const auto& items = std::get<Value::Array>(eager.data);
    require(visit_array(deferred, [&](const Value& item, std::size_t i) { same(items.at(i), item); }), "Lost array");
    return;
  }
  require(eager.data.index() == deferred.data.index(), "Changed value type");
  if (const auto* object = std::get_if<Value::Object>(&eager.data)) {
    const auto& other = std::get<Value::Object>(deferred.data);
    require(object->size() == other.size(), "Changed object size");
    for (std::size_t i = 0; i < object->size(); ++i) {
      require((*object)[i].first == other[i].first, "Changed member order or duplicate");
      same((*object)[i].second, other[i].second);
    }
  } else if (const auto* number = std::get_if<Value::Number>(&eager.data))
    require(number->text == std::get<Value::Number>(deferred.data).text, "Changed number spelling");
  else if (const auto* text = std::get_if<std::string>(&eager.data))
    require(*text == std::get<std::string>(deferred.data), "Changed string");
  else if (const auto* boolean = std::get_if<bool>(&eager.data))
    require(*boolean == std::get<bool>(deferred.data), "Changed boolean");
}
struct Failure { std::string message; std::size_t line, byte; };
Failure failure(std::string_view text, ParseOptions options) {
  try { (void)parse_ordered_json(text, options); }
  catch (const ParseFailure& error) { return {error.what(), error.line, error.byte}; }
  throw std::runtime_error("Invalid JSON was accepted");
}
}

int main() try {
  constexpr std::array<std::string_view, 2> selected{"/entries", "/a~1b/~0"};
  const std::string text = "\xEF\xBB\xBF\n{\"entries\": [\n{\"id\":-0.250e+02,\"id\":7,\"s\":\"\\uD83D\\uDE80\\n\\t\\\\\\\"\",\"v\":[true,false,null,{},[]]},\n42],\"entries\":[],\"a/b\":{\"~\":[1,2]},\"other\":[{\"entries\":[3]}]}";
  const auto eager = parse_ordered_json(text);
  const auto deferred = parse_ordered_json(text, {selected});
  same(eager, deferred);
  const auto& root = std::get<Value::Object>(deferred.data);
  require(std::holds_alternative<Value::DeferredArray>(root[0].second.data), "Selected array materialized");
  require(std::holds_alternative<Value::DeferredArray>(root[1].second.data), "Empty selected array materialized");
  const auto& escaped = std::get<Value::Object>(root[2].second.data)[0].second;
  require(std::holds_alternative<Value::DeferredArray>(escaped.data), "JSON pointer escaping failed");
  const auto& nested = std::get<Value::Object>(std::get<Value::Array>(root[3].second.data)[0].data)[0].second;
  require(std::holds_alternative<Value::Array>(nested.data), "Array-element path falsely matched");
  auto movable = parse_ordered_json(text, {selected});
  const auto again = std::move(movable);
  same(eager, again); // Moving a parsed tree must not invalidate its input views.
  struct Marker {};
  bool threw = false;
  try { visit_array(root[0].second, [](const Value&, std::size_t) { throw Marker{}; }); }
  catch (const Marker&) { threw = true; }
  require(threw, "Visitor exception was swallowed");
  same(eager, deferred); // Throwing a visitor must leave the document usable.
  std::vector<std::string> invalid{
      "{\"entries\":[1,]}", "{\"entries\":[1 2]}", "{\"entries\":[{\"a\" 1}]}",
      "{\"entries\":[01]}", "{\"entries\":[1e+]}", "{\"entries\":[1.]}",
      "{\"entries\":[\"\\uD800x\"]}", "{\"entries\":[\"\\uDC00\"]}",
      "{\"entries\":[\"\\uZZZZ\"]}", "{\"entries\":[\"\\x\"]}",
      "{\"entries\":[\"bad\ntext\"]}", "{\"entries\":[1] } trailing",
      "{\"entries\":[\"\xff\"]}", "{\"entries\":[\"\xc0\x80\"]}",
      "{\"entries\":[\"\xed\xa0\x80\"]}", "{\"entries\":[\"\xf4\x90\x80\x80\"]}",
      "{\"entries\":[\"\xe2\x82\"]}", "{\"entries\":[\"\xe2" "A\xa0\"]}",
      "{\"entries\":[" + std::string(64, '[') + "0" + std::string(64, ']') + "]}"};
  for (const auto& bad : invalid) {
    const auto a = failure(bad, {}), b = failure(bad, {selected});
    require(a.message == b.message && a.line == b.line && a.byte == b.byte, "Deferred validation changed syntax error");
  }
  const std::string depth = "{\"entries\":[" + std::string(62, '[') + "0" + std::string(62, ']') + "]}";
  same(parse_ordered_json(depth), parse_ordered_json(depth, {selected}));
  std::string large = "{\"entries\":[";
  for (int i = 0; i < 10000; ++i) { if (i) large += ','; large += "{\"n\":" + std::to_string(i) + "}"; }
  large += "]}";
  const auto document = parse_ordered_json(large, {selected});
  const auto& entries = std::get<Value::Object>(document.data)[0].second;
  require(array_size(entries) == 10000, "Large array lost records");
  std::size_t sum = 0;
  visit_array(entries, [&](const Value& item, std::size_t i) {
    const auto& n = std::get<Value::Number>(std::get<Value::Object>(item.data)[0].second.data);
    require(std::stoul(n.text) == i, "Large array changed order"); sum += i;
  });
  require(sum == 49995000, "Large array traversal incomplete");
  std::cout << "Ordered/deferred JSON validation, source positions, depth, Unicode and traversal passed.\n";
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
