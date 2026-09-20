#pragma once
#include <stellar/engine/atomic_file_write.hpp>
#include <nlohmann/json.hpp>
#include <utility>
#include <vector>

namespace stellar::core::detail {
// Bounded-memory composition: only one leaf record is materialized at a time.
// A serializer owns validation; this writer owns delimiters and indentation.
class JsonStreamWriter {
 public:
  explicit JsonStreamWriter(stellar::engine::AtomicTextSink sink):sink_(std::move(sink)){
    if(!sink_)throw std::invalid_argument("JSON stream requires a sink");
  }
  void begin_object(){sink_("{");frames_.push_back(false);}
  void begin_array(){sink_("[");frames_.push_back(false);}
  void end_object(){end("}");}
  void end_array(){end("]");}
  void member(std::string_view key){item();sink_(nlohmann::ordered_json(key).dump());sink_(": ");}
  void item(){if(frames_.back())sink_(",");frames_.back()=true;sink_("\n");indent(frames_.size());}
  void value(const nlohmann::ordered_json& value){
    const auto text=value.dump(2);std::size_t offset=0;
    while(true){const auto end=text.find('\n',offset);
      if(end==std::string::npos){sink_(std::string_view(text).substr(offset));break;}
      sink_(std::string_view(text).substr(offset,end-offset+1));indent(frames_.size());offset=end+1;
    }
  }
  void field(std::string_view key,const nlohmann::ordered_json& value){member(key);this->value(value);}
 private:
  void indent(std::size_t depth){for(std::size_t i=0;i<depth;++i)sink_("  ");}
  void end(std::string_view close){const bool any=frames_.back();frames_.pop_back();if(any){sink_("\n");indent(frames_.size());}sink_(close);}
  stellar::engine::AtomicTextSink sink_;std::vector<bool> frames_;
};
} // namespace stellar::core::detail
