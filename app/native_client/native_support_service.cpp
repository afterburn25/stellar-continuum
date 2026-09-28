#include "native_support_service.hpp"
#include <algorithm>
#include <chrono>
#include <memory>
#include <stdexcept>

namespace stellar::native_support {
namespace {
std::string bounded_line(std::string_view text,std::size_t limit){
  auto length=std::min(text.size(),limit);
  if(length<text.size())
    while(length>0&&(static_cast<unsigned char>(text[length])&0xc0)==0x80)--length;
  std::string result(text.substr(0,length));
  std::ranges::replace(result,'\n',' ');
  std::ranges::replace(result,'\r',' ');
  return result;
}
}
NativeSupportService::NativeSupportService(Writer writer):writer_(std::move(writer)){
  if(!writer_)throw std::invalid_argument("Support export requires a writer.");
}
void NativeSupportService::record(std::string_view category,std::string_view message){
  log_.push_back("["+bounded_line(category,48)+"] "+bounded_line(message,1024)+"\n");
  while(log_.size()>128)log_.pop_front();
}
std::string NativeSupportService::log_snapshot() const {
  std::string result;
  for(const auto& line:log_)result+=line;
  return result;
}
bool NativeSupportService::request(SupportBundleRequest request){
  if(busy())return false;
  result_.clear();error_.clear();
  request.session_log=log_snapshot();
  try {
    const auto promise=std::make_shared<std::promise<std::filesystem::path>>();
    worker_=promise->get_future();
    worker_status_=jobs_.submit("support-export",stellar::engine::JobPriority::Normal,stellar::engine::JobCancelToken{},
        [writer=writer_,request=std::move(request),promise]{
      try{promise->set_value(writer(request));}catch(...){try{promise->set_exception(std::current_exception());}catch(...){}}
    });
    state_=SupportExportState::Working;
    return true;
  } catch(const std::exception& error){
    error_=bounded_line(error.what(),1024);
  } catch(...){error_="Unknown failure starting the diagnostic export.";}
  state_=SupportExportState::Failed;
  record("support",error_);
  return false;
}
void NativeSupportService::report_capture_failure(std::string_view message){
  if(busy())return;
  result_.clear();error_=bounded_line(message,1024);state_=SupportExportState::Failed;record("support",error_);
}
bool NativeSupportService::poll(){
  if(!busy()||worker_.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return false;
  try {
    result_=worker_.get();
    if(result_.empty())throw std::runtime_error("Diagnostic writer returned no bundle.");
    state_=SupportExportState::Succeeded;
    record("support","Export completed.");
  } catch(const std::exception& error){
    error_=bounded_line(error.what(),1024);state_=SupportExportState::Failed;
    record("support",error_);
  } catch(...){
    error_="Unknown failure writing the diagnostic export.";state_=SupportExportState::Failed;
    record("support",error_);
  }
  return true;
}
}
