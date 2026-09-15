#include <stellar/engine/native_map_platform.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_render.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#error The native UI preview requires the Windows GDI text rasterizer.
#endif

namespace stellar::native_map {
namespace {
[[nodiscard]] std::runtime_error sdl_error(const char *operation){return std::runtime_error(std::string(operation)+": "+SDL_GetError());}
void require(bool success,const char *operation){if(!success)throw sdl_error(operation);}
[[nodiscard]] std::string utf8_path(const std::filesystem::path &path){const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
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
  static constexpr std::size_t text_cache_capacity=640,text_cache_byte_capacity=32u*1024u*1024u;
  struct CachedImage {std::shared_ptr<const RgbaImage> owner;SDL_Texture *texture{};std::size_t resident_bytes{};std::uint64_t last_use{};};
  SDL_Window *window{};SDL_GPUDevice *device{};SDL_Renderer *renderer{};HDC text_dc{};
  std::filesystem::path private_font_path;bool private_font_added{};
  std::unordered_map<int,HFONT> fonts;std::unordered_map<TextKey,CachedText,TextKeyHash> text_cache;std::size_t text_cache_bytes{};std::uint64_t text_use{};
  std::unordered_map<const RgbaImage*,CachedImage> image_cache;std::size_t image_cache_resident_bytes{};std::uint64_t image_use{},image_uploads{};
  int width{},height{};bool initialized{},left_down{},focused{true},minimized{},vsync{},text_input_requested{},text_input_active{};Point pointer{};Uint64 fallback_interval_ns{},last_present_ns{};
  ~Storage(){
    for(auto &[key,cached]:image_cache){(void)key;if(cached.texture)SDL_DestroyTexture(cached.texture);}
    for(auto &[key,cached]:text_cache){(void)key;if(cached.texture)SDL_DestroyTexture(cached.texture);}
    for(const auto &[size,font_value]:fonts){(void)size;if(font_value)DeleteObject(font_value);}
    if(text_dc)DeleteDC(text_dc);if(private_font_added)RemoveFontResourceExW(private_font_path.c_str(),FR_PRIVATE,nullptr);
    if(renderer)SDL_DestroyRenderer(renderer);if(device)SDL_DestroyGPUDevice(device);if(window)SDL_DestroyWindow(window);if(initialized)SDL_Quit();
  }
  [[nodiscard]] HFONT font(int pixel_size,FontFace role){
    pixel_size=std::clamp(pixel_size,8,72);const auto font_key=pixel_size*2+(role==FontFace::Heading?1:0);if(const auto found=fonts.find(font_key);found!=fonts.end())return found->second;
    const auto face=role==FontFace::Heading?L"Rajdhani SemiBold":L"Segoe UI";
    auto created=CreateFontW(-pixel_size,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_TT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH|FF_DONTCARE,face);
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
    const auto bytes=resource->byte_size();if(bytes>maximum_image_cache_resident_bytes-bytes)throw std::length_error("An image exceeds the texture-cache resident limit.");const auto resident=bytes*2u;evict_images(resident);
    auto *texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,resource->width(),resource->height());if(!texture)throw sdl_error("SDL image texture creation failed");
    try{require(SDL_SetTextureBlendMode(texture,SDL_BLENDMODE_BLEND),"SDL image blend setup failed");require(SDL_SetTextureScaleMode(texture,SDL_SCALEMODE_LINEAR),"SDL image scale setup failed");require(SDL_UpdateTexture(texture,nullptr,resource->pixels().data(),resource->width()*4),"SDL image texture upload failed");}catch(...){SDL_DestroyTexture(texture);throw;}
    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> owner(texture,SDL_DestroyTexture);auto [inserted,ok]=image_cache.emplace(resource.get(),CachedImage{resource,texture,resident,++image_use});if(!ok)throw std::logic_error("Duplicate image cache key.");(void)owner.release();image_cache_resident_bytes+=resident;++image_uploads;return inserted->second;
  }
  void draw_text(const Text &label){
    if(label.value.empty())return;if(!std::isfinite(label.at.x)||!std::isfinite(label.at.y)||(label.clip&&!valid_clip(*label.clip)))throw std::invalid_argument("UI text bounds must be finite and within the drawable range.");auto &cached=text(label);require(SDL_SetTextureColorMod(cached.texture,label.color.r,label.color.g,label.color.b),"SDL text color modulation failed");require(SDL_SetTextureAlphaMod(cached.texture,label.color.a),"SDL text alpha modulation failed");float x=label.at.x;if(label.align==TextAlign::Center)x-=static_cast<float>(cached.width)*.5f;else if(label.align==TextAlign::Right)x-=static_cast<float>(cached.width);const SDL_FRect destination{x,label.at.y,static_cast<float>(cached.width),static_cast<float>(cached.height)};
    if(label.clip){const SDL_Rect clip{static_cast<int>(std::floor(label.clip->x)),static_cast<int>(std::floor(label.clip->y)),static_cast<int>(std::ceil(label.clip->width)),static_cast<int>(std::ceil(label.clip->height))};require(SDL_SetRenderClipRect(renderer,&clip),"SDL text clip setup failed");}
    const auto rendered=SDL_RenderTexture(renderer,cached.texture,nullptr,&destination);if(label.clip)require(SDL_SetRenderClipRect(renderer,nullptr),"SDL text clip reset failed");require(rendered,"SDL cached text draw failed");
  }
  void draw_image(const Image &command){
    if(!valid_positive_rect(command.destination)||(command.clip&&!valid_clip(*command.clip)))throw std::invalid_argument("Image destination and clip bounds must be finite and within the drawable range.");
    if(!command.resource)throw std::invalid_argument("An image command requires an RGBA resource.");std::optional<SDL_FRect> source;
    if(command.source){const auto value=*command.source;if(!valid_positive_rect(value)||value.x<0.f||value.y<0.f||value.x+value.width>static_cast<float>(command.resource->width())||value.y+value.height>static_cast<float>(command.resource->height()))throw std::invalid_argument("Image source bounds must be finite and inside the resource.");source=sdl_rect(value);}
    auto &cached=image(command.resource);
    require(SDL_SetTextureColorMod(cached.texture,command.tint.r,command.tint.g,command.tint.b),"SDL image color modulation failed");require(SDL_SetTextureAlphaMod(cached.texture,command.tint.a),"SDL image alpha modulation failed");
    if(command.clip){const SDL_Rect clip{static_cast<int>(std::floor(command.clip->x)),static_cast<int>(std::floor(command.clip->y)),static_cast<int>(std::ceil(command.clip->width)),static_cast<int>(std::ceil(command.clip->height))};require(SDL_SetRenderClipRect(renderer,&clip),"SDL image clip setup failed");}
    const auto destination=sdl_rect(command.destination);const auto rendered=SDL_RenderTexture(renderer,cached.texture,source?&*source:nullptr,&destination);if(command.clip)require(SDL_SetRenderClipRect(renderer,nullptr),"SDL image clip reset failed");require(rendered,"SDL cached image draw failed");
  }
};

Window::Window(std::string title,int width,int height,bool fullscreen,std::filesystem::path font_path){
  auto candidate=std::make_unique<Storage>();require(SDL_Init(SDL_INIT_VIDEO),"SDL video initialization failed");candidate->initialized=true;const auto flags=SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY|(fullscreen?SDL_WINDOW_FULLSCREEN:0);candidate->window=SDL_CreateWindow(title.c_str(),width,height,flags);if(!candidate->window)throw sdl_error("SDL window creation failed");candidate->device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV,false,"vulkan");if(!candidate->device)throw sdl_error("Vulkan SDL GPU device creation failed");const char *driver=SDL_GetGPUDeviceDriver(candidate->device);if(!driver||std::string(driver)!="vulkan")throw std::runtime_error("Vulkan SDL GPU device creation returned an unexpected backend");candidate->renderer=SDL_CreateGPURenderer(candidate->device,candidate->window);if(!candidate->renderer)throw sdl_error("Vulkan SDL GPU renderer creation failed");require(SDL_SetRenderDrawBlendMode(candidate->renderer,SDL_BLENDMODE_BLEND),"SDL renderer blend setup failed");
  candidate->text_dc=CreateCompatibleDC(nullptr);if(!candidate->text_dc)throw std::runtime_error("Windows text device creation failed.");if(font_path.empty())throw std::invalid_argument("A bundled native UI font path is required.");candidate->private_font_path=std::filesystem::absolute(std::move(font_path));candidate->private_font_added=AddFontResourceExW(candidate->private_font_path.c_str(),FR_PRIVATE,nullptr)>0;if(!candidate->private_font_added)throw std::runtime_error("Bundled Rajdhani font could not be loaded.");
  candidate->vsync=SDL_SetRenderVSync(candidate->renderer,1);if(!candidate->vsync){const std::string reason=SDL_GetError();float refresh=60.f;const auto display=SDL_GetDisplayForWindow(candidate->window);if(display){if(const auto *mode=SDL_GetDesktopDisplayMode(display);mode&&mode->refresh_rate>1.f)refresh=mode->refresh_rate;}candidate->fallback_interval_ns=static_cast<Uint64>(1000000000./static_cast<double>(refresh));SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,"Renderer VSync unavailable (%s); pacing presents at %.2f Hz",reason.c_str(),static_cast<double>(refresh));}
  require(SDL_GetCurrentRenderOutputSize(candidate->renderer,&candidate->width,&candidate->height),"SDL drawable pixel query failed");const auto window_flags=SDL_GetWindowFlags(candidate->window);candidate->focused=(window_flags&SDL_WINDOW_INPUT_FOCUS)!=0;candidate->minimized=(window_flags&SDL_WINDOW_MINIMIZED)!=0;storage_=candidate.release();
}
Window::~Window(){delete storage_;}Window::Window(Window&&other)noexcept:storage_(std::exchange(other.storage_,nullptr)){}Window&Window::operator=(Window&&other)noexcept{if(this!=&other){delete storage_;storage_=std::exchange(other.storage_,nullptr);}return *this;}
InputSnapshot Window::poll(){
  InputSnapshot input;require(SDL_GetWindowSizeInPixels(storage_->window,&storage_->width,&storage_->height),"SDL drawable pixel query failed");const auto convert=[&](float x,float y){Point result;require(SDL_RenderCoordinatesFromWindow(storage_->renderer,x,y,&result.x,&result.y),"SDL input coordinate conversion failed");return result;};SDL_Event event;
  while(SDL_PollEvent(&event)){switch(event.type){
    case SDL_EVENT_QUIT:input.quit_requested=true;break;
    case SDL_EVENT_KEY_DOWN:if(!event.key.repeat&&event.key.key==SDLK_ESCAPE)input.events.push_back({InputEventType::EscapePressed,storage_->pointer,{}});else if(!event.key.repeat&&event.key.key==SDLK_BACKSPACE)input.events.push_back({InputEventType::BackspacePressed,storage_->pointer,{}});break;
    case SDL_EVENT_TEXT_INPUT:if(storage_->text_input_requested&&event.text.text)input.events.push_back({InputEventType::TextEntered,storage_->pointer,{},0.f,event.text.text});break;
    case SDL_EVENT_MOUSE_MOTION:{const auto prior=storage_->pointer;storage_->pointer=convert(event.motion.x,event.motion.y);input.events.push_back({InputEventType::PointerMove,storage_->pointer,{storage_->pointer.x-prior.x,storage_->pointer.y-prior.y}});break;}
    case SDL_EVENT_MOUSE_BUTTON_DOWN:storage_->pointer=convert(event.button.x,event.button.y);if(event.button.button==SDL_BUTTON_LEFT){storage_->left_down=true;input.events.push_back({InputEventType::LeftPressed,storage_->pointer,{},0.f,{},static_cast<std::uint8_t>(event.button.clicks)});}else if(event.button.button==SDL_BUTTON_RIGHT)input.events.push_back({InputEventType::RightPressed,storage_->pointer,{},0.f,{},static_cast<std::uint8_t>(event.button.clicks)});break;
    case SDL_EVENT_MOUSE_BUTTON_UP:storage_->pointer=convert(event.button.x,event.button.y);if(event.button.button==SDL_BUTTON_LEFT){storage_->left_down=false;input.events.push_back({InputEventType::LeftReleased,storage_->pointer,{},0.f,{},static_cast<std::uint8_t>(event.button.clicks)});}else if(event.button.button==SDL_BUTTON_RIGHT)input.events.push_back({InputEventType::RightReleased,storage_->pointer,{},0.f,{},static_cast<std::uint8_t>(event.button.clicks)});break;
    case SDL_EVENT_MOUSE_WHEEL:{storage_->pointer=convert(event.wheel.mouse_x,event.wheel.mouse_y);const auto wheel=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-event.wheel.y:event.wheel.y;input.events.push_back({InputEventType::Wheel,storage_->pointer,{},wheel});break;}
    case SDL_EVENT_WINDOW_FOCUS_LOST:storage_->focused=false;storage_->left_down=false;if(storage_->text_input_active){require(SDL_StopTextInput(storage_->window),"SDL text input stop on focus loss failed");storage_->text_input_active=false;}input.events.push_back({InputEventType::PointerCancelled,storage_->pointer,{}});break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:storage_->focused=true;if(storage_->text_input_requested&&!storage_->text_input_active){require(SDL_StartTextInput(storage_->window),"SDL text input restart failed");storage_->text_input_active=true;}break;
    case SDL_EVENT_WINDOW_MINIMIZED:storage_->minimized=true;storage_->left_down=false;input.events.push_back({InputEventType::PointerCancelled,storage_->pointer,{}});break;
    case SDL_EVENT_WINDOW_RESTORED:storage_->minimized=false;break;default:break;}}
  input.drawable_width=storage_->width;input.drawable_height=storage_->height;input.pointer=storage_->pointer;input.focused=storage_->focused;input.minimized=storage_->minimized;return input;
}
void Window::set_text_input(bool enabled){storage_->text_input_requested=enabled;if(!storage_->focused)return;if(enabled==storage_->text_input_active)return;require(enabled?SDL_StartTextInput(storage_->window):SDL_StopTextInput(storage_->window),enabled?"SDL text input start failed":"SDL text input stop failed");storage_->text_input_active=enabled;}
TextExtent Window::measure_text(const Text &label){if(label.value.empty())return {};const auto &cached=storage_->text(label);return {cached.width,cached.height};}
void Window::draw(const DrawList &draw_list,const std::optional<std::filesystem::path>&screenshot,FrameTiming *timing){
  if(timing)*timing={};
  const auto elapsed_ms=[](const auto started){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();};
  const auto submission_started=timing?std::optional{std::chrono::steady_clock::now()}:std::nullopt;
  const auto draw_line=[&](const Line &line){if(!valid_point(line.from)||!valid_point(line.to))throw std::invalid_argument("Line coordinates must be finite.");require(SDL_SetRenderDrawColor(storage_->renderer,line.color.r,line.color.g,line.color.b,line.color.a),"SDL line color failed");require(SDL_RenderLine(storage_->renderer,line.from.x,line.from.y,line.to.x,line.to.y),"SDL line draw failed");};
  const auto draw_circle=[&](const Circle &circle){if(!valid_point(circle.center)||!std::isfinite(circle.radius)||circle.radius<0.f)throw std::invalid_argument("Circle bounds must be finite and nonnegative.");std::array<SDL_Vertex,soft_circle_segments+2> vertices{};const SDL_FColor center_color{circle.color.r/255.f,circle.color.g/255.f,circle.color.b/255.f,circle.color.a/255.f};const SDL_FColor edge_color{center_color.r,center_color.g,center_color.b,0.f};vertices[0]={{circle.center.x,circle.center.y},center_color,{0,0}};const auto &directions=soft_circle_directions();for(int index=0;index<=soft_circle_segments;++index){const auto direction=directions[static_cast<std::size_t>(index)];const Point edge{circle.center.x+direction.x*circle.radius,circle.center.y+direction.y*circle.radius};if(!valid_point(edge))throw std::invalid_argument("Circle projection produced non-finite geometry.");vertices[static_cast<std::size_t>(index)+1u]={{edge.x,edge.y},edge_color,{0,0}};}const auto &indices=soft_circle_indices();require(SDL_RenderGeometry(storage_->renderer,nullptr,vertices.data(),static_cast<int>(vertices.size()),indices.data(),static_cast<int>(indices.size())),"SDL soft circle draw failed");};
  require(SDL_SetRenderDrawColor(storage_->renderer,5,9,19,255),"SDL clear color failed");require(SDL_RenderClear(storage_->renderer),"SDL render clear failed");for(const auto &line:draw_list.lines)draw_line(line);for(const auto &circle:draw_list.circles)draw_circle(circle);for(const auto &label:draw_list.text)storage_->draw_text(label);
  for(const auto &command:draw_list.world){std::visit([&](const auto &value){using Value=std::decay_t<decltype(value)>;if constexpr(std::is_same_v<Value,Line>)draw_line(value);else if constexpr(std::is_same_v<Value,Circle>)draw_circle(value);else if constexpr(std::is_same_v<Value,Text>)storage_->draw_text(value);else storage_->draw_image(value);},command);}
  for(const auto &command:draw_list.overlay){std::visit([&](const auto &value){using Value=std::decay_t<decltype(value)>;if constexpr(std::is_same_v<Value,FilledRectangle>){if(!valid_clip(value.bounds))throw std::invalid_argument("Panel fill bounds must be finite.");const auto bounds=sdl_rect(value.bounds);require(SDL_SetRenderDrawColor(storage_->renderer,value.color.r,value.color.g,value.color.b,value.color.a),"SDL panel fill color failed");require(SDL_RenderFillRect(storage_->renderer,&bounds),"SDL panel fill failed");}else if constexpr(std::is_same_v<Value,StrokedRectangle>){if(!valid_clip(value.bounds))throw std::invalid_argument("Panel stroke bounds must be finite.");const auto bounds=sdl_rect(value.bounds);require(SDL_SetRenderDrawColor(storage_->renderer,value.color.r,value.color.g,value.color.b,value.color.a),"SDL panel stroke color failed");require(SDL_RenderRect(storage_->renderer,&bounds),"SDL panel stroke failed");}else if constexpr(std::is_same_v<Value,Line>)draw_line(value);else if constexpr(std::is_same_v<Value,Image>)storage_->draw_image(value);else storage_->draw_text(value);},command);}
  if(timing)timing->submission_ms=elapsed_ms(*submission_started);
  if(screenshot){const auto readback_started=timing?std::optional{std::chrono::steady_clock::now()}:std::nullopt;SDL_Surface *surface=SDL_RenderReadPixels(storage_->renderer,nullptr);if(!surface)throw sdl_error("SDL screenshot readback failed");const std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> owner(surface,SDL_DestroySurface);const auto path=utf8_path(*screenshot);require(SDL_SaveBMP(surface,path.c_str()),"SDL screenshot write failed");if(timing)timing->readback_ms=elapsed_ms(*readback_started);}
  if(!storage_->vsync){const auto throttle_started=timing?std::optional{std::chrono::steady_clock::now()}:std::nullopt;const auto now=SDL_GetTicksNS();if(storage_->last_present_ns&&now-storage_->last_present_ns<storage_->fallback_interval_ns)SDL_DelayPrecise(storage_->fallback_interval_ns-(now-storage_->last_present_ns));storage_->last_present_ns=SDL_GetTicksNS();if(timing)timing->throttle_ms=elapsed_ms(*throttle_started);}
  const auto present_started=timing?std::optional{std::chrono::steady_clock::now()}:std::nullopt;require(SDL_RenderPresent(storage_->renderer),"SDL present failed");if(timing)timing->present_ms=elapsed_ms(*present_started);
}
int Window::drawable_width()const noexcept{return storage_->width;}int Window::drawable_height()const noexcept{return storage_->height;}std::string Window::gpu_driver()const{const char *driver=SDL_GetGPUDeviceDriver(storage_->device);if(!driver)throw sdl_error("SDL GPU driver query failed");return driver;}std::string Window::presentation_mode()const{return storage_->vsync?"vsync":"display-refresh-fallback";}
std::size_t Window::text_cache_entries()const noexcept{return storage_->text_cache.size();}std::size_t Window::text_cache_bytes()const noexcept{return storage_->text_cache_bytes;}
std::size_t Window::image_cache_entries()const noexcept{return storage_->image_cache.size();}std::size_t Window::image_cache_resident_bytes()const noexcept{return storage_->image_cache_resident_bytes;}std::uint64_t Window::image_upload_count()const noexcept{return storage_->image_uploads;}
} // namespace stellar::native_map
