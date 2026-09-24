#include <stellar/engine/asset_registry.hpp>
#include <stellar/engine/memory_tracker.hpp>
#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/native_triangle_mesh.hpp>
#include "native_scene3d_gpu.hpp"
#include <stellar/engine/windows_resource_ids.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_render.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iterator>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <tuple>
#include <unordered_map>
#include <utility>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <ShlObj.h>
#include <appmodel.h>
#include <shellapi.h>
#else
#error The native UI preview requires the Windows GDI text rasterizer.
#endif

namespace stellar::native_map {
namespace {
[[nodiscard]] std::runtime_error sdl_error(const char *operation){return std::runtime_error(std::string(operation)+": "+SDL_GetError());}
void require(bool success,const char *operation){if(!success)throw sdl_error(operation);}
[[nodiscard]] std::string utf8_path(const std::filesystem::path &path){const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
struct WindowDecoration { int top{},left{},bottom{},right{}; };
[[nodiscard]] WindowDecoration window_decoration(SDL_Window *window,SDL_DisplayID display){
  WindowDecoration result;
  require(SDL_GetWindowBordersSize(window,&result.top,&result.left,&result.bottom,&result.right),"Window border query failed");
  // SDL reports zero borders while fullscreen. Use the process DPI-aware Win32
  // non-client metrics to filter prospective windowed choices conservatively.
  if(result.top==0&&result.left==0&&result.bottom==0&&result.right==0){
    const auto dpi=static_cast<UINT>(std::clamp(std::lround(SDL_GetDisplayContentScale(display)*96.f),96l,768l));
    const auto frame=GetSystemMetricsForDpi(SM_CXSIZEFRAME,dpi)+GetSystemMetricsForDpi(SM_CXPADDEDBORDER,dpi);
    result.left=result.right=std::max(0,frame);
    result.top=std::max(0,GetSystemMetricsForDpi(SM_CYCAPTION,dpi)+frame);
    result.bottom=std::max(0,frame);
  }
  return result;
}
void report_capture(const std::filesystem::path &path,int width,int height){
  std::string escaped;
  for(const unsigned char value:utf8_path(std::filesystem::absolute(path).lexically_normal())){
    if(value=='"'||value=='\\'){escaped+='\\';escaped+=static_cast<char>(value);}
    else if(value<32){constexpr char hex[]="0123456789abcdef";escaped+="\\u00";escaped+=hex[value>>4];escaped+=hex[value&15];}
    else escaped+=static_cast<char>(value);
  }
  std::cout<<"native_capture={\"path\":\""<<escaped<<"\",\"width\":"<<width<<",\"height\":"<<height<<"}\n";
}
[[nodiscard]] std::filesystem::path player_screenshot_directory(const std::filesystem::path& configured){
  wchar_t override_path[32768]{};
  const auto length=GetEnvironmentVariableW(L"STELLAR_SCREENSHOT_DIR",override_path,static_cast<DWORD>(std::size(override_path)));
  if(length>0&&length<std::size(override_path))return override_path;
  if(!configured.empty())return configured;
  return Window::default_screenshot_directory();
}
struct FolderDialogState {
  std::mutex mutex;
  bool pending{};
  std::optional<FolderDialogResult> result;
};
struct FolderDialogRequest {
  std::shared_ptr<FolderDialogState> state;
  std::uint64_t id{};
  std::string initial_directory;
};
void SDLCALL folder_dialog_complete(void* userdata,const char* const* files,int) noexcept {
  std::unique_ptr<FolderDialogRequest> request(static_cast<FolderDialogRequest*>(userdata));
  // SDL may invoke this from another thread. Copy SDL-owned strings before
  // returning; the shared state remains valid even after Window destruction.
  try {
    FolderDialogResult result{request->id,{},{}};
    if(!files)result.error=std::string("Folder browser failed: ")+SDL_GetError();
    else if(files[0])result.directory=std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(files[0])));
    std::lock_guard lock(request->state->mutex);
    request->state->result=std::move(result);
    request->state->pending=false;
  } catch(...) {
    // Never unwind through SDL's C callback. A missing result is treated as
    // cancellation if an exceptional allocation/conversion failed.
    std::lock_guard lock(request->state->mutex);
    request->state->result=FolderDialogResult{request->id,{},{}};
    request->state->pending=false;
  }
}
}
std::filesystem::path Window::default_screenshot_directory(){
  PWSTR pictures{};
  if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Pictures,0,nullptr,&pictures))&&pictures){
    const std::filesystem::path result=std::filesystem::path(pictures)/L"Stellar Continuum"/L"Screenshots";
    CoTaskMemFree(pictures);return result;
  }
  throw std::runtime_error("Pictures folder is unavailable.");
}
namespace {
[[nodiscard]] std::filesystem::path player_screenshot_path(std::uint64_t serial,const std::filesystem::path& configured){
  SYSTEMTIME time{};GetLocalTime(&time);wchar_t name[96]{};
  swprintf_s(name,L"Stellar-Continuum-%04u%02u%02u-%02u%02u%02u-%03u-%llu.png",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds,static_cast<unsigned long long>(serial));
  auto directory=player_screenshot_directory(configured);std::error_code error;std::filesystem::create_directories(directory,error);
  if(error)throw std::runtime_error("Screenshot folder could not be created.");
  auto path=directory/name;
  for(std::uint32_t suffix=1;std::filesystem::exists(path,error);++suffix){
    if(error||suffix>9999)throw std::runtime_error("A unique screenshot filename could not be allocated.");
    path=directory/(std::filesystem::path(name).stem().wstring()+L"-"+std::to_wstring(suffix)+L".png");
  }
  return path;
}
[[nodiscard]] std::wstring utf16(std::string_view value){
  if(value.empty())return {};
  if(value.size()>static_cast<std::size_t>(std::numeric_limits<int>::max()))throw std::length_error("Text is too long for the Windows rasterizer.");
  const auto size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
  if(size<=0)throw std::runtime_error("UI text is not valid UTF-8.");
  std::wstring result(static_cast<std::size_t>(size),L'\0');
  if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size)!=size)throw std::runtime_error("UI text conversion failed.");
  return result;
}
[[nodiscard]] SDL_FRect sdl_rect(UiRect value){return {value.x,value.y,value.width,value.height};}
struct TextKey {std::string value;int pixel_size{},wrap_pixels{};FontFace face{};bool operator==(const TextKey&)const=default;};
struct TextKeyHash {[[nodiscard]] std::size_t operator()(const TextKey &key)const noexcept{auto hash=std::hash<std::string>{}(key.value);hash^=static_cast<std::size_t>(key.pixel_size)+0x9e3779b9u+(hash<<6u)+(hash>>2u);hash^=static_cast<std::size_t>(key.wrap_pixels)+0x9e3779b9u+(hash<<6u)+(hash>>2u);hash^=static_cast<std::size_t>(key.face)+0x9e3779b9u+(hash<<6u)+(hash>>2u);return hash;}};
struct CachedText {SDL_Texture *texture{};int width{},height{};std::size_t bytes{};std::uint64_t last_use{};};
class GdiObjectOwner {
public:
  explicit GdiObjectOwner(HGDIOBJ value) noexcept:value_(value){}
  ~GdiObjectOwner(){if(value_)DeleteObject(value_);}
  GdiObjectOwner(const GdiObjectOwner&)=delete;GdiObjectOwner& operator=(const GdiObjectOwner&)=delete;
  [[nodiscard]] HGDIOBJ release() noexcept{const auto value=value_;value_=nullptr;return value;}
private:HGDIOBJ value_{};
};
class GdiSelection {
public:
  GdiSelection(HDC dc,HGDIOBJ value):dc_(dc),previous_(SelectObject(dc,value)){if(!previous_||previous_==HGDI_ERROR)throw std::runtime_error("Windows could not select a UI drawing object.");}
  ~GdiSelection(){if(previous_&&previous_!=HGDI_ERROR)(void)SelectObject(dc_,previous_);}
  GdiSelection(const GdiSelection&)=delete;GdiSelection& operator=(const GdiSelection&)=delete;
private:HDC dc_{};HGDIOBJ previous_{};
};
[[nodiscard]] bool valid_clip(UiRect value) noexcept{return std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.width)&&std::isfinite(value.height)&&std::abs(value.x)<=65536.f&&std::abs(value.y)<=65536.f&&value.width>=0.f&&value.width<=65536.f&&value.height>=0.f&&value.height<=65536.f;}
[[nodiscard]] bool valid_positive_rect(UiRect value)noexcept{return valid_clip(value)&&value.width>0.f&&value.height>0.f;}
[[nodiscard]] bool valid_point(Point value)noexcept{return std::isfinite(value.x)&&std::isfinite(value.y);}
constexpr int soft_circle_segments=20;
using SoftCircleDirections=std::array<Point,soft_circle_segments+1>;
using SoftCircleIndices=std::array<int,soft_circle_segments*3>;
[[nodiscard]] const SoftCircleDirections &soft_circle_directions(){
  static const SoftCircleDirections directions=[](){
    SoftCircleDirections result{};
    for(int index=0;index<=soft_circle_segments;++index){const auto angle=2.f*std::numbers::pi_v<float>*static_cast<float>(index)/static_cast<float>(soft_circle_segments);result[static_cast<std::size_t>(index)]={std::cos(angle),std::sin(angle)};}
    return result;
  }();
  return directions;
}
[[nodiscard]] const SoftCircleIndices &soft_circle_indices(){
  static const SoftCircleIndices indices=[](){
    SoftCircleIndices result{};
    for(int index=0;index<soft_circle_segments;++index){const auto offset=static_cast<std::size_t>(index)*3u;result[offset]=0;result[offset+1]=index+1;result[offset+2]=index+2;}
    return result;
  }();
  return indices;
}
}

