#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <variant>

namespace stellar::engine {
enum class DiagnosticSeverity { Info, Warning, Error, Critical };
enum class DiagnosticDetail { ErrorsOnly, Normal, Detailed, Trace };
using DiagnosticValue=std::variant<std::string,std::int64_t,std::uint64_t,double,bool>;
struct DiagnosticRecord {
  std::uint64_t tick{};
  std::string game_date,real_timestamp,subsystem,event_type,message;
  DiagnosticSeverity severity{DiagnosticSeverity::Info};
  DiagnosticDetail detail{DiagnosticDetail::Normal};
  std::optional<std::int64_t> entity_id,civilization_id,system_id;
  std::map<std::string,DiagnosticValue,std::less<>> values;
};
struct DiagnosticLogPolicy {
  DiagnosticDetail detail{DiagnosticDetail::Normal};
  std::uint64_t segment_bytes{4*1024*1024};
  std::uint32_t retained_segments{8};
  std::uint64_t maximum_record_bytes{16*1024};
};
// A new session directory is required. Only files created by this writer are
// rotated; caller-owned data is never reused/deleted. Confined to its owner.
class DiagnosticLog final {
public:
  explicit DiagnosticLog(std::filesystem::path,DiagnosticLogPolicy={});
  DiagnosticLog(const DiagnosticLog&)=delete;
  DiagnosticLog& operator=(const DiagnosticLog&)=delete;
  bool append(DiagnosticRecord);
  void flush();
  [[nodiscard]] const std::filesystem::path &directory()const{return directory_;}
  [[nodiscard]] std::uint64_t written_records()const{return records_;}
  [[nodiscard]] std::uint64_t filtered_records()const{return filtered_;}
  [[nodiscard]] std::uint64_t overwritten_segments()const{return overwritten_;}
private:
  void require_owner()const;
  void open_segment();
  std::filesystem::path directory_;
  DiagnosticLogPolicy policy_;
  std::thread::id owner_{std::this_thread::get_id()};
  std::ofstream file_;
  std::uint64_t bytes_{},segment_{},records_{},filtered_{},overwritten_{};
};
struct BufferedDiagnosticRecord {DiagnosticRecord record;std::string json;};
struct DiagnosticBufferPolicy {
  DiagnosticDetail detail{DiagnosticDetail::Normal};
  std::size_t maximum_records{512},maximum_bytes{2u*1024u*1024u},maximum_record_bytes{16u*1024u};
};
// Bounded in-memory owner-thread history for inspectors and immutable exports.
// Uses the same filtering, JSON encoding and truncation rules as disk logs.
class DiagnosticBuffer final {
public:
  explicit DiagnosticBuffer(DiagnosticBufferPolicy={});
  bool append(DiagnosticRecord);
  void clear();
  void set_detail(DiagnosticDetail);
  [[nodiscard]] DiagnosticDetail detail()const{return policy_.detail;}
  [[nodiscard]] const std::deque<BufferedDiagnosticRecord> &records()const{return records_;}
  [[nodiscard]] std::size_t bytes()const{return bytes_;}
  [[nodiscard]] std::uint64_t overwritten_records()const{return overwritten_;}
  [[nodiscard]] std::uint64_t filtered_records()const{return filtered_;}
private:
  void require_owner()const;
  DiagnosticBufferPolicy policy_;
  std::thread::id owner_{std::this_thread::get_id()};
  std::deque<BufferedDiagnosticRecord> records_;
  std::size_t bytes_{};
  std::uint64_t overwritten_{},filtered_{};
};
[[nodiscard]] std::string diagnostic_utc_now();
[[nodiscard]] std::string diagnostic_record_json(const DiagnosticRecord &);
}
