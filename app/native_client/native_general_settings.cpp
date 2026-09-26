#include "native_general_settings.hpp"
#include "native_menu_style.hpp"
#include "native_ui_theme.hpp"
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
constexpr Color text_color=stellar::native_ui::color::text_primary,panel_fill=stellar::native_ui::color::surface_opaque;
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
    std::move(text),disabled?stellar::native_ui::color::text_muted:text_color,size,rect.width,rect,TextAlign::Center,FontFace::Interface});
}
// Text/subtitle scale presets cycle within the engine accessibility clamp
// (0.75..2.0); a stored value off the grid snaps to the nearest preset.
constexpr std::array<float,5> text_scale_presets{.75f,1.f,1.25f,1.5f,2.f};
float next_text_scale(float current)noexcept{
  const auto at=std::min_element(text_scale_presets.begin(),text_scale_presets.end(),
    [&](float a,float b){return std::abs(a-current)<std::abs(b-current);});
  return text_scale_presets[(static_cast<std::size_t>(at-text_scale_presets.begin())+1)%text_scale_presets.size()];
}
std::string scale_percent(float scale){
  return std::to_string(static_cast<int>(std::lround(scale*100.f)))+"%";
}
}
GeneralSettingsLayout GeneralSettingsLayout::for_viewport(int width,int height) noexcept {
  const float w=static_cast<float>(std::max(width,1)),h=static_cast<float>(std::max(height,1));
  const float s=std::max(.001f,std::min({2.6f,w/1280.f,h/720.f,std::max(1.f,w-24.f)/680.f,std::max(1.f,h-24.f)/500.f}));
  const UiRect panel{(w-680.f*s)*.5f,(h-500.f*s)*.5f,680.f*s,500.f*s};
  const auto r=[&](float x,float y,float rw,float rh){return UiRect{panel.x+x*s,panel.y+y*s,rw*s,rh*s};};
  return {s,std::max(12,static_cast<int>(std::lround(17*s))),std::max(16,static_cast<int>(std::lround(26*s))),panel,
    r(30,65,150,32),r(192,65,150,32),r(30,310,620,66),r(30,380,620,40),
    r(30,424,146,40),r(188,424,146,40),r(346,424,146,40),r(504,424,146,40),r(358,65,292,32),r(30,111,620,32),
    r(30,153,199,32),r(240,153,199,32),r(450,153,200,32),r(30,195,199,32),r(240,195,199,32),r(450,195,200,32),
    r(30,233,199,30),r(240,233,199,30),r(450,233,200,30)};
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
    if(duplicate||!json.is_object()||(json.size()!=2&&json.size()<4||json.size()>15)||!json.contains("schemaVersion")||!json.at("schemaVersion").is_number_integer()||
       json.at("schemaVersion")!=1||!json.contains("screenshotDirectory")||!json.at("screenshotDirectory").is_string())throw std::runtime_error("unsupported settings schema");
    const auto encoded=json.at("screenshotDirectory").get<std::string>();
    const auto value=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(encoded.data()),encoded.size()));
    if(!valid_directory(value))throw std::runtime_error("saved screenshot directory is invalid or unavailable");
    saved_.screenshot_directory=value;
    if(json.contains("assetCategoriesCollapsed")){saved_.asset_categories_collapsed=json.at("assetCategoriesCollapsed").get<std::array<bool,5>>();saved_.assets_hidden=json.at("assetsHidden").get<bool>();}
    if(json.contains("eruptionQuality")){if(!json.at("eruptionQuality").is_number_integer())throw std::runtime_error("Invalid eruption quality");saved_.eruption_quality=json.at("eruptionQuality").get<int>();if(saved_.eruption_quality<0||saved_.eruption_quality>3)throw std::runtime_error("Invalid eruption quality");}
    if(json.contains("nebulaDensity")){if(!json.at("nebulaDensity").is_number_integer())throw std::runtime_error("Invalid visual density");const auto density=json.at("nebulaDensity").get<int>();if(density<0||density>2)throw std::runtime_error("Invalid visual density");saved_.nebula_density=density;}
    if(json.contains("reduceMotion")){if(!json.at("reduceMotion").is_boolean())throw std::runtime_error("Invalid reduced motion flag");saved_.accessibility.reduce_motion=json.at("reduceMotion").get<bool>();}
    if(json.contains("interfaceScale")){if(!json.at("interfaceScale").is_number_integer())throw std::runtime_error("Invalid interface scale");const auto scale=json.at("interfaceScale").get<int>();if(scale<0||scale>3)throw std::runtime_error("Invalid interface scale");saved_.interface_scale=scale;}
    if(json.contains("reduceFlashing")){if(!json.at("reduceFlashing").is_boolean())throw std::runtime_error("Invalid reduced flashing flag");saved_.accessibility.reduce_flashing=json.at("reduceFlashing").get<bool>();}
    if(json.contains("highContrast")){if(!json.at("highContrast").is_boolean())throw std::runtime_error("Invalid high contrast flag");saved_.accessibility.high_contrast=json.at("highContrast").get<bool>();}
    if(json.contains("colorBlind")){if(!json.at("colorBlind").is_number_integer())throw std::runtime_error("Invalid color-blind mode");const auto mode=json.at("colorBlind").get<int>();if(mode<0||mode>3)throw std::runtime_error("Invalid color-blind mode");saved_.accessibility.color_blind=static_cast<stellar::engine::ColorBlindMode>(mode);}
    if(json.contains("subtitlesEnabled")){if(!json.at("subtitlesEnabled").is_boolean())throw std::runtime_error("Invalid subtitles flag");saved_.accessibility.subtitles_enabled=json.at("subtitlesEnabled").get<bool>();}
    if(json.contains("subtitleScale")){if(!json.at("subtitleScale").is_number())throw std::runtime_error("Invalid subtitle scale");const auto scale=json.at("subtitleScale").get<float>();if(scale<0.75f||scale>2.f)throw std::runtime_error("Invalid subtitle scale");saved_.accessibility.subtitle_scale=scale;}
    if(json.contains("textScale")){if(!json.at("textScale").is_number())throw std::runtime_error("Invalid text scale");const auto scale=json.at("textScale").get<float>();if(scale<0.75f||scale>2.f)throw std::runtime_error("Invalid text scale");saved_.accessibility.text_scale=scale;}
    if(json.contains("locale")){if(!json.at("locale").is_string())throw std::runtime_error("Invalid locale id");const auto id=json.at("locale").get<std::string>();if(id.empty()||id.size()>16||id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")!=std::string::npos)throw std::runtime_error("Invalid locale id");saved_.locale=id;}
  } catch(const std::exception& error) {
    error_="Saved screenshot folder unavailable. The default location is active.";
    std::cerr<<"General settings load failed: "<<error.what()<<'\n';
  }
}
bool NativeGeneralSettings::save(GeneralPreferences value) {
  if(value.eruption_quality<0||value.eruption_quality>3){error_="Choose a stellar eruption quality.";return false;}
  if(value.interface_scale<0||value.interface_scale>3){error_="Choose an interface scale.";return false;}
  if(const auto mode=static_cast<int>(value.accessibility.color_blind);mode<0||mode>3){error_="Choose a color-blind mode.";return false;}
  if(value.locale.empty()||value.locale.size()>16||value.locale.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")!=std::string::npos){error_="Choose a language.";return false;}
  if(value.nebula_density<0||value.nebula_density>2){error_="Choose Low, Medium or High nebula density.";return false;}
  if(!valid_directory(value.screenshot_directory)){error_="Choose an existing absolute folder.";return false;}
  try {
    const auto text=nlohmann::json{{"schemaVersion",1},{"screenshotDirectory",utf8(value.screenshot_directory)},{"assetCategoriesCollapsed",value.asset_categories_collapsed},{"assetsHidden",value.assets_hidden},{"nebulaDensity",value.nebula_density},{"eruptionQuality",value.eruption_quality},{"reduceMotion",value.accessibility.reduce_motion},{"interfaceScale",value.interface_scale},{"reduceFlashing",value.accessibility.reduce_flashing},{"highContrast",value.accessibility.high_contrast},{"colorBlind",static_cast<int>(value.accessibility.color_blind)},{"subtitlesEnabled",value.accessibility.subtitles_enabled},{"subtitleScale",value.accessibility.subtitle_scale},{"textScale",value.accessibility.text_scale},{"locale",value.locale}}.dump();
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
void NativeGeneralSettings::open(){nebula_dropdown_.close();eruption_dropdown_.close();hover_feedback_.reset();draft_=saved_;pending_request_.reset();path_scroll_={};focus_=-1;visible_=true;}
void NativeGeneralSettings::cancel(){nebula_dropdown_.close();eruption_dropdown_.close();draft_=saved_;pending_request_.reset();focus_=-1;visible_=false;}
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
  draft_.screenshot_directory=std::move(*result.directory);path_scroll_={};error_.clear();
}
bool NativeGeneralSettings::handle(const InputEvent& event,int width,int height) {
  if(!visible_)return false;
  if(eruption_dropdown_.visible()){if(const auto choice=eruption_dropdown_.handle(event,GeneralSettingsLayout::for_viewport(width,height).eruptions,width,height))draft_.eruption_quality=*choice;return true;}
  if(nebula_dropdown_.visible()){if(const auto choice=nebula_dropdown_.handle(event,GeneralSettingsLayout::for_viewport(width,height).nebula,width,height))draft_.nebula_density=*choice;return true;}
  if(event.type==InputEventType::EscapePressed){cancel();return true;}
  const auto layout=GeneralSettingsLayout::for_viewport(width,height);
  // Focusable order is the hover-target order; while a folder browser is
  // pending only Cancel is reachable.
  const std::array<UiRect,17> focusables{layout.audio,layout.video,layout.nebula,layout.eruptions,layout.motion,layout.iscale,layout.flashing,layout.contrast,layout.colorblind,layout.language,layout.subtitles,layout.subtitle_scale,layout.text_scale,layout.browse,layout.defaults,layout.cancel,layout.save};
  hover_feedback_.update(event,stellar::native_menu_audio::hit(event.position,browsing()?std::span<const UiRect>{&layout.cancel,1}:std::span<const UiRect>{focusables}));
  if(event.type==InputEventType::Wheel&&layout.folder.contains(event.position)&&measure_){
    const auto text=path_text(layout);
    path_scroll_.sync(static_cast<float>(measure_(text).height),text.clip->height);
    path_scroll_.scroll_by(-event.wheel_y*40*layout.scale);return true;
  }
  if(event.type==InputEventType::KeyPressed){
    // SDL_Keycode: Tab/arrows move the focus ring, Return/Space activate.
    constexpr std::uint32_t kTab=9u,kReturn=13u,kSpace=32u;
    constexpr std::uint32_t kRight=0x4000004fu,kLeft=0x40000050u,kDown=0x40000051u,kUp=0x40000052u;
    constexpr std::uint32_t kHome=0x4000004au,kEnd=0x4000004du;
    const int count=browsing()?1:static_cast<int>(focusables.size());
    if(event.key==kHome||event.key==kEnd){
      focus_=event.key==kHome?0:count-1;
      hover_feedback_.cue(static_cast<std::uint64_t>(focus_)+1);return true;
    }
    const bool fwd=(event.key==kTab&&!event.shift)||event.key==kRight||event.key==kDown;
    const bool bwd=(event.key==kTab&&event.shift)||event.key==kLeft||event.key==kUp;
    if(fwd||bwd){
      if(focus_<0)focus_=bwd?count-1:0;else focus_=(focus_+(bwd?-1:1)+count)%count;
      hover_feedback_.cue(static_cast<std::uint64_t>(focus_)+1);return true;
    }
    if((event.key==kReturn||event.key==kSpace)&&focus_>=0){
      const auto& rect=browsing()?layout.cancel:focusables[static_cast<std::size_t>(focus_)];
      activate_at(layout,{rect.x+rect.width*.5f,rect.y+rect.height*.5f});return true;
    }
  }
  if(event.type!=InputEventType::LeftPressed)return true;
  focus_=-1;
  activate_at(layout,event.position);return true;
}
void NativeGeneralSettings::activate_at(const GeneralSettingsLayout& layout,stellar::native_map::Point position) {
  if(layout.cancel.contains(position)){cancel();return;}
  if(browsing())return;
  if(layout.eruptions.contains(position)){eruption_dropdown_.open(0,{"Low","Medium","High","Ultra"},draft_.eruption_quality);return;}
  if(layout.nebula.contains(position)){nebula_dropdown_.open(0,{"Low","Medium","High"},draft_.nebula_density);return;}
  if(layout.motion.contains(position)){draft_.accessibility.reduce_motion=!draft_.accessibility.reduce_motion;return;}
  if(layout.iscale.contains(position)){draft_.interface_scale=(draft_.interface_scale+1)%4;return;}
  if(layout.flashing.contains(position)){draft_.accessibility.reduce_flashing=!draft_.accessibility.reduce_flashing;return;}
  if(layout.contrast.contains(position)){draft_.accessibility.high_contrast=!draft_.accessibility.high_contrast;return;}
  if(layout.colorblind.contains(position)){draft_.accessibility.color_blind=static_cast<stellar::engine::ColorBlindMode>((static_cast<int>(draft_.accessibility.color_blind)+1)%4);return;}
  if(layout.language.contains(position)&&!locales_.empty()){
    const auto at=std::find(locales_.begin(),locales_.end(),draft_.locale);
    const auto index=at==locales_.end()?std::size_t{0}:(static_cast<std::size_t>(at-locales_.begin())+1)%locales_.size();
    draft_.locale=locales_.at(index);return;}
  if(layout.subtitles.contains(position)){draft_.accessibility.subtitles_enabled=!draft_.accessibility.subtitles_enabled;return;}
  if(layout.subtitle_scale.contains(position)){draft_.accessibility.subtitle_scale=next_text_scale(draft_.accessibility.subtitle_scale);return;}
  if(layout.text_scale.contains(position)){draft_.accessibility.text_scale=next_text_scale(draft_.accessibility.text_scale);return;}
  if(layout.audio.contains(position)&&audio_){cancel();audio_();}
  else if(layout.video.contains(position)&&video_){cancel();video_();}
  else if(layout.browse.contains(position)) {
    if(!browse_){error_="Folder browser is unavailable.";return;}
    const auto id=++next_request_;pending_request_=id;
    try {
      if(!browse_(id,draft_.screenshot_directory.empty()?default_directory_:draft_.screenshot_directory)){
        pending_request_.reset();error_="Finish the open folder browser before trying again.";
      } else error_.clear();
    } catch(const std::exception& error){pending_request_.reset();error_="Folder browser could not open. Please try again.";std::cerr<<error.what()<<'\n';}
  } else if(layout.defaults.contains(position)){draft_.screenshot_directory.clear();path_scroll_={};error_.clear();}
  else if(layout.save.contains(position)&&save(draft_))visible_=false;
}
std::string NativeGeneralSettings::focused_label()const{
  if(focus_<0)return{};
  if(browsing())return tr("SETTINGS_CANCEL","Cancel");
  const auto quality_name=[&](std::string_view key,std::string_view fallback){return tr(key,fallback);};
  const std::array<std::string,3> densities{quality_name("SETTINGS_QUALITY_LOW","Low"),quality_name("SETTINGS_QUALITY_MEDIUM","Medium"),quality_name("SETTINGS_QUALITY_HIGH","High")};
  const std::array<std::string,4> details{densities[0],densities[1],densities[2],quality_name("SETTINGS_QUALITY_ULTRA","Ultra")};
  const std::array<std::string,4> scale_names{quality_name("SETTINGS_SCALE_COMPACT","Compact"),quality_name("SETTINGS_SCALE_STANDARD","Standard"),
    quality_name("SETTINGS_SCALE_LARGE","Large"),quality_name("SETTINGS_SCALE_HUGE","Huge")};
  const std::array<std::string,4> colorblind_names{quality_name("SETTINGS_COLORBLIND_NONE","Off"),quality_name("SETTINGS_COLORBLIND_PROTANOPIA","Protanopia"),
    quality_name("SETTINGS_COLORBLIND_DEUTERANOPIA","Deuteranopia"),quality_name("SETTINGS_COLORBLIND_TRITANOPIA","Tritanopia")};
  const auto locale_name=[](std::string_view id){if(id=="en")return std::string("English");if(id=="de")return std::string("Deutsch");return std::string(id);};
  const auto on_off=[&](bool v){return tr(v?"SETTINGS_STATE_ON":"SETTINGS_STATE_OFF",v?"On":"Off");};
  switch(focus_){
  case 0:return tr("SETTINGS_NAV_AUDIO","Audio");
  case 1:return tr("SETTINGS_NAV_VIDEO","Video");
  case 2:return trf("SETTINGS_NEBULA_DENSITY",{densities.at(draft_.nebula_density)},"Space phenomena density: {0}");
  case 3:return trf("SETTINGS_ERUPTION_DETAIL",{details.at(draft_.eruption_quality)},"Stellar eruption detail: {0}");
  case 4:return trf("SETTINGS_REDUCE_MOTION",{on_off(draft_.accessibility.reduce_motion)},"Reduced motion (decorative animation): {0}");
  case 5:return trf("SETTINGS_INTERFACE_SCALE",{scale_names.at(static_cast<std::size_t>(draft_.interface_scale))},"Interface scale: {0}");
  case 6:return trf("SETTINGS_REDUCE_FLASHING",{on_off(draft_.accessibility.reduce_flashing)},"Reduce flashing: {0}");
  case 7:return trf("SETTINGS_HIGH_CONTRAST",{on_off(draft_.accessibility.high_contrast)},"High contrast: {0}");
  case 8:return trf("SETTINGS_COLOR_BLIND",{colorblind_names.at(static_cast<std::size_t>(draft_.accessibility.color_blind))},"Color-blind mode: {0}");
  case 9:return trf("SETTINGS_LANGUAGE",{locale_name(draft_.locale)},"Language: {0}");
  case 10:return trf("SETTINGS_SUBTITLES",{on_off(draft_.accessibility.subtitles_enabled)},"Subtitles: {0}");
  case 11:return trf("SETTINGS_SUBTITLE_SCALE",{scale_percent(draft_.accessibility.subtitle_scale)},"Subtitle size: {0}");
  case 12:return trf("SETTINGS_TEXT_SCALE",{scale_percent(draft_.accessibility.text_scale)},"Text size: {0}");
  case 13:return tr("SETTINGS_BROWSE","Browse");
  case 14:return tr("SETTINGS_USE_DEFAULT","Use default");
  case 15:return tr("SETTINGS_CANCEL","Cancel");
  case 16:return tr("SETTINGS_SAVE","Save");
  default:return{};
  }
}
std::optional<UiRect> NativeGeneralSettings::focused_bounds(int width,int height)const{
  if(focus_<0)return std::nullopt;
  const auto layout=GeneralSettingsLayout::for_viewport(width,height);
  if(browsing())return layout.cancel;
  const std::array<UiRect,17> focusables{layout.audio,layout.video,layout.nebula,layout.eruptions,layout.motion,layout.iscale,layout.flashing,layout.contrast,layout.colorblind,layout.language,layout.subtitles,layout.subtitle_scale,layout.text_scale,layout.browse,layout.defaults,layout.cancel,layout.save};
  return focus_<static_cast<int>(focusables.size())?std::optional<UiRect>{focusables[static_cast<std::size_t>(focus_)]}:std::nullopt;
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
  button(draw,l.motion,trf("SETTINGS_REDUCE_MOTION",{tr(draft_.accessibility.reduce_motion?"SETTINGS_STATE_ON":"SETTINGS_STATE_OFF",draft_.accessibility.reduce_motion?"On":"Off")},"Reduced motion (decorative animation): {0}"),l.font_pixels,false,browsing());
  const std::array<std::string,4> scale_names{quality_name("SETTINGS_SCALE_COMPACT","Compact"),quality_name("SETTINGS_SCALE_STANDARD","Standard"),
    quality_name("SETTINGS_SCALE_LARGE","Large"),quality_name("SETTINGS_SCALE_HUGE","Huge")};
  button(draw,l.iscale,trf("SETTINGS_INTERFACE_SCALE",{scale_names.at(static_cast<std::size_t>(draft_.interface_scale))},"Interface scale: {0}"),l.font_pixels,false,browsing());
  button(draw,l.flashing,trf("SETTINGS_REDUCE_FLASHING",{tr(draft_.accessibility.reduce_flashing?"SETTINGS_STATE_ON":"SETTINGS_STATE_OFF",draft_.accessibility.reduce_flashing?"On":"Off")},"Reduce flashing: {0}"),l.font_pixels,false,browsing());
  button(draw,l.contrast,trf("SETTINGS_HIGH_CONTRAST",{tr(draft_.accessibility.high_contrast?"SETTINGS_STATE_ON":"SETTINGS_STATE_OFF",draft_.accessibility.high_contrast?"On":"Off")},"High contrast: {0}"),l.font_pixels,false,browsing());
  const std::array<std::string,4> colorblind_names{quality_name("SETTINGS_COLORBLIND_NONE","Off"),quality_name("SETTINGS_COLORBLIND_PROTANOPIA","Protanopia"),
    quality_name("SETTINGS_COLORBLIND_DEUTERANOPIA","Deuteranopia"),quality_name("SETTINGS_COLORBLIND_TRITANOPIA","Tritanopia")};
  button(draw,l.colorblind,trf("SETTINGS_COLOR_BLIND",{colorblind_names.at(static_cast<std::size_t>(draft_.accessibility.color_blind))},"Color-blind mode: {0}"),l.font_pixels,false,browsing());
  // Native-language names for shipped locale ids; unknown ids display raw.
  const auto locale_name=[&](std::string_view id){if(id=="en")return std::string("English");if(id=="de")return std::string("Deutsch");return std::string(id);};
  button(draw,l.language,trf("SETTINGS_LANGUAGE",{locale_name(draft_.locale)},"Language: {0}"),l.font_pixels,false,browsing());
  button(draw,l.subtitles,trf("SETTINGS_SUBTITLES",{tr(draft_.accessibility.subtitles_enabled?"SETTINGS_STATE_ON":"SETTINGS_STATE_OFF",draft_.accessibility.subtitles_enabled?"On":"Off")},"Subtitles: {0}"),l.font_pixels,false,browsing());
  button(draw,l.subtitle_scale,trf("SETTINGS_SUBTITLE_SCALE",{scale_percent(draft_.accessibility.subtitle_scale)},"Subtitle size: {0}"),l.font_pixels,false,browsing());
  button(draw,l.text_scale,trf("SETTINGS_TEXT_SCALE",{scale_percent(draft_.accessibility.text_scale)},"Text size: {0}"),l.font_pixels,false,browsing());
  label(draw,{l.folder.x,l.panel.y+266*s,l.folder.width,20*s},tr("SETTINGS_SCREENSHOT_FOLDER","SCREENSHOT FOLDER"),l.font_pixels);
  label(draw,{l.folder.x,l.panel.y+288*s,l.folder.width,18*s},tr("SETTINGS_SCREENSHOT_HINT","Press F12 to save a PNG of the game."),l.font_pixels);
  draw.overlay.emplace_back(FilledRectangle{l.folder,{5,16,30,255}});draw.overlay.emplace_back(StrokedRectangle{l.folder,{65,111,143,255}});
  auto path=path_text(l);
  const auto maximum=measure_?std::max(0.f,static_cast<float>(measure_(path).height)-path.clip->height):0.f;
  path.at.y-=std::min(path_scroll_.scroll_offset,maximum);draw.overlay.emplace_back(std::move(path));
  label(draw,l.status,browsing()?"Choose a folder in the Windows browser...":!error_.empty()?error_:
    maximum>0?"Scroll over the path to see the full folder. Save to keep your choice.":draft_.screenshot_directory.empty()?"Using the default Pictures folder. Save to keep this choice.":"Save to use this folder. Cancel keeps your current location.",l.font_pixels);
  button(draw,l.browse,browsing()?tr("SETTINGS_BROWSING","BROWSING..."):tr("SETTINGS_BROWSE","BROWSE"),l.font_pixels,false,browsing());
  button(draw,l.defaults,tr("SETTINGS_USE_DEFAULT","USE DEFAULT"),l.font_pixels,false,browsing());button(draw,l.cancel,tr("SETTINGS_CANCEL","CANCEL"),l.font_pixels);
  button(draw,l.save,tr("SETTINGS_SAVE","SAVE"),l.font_pixels,true,browsing());
  if(focus_>=0){
    const std::array<UiRect,17> focusables{l.audio,l.video,l.nebula,l.eruptions,l.motion,l.iscale,l.flashing,l.contrast,l.colorblind,l.language,l.subtitles,l.subtitle_scale,l.text_scale,l.browse,l.defaults,l.cancel,l.save};
    const auto& rect=browsing()?l.cancel:focusables[static_cast<std::size_t>(std::min(focus_,16))];
    stellar::native_ui::focus_ring(draw,rect);
  }
  nebula_dropdown_.render(draw,l.nebula,width,height,l.font_pixels);
  eruption_dropdown_.render(draw,l.eruptions,width,height,l.font_pixels);
}
} // namespace stellar::native_general