struct Window::Storage {
  std::shared_ptr<FolderDialogState> folder_dialog{std::make_shared<FolderDialogState>()};
  static constexpr std::size_t text_cache_capacity=640,text_cache_byte_capacity=32u*1024u*1024u;
  struct CachedImage {std::shared_ptr<const RgbaImage> owner;SDL_Texture *texture{};std::size_t resident_bytes{};std::uint64_t last_use{};};
  SDL_Window *window{};SDL_GPUDevice *device{};SDL_Renderer *renderer{};HDC text_dc{};
  std::vector<std::uint8_t> private_font_bytes;HANDLE private_font_handle{};
  std::vector<SDL_Vertex> triangle_vertices;
  std::unordered_map<int,HFONT> fonts;std::unordered_map<TextKey,CachedText,TextKeyHash> text_cache;std::size_t text_cache_bytes{};std::uint64_t text_use{};
  std::unordered_map<const RgbaImage*,CachedImage> image_cache;std::size_t image_cache_resident_bytes{};std::uint64_t image_use{},image_uploads{};
  int width{},height{},windowed_x{},windowed_y{},windowed_width{},windowed_height{};bool has_windowed_bounds{},initialized{},left_down{},focused{true},minimized{},vsync{},auto_frame_cap{},text_input_requested{},text_input_active{};Point pointer{};std::filesystem::path screenshot_directory;std::optional<std::filesystem::path> player_screenshot;std::optional<std::string> screenshot_status;std::uint64_t screenshot_status_until_ns{},screenshot_serial{};Uint64 fallback_interval_ns{},frame_cap_interval_ns{},last_present_ns{};
  SDL_Texture* scene_target{};
  SDL_Gamepad *gamepad{};SDL_JoystickID gamepad_id{};
  std::unique_ptr<Scene3DRenderer> scene3d;
  // MemoryTracker VRAM attribution — the 3D backend reports its resident
  // texture/mesh/render-target bytes once a scene3d view draws.
  engine::MemoryTracker::SubsystemId gpu_texture_subsystem{engine::MemoryTracker::invalid_subsystem},
      gpu_mesh_subsystem{engine::MemoryTracker::invalid_subsystem},
      gpu_target_subsystem{engine::MemoryTracker::invalid_subsystem},
      image_cache_subsystem{engine::MemoryTracker::invalid_subsystem},
      text_cache_subsystem{engine::MemoryTracker::invalid_subsystem};
  int scene_width{},scene_height{},scene_percent{100},scene_samples{1};
  void prepare_scene_target(int percent,int samples) {
    if(percent==100&&samples==1){if(scene_target)SDL_DestroyTexture(scene_target);scene_target=nullptr;scene_width=scene_height=0;return;}
    const double factor=percent*.01*std::sqrt(static_cast<double>(samples));
    const int w=std::max(1,static_cast<int>(std::ceil(width*factor))),h=std::max(1,static_cast<int>(std::ceil(height*factor)));
    if(scene_target&&w==scene_width&&h==scene_height)return;
    const auto maximum=SDL_GetNumberProperty(SDL_GetRendererProperties(renderer),SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER,8192);
    if(w>maximum||h>maximum||static_cast<std::uint64_t>(w)*h*4>192u*1024u*1024u)
      throw std::runtime_error("Selected scene quality exceeds the 192 MiB render-target budget at this resolution.");
    auto* next=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA8888,SDL_TEXTUREACCESS_TARGET,w,h);
    if(!next)throw sdl_error("Scene quality render target could not be created");
    if(!SDL_SetTextureScaleMode(next,SDL_SCALEMODE_LINEAR)||!SDL_SetTextureBlendMode(next,SDL_BLENDMODE_NONE)){SDL_DestroyTexture(next);throw sdl_error("Scene quality filtering failed");}
    if(scene_target)SDL_DestroyTexture(scene_target);scene_target=next;scene_width=w;scene_height=h;
  }
  ~Storage(){
    scene3d.reset();
    if(scene_target)SDL_DestroyTexture(scene_target);
    for(auto &[key,cached]:image_cache){(void)key;if(cached.texture)SDL_DestroyTexture(cached.texture);}
    for(auto &[key,cached]:text_cache){(void)key;if(cached.texture)SDL_DestroyTexture(cached.texture);}
    for(const auto &[size,font_value]:fonts){(void)size;if(font_value)DeleteObject(font_value);}
    if(text_dc)DeleteDC(text_dc);if(private_font_handle)RemoveFontMemResourceEx(private_font_handle);
    if(renderer)SDL_DestroyRenderer(renderer);if(gamepad)SDL_CloseGamepad(gamepad);if(device)SDL_DestroyGPUDevice(device);if(window)SDL_DestroyWindow(window);if(initialized)SDL_Quit();
  }
  [[nodiscard]] HFONT font(int pixel_size,FontFace role){
    pixel_size=std::clamp(pixel_size,8,72);const auto font_key=pixel_size*2+(role==FontFace::Heading?1:0);if(const auto found=fonts.find(font_key);found!=fonts.end())return found->second;
    const auto face=role==FontFace::Heading?L"Rajdhani SemiBold":L"Segoe UI";
    auto created=CreateFontW(-pixel_size,0,0,0,role==FontFace::Heading?FW_SEMIBOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_TT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH|FF_DONTCARE,face);
    if(!created)throw std::runtime_error("Windows could not create the UI font.");GdiObjectOwner created_owner(created);
    wchar_t selected[64]{};int count{};{const GdiSelection selection(text_dc,created);count=GetTextFaceW(text_dc,static_cast<int>(std::size(selected)),selected);}
    const auto expected=role==FontFace::Heading?L"Rajdhani":L"Segoe UI";if(count<=0||std::wstring_view(selected).find(expected)==std::wstring_view::npos)throw std::runtime_error(role==FontFace::Heading?"Windows did not select the bundled Rajdhani font.":"Windows did not select the native interface font.");
    const auto [inserted,ok]=fonts.emplace(font_key,created);if(!ok)throw std::logic_error("Duplicate UI font cache key.");(void)created_owner.release();return inserted->second;
  }
  void evict_text(std::size_t incoming){
    if(incoming>text_cache_byte_capacity)throw std::length_error("A UI text layout exceeds the cache byte limit.");
    while(text_cache.size()>=text_cache_capacity||text_cache_bytes+incoming>text_cache_byte_capacity){
      const auto oldest=std::min_element(text_cache.begin(),text_cache.end(),[](const auto &left,const auto &right){return left.second.last_use<right.second.last_use;});
      if(oldest==text_cache.end())break;text_cache_bytes-=oldest->second.bytes;SDL_DestroyTexture(oldest->second.texture);text_cache.erase(oldest);
    }
  }
  [[nodiscard]] CachedText &text(const Text &label){
    if(!std::isfinite(label.wrap_width)||label.wrap_width>16384.f)throw std::invalid_argument("UI text wrap width must be finite and at most 16384 pixels.");
    const auto pixel_size=std::clamp(label.font_pixel_size,8,72);const auto wrap_pixels=label.wrap_width>0.f?std::max(1,static_cast<int>(std::lround(label.wrap_width))):0;TextKey key{label.value,pixel_size,wrap_pixels,label.face};
    if(const auto found=text_cache.find(key);found!=text_cache.end()){found->second.last_use=++text_use;return found->second;}
    const auto wide=utf16(label.value);RECT bounds{0,0,wrap_pixels,0};const auto flags=DT_CALCRECT|DT_NOPREFIX|(wrap_pixels>0?DT_WORDBREAK:DT_SINGLELINE);
    {const GdiSelection selection(text_dc,font(pixel_size,label.face));if(!DrawTextW(text_dc,wide.c_str(),static_cast<int>(wide.size()),&bounds,flags))throw std::runtime_error("Windows could not measure UI text.");}
    const auto text_width=std::max(1L,bounds.right-bounds.left),text_height=std::max(1L,bounds.bottom-bounds.top);if(text_width>16384L||text_height>16384L)throw std::length_error("A UI text layout exceeds the rasterizer dimension limit.");const auto byte_count=static_cast<std::size_t>(text_width)*static_cast<std::size_t>(text_height)*4u;evict_text(byte_count);
    BITMAPINFO bitmap{};bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bitmap.bmiHeader.biWidth=text_width;bitmap.bmiHeader.biHeight=-text_height;bitmap.bmiHeader.biPlanes=1;bitmap.bmiHeader.biBitCount=32;bitmap.bmiHeader.biCompression=BI_RGB;
    void *pixels=nullptr;const auto dib=CreateDIBSection(text_dc,&bitmap,DIB_RGB_COLORS,&pixels,nullptr,0);GdiObjectOwner dib_owner(dib);if(!dib||!pixels)throw std::runtime_error("Windows could not allocate a UI text bitmap.");
    std::memset(pixels,0,byte_count);RECT draw_bounds{0,0,text_width,text_height};int drawn{};{const GdiSelection font_selection(text_dc,font(pixel_size,label.face));const GdiSelection bitmap_selection(text_dc,dib);if(SetBkMode(text_dc,TRANSPARENT)==0||SetTextColor(text_dc,RGB(255,255,255))==CLR_INVALID)throw std::runtime_error("Windows could not configure UI text drawing.");drawn=DrawTextW(text_dc,wide.c_str(),static_cast<int>(wide.size()),&draw_bounds,flags&~DT_CALCRECT);if(!GdiFlush())throw std::runtime_error("Windows could not flush UI text drawing.");}
    if(!drawn)throw std::runtime_error("Windows could not rasterize UI text.");
    auto *bytes=static_cast<std::uint8_t*>(pixels);for(std::size_t index=0;index<byte_count/4u;++index){auto *pixel=bytes+index*4u;const auto coverage=std::max({pixel[0],pixel[1],pixel[2]});pixel[0]=255;pixel[1]=255;pixel[2]=255;pixel[3]=coverage;}
    auto *texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,static_cast<int>(text_width),static_cast<int>(text_height));if(!texture)throw sdl_error("SDL text texture creation failed");
    try{require(SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND),"SDL text blend setup failed");require(SDL_SetTextureScaleMode(texture,SDL_SCALEMODE_LINEAR),"SDL text scale setup failed");require(SDL_UpdateTexture(texture,nullptr,pixels,static_cast<int>(text_width)*4),"SDL text texture upload failed");}catch(...){SDL_DestroyTexture(texture);throw;}
    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> texture_owner(texture,SDL_DestroyTexture);auto [inserted,ok]=text_cache.emplace(std::move(key),CachedText{texture,static_cast<int>(text_width),static_cast<int>(text_height),byte_count,++text_use});if(!ok)throw std::logic_error("Duplicate UI text cache key.");(void)texture_owner.release();text_cache_bytes+=byte_count;return inserted->second;
  }
  void evict_images(std::size_t incoming){
    if(incoming>maximum_image_cache_resident_bytes)throw std::length_error("An image exceeds the 192 MiB texture-cache resident limit.");
    while(image_cache.size()>=maximum_image_cache_entries||image_cache_resident_bytes+incoming>maximum_image_cache_resident_bytes){
      const auto oldest=std::min_element(image_cache.begin(),image_cache.end(),[](const auto &left,const auto &right){return left.second.last_use<right.second.last_use;});
      if(oldest==image_cache.end())break;image_cache_resident_bytes-=oldest->second.resident_bytes;SDL_DestroyTexture(oldest->second.texture);image_cache.erase(oldest);
    }
  }
  [[nodiscard]] CachedImage &image(const std::shared_ptr<const RgbaImage> &resource){
    if(!resource)throw std::invalid_argument("An image command requires an RGBA resource.");
    if(const auto found=image_cache.find(resource.get());found!=image_cache.end()){found->second.last_use=++image_use;return found->second;}
    const auto bytes=resource->byte_size(),gpu_bytes=resource->pixels().size();
    if(bytes>maximum_image_cache_resident_bytes||gpu_bytes>maximum_image_cache_resident_bytes-bytes)throw std::length_error("An image exceeds the texture-cache resident limit.");
    const auto resident=bytes+gpu_bytes;evict_images(resident);
    auto *texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,resource->width(),resource->height());if(!texture)throw sdl_error("SDL image texture creation failed");
    try{require(SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND),"SDL image blend setup failed");require(SDL_SetTextureScaleMode(texture,SDL_SCALEMODE_LINEAR),"SDL image scale setup failed");require(SDL_UpdateTexture(texture,nullptr,resource->pixels().data(),resource->width()*4),"SDL image texture upload failed");}catch(...){SDL_DestroyTexture(texture);throw;}
    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> owner(texture,SDL_DestroyTexture);auto [inserted,ok]=image_cache.emplace(resource.get(),CachedImage{resource,texture,resident,++image_use});if(!ok)throw std::logic_error("Duplicate image cache key.");(void)owner.release();image_cache_resident_bytes+=resident;++image_uploads;return inserted->second;
  }
  void draw_text(const Text &label){
    if(label.value.empty())return;if(!std::isfinite(label.rotation_degrees))throw std::invalid_argument("Text rotation must be finite.");if(!std::isfinite(label.at.x)||!std::isfinite(label.at.y)||(label.clip&&!valid_clip(*label.clip)))throw std::invalid_argument("UI text bounds must be finite and within the drawable range.");auto &cached=text(label);require(SDL_SetTextureColorMod(cached.texture,label.color.r,label.color.g,label.color.b),"SDL text color modulation failed");require(SDL_SetTextureAlphaMod(cached.texture,label.color.a),"SDL text alpha modulation failed");float x=label.at.x;if(label.align==TextAlign::Center)x-=static_cast<float>(cached.width)*.5f;else if(label.align==TextAlign::Right)x-=static_cast<float>(cached.width);const SDL_FRect destination{x,label.at.y,static_cast<float>(cached.width),static_cast<float>(cached.height)};
    if(label.clip){const SDL_Rect clip{static_cast<int>(std::floor(label.clip->x)),static_cast<int>(std::floor(label.clip->y)),static_cast<int>(std::ceil(label.clip->width)),static_cast<int>(std::ceil(label.clip->height))};require(SDL_SetRenderClipRect(renderer,&clip),"SDL text clip setup failed");}
    const auto rendered=label.rotation_degrees==0.f?SDL_RenderTexture(renderer,cached.texture,nullptr,&destination):SDL_RenderTextureRotated(renderer,cached.texture,nullptr,&destination,std::fmod(static_cast<double>(label.rotation_degrees),360.),nullptr,SDL_FLIP_NONE);if(label.clip)require(SDL_SetRenderClipRect(renderer,nullptr),"SDL text clip reset failed");require(rendered,"SDL cached text draw failed");
  }
  void draw_image(const Image &command){
    if(!std::isfinite(command.rotation_degrees))throw std::invalid_argument("Image rotation must be finite.");
    if(!valid_positive_rect(command.destination)||(command.clip&&!valid_clip(*command.clip)))throw std::invalid_argument("Image destination and clip bounds must be finite and within the drawable range. Destination: "+std::to_string(command.destination.x)+", "+std::to_string(command.destination.y)+", "+std::to_string(command.destination.width)+", "+std::to_string(command.destination.height));
    if(!command.resource)throw std::invalid_argument("An image command requires an RGBA resource.");std::optional<SDL_FRect> source;
    if(command.source){const auto value=*command.source;if(!valid_positive_rect(value)||value.x<0.f||value.y<0.f||value.x+value.width>static_cast<float>(command.resource->width())||value.y+value.height>static_cast<float>(command.resource->height()))throw std::invalid_argument("Image source bounds must be finite and inside the resource.");source=sdl_rect(value);}
    auto &cached=image(command.resource);
    require(SDL_SetTextureColorMod(cached.texture,command.tint.r,command.tint.g,command.tint.b),"SDL image color modulation failed");require(SDL_SetTextureAlphaMod(cached.texture,command.tint.a),"SDL image alpha modulation failed");
    if(command.clip){const SDL_Rect clip{static_cast<int>(std::floor(command.clip->x)),static_cast<int>(std::floor(command.clip->y)),static_cast<int>(std::ceil(command.clip->width)),static_cast<int>(std::ceil(command.clip->height))};require(SDL_SetRenderClipRect(renderer,&clip),"SDL image clip setup failed");}
    const auto destination=sdl_rect(command.destination);
    const auto flip=static_cast<SDL_FlipMode>(
        (command.flip_horizontal?SDL_FLIP_HORIZONTAL:0)|
        (command.flip_vertical?SDL_FLIP_VERTICAL:0));
    const auto rendered=(command.rotation_degrees==0.f&&flip==SDL_FLIP_NONE)
        ?SDL_RenderTexture(renderer,cached.texture,source?&*source:nullptr,&destination)
        :SDL_RenderTextureRotated(renderer,cached.texture,source?&*source:nullptr,&destination,
            std::fmod(static_cast<double>(command.rotation_degrees),360.),nullptr,flip);
    if(command.clip)require(SDL_SetRenderClipRect(renderer,nullptr),"SDL image clip reset failed");require(rendered,"SDL cached image draw failed");
  }
  void draw_triangle_mesh(const TriangleMesh &mesh) {
    validate_triangle_mesh(mesh);
    if (mesh.indices.empty() || (mesh.clip &&
        (mesh.clip->width == 0.f || mesh.clip->height == 0.f))) return;
    triangle_vertices.resize(mesh.vertices.size());
    const SDL_FColor color{mesh.color.r / 255.f, mesh.color.g / 255.f,
                           mesh.color.b / 255.f, mesh.color.a / 255.f};
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i){
      auto tint=color;
      if(!mesh.vertex_colors.empty()){const auto c=mesh.vertex_colors[i];tint={c.r/255.f,c.g/255.f,c.b/255.f,c.a/255.f};}
      const auto uv=mesh.texture_coordinates.empty()?Point{}:mesh.texture_coordinates[i];
      triangle_vertices[i] = {{mesh.vertices[i].x, mesh.vertices[i].y}, tint, {uv.x, uv.y}};
    }
    if (mesh.clip) {
      const auto left = static_cast<int>(std::floor(mesh.clip->x));
      const auto top = static_cast<int>(std::floor(mesh.clip->y));
      const SDL_Rect clip{left, top,
          static_cast<int>(std::ceil(mesh.clip->x + mesh.clip->width)) - left,
          static_cast<int>(std::ceil(mesh.clip->y + mesh.clip->height)) - top};
      require(SDL_SetRenderClipRect(renderer, &clip), "SDL triangle clip setup failed");
    }
    SDL_Texture* texture=mesh.texture?image(mesh.texture).texture:nullptr;
    const auto rendered = SDL_RenderGeometry(renderer, texture,
        triangle_vertices.data(), static_cast<int>(triangle_vertices.size()),
        mesh.indices.data(), static_cast<int>(mesh.indices.size()));
    // Restore clip before propagating a submission failure to the entry point.
    if (mesh.clip)
      require(SDL_SetRenderClipRect(renderer, nullptr), "SDL triangle clip reset failed");
    require(rendered, "SDL triangle mesh draw failed");
  }
};

