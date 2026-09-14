#include <stellar/engine/native_map_platform.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_render.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <utility>
namespace stellar::native_map {
namespace {
[[nodiscard]] std::runtime_error sdl_error(const char *operation) { return std::runtime_error(std::string(operation) + ": " + SDL_GetError()); }
void require(bool success, const char *operation) { if (!success) throw sdl_error(operation); }
[[nodiscard]] std::string utf8_path(const std::filesystem::path &path){const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};}
}
struct Window::Storage {
  SDL_Window *window{}; SDL_GPUDevice *device{}; SDL_Renderer *renderer{};
  int width{}, height{}; bool initialized{}, left_down{}, focused{true}, minimized{},vsync{};Point pointer{};Uint64 fallback_interval_ns{},last_present_ns{};
  ~Storage() { if (renderer) SDL_DestroyRenderer(renderer); if (device) SDL_DestroyGPUDevice(device); if (window) SDL_DestroyWindow(window); if (initialized) SDL_Quit(); }
};
Window::Window(std::string title, int width, int height, bool fullscreen) {
  auto candidate = std::make_unique<Storage>();
  require(SDL_Init(SDL_INIT_VIDEO), "SDL video initialization failed"); candidate->initialized = true;
  const auto flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
  candidate->window = SDL_CreateWindow(title.c_str(), width, height, flags);
  if (!candidate->window) throw sdl_error("SDL window creation failed");
  candidate->device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, "vulkan");
  if (!candidate->device) throw sdl_error("Vulkan SDL GPU device creation failed");
  const char *driver=SDL_GetGPUDeviceDriver(candidate->device);
  if(!driver||std::string(driver)!="vulkan")throw std::runtime_error("Vulkan SDL GPU device creation returned an unexpected backend");
  candidate->renderer = SDL_CreateGPURenderer(candidate->device, candidate->window);
  if (!candidate->renderer) throw sdl_error("Vulkan SDL GPU renderer creation failed");
  require(SDL_SetRenderDrawBlendMode(candidate->renderer, SDL_BLENDMODE_BLEND), "SDL renderer blend setup failed");
  candidate->vsync=SDL_SetRenderVSync(candidate->renderer,1);
  if(!candidate->vsync){const std::string reason=SDL_GetError();float refresh=60.f;const auto display=SDL_GetDisplayForWindow(candidate->window);if(display){if(const auto *mode=SDL_GetDesktopDisplayMode(display);mode&&mode->refresh_rate>1.f)refresh=mode->refresh_rate;}candidate->fallback_interval_ns=static_cast<Uint64>(1000000000./static_cast<double>(refresh));SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,"Renderer VSync unavailable (%s); pacing presents at %.2f Hz",reason.c_str(),static_cast<double>(refresh));}
  require(SDL_GetCurrentRenderOutputSize(candidate->renderer, &candidate->width, &candidate->height), "SDL drawable pixel query failed");
  const auto window_flags=SDL_GetWindowFlags(candidate->window);candidate->focused=(window_flags&SDL_WINDOW_INPUT_FOCUS)!=0;candidate->minimized=(window_flags&SDL_WINDOW_MINIMIZED)!=0;
  storage_ = candidate.release();
}
Window::~Window() { delete storage_; }
Window::Window(Window &&other) noexcept : storage_(std::exchange(other.storage_, nullptr)) {}
Window &Window::operator=(Window &&other) noexcept { if (this != &other) { delete storage_; storage_ = std::exchange(other.storage_, nullptr); } return *this; }
InputSnapshot Window::poll() {
  InputSnapshot input;
  require(SDL_GetWindowSizeInPixels(storage_->window,&storage_->width,&storage_->height),"SDL drawable pixel query failed");
  const auto convert = [&](float x, float y) { Point result; require(SDL_RenderCoordinatesFromWindow(storage_->renderer, x, y, &result.x, &result.y), "SDL input coordinate conversion failed"); return result; };
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_EVENT_QUIT: input.quit_requested = true; break;
      case SDL_EVENT_KEY_DOWN: if(!event.key.repeat&&event.key.key==SDLK_ESCAPE)input.events.push_back({InputEventType::EscapePressed,storage_->pointer,{}});break;
      case SDL_EVENT_MOUSE_MOTION:{const auto prior=storage_->pointer;storage_->pointer=convert(event.motion.x,event.motion.y);if(storage_->left_down)input.events.push_back({InputEventType::PointerMove,storage_->pointer,{storage_->pointer.x-prior.x,storage_->pointer.y-prior.y}});break;}
      case SDL_EVENT_MOUSE_BUTTON_DOWN:storage_->pointer=convert(event.button.x,event.button.y);if(event.button.button==SDL_BUTTON_LEFT){storage_->left_down=true;input.events.push_back({InputEventType::LeftPressed,storage_->pointer,{}});}break;
      case SDL_EVENT_MOUSE_BUTTON_UP:storage_->pointer=convert(event.button.x,event.button.y);if(event.button.button==SDL_BUTTON_LEFT){storage_->left_down=false;input.events.push_back({InputEventType::LeftReleased,storage_->pointer,{}});}break;
      case SDL_EVENT_MOUSE_WHEEL:{storage_->pointer=convert(event.wheel.mouse_x,event.wheel.mouse_y);const auto wheel=event.wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-event.wheel.y:event.wheel.y;input.events.push_back({InputEventType::Wheel,storage_->pointer,{},wheel});break;}
      case SDL_EVENT_WINDOW_FOCUS_LOST:storage_->focused=false;storage_->left_down=false;input.events.push_back({InputEventType::PointerCancelled,storage_->pointer,{}});break;
      case SDL_EVENT_WINDOW_FOCUS_GAINED:storage_->focused=true;break;
      case SDL_EVENT_WINDOW_MINIMIZED:storage_->minimized=true;storage_->left_down=false;input.events.push_back({InputEventType::PointerCancelled,storage_->pointer,{}});break;
      case SDL_EVENT_WINDOW_RESTORED:storage_->minimized=false;break;
      default: break;
    }
  }
  input.drawable_width=storage_->width;input.drawable_height=storage_->height;input.focused=storage_->focused;input.minimized=storage_->minimized;return input;
}
void Window::draw(const DrawList &draw_list,const std::optional<std::filesystem::path> &screenshot) {
  require(SDL_SetRenderDrawColor(storage_->renderer,5,9,19,255),"SDL clear color failed"); require(SDL_RenderClear(storage_->renderer),"SDL render clear failed");
  for (const auto &line:draw_list.lines) { require(SDL_SetRenderDrawColor(storage_->renderer,line.color.r,line.color.g,line.color.b,line.color.a),"SDL line color failed"); require(SDL_RenderLine(storage_->renderer,line.from.x,line.from.y,line.to.x,line.to.y),"SDL line draw failed"); }
  for(const auto &circle:draw_list.circles){
    constexpr int segments=20;std::vector<SDL_Vertex> vertices;std::vector<int> indices;vertices.reserve(segments+2);indices.reserve(segments*3);
    const SDL_FColor center_color{circle.color.r/255.f,circle.color.g/255.f,circle.color.b/255.f,circle.color.a/255.f};
    const SDL_FColor edge_color{center_color.r,center_color.g,center_color.b,0.f};
    vertices.push_back({{circle.center.x,circle.center.y},center_color,{0,0}});
    for(int index=0;index<=segments;++index){const auto angle=2.f*std::numbers::pi_v<float>*static_cast<float>(index)/static_cast<float>(segments);vertices.push_back({{circle.center.x+std::cos(angle)*circle.radius,circle.center.y+std::sin(angle)*circle.radius},edge_color,{0,0}});}
    for(int index=0;index<segments;++index){indices.push_back(0);indices.push_back(index+1);indices.push_back(index+2);}
    require(SDL_RenderGeometry(storage_->renderer,nullptr,vertices.data(),static_cast<int>(vertices.size()),indices.data(),static_cast<int>(indices.size())),"SDL soft circle draw failed");
  }
  for(const auto &label:draw_list.text){ require(SDL_SetRenderDrawColor(storage_->renderer,label.color.r,label.color.g,label.color.b,label.color.a),"SDL text color failed"); require(SDL_RenderDebugText(storage_->renderer,label.at.x,label.at.y,label.value.c_str()),"SDL text draw failed"); }
  if(screenshot){SDL_Surface *surface=SDL_RenderReadPixels(storage_->renderer,nullptr);if(!surface)throw sdl_error("SDL screenshot readback failed");const std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> owner(surface,SDL_DestroySurface);const auto path=utf8_path(*screenshot);require(SDL_SaveBMP(surface,path.c_str()),"SDL screenshot write failed");}
  if(!storage_->vsync){const auto now=SDL_GetTicksNS();if(storage_->last_present_ns&&now-storage_->last_present_ns<storage_->fallback_interval_ns)SDL_DelayPrecise(storage_->fallback_interval_ns-(now-storage_->last_present_ns));storage_->last_present_ns=SDL_GetTicksNS();}
  require(SDL_RenderPresent(storage_->renderer),"SDL present failed");
}
int Window::drawable_width() const noexcept{return storage_->width;} int Window::drawable_height() const noexcept{return storage_->height;}
std::string Window::gpu_driver() const {const char *driver=SDL_GetGPUDeviceDriver(storage_->device);if(!driver)throw sdl_error("SDL GPU driver query failed");return driver;}
std::string Window::presentation_mode() const{return storage_->vsync?"vsync":"display-refresh-fallback";}
} // namespace stellar::native_map
