#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
namespace stellar::native_map {
struct Color { std::uint8_t r{}, g{}, b{}, a{255}; };
struct Point { float x{}, y{}; };
struct Line { Point from, to; Color color; };
struct Circle { Point center; float radius{}; Color color; };
struct Text { Point at; std::string value; Color color; };
// A completed scene is immutable at this boundary. Coordinates are drawable
// pixels, already projected relative to the camera by the application.
struct DrawList { std::vector<Line> lines; std::vector<Circle> circles; std::vector<Text> text; };
enum class InputEventType { PointerMove, LeftPressed, LeftReleased, Wheel,
                            EscapePressed, PointerCancelled };
struct InputEvent {
  InputEventType type{};
  Point position{}, delta{};
  float wheel_y{};
};
struct InputSnapshot {
  std::vector<InputEvent> events;
  int drawable_width{}, drawable_height{};
  bool focused{true}, minimized{}, quit_requested{};
  [[nodiscard]] bool renderable() const noexcept {
    return !minimized&&drawable_width>0&&drawable_height>0;
  }
};
// Owns the fullscreen, high-DPI Vulkan renderer. It has no Core dependency.
class Window final {
 public:
  Window(std::string title, int initial_width, int initial_height, bool fullscreen = true);
  ~Window();
  Window(Window &&) noexcept;
  Window &operator=(Window &&) noexcept;
  Window(const Window &) = delete;
  Window &operator=(const Window &) = delete;
  [[nodiscard]] InputSnapshot poll();
  void draw(const DrawList &draw_list,
            const std::optional<std::filesystem::path> &screenshot = std::nullopt);
  [[nodiscard]] int drawable_width() const noexcept;
  [[nodiscard]] int drawable_height() const noexcept;
  [[nodiscard]] std::string gpu_driver() const;
  [[nodiscard]] std::string presentation_mode() const;
 private:
  struct Storage; Storage *storage_{};
};
} // namespace stellar::native_map