Window::Window(std::string title,int width,int height,bool fullscreen,std::filesystem::path font_path){
  // SDL reads the Windows application icon hints during video initialization.
  // Keep test hosts without the game resource on their existing default icon.
  if(FindResource(GetModuleHandle(nullptr),MAKEINTRESOURCE(STELLAR_APPLICATION_ICON_ID),RT_GROUP_ICON)){
    const auto icon_id=std::to_string(STELLAR_APPLICATION_ICON_ID);
    SDL_SetHintWithPriority(SDL_HINT_WINDOWS_INTRESOURCE_ICON,icon_id.c_str(),SDL_HINT_OVERRIDE);
    SDL_SetHintWithPriority(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL,icon_id.c_str(),SDL_HINT_OVERRIDE);
  }
  auto candidate=std::make_unique<Storage>();require(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD),"SDL initialization failed");candidate->initialized=true;const auto flags=SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY|(fullscreen?SDL_WINDOW_FULLSCREEN:0);candidate->window=SDL_CreateWindow(title.c_str(),width,height,flags);if(!candidate->window)throw sdl_error("SDL window creation failed");candidate->device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV,false,"vulkan");if(!candidate->device)throw sdl_error("Vulkan SDL GPU device creation failed");const char *driver=SDL_GetGPUDeviceDriver(candidate->device);if(!driver||std::string(driver)!="vulkan")throw std::runtime_error("Vulkan SDL GPU device creation returned an unexpected backend");candidate->renderer=SDL_CreateGPURenderer(candidate->device,candidate->window);if(!candidate->renderer)throw sdl_error("Vulkan SDL GPU renderer creation failed");require(SDL_SetRenderDrawBlendMode(candidate->renderer,SDL_BLENDMODE_BLEND),"SDL renderer blend setup failed");
  candidate->text_dc=CreateCompatibleDC(nullptr);if(!candidate->text_dc)throw std::runtime_error("Windows text device creation failed.");if(font_path.empty())throw std::invalid_argument("A bundled native UI font path is required.");candidate->private_font_bytes=stellar::engine::read_resource(font_path,16u*1024u*1024u);DWORD added_fonts=0;candidate->private_font_handle=AddFontMemResourceEx(candidate->private_font_bytes.data(),static_cast<DWORD>(candidate->private_font_bytes.size()),nullptr,&added_fonts);if(!candidate->private_font_handle)throw std::runtime_error("Bundled Rajdhani font could not be loaded.");
  candidate->vsync=SDL_SetRenderVSync(candidate->renderer,1);if(!candidate->vsync){const std::string reason=SDL_GetError();float refresh=60.f;const auto display=SDL_GetDisplayForWindow(candidate->window);if(display){if(const auto *mode=SDL_GetDesktopDisplayMode(display);mode&&mode->refresh_rate>1.f)refresh=mode->refresh_rate;}candidate->fallback_interval_ns=static_cast<Uint64>(1000000000./static_cast<double>(refresh));SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,"Renderer VSync unavailable (%s); pacing presents at %.2f Hz",reason.c_str(),static_cast<double>(refresh));}
  require(SDL_GetCurrentRenderOutputSize(candidate->renderer,&candidate->width,&candidate->height),"SDL drawable pixel query failed");const auto window_flags=SDL_GetWindowFlags(candidate->window);candidate->focused=(window_flags&SDL_WINDOW_INPUT_FOCUS)!=0;candidate->minimized=(window_flags&SDL_WINDOW_MINIMIZED)!=0;storage_=candidate.release();
}
Window::~Window(){delete storage_;}Window::Window(Window&&other)noexcept:storage_(std::exchange(other.storage_,nullptr)){}Window&Window::operator=(Window&&other)noexcept{if(this!=&other){delete storage_;storage_=std::exchange(other.storage_,nullptr);}return *this;}
InputSnapshot Window::poll(){
  InputSnapshot input;require(SDL_GetWindowSizeInPixels(storage_->window,&storage_->width,&storage_->height),"SDL drawable pixel query failed");const auto convert=[&](float x,float y){Point result;require(SDL_RenderCoordinatesFromWindow(storage_->renderer,x,y,&result.x,&result.y),"SDL input coordinate conversion failed");return result;};SDL_Event event;
  while(SDL_PollEvent(&event)){switch(event.type){
    case SDL_EVENT_QUIT:input.quit_requested=true;break;
    case SDL_EVENT_KEY_DOWN:if(!event.key.repeat){
      if(event.key.key==SDLK_F12&&(event.key.mod&SDL_KMOD_CTRL)&&(event.key.mod&SDL_KMOD_SHIFT)){
        input.events.push_back({InputEventType::KeyPressed,storage_->pointer,{},0.f,{},0,static_cast<std::uint32_t>(event.key.key),true,true,(event.key.mod&SDL_KMOD_ALT)!=0});
      }else if(event.key.key==SDLK_F12||event.key.key==SDLK_PRINTSCREEN){
        if(!storage_->player_screenshot)try{storage_->player_screenshot=player_screenshot_path(++storage_->screenshot_serial,storage_->screenshot_directory);storage_->screenshot_status="Saving screenshot...";storage_->screenshot_status_until_ns=SDL_GetTicksNS()+4000000000ull;}catch(const std::exception& error){storage_->screenshot_status=std::string("Screenshot failed: ")+error.what();storage_->screenshot_status_until_ns=SDL_GetTicksNS()+5000000000ull;}
      }else if(event.key.key==SDLK_ESCAPE)input.events.push_back({InputEventType::EscapePressed,storage_->pointer,{}});else if(event.key.key==SDLK_BACKSPACE)input.events.push_back({InputEventType::BackspacePressed,storage_->pointer,{}});else input.events.push_back({InputEventType::KeyPressed,storage_->pointer,{},0.f,{},0,static_cast<std::uint32_t>(event.key.key),(event.key.mod&SDL_KMOD_CTRL)!=0,(event.key.mod&SDL_KMOD_SHIFT)!=0,(event.key.mod&SDL_KMOD_ALT)!=0});
    }break;
    case SDL_EVENT_KEY_UP:if(event.key.key!=SDLK_ESCAPE&&event.key.key!=SDLK_BACKSPACE)input.events.push_back({InputEventType::KeyReleased,storage_->pointer,{},0.f,{},0,static_cast<std::uint32_t>(event.key.key)});break;
    case SDL_EVENT_TEXT_INPUT:if(storage_->text_input_requested&&event.text.text)input.events.push_back({InputEventType::TextEntered,storage_->pointer,{},0.f,event.text.text});break;
    case SDL_EVENT_MOUSE_MOTION:{const auto prior=storage_->pointer;storage_->pointer=convert(event.motion.x,event.motion.y);input.events.push_back({InputEventType::PointerMove,storage_->pointer,{storage_->pointer.x-prior.x,storage_->pointer.y-prior.y}});break;}
    case SDL_EVENT_MOUSE_BUTTON_DOWN:storage_->pointer=convert(event.button.x,event.button.y);if(event.button.button==SDL_BUTTON_LEFT){storage_->left_down=true;input.events.push_back({InputEventType::LeftPressed,storage_->pointer,{},0.f,{},static_cast<std::uint8_t>(event.button.clicks)});}else if(event.button.button==SDL_BUTTON_RIGHT)input.events.push_back({InputEventType::RightPressed,storage_->pointer,{},0.f,{},static_cast<std::uint8_t>(event.button.clicks)});break;
    case SDL_EVENT_MOUSE_BUTTON_UP:storage_->pointer=convert(event.button.x,event.button.y);if(event.button.button==SDL_BUTTON_LEFT){storage_->left_down=false;input.events.push_back({InputEventType::LeftReleased,storage_->pointer,{},0.f,{},static_cast<std::uint8_t>(event.button.clicks)});}else if(event.button.button==SDL_BUTTON_RIGHT)input.events.push_back({InputEventType::RightReleased,storage_->pointer,{},0.f,{},static_cast<std::uint8_t>(event.button.clicks)});break;
    case SDL_EVENT_MOUSE_WHEEL:{storage_->pointer=convert(event.wheel.mouse_x,event.wheel.mouse_y);const auto wheel=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-event.wheel.y:event.wheel.y;input.events.push_back({InputEventType::Wheel,storage_->pointer,{},wheel});break;}
    case SDL_EVENT_GAMEPAD_ADDED:
      // First attached pad wins; extra devices are ignored for now.
      if(!storage_->gamepad){storage_->gamepad=SDL_OpenGamepad(event.gdevice.which);if(storage_->gamepad)storage_->gamepad_id=SDL_GetGamepadID(storage_->gamepad);}break;
    case SDL_EVENT_GAMEPAD_REMOVED:
      if(storage_->gamepad&&event.gdevice.which==storage_->gamepad_id){SDL_CloseGamepad(storage_->gamepad);storage_->gamepad=nullptr;}break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
      if(storage_->gamepad&&event.gbutton.which==storage_->gamepad_id){InputEvent pad{};pad.type=event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN?InputEventType::GamepadPressed:InputEventType::GamepadReleased;pad.gamepad_button=event.gbutton.button;input.events.push_back(pad);}break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
      if(storage_->gamepad&&event.gaxis.which==storage_->gamepad_id){InputEvent pad{};pad.type=InputEventType::GamepadAxis;pad.gamepad_axis=event.gaxis.axis;pad.gamepad_axis_value=std::clamp(static_cast<float>(event.gaxis.value)/32767.f,-1.f,1.f);input.events.push_back(pad);}break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
      // A captured drag may cross the window edge. Ordinary hover must stop
      // immediately rather than leaving its last control highlighted.
      if(!storage_->left_down){storage_->pointer={-1.f,-1.f};input.events.push_back({InputEventType::PointerCancelled,storage_->pointer,{}});}break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:storage_->focused=false;storage_->left_down=false;storage_->pointer={-1.f,-1.f};if(storage_->text_input_active){require(SDL_StopTextInput(storage_->window),"SDL text input stop on focus loss failed");storage_->text_input_active=false;}input.events.push_back({InputEventType::PointerCancelled,storage_->pointer,{}});break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:storage_->focused=true;if(storage_->text_input_requested&&!storage_->text_input_active){require(SDL_StartTextInput(storage_->window),"SDL text input restart failed");storage_->text_input_active=true;}break;
    case SDL_EVENT_WINDOW_MINIMIZED:storage_->minimized=true;storage_->left_down=false;storage_->pointer={-1.f,-1.f};input.events.push_back({InputEventType::PointerCancelled,storage_->pointer,{}});break;
    case SDL_EVENT_WINDOW_RESTORED:storage_->minimized=false;break;
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:if(storage_->auto_frame_cap){const auto refresh=display_refresh_hz();storage_->frame_cap_interval_ns=static_cast<Uint64>(1000000000./refresh);storage_->last_present_ns=0;}break;
    default:break;}}
  input.drawable_width=storage_->width;input.drawable_height=storage_->height;input.pointer=storage_->pointer;input.focused=storage_->focused;input.minimized=storage_->minimized;return input;
}
void Window::set_text_input(bool enabled){storage_->text_input_requested=enabled;if(!storage_->focused)return;if(enabled==storage_->text_input_active)return;require(enabled?SDL_StartTextInput(storage_->window):SDL_StopTextInput(storage_->window),enabled?"SDL text input start failed":"SDL text input stop failed");storage_->text_input_active=enabled;}
bool Window::request_screenshot(std::filesystem::path path){
  if(storage_->player_screenshot||path.empty())return false;
  storage_->player_screenshot=std::move(path);storage_->screenshot_status="Saving screenshot...";storage_->screenshot_status_until_ns=SDL_GetTicksNS()+4000000000ull;return true;
}
void Window::set_screenshot_directory(std::filesystem::path path){
  if((!path.empty()&&!path.is_absolute())||path.native().find(L'\0')!=std::wstring::npos)throw std::invalid_argument("Screenshot directory must be absolute and contain no NUL.");
  storage_->screenshot_directory=std::move(path);
}
bool Window::request_folder_dialog(std::uint64_t request_id,std::filesystem::path initial_directory){
  auto request=std::make_unique<FolderDialogRequest>(FolderDialogRequest{
      storage_->folder_dialog,request_id,utf8_path(initial_directory)});
  {
    std::lock_guard lock(storage_->folder_dialog->mutex);
    if(storage_->folder_dialog->pending||storage_->folder_dialog->result)return false;
    storage_->folder_dialog->pending=true;
  }
  auto* payload=request.release();
  SDL_ShowOpenFolderDialog(folder_dialog_complete,payload,storage_->window,
      payload->initial_directory.empty()?nullptr:payload->initial_directory.c_str(),false);
  return true;
}
std::optional<FolderDialogResult> Window::take_folder_dialog_result(){
  std::lock_guard lock(storage_->folder_dialog->mutex);
  return std::exchange(storage_->folder_dialog->result,std::nullopt);
}
std::optional<std::string> Window::take_screenshot_status(){return std::exchange(storage_->screenshot_status,std::nullopt);}
std::vector<DisplayMode> Window::display_modes()const{
  int count{};const auto display=SDL_GetDisplayForWindow(storage_->window);
  auto **modes=SDL_GetFullscreenDisplayModes(display,&count);
  if(!modes)throw sdl_error("Display mode enumeration failed");
  const std::unique_ptr<SDL_DisplayMode*,decltype(&SDL_free)> owner(modes,SDL_free);
  std::vector<DisplayMode> result;
  for(int i=0;i<count&&result.size()<256;++i){
    const auto &mode=*modes[i];
    if(mode.w<640||mode.h<360||!std::isfinite(mode.refresh_rate)||mode.refresh_rate<1.f)continue;
    const DisplayMode value{mode.w,mode.h,mode.refresh_rate};
    if(std::ranges::none_of(result,[&](const auto &item){return item.width==value.width&&item.height==value.height&&std::abs(item.refresh_hz-value.refresh_hz)<.01f;}))result.push_back(value);
  }
  std::ranges::sort(result,[](const auto&a,const auto&b){return std::tie(a.width,a.height,a.refresh_hz)>std::tie(b.width,b.height,b.refresh_hz);});
  return result;
}
std::vector<DisplayMode> Window::windowed_display_modes()const{
  const auto display=SDL_GetDisplayForWindow(storage_->window);if(!display)throw sdl_error("Current display query failed");
  SDL_Rect usable{};require(SDL_GetDisplayUsableBounds(display,&usable),"Usable display bounds query failed");
  const auto decoration=window_decoration(storage_->window,display);
  auto modes=display_modes();
  modes.erase(std::remove_if(modes.begin(),modes.end(),[&](const auto& mode){return mode.width>usable.w-decoration.left-decoration.right||mode.height>usable.h-decoration.top-decoration.bottom;}),modes.end());
  return modes;
}
DisplayMode Window::desktop_display_mode()const{
  const auto display=SDL_GetDisplayForWindow(storage_->window);if(!display)throw sdl_error("Current display query failed");
  const auto *mode=SDL_GetDesktopDisplayMode(display);if(!mode||mode->w<=0||mode->h<=0||!std::isfinite(mode->refresh_rate)||mode->refresh_rate<1.f)throw sdl_error("Desktop display mode query failed");
  return {mode->w,mode->h,mode->refresh_rate};
}
float Window::display_refresh_hz()const{
  const auto *mode=SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(storage_->window));
  return mode&&std::isfinite(mode->refresh_rate)&&mode->refresh_rate>1.f?mode->refresh_rate:60.f;
}
void *Window::native_window_handle()const noexcept{
#ifdef _WIN32
  return SDL_GetPointerProperty(SDL_GetWindowProperties(storage_->window),
                                SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr);
#else
  return nullptr;
#endif
}
void Window::set_display_mode(WindowDisplayMode requested,int width,int height,float refresh_hz){
  if(width<0||height<0||!std::isfinite(refresh_hz)||refresh_hz<0.f)
    throw std::invalid_argument("Invalid display resolution or refresh rate.");
  int count{};const auto display=SDL_GetDisplayForWindow(storage_->window);
  if (!display) throw sdl_error("Current display query failed");
  if (requested == WindowDisplayMode::Windowed) {
    require(SDL_SetWindowFullscreen(storage_->window,false),"Windowed mode was rejected");
    require(SDL_SyncWindow(storage_->window),"Windowed mode change did not complete");
    // Fullscreen may have inherited a maximized restore state from Windows.
    // Explicitly restore the decorated client before measuring or resizing it.
    require(SDL_SetWindowFullscreenMode(storage_->window,nullptr),"Exclusive display selection could not be cleared");
    require(SDL_RestoreWindow(storage_->window),"Window could not be restored");
    require(SDL_SetWindowBordered(storage_->window,true),"Window borders could not be restored");
    require(SDL_SetWindowResizable(storage_->window,true),"Window resizing could not be restored");
    require(SDL_SyncWindow(storage_->window),"Window restoration did not complete");
    const auto windowed_display=SDL_GetDisplayForWindow(storage_->window);
    if(!windowed_display)throw sdl_error("Windowed display query failed");
    SDL_Rect usable{};
    require(SDL_GetDisplayUsableBounds(windowed_display,&usable),"Usable display bounds query failed");
    const auto decoration=window_decoration(storage_->window,windowed_display);
    const int limit_width=std::max(320,usable.w-decoration.left-decoration.right);
    const int limit_height=std::max(240,usable.h-decoration.top-decoration.bottom);
    const bool explicit_size=width!=0||height!=0;
    if(width==0&&height==0&&storage_->has_windowed_bounds){
      width=storage_->windowed_width;height=storage_->windowed_height;
    }
    if(width==0&&height==0){width=std::min(1280,limit_width);height=std::min(720,limit_height);}
    if(explicit_size&&(width<320||height<240||width>limit_width||height>limit_height))
      throw std::invalid_argument("The selected window size does not fit the current display's usable area.");
    width=std::clamp(width,320,limit_width);height=std::clamp(height,240,limit_height);
    require(SDL_SetWindowSize(storage_->window,width,height),"Windowed size was rejected");
    const int outer_width=width+decoration.left+decoration.right,outer_height=height+decoration.top+decoration.bottom;
    int x=usable.x+(usable.w-outer_width)/2,y=usable.y+(usable.h-outer_height)/2;
    if(storage_->has_windowed_bounds){
      x=std::clamp(storage_->windowed_x,usable.x,usable.x+std::max(0,usable.w-outer_width));
      y=std::clamp(storage_->windowed_y,usable.y,usable.y+std::max(0,usable.h-outer_height));
    }
    require(SDL_SetWindowPosition(storage_->window,x,y),"Windowed position was rejected");
    require(SDL_SyncWindow(storage_->window),"Windowed size change did not complete");
    if(SDL_GetWindowFlags(storage_->window)&SDL_WINDOW_FULLSCREEN)
      throw std::runtime_error("The window manager did not leave fullscreen.");
    require(SDL_GetCurrentRenderOutputSize(storage_->renderer,&storage_->width,&storage_->height),"Changed display dimensions could not be read");
    require(SDL_GetWindowPosition(storage_->window,&storage_->windowed_x,&storage_->windowed_y),"Windowed position query failed");
    require(SDL_GetWindowSize(storage_->window,&storage_->windowed_width,&storage_->windowed_height),"Windowed size query failed");
    storage_->has_windowed_bounds=true;
    storage_->last_present_ns=0;
    return;
  }
  if(!(SDL_GetWindowFlags(storage_->window)&SDL_WINDOW_FULLSCREEN)){
    (void)SDL_GetWindowPosition(storage_->window,&storage_->windowed_x,&storage_->windowed_y);
    (void)SDL_GetWindowSize(storage_->window,&storage_->windowed_width,&storage_->windowed_height);
    storage_->has_windowed_bounds=true;
  }
  auto **modes=SDL_GetFullscreenDisplayModes(display,&count);
  if(!modes)throw sdl_error("Display mode enumeration failed");
  const std::unique_ptr<SDL_DisplayMode*,decltype(&SDL_free)> owner(modes,SDL_free);
  const SDL_DisplayMode *selected=nullptr;
  if(requested==WindowDisplayMode::ExclusiveFullscreen){
    const auto *desktop=SDL_GetDesktopDisplayMode(display);
    if(!desktop)throw sdl_error("Desktop display query failed");
    if(width==0&&height==0){width=desktop->w;height=desktop->h;refresh_hz=desktop->refresh_rate;}
    for(int i=0;i<count;++i){const auto *mode=modes[i];
      if(mode->w==width&&mode->h==height&&(refresh_hz==0.f||std::abs(mode->refresh_rate-refresh_hz)<.02f)&&
         (!selected||mode->refresh_rate>selected->refresh_rate))selected=mode;
    }
    if(!selected)throw std::invalid_argument("The selected resolution is no longer available on this display.");
  }
  require(SDL_SetWindowFullscreenMode(storage_->window,selected),"Fullscreen mode was rejected");
  require(SDL_SetWindowFullscreen(storage_->window,true),"Fullscreen was rejected");
  require(SDL_SyncWindow(storage_->window),"Fullscreen change did not complete");
  if(!(SDL_GetWindowFlags(storage_->window)&SDL_WINDOW_FULLSCREEN))
    throw std::runtime_error("The window manager did not enter fullscreen.");
  const auto *actual=SDL_GetWindowFullscreenMode(storage_->window);
  const bool exclusive=requested==WindowDisplayMode::ExclusiveFullscreen;
  if(exclusive?(!actual||actual->w!=width||actual->h!=height||std::abs(actual->refresh_rate-selected->refresh_rate)>.02f):actual!=nullptr)
    throw std::runtime_error("The window manager did not apply the selected display mode.");
  if(exclusive){
    const auto *current=SDL_GetCurrentDisplayMode(display);
    if(!current||current->w!=width||current->h!=height||std::abs(current->refresh_rate-selected->refresh_rate)>.1f)
      throw std::runtime_error("The display did not switch to the selected resolution and refresh rate.");
  }
  require(SDL_GetCurrentRenderOutputSize(storage_->renderer,&storage_->width,&storage_->height),"Changed display dimensions could not be read");
  storage_->last_present_ns=0;
}
void Window::set_fullscreen_mode(bool exclusive,int width,int height,float refresh_hz){
  set_display_mode(exclusive?WindowDisplayMode::ExclusiveFullscreen:WindowDisplayMode::Borderless,
                   width,height,refresh_hz);
}
void Window::set_vsync(int mode){
  if(mode!=0&&mode!=1&&mode!=-1)throw std::invalid_argument("Unsupported VSync mode.");
  require(SDL_SetRenderVSync(storage_->renderer,mode),"Requested VSync mode is unavailable");
  int actual{};require(SDL_GetRenderVSync(storage_->renderer,&actual),"VSync state query failed");
  if(actual!=mode)throw std::runtime_error("The renderer did not apply the requested VSync mode.");
  storage_->vsync=actual!=0;storage_->fallback_interval_ns=0;storage_->last_present_ns=0;
}
void Window::set_frame_cap(double hz){
  if(!std::isfinite(hz)||hz<0.||hz>1000.)throw std::invalid_argument("Invalid frame cap.");
  storage_->auto_frame_cap=false;storage_->frame_cap_interval_ns=hz>=1.?static_cast<Uint64>(1000000000./hz):0;
  storage_->last_present_ns=0;
}
void Window::set_auto_frame_cap(){
  const auto hz=display_refresh_hz();storage_->auto_frame_cap=true;
  storage_->frame_cap_interval_ns=static_cast<Uint64>(1000000000./hz);storage_->last_present_ns=0;
}
void Window::set_scene_quality(int percent,int samples){
  if((percent!=50&&percent!=75&&percent!=100)||(samples!=1&&samples!=2&&samples!=4))throw std::invalid_argument("Unsupported scene quality.");
  storage_->prepare_scene_target(percent,samples);storage_->scene_percent=percent;storage_->scene_samples=samples;
}
std::string Window::graphics_adapter()const {
  const auto props=SDL_GetGPUDeviceProperties(storage_->device);
  return std::string(SDL_GetStringProperty(props,SDL_PROP_GPU_DEVICE_NAME_STRING,"Graphics device"))+" · "+gpu_driver();
}
namespace {
std::filesystem::path nvidia_panel_path(){
  PWSTR folder{};if(FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFiles,0,nullptr,&folder)))return {};
  const auto path=std::filesystem::path(folder)/"NVIDIA Corporation/Control Panel Client/nvcplui.exe";CoTaskMemFree(folder);
  std::error_code ec;return std::filesystem::is_regular_file(path,ec)?path:std::filesystem::path{};
}
bool nvidia_store_panel_available(){
  UINT32 count{},length{};
  const auto result=GetPackagesByPackageFamily(L"NVIDIACorp.NVIDIAControlPanel_56jybvy8sckqj",&count,nullptr,&length,nullptr);
  return result==ERROR_INSUFFICIENT_BUFFER&&count>0;
}
}
bool Window::has_nvidia_control_panel()const{return !nvidia_panel_path().empty()||nvidia_store_panel_available();}
void Window::open_nvidia_control_panel(){
  const auto path=nvidia_panel_path();
  if(path.empty()){
    if(!nvidia_store_panel_available())throw std::runtime_error("NVIDIA Control Panel was not found.");
    if(reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",L"shell:AppsFolder\\NVIDIACorp.NVIDIAControlPanel_56jybvy8sckqj!NVIDIACorp.NVIDIAControlPanel",nullptr,nullptr,SW_SHOWNORMAL))<=32)
      throw std::runtime_error("Windows could not open NVIDIA Control Panel.");
    return;
  }
  if(reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",path.c_str(),nullptr,path.parent_path().c_str(),SW_SHOWNORMAL))<=32)
    throw std::runtime_error("Windows could not open NVIDIA Control Panel.");
}
void Window::set_clipboard_text(const std::string& text){if(text.size()>16384)throw std::invalid_argument("Clipboard text exceeds limit.");require(SDL_SetClipboardText(text.c_str()),"Clipboard copy failed");}
TextExtent Window::measure_text(const Text &label){if(label.value.empty())return {};const auto &cached=storage_->text(label);return {cached.width,cached.height};}
void Window::draw(const DrawList &draw_list,const std::optional<std::filesystem::path>&screenshot,FrameTiming *timing){
  if(timing)*timing={};
  const auto elapsed_ms=[](const auto started){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();};
  const auto submission_started=timing?std::optional{std::chrono::steady_clock::now()}:std::nullopt;
  const auto has_3d=[](const auto& commands){return std::any_of(commands.begin(),commands.end(),[](const auto& c){return std::holds_alternative<Scene3DView>(c);});};
  if(!storage_->scene3d&&(has_3d(draw_list.world)||has_3d(draw_list.overlay)))storage_->scene3d=std::make_unique<Scene3DRenderer>(storage_->device,storage_->renderer);
  if(storage_->scene3d){
    storage_->scene3d->prepare(draw_list);
    // VRAM attribution: report the backend's resident texture, mesh and
    // render-target bytes into MemoryTracker subsystems once per draw so
    // the memory overlay attributes GPU residency to the renderer.
    auto& tracker=engine::MemoryTracker::instance();
    if(storage_->gpu_texture_subsystem==engine::MemoryTracker::invalid_subsystem){
      storage_->gpu_texture_subsystem=tracker.register_subsystem("scene3d-textures");
      storage_->gpu_mesh_subsystem=tracker.register_subsystem("scene3d-meshes");
      storage_->gpu_target_subsystem=tracker.register_subsystem("scene3d-targets");
    }
    const auto gpu_stats=storage_->scene3d->statistics();
    tracker.report(storage_->gpu_texture_subsystem,gpu_stats.texture_cache_bytes,maximum_scene3d_texture_cache_bytes);
    tracker.report(storage_->gpu_mesh_subsystem,gpu_stats.mesh_cache_bytes,maximum_mesh3d_cache_bytes);
    tracker.report(storage_->gpu_target_subsystem,gpu_stats.target_bytes,0);
  }
  { // The 2D caches are equally bounded ledgers; attribute them every draw.
    auto& tracker=engine::MemoryTracker::instance();
    if(storage_->image_cache_subsystem==engine::MemoryTracker::invalid_subsystem){
      storage_->image_cache_subsystem=tracker.register_subsystem("ui-image-cache");
      storage_->text_cache_subsystem=tracker.register_subsystem("ui-text-cache");
    }
    tracker.report(storage_->image_cache_subsystem,storage_->image_cache_resident_bytes,0);
    tracker.report(storage_->text_cache_subsystem,storage_->text_cache_bytes,Storage::text_cache_byte_capacity);
  }
  const auto draw_line=[&](const Line &line){
    if(!valid_point(line.from)||!valid_point(line.to))throw std::invalid_argument("Line coordinates must be finite.");
    const float dx=line.to.x-line.from.x,dy=line.to.y-line.from.y,length=std::hypot(dx,dy);
    // Rasterize the geometric line at the scene target's resolution. SDL_RenderLine
    // rasterizes logical pixels before scaling and otherwise defeats supersampling.
    if(SDL_GetRenderTarget(storage_->renderer)&&length>.001f){
      const float nx=-dy/length*.5f,ny=dx/length*.5f;
      const SDL_FColor color{line.color.r/255.f,line.color.g/255.f,line.color.b/255.f,line.color.a/255.f};
      const SDL_Vertex vertices[]{{{line.from.x+nx,line.from.y+ny},color,{}},{{line.to.x+nx,line.to.y+ny},color,{}},
                                  {{line.to.x-nx,line.to.y-ny},color,{}},{{line.from.x-nx,line.from.y-ny},color,{}}};
      constexpr int indices[]{0,1,2,0,2,3};
      require(SDL_RenderGeometry(storage_->renderer,nullptr,vertices,4,indices,6),"SDL scene line failed");
    }else{
      require(SDL_SetRenderDrawColor(storage_->renderer,line.color.r,line.color.g,line.color.b,line.color.a),"SDL line color failed");
      require(SDL_RenderLine(storage_->renderer,line.from.x,line.from.y,line.to.x,line.to.y),"SDL line draw failed");
    }
  };
  const auto draw_circle=[&](const Circle &circle){if(!valid_point(circle.center)||!std::isfinite(circle.radius)||circle.radius<0.f)throw std::invalid_argument("Circle bounds must be finite and nonnegative.");std::array<SDL_Vertex,soft_circle_segments+2> vertices{};const SDL_FColor center_color{circle.color.r/255.f,circle.color.g/255.f,circle.color.b/255.f,circle.color.a/255.f};const SDL_FColor edge_color{center_color.r,center_color.g,center_color.b,0.f};vertices[0]={{circle.center.x,circle.center.y},center_color,{0,0}};const auto &directions=soft_circle_directions();for(int index=0;index<=soft_circle_segments;++index){const auto direction=directions[static_cast<std::size_t>(index)];const Point edge{circle.center.x+direction.x*circle.radius,circle.center.y+direction.y*circle.radius};if(!valid_point(edge))throw std::invalid_argument("Circle projection produced non-finite geometry.");vertices[static_cast<std::size_t>(index)+1u]={{edge.x,edge.y},edge_color,{0,0}};}const auto &indices=soft_circle_indices();require(SDL_RenderGeometry(storage_->renderer,nullptr,vertices.data(),static_cast<int>(vertices.size()),indices.data(),static_cast<int>(indices.size())),"SDL soft circle draw failed");};
  // World quality is independent of the native-resolution interface and hit testing.
  struct RestoreTarget {SDL_Renderer* renderer;~RestoreTarget(){SDL_SetRenderTarget(renderer,nullptr);SDL_SetRenderScale(renderer,1.f,1.f);SDL_SetRenderClipRect(renderer,nullptr);}} restore_target{storage_->renderer};
  try {storage_->prepare_scene_target(storage_->scene_percent,storage_->scene_samples);}
  catch(const std::exception& error){
    storage_->prepare_scene_target(100,1);storage_->scene_percent=100;storage_->scene_samples=1;
    storage_->screenshot_status=std::string("Scene quality returned to native after resizing: ")+error.what();storage_->screenshot_status_until_ns=SDL_GetTicksNS()+8000000000ull;
  }
  if(storage_->scene_target){require(SDL_SetRenderTarget(storage_->renderer,storage_->scene_target),"Scene target failed");require(SDL_SetRenderScale(storage_->renderer,static_cast<float>(storage_->scene_width)/storage_->width,static_cast<float>(storage_->scene_height)/storage_->height),"Scene scaling failed");}
  require(SDL_SetRenderDrawColor(storage_->renderer,5,9,19,255),"SDL clear color failed");require(SDL_RenderClear(storage_->renderer),"SDL render clear failed");for(const auto &line:draw_list.lines)draw_line(line);for(const auto &circle:draw_list.circles)draw_circle(circle);for(const auto &label:draw_list.text)storage_->draw_text(label);
  for(const auto &command:draw_list.world){std::visit([&](const auto &value){using Value=std::decay_t<decltype(value)>;if constexpr(std::is_same_v<Value,Line>)draw_line(value);else if constexpr(std::is_same_v<Value,Circle>)draw_circle(value);else if constexpr(std::is_same_v<Value,Text>)storage_->draw_text(value);else if constexpr(std::is_same_v<Value,Image>)storage_->draw_image(value);else if constexpr(std::is_same_v<Value,Scene3DView>)storage_->scene3d->composite(value);else storage_->draw_triangle_mesh(value);},command);}
  if(storage_->scene_target){
    require(SDL_SetRenderTarget(storage_->renderer,nullptr),"Scene resolve target failed");
    require(SDL_SetRenderScale(storage_->renderer,1.f,1.f),"Native UI scale reset failed");
    require(SDL_SetRenderClipRect(storage_->renderer,nullptr),"Scene clip reset failed");
    const SDL_FRect full{0,0,static_cast<float>(storage_->width),static_cast<float>(storage_->height)};
    require(SDL_RenderTexture(storage_->renderer,storage_->scene_target,nullptr,&full),"Scene quality resolve failed");
  }
  for(const auto &command:draw_list.overlay){std::visit([&](const auto &value){using Value=std::decay_t<decltype(value)>;if constexpr(std::is_same_v<Value,FilledRectangle>){if(!valid_clip(value.bounds))throw std::invalid_argument("Panel fill bounds must be finite.");const auto bounds=sdl_rect(value.bounds);require(SDL_SetRenderDrawColor(storage_->renderer,value.color.r,value.color.g,value.color.b,value.color.a),"SDL panel fill color failed");require(SDL_RenderFillRect(storage_->renderer,&bounds),"SDL panel fill failed");}else if constexpr(std::is_same_v<Value,StrokedRectangle>){if(!valid_clip(value.bounds))throw std::invalid_argument("Panel stroke bounds must be finite.");const auto bounds=sdl_rect(value.bounds);require(SDL_SetRenderDrawColor(storage_->renderer,value.color.r,value.color.g,value.color.b,value.color.a),"SDL panel stroke color failed");require(SDL_RenderRect(storage_->renderer,&bounds),"SDL panel stroke failed");}else if constexpr(std::is_same_v<Value,Line>)draw_line(value);else if constexpr(std::is_same_v<Value,Image>)storage_->draw_image(value);else if constexpr(std::is_same_v<Value,TriangleMesh>)storage_->draw_triangle_mesh(value);else if constexpr(std::is_same_v<Value,Scene3DView>)storage_->scene3d->composite(value);else storage_->draw_text(value);},command);}
  if(timing)timing->submission_ms=elapsed_ms(*submission_started);
  const bool player_capture=!screenshot&&storage_->player_screenshot.has_value();
  const auto capture=screenshot?screenshot:storage_->player_screenshot;
  if(capture){
    int captured_width{},captured_height{};
    const auto save_capture=[&](const std::filesystem::path& output,bool png){
      const auto readback_started=timing?std::optional{std::chrono::steady_clock::now()}:std::nullopt;
      SDL_Surface *surface=SDL_RenderReadPixels(storage_->renderer,nullptr);if(!surface)throw sdl_error("SDL screenshot readback failed");
      const std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> owner(surface,SDL_DestroySurface);
      const auto path=utf8_path(output);
      require(png?SDL_SavePNG(surface,path.c_str()):SDL_SaveBMP(surface,path.c_str()),png?"SDL PNG screenshot write failed":"SDL screenshot write failed");
      captured_width=surface->w;captured_height=surface->h;
      if(timing)timing->readback_ms=elapsed_ms(*readback_started);
    };
    storage_->player_screenshot.reset();
    const bool png=capture->extension()==L".png"||capture->extension()==L".PNG";
    if(player_capture){std::filesystem::path temporary;try{
      temporary=*capture;temporary+=L"."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(storage_->screenshot_serial)+L".partial";
      std::error_code ignored;std::filesystem::remove(temporary,ignored);
      save_capture(temporary,png);
      if(!MoveFileExW(temporary.c_str(),capture->c_str(),MOVEFILE_WRITE_THROUGH)){
        std::filesystem::remove(temporary,ignored);
        throw std::runtime_error("Screenshot filename already exists or could not be published.");
      }
      report_capture(*capture,captured_width,captured_height);
      storage_->screenshot_status="Screenshot saved: "+utf8_path(capture->filename());storage_->screenshot_status_until_ns=SDL_GetTicksNS()+5000000000ull;
    }catch(const std::exception& error){std::error_code ignored;if(!temporary.empty())std::filesystem::remove(temporary,ignored);storage_->screenshot_status=std::string("Screenshot failed: ")+error.what();storage_->screenshot_status_until_ns=SDL_GetTicksNS()+5000000000ull;}}
    else {save_capture(*capture,png);report_capture(*capture,captured_width,captured_height);}
  }
  if(storage_->screenshot_status&&SDL_GetTicksNS()<storage_->screenshot_status_until_ns)
    storage_->draw_text(Text{{20.f,std::max(20.f,static_cast<float>(storage_->height-44))},*storage_->screenshot_status,{220,240,255,255},16,static_cast<float>(std::max(100,storage_->width-40)),std::nullopt,TextAlign::Left,FontFace::Interface});
  else if(storage_->screenshot_status)storage_->screenshot_status.reset();
  const auto pace=std::max(storage_->frame_cap_interval_ns,storage_->vsync?Uint64{0}:storage_->fallback_interval_ns);
  if(pace){const auto throttle_started=timing?std::optional{std::chrono::steady_clock::now()}:std::nullopt;const auto now=SDL_GetTicksNS();if(storage_->last_present_ns&&now-storage_->last_present_ns<pace)SDL_DelayPrecise(pace-(now-storage_->last_present_ns));storage_->last_present_ns=SDL_GetTicksNS();if(timing)timing->throttle_ms=elapsed_ms(*throttle_started);}
  const auto present_started=timing?std::optional{std::chrono::steady_clock::now()}:std::nullopt;require(SDL_RenderPresent(storage_->renderer),"SDL present failed");if(timing)timing->present_ms=elapsed_ms(*present_started);
}
int Window::drawable_width()const noexcept{return storage_->width;}int Window::drawable_height()const noexcept{return storage_->height;}std::string Window::gpu_driver()const{const char *driver=SDL_GetGPUDeviceDriver(storage_->device);if(!driver)throw sdl_error("SDL GPU driver query failed");return driver;}std::string Window::presentation_mode()const{return storage_->vsync?"vsync":storage_->fallback_interval_ns?"display-refresh-fallback":storage_->frame_cap_interval_ns?"frame-cap":"unbounded";}
std::size_t Window::text_cache_entries()const noexcept{return storage_->text_cache.size();}std::size_t Window::text_cache_bytes()const noexcept{return storage_->text_cache_bytes;}
std::size_t Window::image_cache_entries()const noexcept{return storage_->image_cache.size();}std::size_t Window::image_cache_resident_bytes()const noexcept{return storage_->image_cache_resident_bytes;}std::uint64_t Window::image_upload_count()const noexcept{return storage_->image_uploads;}
Scene3DStatistics Window::scene3d_statistics()const noexcept{return storage_->scene3d?storage_->scene3d->statistics():Scene3DStatistics{};}
void Window::set_scene3d_texture_budget(std::uint64_t bytes){if(storage_->scene3d)storage_->scene3d->set_texture_budget(bytes);}
} // namespace stellar::native_map

