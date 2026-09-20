#include <stellar/engine/diagnostic_log.hpp>
#include <nlohmann/json.hpp>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace stellar::engine {
namespace {
using Json=nlohmann::ordered_json;
std::string_view severity_name(DiagnosticSeverity value){
  switch(value){case DiagnosticSeverity::Info:return "info";case DiagnosticSeverity::Warning:return "warning";
    case DiagnosticSeverity::Error:return "error";case DiagnosticSeverity::Critical:return "critical";}
  throw std::invalid_argument("Invalid diagnostic severity.");
}
std::string encode(const DiagnosticRecord &r){
  Json values=Json::object();for(const auto &[key,value]:r.values)std::visit([&](const auto &v){
    if constexpr(std::is_same_v<std::decay_t<decltype(v)>,double>){
      if(!std::isfinite(v)){values[key]=std::isnan(v)?"NaN":(v>0?"Infinity":"-Infinity");return;}
    }
    values[key]=v;
  },value);
  Json result={{"schemaVersion",1},{"tick",r.tick},{"gameDate",r.game_date},{"realTimestamp",r.real_timestamp},
    {"severity",severity_name(r.severity)},{"subsystem",r.subsystem},{"eventType",r.event_type},{"message",r.message},
    {"relevantValues",std::move(values)}};
  if(r.entity_id)result["entityId"]=*r.entity_id;
  if(r.civilization_id)result["civilizationId"]=*r.civilization_id;
  if(r.system_id)result["systemId"]=*r.system_id;
  return result.dump()+"\n";
}
std::string bounded(std::string value,std::size_t limit){
  if(value.size()<=limit)return value;
  while(limit&&(static_cast<unsigned char>(value[limit])&0xc0)==0x80)--limit;
  value.resize(limit);return value+"...";
}
std::string bounded_record(DiagnosticRecord &record,std::size_t limit){
  auto line=encode(record);
  if(line.size()>limit){
    const auto original=line.size();record.values.clear();record.values["originalRecordBytes"]=static_cast<std::uint64_t>(original);
    record.values["originalEventType"]=bounded(record.event_type,80);
    record.event_type="diagnostic_record_truncated";record.message=bounded(record.message,256);
    record.subsystem=bounded(record.subsystem,80);record.game_date=bounded(record.game_date,32);record.real_timestamp=bounded(record.real_timestamp,40);
    line=encode(record);
    if(line.size()>limit){
      // JSON escaping can expand control characters by six times. Retain the
      // typed identity and size even when no original text fits the budget.
      record.subsystem="diagnostics";record.message="Record text exceeded the byte limit after JSON escaping.";
      record.game_date.clear();record.real_timestamp.clear();record.values.erase("originalEventType");line=encode(record);
    }
    if(line.size()>limit)throw std::runtime_error("Diagnostic record could not be safely bounded.");
  }
  return line;
}
bool filtered_record(const DiagnosticRecord &record,DiagnosticDetail detail){
  (void)severity_name(record.severity);
  if(static_cast<int>(record.detail)<0||static_cast<int>(record.detail)>3)throw std::invalid_argument("Invalid diagnostic detail.");
  return (detail==DiagnosticDetail::ErrorsOnly&&record.severity<DiagnosticSeverity::Error)||
      (record.severity==DiagnosticSeverity::Info&&record.detail>detail);
}

}
std::string diagnostic_record_json(const DiagnosticRecord &record){return encode(record);}
DiagnosticLog::DiagnosticLog(std::filesystem::path directory,DiagnosticLogPolicy policy)
    :directory_(std::filesystem::absolute(std::move(directory))),policy_(policy){
  if(policy.retained_segments<1||policy.retained_segments>64||policy.maximum_record_bytes<1024||
      policy.maximum_record_bytes>1024*1024||policy.segment_bytes<policy.maximum_record_bytes||
      policy.segment_bytes>1024ULL*1024*1024||static_cast<int>(policy.detail)<0||static_cast<int>(policy.detail)>3)
    throw std::invalid_argument("Invalid bounded diagnostic log policy.");
  if(!std::filesystem::create_directories(directory_))throw std::runtime_error("Diagnostic session directory already exists.");
  open_segment();
}
void DiagnosticLog::require_owner()const{
  if(std::this_thread::get_id()!=owner_)throw std::logic_error("Diagnostic log must be used by its owner thread.");
}
void DiagnosticLog::open_segment(){
  if(file_.is_open()){flush();file_.close();}
  std::ostringstream name;name<<"events-"<<std::setw(3)<<std::setfill('0')<<(segment_%policy_.retained_segments)<<".jsonl";
  file_.open(directory_/name.str(),std::ios::binary|std::ios::trunc);
  if(!file_)throw std::runtime_error("Could not open diagnostic log segment.");
  if(segment_>=policy_.retained_segments)++overwritten_;
  bytes_=0;
}
bool DiagnosticLog::append(DiagnosticRecord record){
  require_owner();
  if(filtered_record(record,policy_.detail)){++filtered_;return false;}
  auto line=bounded_record(record,policy_.maximum_record_bytes);
  if(bytes_+line.size()>policy_.segment_bytes){++segment_;open_segment();}
  file_.write(line.data(),static_cast<std::streamsize>(line.size()));
  if(!file_)throw std::runtime_error("Diagnostic log write failed.");
  bytes_+=line.size();++records_;return true;
}
void DiagnosticLog::flush(){require_owner();file_.flush();if(!file_)throw std::runtime_error("Diagnostic log flush failed.");}
DiagnosticBuffer::DiagnosticBuffer(DiagnosticBufferPolicy policy):policy_(policy){
  if(policy.maximum_records<1||policy.maximum_records>65536||policy.maximum_record_bytes<1024||
     policy.maximum_record_bytes>1024*1024||policy.maximum_bytes<policy.maximum_record_bytes||policy.maximum_bytes>64u*1024u*1024u)
    throw std::invalid_argument("Invalid diagnostic buffer policy.");
  set_detail(policy.detail);
}
void DiagnosticBuffer::require_owner()const{
  if(owner_!=std::this_thread::get_id())throw std::logic_error("Diagnostic buffer requires its owner thread.");
}
void DiagnosticBuffer::set_detail(DiagnosticDetail detail){
  require_owner();if(static_cast<int>(detail)<0||static_cast<int>(detail)>3)throw std::invalid_argument("Invalid diagnostic detail.");policy_.detail=detail;
}
void DiagnosticBuffer::clear(){require_owner();records_.clear();bytes_=0;overwritten_=filtered_=0;}
bool DiagnosticBuffer::append(DiagnosticRecord record){
  require_owner();if(filtered_record(record,policy_.detail)){++filtered_;return false;}
  auto line=bounded_record(record,policy_.maximum_record_bytes);
  while(!records_.empty()&&(records_.size()>=policy_.maximum_records||bytes_+line.size()>policy_.maximum_bytes)){
    bytes_-=records_.front().json.size();records_.pop_front();++overwritten_;
  }
  bytes_+=line.size();records_.push_back({std::move(record),std::move(line)});return true;
}
std::string diagnostic_utc_now(){
  const auto value=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());std::tm utc{};
#ifdef _WIN32
  if(gmtime_s(&utc,&value))throw std::runtime_error("Could not read UTC time.");
#else
  if(!gmtime_r(&value,&utc))throw std::runtime_error("Could not read UTC time.");
#endif
  std::ostringstream out;out<<std::put_time(&utc,"%Y-%m-%dT%H:%M:%SZ");return out.str();
}
} // namespace stellar::engine
