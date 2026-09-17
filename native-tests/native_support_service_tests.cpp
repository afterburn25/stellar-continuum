#include "native_support_service.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace stellar::native_support;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void complete(NativeSupportService& service){
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  while(service.busy()&&std::chrono::steady_clock::now()<deadline){
    (void)service.poll();std::this_thread::yield();
  }
  require(!service.busy(),"Support worker did not finish within its test deadline");
}
void bounded_single_worker(){
  std::promise<void> release;
  auto ready=release.get_future().share();
  std::string captured;
  NativeSupportService service([&](const SupportBundleRequest& request){
    ready.wait();captured=request.session_log;return std::filesystem::path("bundle.zip");
  });
  service.record("session","First campaign");
  require(service.request({}),"First export was not admitted");
  const bool duplicate=service.request({});
  const bool polled=service.poll();
  service.record("session","Later campaign");
  release.set_value();complete(service);
  require(!duplicate&&!polled,"Busy support writer queued another job or blocked polling");
  require(service.state()==SupportExportState::Succeeded&&service.result()=="bundle.zip",
          "Completed support path was lost");
  require(captured.find("First campaign")!=std::string::npos&&
          captured.find("Later campaign")==std::string::npos,"Worker did not own an immutable log snapshot");
  for(int i=0;i<1000;++i)service.record("category",std::string(3000,'x'));
  const auto bounded=service.log_snapshot();
  require(bounded.size()<140000&&bounded.find("First campaign")==std::string::npos,
          "Session diagnostics accumulated unbounded history");
}
void clean_failure_and_explicit_retry(){
  int attempt=0;
  NativeSupportService service([&](const SupportBundleRequest&)->std::filesystem::path{
    if(++attempt==1)throw std::runtime_error("Disk is not writable");
    return "recovered.zip";
  });
  require(service.request({}),"Failure test could not start export");complete(service);
  require(service.state()==SupportExportState::Failed&&service.error()=="Disk is not writable",
          "Worker exception escaped or lost its diagnostic");
  for(int i=0;i<20;++i)(void)service.poll();
  require(attempt==1,"Export automatically retried after terminal failure");
  require(service.request({}),"Explicit export retry was not admitted");complete(service);
  require(service.state()==SupportExportState::Succeeded&&service.error().empty()&&attempt==2,
          "Explicit recovery retained stale failure state");
}
}
int main(){try{bounded_single_worker();clean_failure_and_explicit_retry();}
  catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}return 0;}
