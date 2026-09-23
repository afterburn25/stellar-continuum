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
  stellar::engine::ui_skin::control(draw,rect,false,primary,!disabled,std::max(.5f,size/17.f));
  draw.overlay.emplace_back(Text{{rect.x+rect.width*.5f,rect.y+(rect.height-static_cast<float>(size))*.5f},
    std::move(text),disabled?Color{123,147,162,255}:text_color,size,rect.width,rect,TextAlign::Center,FontFace::Interface});
}
}
GeneralSettingsLayout GeneralSettingsLayout::for_viewport(int width,int height) noexcept {
  const float w=static_cast<float>(std::max(width,1)),h=static_cast<float>(std::max(height,1));
  const float s=std::max(.001f,std::min({2.6f,w/1280.f,h/720.f,std::max(1.f,w-24.f)/680.f,std::max(1.f,h-24.f)/500.f}));
  const UiRect panel{(w-680.f*s)*.5f,(h-500.f*s)*.5f,680.f*s,500.f*s};
  const auto r=[&](float x,float y,float rw,float rh){return UiRect{panel.x+x*s,panel.y+y*s,rw*s,rh*s};};
  return {s,std::max(12,static_cast<int>(std::lround(17*s))),std::max(16,static_cast<int>(std::lround(26*s))),panel,
    r(30,65,150,32),r(192,65,150,32),r(30,286,620,67),r(30,355,620,48),
    r(30,424,146,40),r(188,424,146,40),r(346,424,146,40),r(504,424,146,40),r(358,65,292,32),r(30,111,620,32),
    r(30,153,199,32),r(240,153,199,32),r(450,153,200,32),r(30,195,199,32),r(240,195,199,32),r(450,195,200,32)};
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
    if(duplicate||!json.is_object()||(json.size()!=2&&json.size()<4||json.size()>12)||!json.contains("schemaVersion")||!json.at("schemaVersion").is_number_integer()||
       json.at("schemaVersion")!=1||!json.contains("screenshotDirectory")||!json.at("screenshotDirectory").is_string())throw std::runtime_error("unsupported settings schema");
    const auto encoded=json.at("screenshotDirectory").get<std::string>();
    const auto value=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(encoded.data()),encoded.size()));
    if(!valid_directory(value))throw std::runtime_error("saved screenshot directory is invalid or unavailable");
    saved_.screenshot_directory=value;
    if(json.contains("assetCategoriesCollapsed")){saved_.asset_categories_collapsed=json.at("assetCategoriesCollapsed").get<std::array<bool,5>>();saved_.assets_hidden=json.at("assetsHidden").get<bool>();}
    if(json.contains("eruptionQuality")){if(!json.at("eruptionQuality").is_number_integer())throw std::runtime_error("Invalid eruption quality");saved_.eruption_quality=json.at("eruptionQuality").get<int>();if(saved_.eruption_quality<0||saved_.eruption_quality>3)throw std::runtime_error("Invalid eruption quality");}
    if(json.contains("nebulaDensity")){if(!json.at("nebulaDensity").is_number_integer())throw std::runtime_error("Invalid visual density");const auto density=json.at("nebulaDensity").get<int>();if(density<0||density>2)throw std::runtime_error("Invalid visual density");saved_.nebula_density=density;}
    if(json.contains("reduceMotion")){if(!json.at("reduceMotion").is_boolean())throw std::runtime_error("Invalid reduced motion flag");saved_.reduce_motion=json.at("reduceMotion").get<bool>();}
    if(json.contains("interfaceScale")){if(!json.at("interfaceScale").is_number_integer())throw std::runtime_error("Invalid interface scale");const auto scale=json.at("interfaceScale").get<int>();if(scale<0||scale>3)throw std::runtime_error("Invalid interface scale");saved_.interface_scale=scale;}
    if(json.contains("reduceFlashing")){if(!json.at("reduceFlashing").is_boolean())throw std::runtime_error("Invalid reduced flashing flag");saved_.reduce_flashing=json.at("reduceFlashing").get<bool>();}
    if(json.contains("highContrast")){if(!json.at("highContrast").is_boolean())throw std::runtime_error("Invalid high contrast flag");saved_.high_contrast=json.at("highContrast").get<bool>();}
    if(json.contains("colorBlind")){if(!json.at("colorBlind").is_number_integer())throw std::runtime_error("Invalid color-blind mode");const auto mode=json.at("colorBlind").get<int>();if(mode<0||mode>3)throw std::runtime_error("Invalid color-blind mode");saved_.color_blind=mode;}
    if(json.contains("locale")){if(!json.at("locale").is_string())throw std::runtime_error("Invalid locale id");const auto id=json.at("locale").get<std::string>();if(id.empty()||id.size()>16||id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")!=std::string::npos)throw std::runtime_error("Invalid locale id");saved_.locale=id;}
  } catch(const std::exception& error) {
    error_="Saved screenshot folder unavailable. The default location is active.";
    std::cerr<<"General settings load failed: "<<error.what()<<'\n';
  }
}
bool NativeGeneralSettings::save(GeneralPreferences value) {
  if(value.eruption_quality<0||value.eruption_quality>3){error_="Choose a stellar eruption quality.";return false;}
  if(value.interface_scale<0||value.interface_scale>3){error_="Choose an interface scale.";return false;}
  if(value.color_blind<0||value.color_blind>3){error_="Choose a color-blind mode.";return false;}
  if(value.locale.empty()||value.locale.size()>16||value.locale.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")!=std::string::npos){error_="Choose a language.";return false;}
  if(value.nebula_density<0||value.nebula_density>2){error_="Choose Low, Medium or High nebula density.";return false;}
  if(!valid_directory(value.screenshot_directory)){error_="Choose an existing absolute folder.";return false;}
  try {
    const auto text=nlohmann::json{{"schemaVersion",1},{"screenshotDirectory",utf8(value.screenshot_directory)},{"assetCategoriesCollapsed",value.asset_categories_collapsed},{"assetsHidden",value.assets_hidden},{"nebulaDensity",value.nebula_density},{"eruptionQuality",value.eruption_quality},{"reduceMotion",value.reduce_motion},{"interfaceScale",value.interface_scale},{"reduceFlashing",value.reduce_flashing},{"highContrast",value.high_contrast},{"colorBlind",value.color_blind},{"locale",value.locale}}.dump();
    if(text.size()>maximum_bytes)throw std::runtime_error("settings are oversized");
    stellar::engine::write_file_atomically(path_,std::span{reinterpret_cast<const std::byte*>(text.data()),text.size()});
  } catch(const std::exception& error) {
    error_="Could not save settings. Your previous screenshot folder is retained.";
    std::cerr<<"General settings save failed: "<<error.what()<<'\n';return false;
  }
  saved_=std::move(value);error_.clear();if(apply_)apply_(saved_);return true;
}
std::string NativeGeneralSettings::tr(std::string_view key,std::string_view fallback)const {
  if(locale_&&locale_->contains(key))return std::string(locale_->translate(key));
  return std::string(fallback);
}
std::string NativeGeneralSettings::trf(std::string_view key,std::initializer_list<std::string> args,std::string_view fallback)const {
  if(locale_&&locale_->contains(key)) {
    const std::vector<std::string> values(args.begin(),args.end());
    return locale_->format(key,std::span<const std::string>(values));
  }
  std::string out{fallback};
  for(std::size_t i=0;i<args.size();++i) {
    const auto marker="{"+std::to_string(i)+"}";
    if(const auto at=out.find(marker);at!=std::string::npos)out.replace(at,marker.size(),*(args.begin()+i));
  }
  return out;
}
void NativeGeneralSettings::open(){nebula_dropdown_.close();eruption_dropdown_.close();hover_feedback_.reset();draft_=saved_;pending_request_.reset();path_scroll_=0;visible_=true;}
void NativeGeneralSettings::cancel(){nebula_dropdown_.close();eruption_dropdown_.close();draft_=saved_;pending_request_.reset();visible_=false;}
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
  if(eruption_dropdown_.visible()){if(const auto choice=eruption_dropdown_.handle(event,GeneralSettingsLayout::for_viewport(width,height).eruptions,width,height))draft_.eruption_quality=*choice;return true;}
  if(nebula_dropdown_.visible()){if(const auto choice=nebula_dropdown_.handle(event,GeneralSettingsLayout::for_viewport(width,height).nebula,width,height))draft_.nebula_density=*choice;return true;}
  if(event.type==InputEventType::EscapePressed){cancel();return true;}
  const auto layout=GeneralSettingsLayout::for_viewport(width,height);
  hover_feedback_.update(event,browsing()?stellar::native_menu_audio::hit(event.position,{layout.cancel}):stellar::native_menu_audio::hit(event.position,{layout.audio,layout.video,layout.nebula,layout.eruptions,layout.motion,layout.iscale,layout.flashing,layout.contrast,layout.colorblind,layout.language,layout.browse,layout.defaults,layout.cancel,layout.save}));
  if(event.type==InputEventType::Wheel&&layout.folder.contains(event.position)&&measure_){
    const auto text=path_text(layout);
    const auto max_scroll=std::max(0.f,static_cast<float>(measure_(text).height)-text.clip->height);
    path_scroll_=std::clamp(path_scroll_-event.wheel_y*40*layout.scale,0.f,max_scroll);return true;
  }
  if(event.type!=InputEventType::LeftPressed)return true;
  if(layout.cancel.contains(event.position)){cancel();return true;}
  if(browsing())return true;
  if(layout.eruptions.contains(event.position)){eruption_dropdown_.open(0,{"Low","Medium","High","Ultra"},draft_.eruption_quality);return true;}
  if(layout.nebula.contains(event.position)){nebula_dropdown_.open(0,{"Low","Medium","High"},draft_.nebula_density);return true;}
  if(layout.motion.contains(event.position)){draft_.reduce_motion=!draft_.reduce_motion;return true;}
  if(layout.iscale.contains(event.position)){draft_.interface_scale=(draft_.interface_scale+1)%4;return true;}
  if(layout.flashing.contains(event.position)){draft_.reduce_flashing=!draft_.reduce_flashing;return true;}
  if(layout.contrast.contains(event.position)){draft_.high_contrast=!draft_.high_contrast;return true;}
  if(layout.colorblind.contains(event.position)){draft_.color_blind=(draft_.color_blind+1)%4;return true;}
  if(layout.language.contains(event.position)&&!locales_.empty()){
    const auto at=std::find(locales_.begin(),locales_.end(),draft_.locale);
    const auto index=at==locales_.end()?std::size_t{0}:(static_cast<std::size_t>(at-locales_.begin())+1)%locales_.size();
    draft_.locale=locales_.at(index);return true;}
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
  } else if(layout.defaults.contains(event.position)){draft_.screenshot_directory.clear();path_scroll_=0;error_.clear();}
  else if(layout.save.contains(event.position)&&save(draft_))visible_=false;
  return true;
}
void NativeGeneralSettings::render(DrawList& draw,int width,int height)const {
  if(!visible_)return;
  const auto l=GeneralSettingsLayout::for_viewport(width,height);const auto s=l.scale;
  draw.overlay.emplace_back(FilledRectangle{{0,0,static_cast<float>(width),static_cast<float>(height)},{3,10,22,48}});
  native_menu_style::panel(draw,l.panel,l.scale);
  const auto quality_name=[&](std::string_view key,std::string_view fallback){return tr(key,fallback);};
  const std::array<std::string,3> densities{quality_name("SETTINGS_QUALITY_LOW","Low"),quality_name("SETTINGS_QUALITY_MEDIUM","Medium"),quality_name("SETTINGS_QUALITY_HIGH","High")};
  const std::array<std::string,4> details{densities[0],densities[1],densities[2],quality_name("SETTINGS_QUALITY_ULTRA","Ultra")};
  label(draw,{l.panel.x+30*s,l.panel.y+20*s,l.panel.width-60*s,36*s},tr("SETTINGS_GENERAL_TITLE","GENERAL SETTINGS"),l.heading_pixels);
  button(draw,l.audio,tr("SETTINGS_NAV_AUDIO","AUDIO"),l.font_pixels,false,browsing());button(draw,l.video,tr("SETTINGS_NAV_VIDEO","VIDEO"),l.font_pixels,false,browsing());
  button(draw,l.nebula,trf("SETTINGS_NEBULA_DENSITY",{densities.at(draft_.nebula_density)},"Space phenomena density: {0} ▾"),l.font_pixels,false,browsing());
  button(draw,l.eruptions,trf("SETTINGS_ERUPTION_DETAIL",{details.at(draft_.eruption_quality)},"Stellar eruption detail: {0} ▾"),l.font_pixels,false,browsing());
  button(draw,l.motion,trf("SETTINGS_REDUCE_MOTION",{tr(draft_.reduce_motion?"SETTINGS_STATE_ON":"SETTINGS_STATE_OFF",draft_.reduce_motion?"On":"Off")},"Reduced motion (decorative animation): {0}"),l.font_pixels,false,browsing());
  const std::array<std::string,4> scale_names{quality_name("SETTINGS_SCALE_COMPACT","Compact"),quality_name("SETTINGS_SCALE_STANDARD","Standard"),
    quality_name("SETTINGS_SCALE_LARGE","Large"),quality_name("SETTINGS_SCALE_HUGE","Huge")};
  button(draw,l.iscale,trf("SETTINGS_INTERFACE_SCALE",{scale_names.at(static_cast<std::size_t>(draft_.interface_scale))},"Interface scale: {0}"),l.font_pixels,false,browsing());
  button(draw,l.flashing,trf("SETTINGS_REDUCE_FLASHING",{tr(draft_.reduce_flashing?"SETTINGS_STATE_ON":"SETTINGS_STATE_OFF",draft_.reduce_flashing?"On":"Off")},"Reduce flashing: {0}"),l.font_pixels,false,browsing());
  button(draw,l.contrast,trf("SETTINGS_HIGH_CONTRAST",{tr(draft_.high_contrast?"SETTINGS_STATE_ON":"SETTINGS_STATE_OFF",draft_.high_contrast?"On":"Off")},"High contrast: {0}"),l.font_pixels,false,browsing());
  const std::array<std::string,4> colorblind_names{quality_name("SETTINGS_COLORBLIND_NONE","Off"),quality_name("SETTINGS_COLORBLIND_PROTANOPIA","Protanopia"),
    quality_name("SETTINGS_COLORBLIND_DEUTERANOPIA","Deuteranopia"),quality_name("SETTINGS_COLORBLIND_TRITANOPIA","Tritanopia")};
  button(draw,l.colorblind,trf("SETTINGS_COLOR_BLIND",{colorblind_names.at(static_cast<std::size_t>(draft_.color_blind))},"Color-blind mode: {0}"),l.font_pixels,false,browsing());
  // Native-language names for shipped locale ids; unknown ids display raw.
  const auto locale_name=[&](std::string_view id){if(id=="en")return std::string("English");if(id=="de")return std::string("Deutsch");return std::string(id);};
  button(draw,l.language,trf("SETTINGS_LANGUAGE",{locale_name(draft_.locale)},"Language: {0}"),l.font_pixels,false,browsing());
  label(draw,{l.folder.x,l.panel.y+231*s,l.folder.width,26*s},tr("SETTINGS_SCREENSHOT_FOLDER","SCREENSHOT FOLDER"),l.font_pixels);
  label(draw,{l.folder.x,l.panel.y+259*s,l.folder.width,25*s},tr("SETTINGS_SCREENSHOT_HINT","Press F12 to save a PNG of the game."),l.font_pixels);
  draw.overlay.emplace_back(FilledRectangle{l.folder,{5,16,30,255}});draw.overlay.emplace_back(StrokedRectangle{l.folder,{65,111,143,255}});
  auto path=path_text(l);
  const auto maximum=measure_?std::max(0.f,static_cast<float>(measure_(path).height)-path.clip->height):0.f;
  path.at.y-=std::min(path_scroll_,maximum);draw.overlay.emplace_back(std::move(path));
  label(draw,l.status,browsing()?"Choose a folder in the Windows browser...":!error_.empty()?error_:
    maximum>0?"Scroll over the path to see the full folder. Save to keep your choice.":draft_.screenshot_directory.empty()?"Using the default Pictures folder. Save to keep this choice.":"Save to use this folder. Cancel keeps your current location.",l.font_pixels);
  button(draw,l.browse,browsing()?tr("SETTINGS_BROWSING","BROWSING..."):tr("SETTINGS_BROWSE","BROWSE"),l.font_pixels,false,browsing());
  button(draw,l.defaults,tr("SETTINGS_USE_DEFAULT","USE DEFAULT"),l.font_pixels,false,browsing());button(draw,l.cancel,tr("SETTINGS_CANCEL","CANCEL"),l.font_pixels);
  button(draw,l.save,tr("SETTINGS_SAVE","SAVE"),l.font_pixels,true,browsing());
  nebula_dropdown_.render(draw,l.nebula,width,height,l.font_pixels);
  eruption_dropdown_.render(draw,l.eruptions,width,height,l.font_pixels);
}
} // namespace stellar::native_general
