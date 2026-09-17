#include "native_general_settings.hpp"
#include "native_menu_style.hpp"
#include <stellar/engine/atomic_file_write.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>

namespace stellar::native_general {
namespace {
using namespace stellar::native_map;
constexpr std::size_t maximum_bytes=4096;
constexpr Color text_color{232,243,250,255},panel_fill{10,25,45,250};
std::string utf8(const std::filesystem::path& path) {
  const auto raw=path.u8string();
  return {reinterpret_cast<const char*>(raw.data()),raw.size()};
}
bool valid_directory(const std::filesystem::path& path) {
  if(path.empty())return true;
  if(!path.is_absolute()||path.native().find(std::filesystem::path::value_type{})!=std::filesystem::path::string_type::npos)return false;
  std::error_code error;
  return std::filesystem::is_directory(path,error)&&!error;
}
void label(DrawList& draw,UiRect rect,std::string text,int size) {
  draw.overlay.emplace_back(Text{{rect.x,rect.y},std::move(text),text_color,size,rect.width,rect});
}
void button(DrawList& draw,UiRect rect,std::string text,int size,bool primary=false,bool disabled=false) {
  draw.overlay.emplace_back(FilledRectangle{rect,primary?Color{104,224,188,255}:Color{22,53,76,255}});
  draw.overlay.emplace_back(StrokedRectangle{rect,{104,184,212,255}});
  draw.overlay.emplace_back(Text{{rect.x+rect.width*.5f,rect.y+(rect.height-static_cast<float>(size))*.5f},
    std::move(text),disabled?Color{123,147,162,255}:primary?panel_fill:text_color,size,rect.width,rect,TextAlign::Center});
}
}
GeneralSettingsLayout GeneralSettingsLayout::for_viewport(int width,int height) noexcept {
  const float w=static_cast<float>(std::max(width,1)),h=static_cast<float>(std::max(height,1));
  const float s=std::max(.001f,std::min({2.6f,w/1280.f,h/720.f,std::max(1.f,w-24.f)/680.f,std::max(1.f,h-24.f)/450.f}));
  const UiRect panel{(w-680.f*s)*.5f,(h-450.f*s)*.5f,680.f*s,450.f*s};
  const auto r=[&](float x,float y,float rw,float rh){return UiRect{panel.x+x*s,panel.y+y*s,rw*s,rh*s};};
  return {s,std::max(12,static_cast<int>(std::lround(17*s))),std::max(16,static_cast<int>(std::lround(26*s))),panel,
    r(30,65,150,32),r(192,65,150,32),r(30,181,620,112),r(30,307,620,48),
    r(30,376,146,40),r(188,376,146,40),r(346,376,146,40),r(504,376,146,40)};
}
NativeGeneralSettings::NativeGeneralSettings(std::filesystem::path path):path_(std::move(path)) {
  try {
    if(!std::filesystem::exists(path_))return;
    if(!std::filesystem::is_regular_file(path_))throw std::runtime_error("settings are not a file");
    std::ifstream input(path_,std::ios::binary);
    if(!input)throw std::runtime_error("settings could not be opened");
    std::array<char,maximum_bytes+1> bytes{};
    input.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    if(input.bad()||input.gcount()>static_cast<std::streamsize>(maximum_bytes))throw std::runtime_error("settings read failed or oversized");
    bool duplicate{};std::vector<std::string> keys;
    const auto json=nlohmann::json::parse(std::string(bytes.data(),static_cast<std::size_t>(input.gcount())),
      [&](int depth,nlohmann::json::parse_event_t event,nlohmann::json& value){
        if(depth==1&&event==nlohmann::json::parse_event_t::key){auto key=value.get<std::string>();if(std::find(keys.begin(),keys.end(),key)!=keys.end())duplicate=true;keys.push_back(std::move(key));}return true;});
    if(duplicate||!json.is_object()||json.size()!=2||!json.contains("schemaVersion")||!json.at("schemaVersion").is_number_integer()||
       json.at("schemaVersion")!=1||!json.contains("screenshotDirectory")||!json.at("screenshotDirectory").is_string())throw std::runtime_error("unsupported settings schema");
    const auto encoded=json.at("screenshotDirectory").get<std::string>();
    const auto value=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(encoded.data()),encoded.size()));
    if(!valid_directory(value))throw std::runtime_error("saved screenshot directory is invalid or unavailable");
    saved_.screenshot_directory=value;
  } catch(const std::exception& error) {
    error_="Saved screenshot folder unavailable. The default location is active.";
    std::cerr<<"General settings load failed: "<<error.what()<<'\n';
  }
}
bool NativeGeneralSettings::save(GeneralPreferences value) {
  if(!valid_directory(value.screenshot_directory)){error_="Choose an existing absolute folder.";return false;}
  try {
    const auto text=nlohmann::json{{"schemaVersion",1},{"screenshotDirectory",utf8(value.screenshot_directory)}}.dump();
    if(text.size()>maximum_bytes)throw std::runtime_error("settings are oversized");
    stellar::engine::write_file_atomically(path_,std::span{reinterpret_cast<const std::byte*>(text.data()),text.size()});
  } catch(const std::exception& error) {
    error_="Could not save settings. Your previous screenshot folder is retained.";
    std::cerr<<"General settings save failed: "<<error.what()<<'\n';return false;
  }
  saved_=std::move(value);error_.clear();if(apply_)apply_(saved_);return true;
}
void NativeGeneralSettings::open(){hover_feedback_.reset();draft_=saved_;pending_request_.reset();path_scroll_=0;visible_=true;}
void NativeGeneralSettings::cancel(){draft_=saved_;pending_request_.reset();visible_=false;}
Text NativeGeneralSettings::path_text(const GeneralSettingsLayout& l) const {
  const UiRect clip{l.folder.x+12*l.scale,l.folder.y+12*l.scale,l.folder.width-24*l.scale,l.folder.height-24*l.scale};
  const auto& path=draft_.screenshot_directory.empty()?default_directory_:draft_.screenshot_directory;
  const auto source=path.empty()?std::string("Pictures / Stellar Continuum / Screenshots"):utf8(path);
  if(source!=cached_path_source_||clip.width!=cached_path_width_||l.font_pixels!=cached_path_font_) {
    std::string wrapped;std::size_t start{};
    while(start<source.size()) {
      // Bound each measurement and split only at Unicode codepoint boundaries.
      std::vector<std::size_t> ends;
      for(auto at=start;at<source.size()&&ends.size()<128;){
        ++at;while(at<source.size()&&(static_cast<unsigned char>(source[at])&0xc0)==0x80)++at;
        ends.push_back(at);
      }
      std::size_t fit=1;
      if(measure_) {
        std::size_t low=1,high=ends.size();
        while(low<=high){const auto mid=(low+high)/2;
          const Text candidate{{},source.substr(start,ends[mid-1]-start),text_color,l.font_pixels};
          if(static_cast<float>(measure_(candidate).width)<=clip.width){fit=mid;low=mid+1;}else high=mid-1;
        }
      } else fit=std::min(ends.size(),static_cast<std::size_t>(std::max(1,static_cast<int>(clip.width/l.font_pixels))));
      auto end=ends[fit-1];
      if(end<source.size()) {
        const auto separator=source.find_last_of("\\/",end-1);
        if(separator!=std::string::npos&&separator>=start+(end-start)/2)end=separator+1;
      }
      if(!wrapped.empty())wrapped+='\n';wrapped.append(source,start,end-start);start=end;
    }
    cached_path_source_=source;cached_path_lines_=std::move(wrapped);cached_path_width_=clip.width;cached_path_font_=l.font_pixels;
  }
  return {{clip.x,clip.y},cached_path_lines_,text_color,l.font_pixels,clip.width,clip};
}
void NativeGeneralSettings::accept_browse_result(FolderDialogResult result) {
  if(!visible_||!pending_request_||result.request_id!=*pending_request_)return;
  pending_request_.reset();
  if(!result.error.empty()){error_="Folder browser could not open. Please try again.";std::cerr<<result.error<<'\n';return;}
  if(!result.directory)return;
  if(result.directory->empty()||!valid_directory(*result.directory)){error_="Choose an existing absolute folder.";return;}
  draft_.screenshot_directory=std::move(*result.directory);path_scroll_=0;error_.clear();
}
bool NativeGeneralSettings::handle(const InputEvent& event,int width,int height) {
  if(!visible_)return false;
  if(event.type==InputEventType::EscapePressed){cancel();return true;}
  const auto layout=GeneralSettingsLayout::for_viewport(width,height);
  hover_feedback_.update(event,browsing()?stellar::native_menu_audio::hit(event.position,{layout.cancel}):stellar::native_menu_audio::hit(event.position,{layout.audio,layout.video,layout.browse,layout.defaults,layout.cancel,layout.save}));
  if(event.type==InputEventType::Wheel&&layout.folder.contains(event.position)&&measure_){
    const auto text=path_text(layout);
    const auto max_scroll=std::max(0.f,static_cast<float>(measure_(text).height)-text.clip->height);
    path_scroll_=std::clamp(path_scroll_-event.wheel_y*40*layout.scale,0.f,max_scroll);return true;
  }
  if(event.type!=InputEventType::LeftPressed)return true;
  if(layout.cancel.contains(event.position)){cancel();return true;}
  if(browsing())return true;
  if(layout.audio.contains(event.position)&&audio_){cancel();audio_();}
  else if(layout.video.contains(event.position)&&video_){cancel();video_();}
  else if(layout.browse.contains(event.position)) {
    if(!browse_){error_="Folder browser is unavailable.";return true;}
    const auto id=++next_request_;pending_request_=id;
    try {
      if(!browse_(id,draft_.screenshot_directory.empty()?default_directory_:draft_.screenshot_directory)){
        pending_request_.reset();error_="Finish the open folder browser before trying again.";
      } else error_.clear();
    } catch(const std::exception& error){pending_request_.reset();error_="Folder browser could not open. Please try again.";std::cerr<<error.what()<<'\n';}
  } else if(layout.defaults.contains(event.position)){draft_={};path_scroll_=0;error_.clear();}
  else if(layout.save.contains(event.position)&&save(draft_))visible_=false;
  return true;
}
void NativeGeneralSettings::render(DrawList& draw,int width,int height)const {
  if(!visible_)return;
  const auto l=GeneralSettingsLayout::for_viewport(width,height);const auto s=l.scale;
  draw.overlay.emplace_back(FilledRectangle{{0,0,static_cast<float>(width),static_cast<float>(height)},{3,10,22,48}});
  native_menu_style::panel(draw,l.panel,l.scale);
  label(draw,{l.panel.x+30*s,l.panel.y+20*s,l.panel.width-60*s,36*s},"GENERAL SETTINGS",l.heading_pixels);
  button(draw,l.audio,"AUDIO",l.font_pixels,false,browsing());button(draw,l.video,"VIDEO",l.font_pixels,false,browsing());
  label(draw,{l.folder.x,l.panel.y+119*s,l.folder.width,26*s},"SCREENSHOT FOLDER",l.font_pixels);
  label(draw,{l.folder.x,l.panel.y+149*s,l.folder.width,25*s},"Press F12 to save a PNG of the game.",l.font_pixels);
  draw.overlay.emplace_back(FilledRectangle{l.folder,{5,16,30,255}});draw.overlay.emplace_back(StrokedRectangle{l.folder,{65,111,143,255}});
  auto path=path_text(l);
  const auto maximum=measure_?std::max(0.f,static_cast<float>(measure_(path).height)-path.clip->height):0.f;
  path.at.y-=std::min(path_scroll_,maximum);draw.overlay.emplace_back(std::move(path));
  label(draw,l.status,browsing()?"Choose a folder in the Windows browser...":!error_.empty()?error_:
    maximum>0?"Scroll over the path to see the full folder. Save to keep your choice.":draft_.screenshot_directory.empty()?"Using the default Pictures folder. Save to keep this choice.":"Save to use this folder. Cancel keeps your current location.",l.font_pixels);
  button(draw,l.browse,browsing()?"BROWSING...":"BROWSE",l.font_pixels,false,browsing());
  button(draw,l.defaults,"USE DEFAULT",l.font_pixels,false,browsing());button(draw,l.cancel,"CANCEL",l.font_pixels);
  button(draw,l.save,"SAVE",l.font_pixels,true,browsing());
}
} // namespace stellar::native_general
