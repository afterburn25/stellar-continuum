#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>
namespace stellar::native_map {
struct Color { std::uint8_t r{}, g{}, b{}, a{255}; };
struct Point { float x{}, y{}; };
struct UiRect { float x{}, y{}, width{}, height{}; [[nodiscard]] bool contains(Point point)const noexcept{return point.x>=x&&point.y>=y&&point.x<x+width&&point.y<y+height;} };
struct Line { Point from, to; Color color; };
struct Circle { Point center; float radius{}; Color color; };
enum class TextAlign { Left, Center, Right };
enum class FontFace { Interface, Heading };
struct Text { Point at; std::string value; Color color; int font_pixel_size{15}; float wrap_width{}; std::optional<UiRect> clip; TextAlign align{TextAlign::Left}; FontFace face{FontFace::Interface}; float rotation_degrees{}; };
struct TextExtent { int width{},height{}; };
struct FilledRectangle { UiRect bounds; Color color; };
struct StrokedRectangle { UiRect bounds; Color color; };
// Ordered, untextured geometry in drawable pixels. Each three indices form one
// triangle; callers batch compatible materials to keep submission work bounded.
class RgbaImage;
struct TriangleMesh {
  std::vector<Point> vertices;
  std::vector<int> indices;
  Color color;
  std::optional<UiRect> clip;
  std::shared_ptr<const RgbaImage> texture;
  std::vector<Point> texture_coordinates;
  std::vector<Color> vertex_colors;
};
inline constexpr int maximum_rgba_image_dimension=8192;
inline constexpr std::size_t maximum_rgba_image_bytes=64u*1024u*1024u;
inline constexpr std::size_t maximum_image_cache_entries=128;
// Includes the immutable CPU pixels retained by the cache and the estimated
// RGBA texture allocation. Images also retained by callers are outside it.
inline constexpr std::size_t maximum_image_cache_resident_bytes=192u*1024u*1024u;
struct Bc1MipLevel {int width{},height{};std::vector<std::uint8_t> blocks;};
enum class TextureFormat;
class RgbaImage final {
 public:
  [[nodiscard]] static std::shared_ptr<const RgbaImage> create(
      int width,int height,std::vector<std::uint8_t> rgba_pixels,std::vector<Bc1MipLevel> bc1_mips={});
  [[nodiscard]] static std::shared_ptr<const RgbaImage> create_cooked(TextureFormat,std::vector<Bc1MipLevel>);
  [[nodiscard]] const auto& cooked_mips()const noexcept{return cooked_mips_;}
  [[nodiscard]] TextureFormat cooked_format()const noexcept{return cooked_format_;}
  [[nodiscard]] int width() const noexcept{return width_;}
  [[nodiscard]] int height() const noexcept{return height_;}
  // Lossless cooked textures share the base mip with CPU consumers. Both views
  // remain immutable, so no lazy decoding or synchronization is required.
  [[nodiscard]] const std::vector<std::uint8_t>& pixels() const noexcept{return pixels_.empty()&&!cooked_mips_.empty()?cooked_mips_.front().blocks:pixels_;}
  [[nodiscard]] std::size_t byte_size() const noexcept{std::size_t n=pixels_.size();for(const auto& mip:bc1_mips_)n+=mip.blocks.size();for(const auto& mip:cooked_mips_)n+=mip.blocks.size();return n;}
  [[nodiscard]] const auto& bc1_mips()const noexcept{return bc1_mips_;}
 private:
  RgbaImage(int width,int height,std::vector<std::uint8_t> pixels)
      :width_(width),height_(height),pixels_(std::move(pixels)){}
  int width_{},height_{};std::vector<std::uint8_t> pixels_;std::vector<Bc1MipLevel> bc1_mips_;
  TextureFormat cooked_format_{};std::vector<Bc1MipLevel> cooked_mips_;
};
enum class ImageDecodeUsage { PreserveMipChain, PixelsOnly };
// Exact retained output size for cooked inputs, including compressed mips and
// decoded base pixels. Loose callers supply their known raster allocation.
[[nodiscard]] std::size_t image_decode_output_bytes(
    const std::filesystem::path &path,std::size_t loose_output_bytes,
    int maximum_width=0,ImageDecodeUsage usage=ImageDecodeUsage::PreserveMipChain);
// PixelsOnly reads just the selected mip from a package. Use it for 2D uploads
// and CPU preparation; 3D materials retain the default precomputed mip chain.
[[nodiscard]] std::shared_ptr<const RgbaImage> decode_rgba_image(
    const std::filesystem::path &path,int maximum_width=0,
    ImageDecodeUsage usage=ImageDecodeUsage::PreserveMipChain);
struct Image {
  std::shared_ptr<const RgbaImage> resource;
  UiRect destination;
  std::optional<UiRect> source;
  Color tint{255,255,255,255};
  std::optional<UiRect> clip;
  // Clockwise rotation about the destination center, in drawable space.
  // Appended with a neutral default to preserve existing image callers.
  float rotation_degrees{};
  // Mirror the source rect across the destination's axes. Flips compose
  // with rotation exactly as SDL applies them (flip after rotate).
  bool flip_horizontal{};
  bool flip_vertical{};
};
class Scene3D;
struct Scene3DStatistics;
// Quality policy for a 3D view: gates expensive sampling (bloom taps,
// sharpen, MSAA). Low must remain correct, just cheaper.
enum class RenderQuality3D { Low, Medium, High, Ultra };
// Diagnostic shading for editor/QA views. Lit is the production path;
// the rest isolate one channel for material and lighting review:
// Unlit = tinted surface without illumination, Albedo = sampled surface
// before tint, Normals = view-space normal *0.5+0.5, Roughness/Metallic
// show the active GGX factors, Emissive = emissive + atmosphere
// contribution only, LightingOnly = shading with the albedo divided out,
// Lod = per-draw LOD class tint (gray full mesh, level ramp, magenta
// group proxy; screen-door bands show their dithered partition).
enum class DebugView3D {
  Lit, Unlit, Albedo, Normals, Roughness, Metallic, Emissive, LightingOnly,
  Lod
};
// Per-view post-processing, all in linear HDR space before the tonemap
// resolve. Exposure multiplies incoming radiance; bloom reads the HDR mip
// chain above its soft threshold; sharpen is an unsharp mask amount.
struct RenderOptions3D {
  RenderQuality3D quality{RenderQuality3D::High};
  float exposure{1.f};
  float bloom_strength{0.f};
  float bloom_threshold{1.f};
  float contrast{1.f};   // 0..2 about mid gray
  float saturation{1.f}; // 0..2
  float sharpen{0.f};    // 0..1 unsharp amount
  DebugView3D debug_view{DebugView3D::Lit};
};
// A depth-tested 3D viewport composites at this exact place in either layer.
// Its geometry stays in 3D; only this destination uses drawable pixels.
struct Scene3DView { std::shared_ptr<const Scene3D> scene;UiRect destination;RenderOptions3D options; };
using WorldCommand=std::variant<Line,Circle,Text,Image,TriangleMesh,Scene3DView>;
using UiOverlayCommand=std::variant<FilledRectangle,StrokedRectangle,Line,Text,Image,TriangleMesh,Scene3DView>;
// A completed scene is immutable at this boundary. Coordinates are drawable
// pixels, already projected relative to the camera by the application.
struct DrawList {
  std::vector<Line> lines;
  std::vector<Circle> circles;
  std::vector<Text> text;
  std::vector<UiOverlayCommand> overlay;
  // New scenes use this ordered layer. Legacy batches are drawn first and the
  // UI overlay remains last, preserving every existing aggregate caller.
  std::vector<WorldCommand> world;
};
// Optional CPU wall-clock breakdown for a single draw. Submission and present
// may include driver or GPU waits; these are not GPU execution measurements.
struct FrameTiming { double submission_ms{},readback_ms{},throttle_ms{},present_ms{}; };
struct DisplayMode { int width{},height{}; float refresh_hz{}; };
enum class WindowDisplayMode { Windowed, Borderless, ExclusiveFullscreen };
struct FolderDialogResult {
  std::uint64_t request_id{};
  std::optional<std::filesystem::path> directory;
  std::string error;
};
enum class InputEventType { PointerMove, LeftPressed, LeftReleased,
                            RightPressed, RightReleased, Wheel,
                            EscapePressed, BackspacePressed, KeyPressed,
                            KeyReleased, TextEntered, PointerCancelled,
                            GamepadPressed, GamepadReleased, GamepadAxis };
struct InputEvent {
  InputEventType type{};
  Point position{}, delta{};
  float wheel_y{};
  std::string text;
  std::uint8_t click_count{};
  // SDL_Keycode for non-repeating KeyPressed events.
  std::uint32_t key{};
  bool control{}, shift{}, alt{};
  // SDL_GamepadButton / SDL_GamepadAxis codes; axis_value is -1..1.
  // gamepad_device is the platform slot index (0..3) of the pad that
  // produced the event — bindings can pin a slot, -wildcard consumers
  // treat all pads alike.
  std::uint8_t gamepad_button{}, gamepad_axis{}, gamepad_device{};
  float gamepad_axis_value{};
};
struct InputSnapshot {
  std::vector<InputEvent> events;
  int drawable_width{}, drawable_height{};
  Point pointer{};
  bool focused{true}, minimized{}, quit_requested{};
  [[nodiscard]] bool renderable() const noexcept {
    return !minimized&&drawable_width>0&&drawable_height>0;
  }
};
// Owns the fullscreen, high-DPI Vulkan renderer. It has no Core dependency.
class Window final {
 public:
  Window(std::string title, int initial_width, int initial_height, bool fullscreen,
         std::filesystem::path font_path);
  ~Window();
  Window(Window &&) noexcept;
  Window &operator=(Window &&) noexcept;
  Window(const Window &) = delete;
  Window &operator=(const Window &) = delete;
  [[nodiscard]] InputSnapshot poll();
  void set_text_input(bool enabled);
  [[nodiscard]] std::vector<DisplayMode> display_modes() const;
  [[nodiscard]] std::vector<DisplayMode> windowed_display_modes() const;
  [[nodiscard]] DisplayMode desktop_display_mode() const;
  [[nodiscard]] float display_refresh_hz() const;
  // Opaque OS handle (HWND on Windows, nullptr elsewhere) for platform
  // accessibility bridging. Read-only; the window retains ownership.
  [[nodiscard]] void *native_window_handle() const noexcept;
  // Display names of the pads occupying the fixed slots — one entry per
  // slot, empty for a free slot. Rebind UIs show these beside
  // InputBinding::device pins instead of bare slot numbers.
  [[nodiscard]] std::vector<std::string> gamepad_names() const;
  // Owner-thread operations. Driver rejection is reported to the host's
  // transactional preview controller; no requested setting is reported saved.
  void set_display_mode(WindowDisplayMode mode,int width=0,int height=0,float refresh_hz=0);
  void set_fullscreen_mode(bool exclusive,int width=0,int height=0,float refresh_hz=0);
  void set_vsync(int mode);
  void set_scene_quality(int resolution_percent, int samples);
  [[nodiscard]] std::string graphics_adapter() const;
  [[nodiscard]] bool has_nvidia_control_panel() const;
  void open_nvidia_control_panel();
  void set_clipboard_text(const std::string& text);
  void set_frame_cap(double hz);
  void set_auto_frame_cap();
  // Queues at most one player capture. F12/PrintScreen call this internally;
  // tests may use it with an isolated directory.
  [[nodiscard]] bool request_screenshot(std::filesystem::path path);
  // Empty restores the standard Pictures destination. Environment override is
  // intentionally resolved above this application preference.
  void set_screenshot_directory(std::filesystem::path path);
  [[nodiscard]] static std::filesystem::path default_screenshot_directory();
  // SDL's asynchronous native picker. Invoke/consume on the window thread;
  // the callback owns independent state and never touches the Window or UI.
  [[nodiscard]] bool request_folder_dialog(std::uint64_t request_id,
                                         std::filesystem::path initial_directory);
  [[nodiscard]] std::optional<FolderDialogResult> take_folder_dialog_result();
  [[nodiscard]] std::optional<std::string> take_screenshot_status();
  [[nodiscard]] TextExtent measure_text(const Text &);
  void draw(const DrawList &draw_list,
            const std::optional<std::filesystem::path> &screenshot = std::nullopt,
            FrameTiming *timing = nullptr);
  [[nodiscard]] int drawable_width() const noexcept;
  [[nodiscard]] int drawable_height() const noexcept;
  [[nodiscard]] std::string gpu_driver() const;
  [[nodiscard]] std::string presentation_mode() const;
  [[nodiscard]] std::size_t text_cache_entries() const noexcept;
  [[nodiscard]] std::size_t text_cache_bytes() const noexcept;
  [[nodiscard]] std::size_t image_cache_entries() const noexcept;
  [[nodiscard]] std::size_t image_cache_resident_bytes() const noexcept;
  [[nodiscard]] std::uint64_t image_upload_count() const noexcept;
  [[nodiscard]] Scene3DStatistics scene3d_statistics() const noexcept;
  // Retunes the 3D texture-streaming byte budget; takes effect next frame.
  void set_scene3d_texture_budget(std::uint64_t bytes);
 private:
  struct Storage; Storage *storage_{};
};
} // namespace stellar::native_map
