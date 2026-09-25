#pragma once

#include <stellar/engine/accessibility.hpp>
#include <stellar/engine/localization.hpp>
#include <stellar/engine/native_map_platform.hpp>

#include <optional>
#include <string>
#include <vector>

namespace stellar::native_quick_find {

// A command-palette row projected from authoritative campaign state by the
// caller — the palette never reaches into the simulation itself.
enum class EntryKind { System, Colony, Fleet, Contact };
struct Entry {
  EntryKind kind{};
  int id{};
  std::string label, detail;
};

struct QuickFindLayout {
  float scale{};
  stellar::native_map::UiRect panel, title, field, list, hint;
  int body_pixels{}, small_pixels{};
  int row_pixels{};
  [[nodiscard]] static QuickFindLayout make(int width, int height);
};

struct Command {
  bool captured{};
  std::optional<Entry> activated;
};

// Modal quick-find overlay: one edit field plus a filtered result list.
// Typing always edits the query; arrows/Home/End move the highlight;
// Return/Space or a click activates the highlighted entry. Escape or a
// click outside the panel closes the palette.
class QuickFind {
 public:
  void set_localization(const stellar::engine::LocalizationTable *table) noexcept {
    locale_ = table;
  }
  void open(std::vector<Entry> entries);
  void close() noexcept;
  void discard_campaign() noexcept { close(); }
  [[nodiscard]] bool visible() const noexcept { return visible_; }
  [[nodiscard]] bool wants_text_input() const noexcept { return visible_; }
  [[nodiscard]] const std::string &query() const noexcept { return query_; }
  [[nodiscard]] std::size_t match_count() const noexcept { return matches_.size(); }
  [[nodiscard]] std::size_t entry_count() const noexcept { return entries_.size(); }
  [[nodiscard]] int focus() const noexcept;
  [[nodiscard]] Command handle(const stellar::native_map::InputEvent &event,
                               int width, int height);
  void render(stellar::native_map::DrawList &out, int width, int height) const;

  // AT contract — the field reports Edit; a highlighted row reports Custom
  // with the row label so arrow navigation stays audible while typing.
  [[nodiscard]] std::string focused_label(int width, int height) const;
  [[nodiscard]] std::optional<stellar::native_map::UiRect>
  focused_bounds(int width, int height) const;
  [[nodiscard]] stellar::engine::AnnouncementControl
  focused_control(int width, int height) const;
  [[nodiscard]] std::optional<stellar::engine::AnnouncementValue>
  focused_value(int width, int height) const;
  bool set_focused_text(std::string text, int width, int height);

 private:
  [[nodiscard]] std::string tr(std::string_view key,
                               std::string_view fallback) const;
  [[nodiscard]] std::string kind_label(EntryKind kind) const;
  void refilter();
  [[nodiscard]] stellar::native_map::UiRect row_bounds(
      const QuickFindLayout &layout, std::size_t match) const;

  bool visible_{};
  std::vector<Entry> entries_;
  std::vector<std::size_t> matches_;
  std::string query_;
  int selected_{-1};  // index into matches_; -1 highlights nothing
  std::optional<std::size_t> pressed_;
  float scroll_{};
  stellar::native_map::Point pointer_{-1.f, -1.f};
  const stellar::engine::LocalizationTable *locale_{};
};

} // namespace stellar::native_quick_find
