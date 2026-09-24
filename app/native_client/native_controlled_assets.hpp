#pragma once
#include <stellar/engine/localization.hpp>
#include <stellar/engine/ui_viewmodels.hpp>
#include "native_colony_roster.hpp"
#include "native_fleet_controller.hpp"
#include "native_shipyard_controller.hpp"
#include <array>
#include <functional>

namespace stellar::native_assets {
enum class Category { Planets, Outposts, Fleets, Stations, Shipyards };
struct Key { Category category{}; int id{}; bool operator==(const Key&) const = default; };
struct Row {
  Key key;
  int system_id{-1}, body_id{-1};
  std::string name, detail, activity, tooltip, search;
  bool controlled{}, actionable{true};
  int severity{};
  std::optional<double> progress;
  bool operator==(const Row&) const = default;
};
struct View {
  std::uint64_t generation{};
  int observer{-1};
  std::vector<Row> rows;
};
// The navigator is a detached projection, never an ownership registry.
[[nodiscard]] View build(const stellar::core::FreshCampaignState&,
    const stellar::native_colony_roster::View&,
    const stellar::native_fleet::NativeFleetMapView&,
    const stellar::native_shipyard::NativeShipyardView*,
    const std::function<std::string(int)>& known_system_name,
    const stellar::engine::LocalizationTable* locale = nullptr);
struct Preferences {
  std::array<bool,5> collapsed{false,true,false,true,false};
  bool hidden{};
  bool operator==(const Preferences&)const=default;
};
struct Layout {
  stellar::native_map::UiRect panel, header, hide, search, clear, list, restore;
  float scale{}, row_height{}, category_height{};
  [[nodiscard]] static Layout make(int width,int height);
};
struct Command {
  bool captured{}, manage{};
  std::optional<Key> key;
  std::uint64_t generation{};
  int observer{-1};
};
class Navigator {
public:
  using Picture=std::shared_ptr<const stellar::native_map::RgbaImage>;
  using Art=std::function<Picture(const Row&)>;
  void set_view(View);
  void set_selection(std::optional<Key>,bool external=true);
  void set_preferences(Preferences p){preferences_=p;rebuild();}
  void set_persist(std::function<bool(const Preferences&)> fn){persist_=std::move(fn);}
  void set_localization(const stellar::engine::LocalizationTable* table)noexcept{locale_=table;}
  [[nodiscard]] const Preferences& preferences()const{return preferences_;}
  [[nodiscard]] const View& view()const{return view_;}
  [[nodiscard]] const std::string& search()const{return search_;}
  [[nodiscard]] bool wants_text_input()const{return search_focused_&&!preferences_.hidden;}
  [[nodiscard]] std::optional<Key> selection()const{return selected_;}
  [[nodiscard]] float scroll_offset()const{return scroll_.scroll_offset;}
  [[nodiscard]] int focus()const noexcept{return focus_;}
  // Localized label of the ringed control — the announcement surface for
  // screen-reader/live-region consumers. Empty when nothing is focused.
  [[nodiscard]] std::string focused_label(int,int)const;
  [[nodiscard]] std::optional<stellar::native_map::UiRect> row_bounds(Key,int,int)const;
  [[nodiscard]] stellar::native_map::UiRect category_bounds(Category,int,int)const;
  [[nodiscard]] Command handle(const stellar::native_map::InputEvent&,int,int);
  void render(stellar::native_map::DrawList&,int,int,const Art&);
  void cancel_input(){pressed_.reset();search_focused_=false;focus_=-1;}
private:
  struct Entry { std::optional<std::size_t> row; Category category{}; };
  // Ring rect plus the entries_ index it came from (headers and rows
  // alike); plain controls carry no entry. Scroll-follow uses the entry's
  // unclipped bounds.
  struct FocusTarget { stellar::native_map::UiRect bounds; std::optional<std::size_t> entry{}; std::string label; };
  [[nodiscard]] std::vector<FocusTarget> focusables(const Layout&) const;
  void rebuild();
  void commit_preferences(Preferences);
  [[nodiscard]] std::string tr(std::string_view key,std::string_view fallback)const;
  [[nodiscard]] std::string trf(std::string_view key,std::initializer_list<std::string> args,std::string_view fallback)const;
  [[nodiscard]] float extent(const Layout&)const;
  [[nodiscard]] stellar::native_map::UiRect entry_bounds(std::size_t,const Layout&)const;
  View view_;
  Preferences preferences_;
  std::function<bool(const Preferences&)> persist_;
  std::vector<Entry> entries_;
  // Category headers own the row children — the first player-facing
  // TreeModel consumer. entries_ is the flattened projection; collapse
  // truth stays in preferences_ (persisted), search/temporary reveal
  // force-expand per rebuild.
  stellar::engine::TreeModel tree_;
  std::array<int,5> counts_{},matches_{};
  std::string search_,error_;
  std::optional<Key> selected_,temporary_reveal_;
  std::optional<std::size_t> pressed_;
  std::uint64_t pressed_generation_{};
  int pressed_observer_{},click_count_{};
  bool search_focused_{},reveal_selection_{};
  int focus_{-1};
  mutable stellar::engine::ScrollView scroll_{};
  stellar::native_map::Point pointer_{};
  const stellar::engine::LocalizationTable* locale_{};
};
}
