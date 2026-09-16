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
struct Text { Point at; std::string value; Color color; int font_pixel_size{15}; float wrap_width{}; std::optional<UiRect> clip; TextAlign align{TextAlign::Left}; FontFace face{FontFace::Interface}; };
struct TextExtent { int width{},height{}; };
struct FilledRectangle { UiRect bounds; Color color; };
struct StrokedRectangle { UiRect bounds; Color color; };
// Ordered, untextured geometry in drawable pixels. Each three indices form one
// triangle; callers batch compatible materials to keep submission work bounded.
struct TriangleMesh {
  std::vector<Point> vertices;
  std::vector<int> indices;
  Color color;
  std::optional<UiRect> clip;
};
inline constexpr int maximum_rgba_image_dimension=8192;
inline constexpr std::size_t maximum_rgba_image_bytes=64u*1024u*1024u;
inline constexpr std::size_t maximum_image_cache_entries=128;
// Includes the immutable CPU pixels retained by the cache and the estimated
// RGBA texture allocation. Images also retained by callers are outside it.
inline constexpr std::size_t maximum_image_cache_resident_bytes=192u*1024u*1024u;
class RgbaImage final {
 public:
  [[nodiscard]] static std::shared_ptr<const RgbaImage> create(
      int width,int height,std::vector<std::uint8_t> rgba_pixels);
  [[nodiscard]] int width() const noexcept{return width_;}
  [[nodiscard]] int height() const noexcept{return height_;}
  [[nodiscard]] const std::vector<std::uint8_t>& pixels() const noexcept{return pixels_;}
  [[nodiscard]] std::size_t byte_size() const noexcept{return pixels_.size();}
 private:
  RgbaImage(int width,int height,std::vector<std::uint8_t> pixels)
      :width_(width),height_(height),pixels_(std::move(pixels)){}
  int width_{},height_{};std::vector<std::uint8_t> pixels_;
};
[[nodiscard]] std::shared_ptr<const RgbaImage> decode_rgba_image(
    const std::filesystem::path &path);
struct Image {
  std::shared_ptr<const RgbaImage> resource;
  UiRect destination;
  std::optional<UiRect> source;
  Color tint{255,255,255,255};
  std::optional<UiRect> clip;
  // Clockwise rotation about the destination center, in drawable space.
  // Appended with a neutral default to preserve existing image callers.
  float rotation_degrees{};
};
using WorldCommand=std::variant<Line,Circle,Text,Image>;
using UiOverlayCommand=std::variant<FilledRectangle,StrokedRectangle,Line,Text,Image,TriangleMesh>;
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
enum class InputEventType { PointerMove, LeftPressed, LeftReleased,
                            RightPressed, RightReleased, Wheel,
                            EscapePressed, BackspacePressed, KeyPressed, TextEntered,
                            PointerCancelled };
struct InputEvent {
  InputEventType type{};
  Point position{}, delta{};
  float wheel_y{};
  std::string text;
  std::uint8_t click_count{};
  // SDL_Keycode for non-repeating KeyPressed events.
  std::uint32_t key{};
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
  // Owner-thread operations. Driver rejection is reported to the host's
  // transactional preview controller; no requested setting is reported saved.
  void set_display_mode(WindowDisplayMode mode,int width=0,int height=0,float refresh_hz=0);
  void set_fullscreen_mode(bool exclusive,int width=0,int height=0,float refresh_hz=0);
  void set_vsync(int mode);
  void set_frame_cap(double hz);
  void set_auto_frame_cap();
  // Queues at most one player capture. F12/PrintScreen call this internally;
  // tests may use it with an isolated directory.
  [[nodiscard]] bool request_screenshot(std::filesystem::path path);
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
 private:
  struct Storage; Storage *storage_{};
};
} // namespace stellar::native_map
