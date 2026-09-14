#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
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
struct FilledRectangle { UiRect bounds; Color color; };
struct StrokedRectangle { UiRect bounds; Color color; };
using UiOverlayCommand=std::variant<FilledRectangle,StrokedRectangle,Line,Text>;
// A completed scene is immutable at this boundary. Coordinates are drawable
// pixels, already projected relative to the camera by the application.
struct DrawList { std::vector<Line> lines; std::vector<Circle> circles; std::vector<Text> text; std::vector<UiOverlayCommand> overlay; };
enum class InputEventType { PointerMove, LeftPressed, LeftReleased, Wheel,
                            EscapePressed, BackspacePressed, TextEntered,
                            PointerCancelled };
struct InputEvent {
  InputEventType type{};
  Point position{}, delta{};
  float wheel_y{};
  std::string text;
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
  void draw(const DrawList &draw_list,
            const std::optional<std::filesystem::path> &screenshot = std::nullopt);
  [[nodiscard]] int drawable_width() const noexcept;
  [[nodiscard]] int drawable_height() const noexcept;
  [[nodiscard]] std::string gpu_driver() const;
  [[nodiscard]] std::string presentation_mode() const;
  [[nodiscard]] std::size_t text_cache_entries() const noexcept;
  [[nodiscard]] std::size_t text_cache_bytes() const noexcept;
 private:
  struct Storage; Storage *storage_{};
};
} // namespace stellar::native_map
