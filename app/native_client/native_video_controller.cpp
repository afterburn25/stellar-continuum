#include "native_video_controller.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace stellar::native_video_settings {
namespace {
std::string panel_notice(const std::string& detail,bool failed){
  if(failed)return "Display recovery failed. Restart the game. Preferences were not saved.";
  if(detail.find("were not saved")!=std::string::npos)return "Could not save this change. Previous display settings restored.";
  if(detail.find("Safe display")!=std::string::npos)return "Display change failed. Safe settings restored; preferences unchanged.";
  if(detail.find("rejected")!=std::string::npos)return "Display change rejected. Previous settings restored.";
  return detail;
}
}
NativeVideoController::NativeVideoController(std::filesystem::path path,Apply apply,Now now,Persist persist,std::optional<NativeVideoSettings> launch_override)
    :path_(std::move(path)),apply_(std::move(apply)),now_(std::move(now)),persist_(std::move(persist)){
  if(!apply_||!now_)throw std::invalid_argument("Video settings require an owner-thread backend and clock.");
  if(!persist_)persist_=[this](const NativeVideoSettings& values){values.save(path_);};
  active_=launch_override?launch_override->sanitized():NativeVideoSettings::load(path_);
  try{apply_(active_);}catch(const std::exception& error){recover(std::string("Saved display settings could not be applied: ")+error.what());}
  view_.close();
}
NativeVideoController::~NativeVideoController(){
  if(previous_)try{restore("Display preview closed.");}catch(...){/* backend lifetime remains host-owned */}
}
void NativeVideoController::show_error(std::string reason){
  notice_=std::move(reason);view_.open(active_);view_.set_error(panel_notice(notice_,faulted_));
  std::cerr<<"Video settings: "<<notice_<<'\n';
}
void NativeVideoController::recover(std::string reason){
  // Off + automatic refresh pacing is a useful fallback even on a driver that
  // rejects VSync. Never overwrite the last saved preferences with recovery.
  NativeVideoSettings safe;safe.vsync=VideoVsync::Off;
  try{apply_(safe);active_=safe;faulted_=false;reason+=" Safe display settings restored.";}
  catch(const std::exception& error){faulted_=true;reason+=" Display state is unknown. Restart the game. Recovery failed: ";reason+=error.what();}
  previous_.reset();show_error(std::move(reason));
}
void NativeVideoController::open(){view_.open(active_);view_.set_error(panel_notice(notice_,faulted_));}
void NativeVideoController::close(){if(previous_)restore("Display preview cancelled.");view_.close();}
void NativeVideoController::restore(std::string reason){
  if(!previous_)return;
  const auto prior=*previous_;previous_.reset();
  try{apply_(prior);active_=prior;show_error(std::move(reason));}
  catch(const std::exception& error){recover(std::move(reason)+" Previous settings could not be restored: "+error.what());}
}
double NativeVideoController::remaining_seconds()const{
  return previous_?std::max(0.,std::chrono::duration<double>(deadline_-now_()).count()):0.;
}
void NativeVideoController::service(bool focused,bool renderable){
  if(previous_&&(!focused||!renderable||now_()>=deadline_))
    restore(!focused||!renderable?"Display preview reverted while the game was inactive.":"Display preview expired; previous settings restored.");
}
bool NativeVideoController::handle(const stellar::native_map::InputEvent& event,int width,int height){
  const bool was_preview=previewing();service();
  if(was_preview&&!previewing())return true; // Never reuse a late Keep click.
  const auto result=view_.handle(event,width,height);
  switch(result.command){
    case VideoSettingsCommand::Apply:
      if(faulted_||previous_)break;
      previous_=active_;
      try{
        apply_(result.values);active_=result.values.sanitized();
        deadline_=now_()+std::chrono::seconds(15);
        notice_.clear();view_.set_error({});view_.set_confirming(true);
      }catch(const std::exception& error){restore(std::string("Display change rejected: ")+error.what());}
      break;
    case VideoSettingsCommand::Keep:
      if(!previous_||faulted_)break;
      try{persist_(active_);previous_.reset();notice_.clear();view_.close();}
      catch(const std::exception& error){restore(std::string("Display settings were not saved: ")+error.what());}
      break;
    case VideoSettingsCommand::Revert:restore("Previous display settings restored.");break;
    case VideoSettingsCommand::Cancel:close();break;
    default:break;
  }
  return result.captured;
}
void NativeVideoController::render(stellar::native_map::DrawList& draw,int width,int height)const{
  view_.render(draw,width,height,remaining_seconds());
}
void NativeVideoController::set_display_choices(std::vector<VideoDisplayChoice> choices,std::string label){
  view_.set_display_choices(std::move(choices),std::move(label));
}
void NativeVideoController::set_windowed_display_choices(std::vector<VideoDisplayChoice> choices){view_.set_windowed_choices(std::move(choices));}
}
