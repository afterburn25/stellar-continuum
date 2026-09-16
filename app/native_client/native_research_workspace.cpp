#include "native_research_workspace.hpp"
#include "native_ui_theme.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <map>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace stellar::native_research_ui {
namespace {
using namespace stellar::native_map;
using namespace stellar::native_research;

constexpr Color background = native_ui::color::canvas;
constexpr Color raised = native_ui::color::surface_secondary;
constexpr Color hover = native_ui::color::surface_hover;
constexpr Color selected = native_ui::color::surface_raised;
constexpr Color border = native_ui::color::keyline_strong;
constexpr Color bright = native_ui::color::text_primary;
constexpr Color muted = native_ui::color::text_secondary;
constexpr Color positive = native_ui::color::science;
constexpr Color warning = native_ui::color::caution;
constexpr Color failure = native_ui::color::danger;

[[nodiscard]] bool contains_rect(UiRect outer, UiRect inner) noexcept {
  return inner.x >= outer.x && inner.y >= outer.y &&
         inner.x + inner.width <= outer.x + outer.width &&
         inner.y + inner.height <= outer.y + outer.height;
}

[[nodiscard]] std::optional<UiRect> intersection(UiRect left,
                                                 UiRect right) noexcept {
  const auto x = std::max(left.x, right.x);
  const auto y = std::max(left.y, right.y);
  const auto right_edge = std::min(left.x + left.width, right.x + right.width);
  const auto bottom = std::min(left.y + left.height, right.y + right.height);
  if (right_edge <= x || bottom <= y) return std::nullopt;
  return UiRect{x, y, right_edge - x, bottom - y};
}

void fill(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(FilledRectangle{bounds, color});
}

void stroke(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(StrokedRectangle{bounds, color});
}

void text(DrawList &out, UiRect bounds, std::string value, Color color,
          int pixels, TextAlign align = TextAlign::Left,
          FontFace face = FontFace::Interface) {
  const auto x = align == TextAlign::Left
                     ? bounds.x
                     : align == TextAlign::Center
                           ? bounds.x + bounds.width * .5f
                           : bounds.x + bounds.width;
  out.overlay.emplace_back(Text{{x, bounds.y}, std::move(value), color, pixels,
                                bounds.width, bounds, align, face});
}

void clipped_text(DrawList &out, UiRect bounds, UiRect clipping,
                  std::string value, Color color, int pixels,
                  TextAlign align = TextAlign::Left,
                  FontFace face = FontFace::Interface) {
  const auto visible = intersection(bounds, clipping);
  if (!visible) return;
  const auto x = align == TextAlign::Left
                     ? bounds.x
                     : align == TextAlign::Center
                           ? bounds.x + bounds.width * .5f
                           : bounds.x + bounds.width;
  out.overlay.emplace_back(Text{{x, bounds.y}, std::move(value), color, pixels,
                                bounds.width, *visible, align, face});
}

[[nodiscard]] std::optional<std::pair<Point, Point>> clipped_line(
    Point from, Point to, UiRect clip) noexcept {
  const auto dx = to.x - from.x;
  const auto dy = to.y - from.y;
  const float p[] = {-dx, dx, -dy, dy};
  const float q[] = {from.x - clip.x, clip.x + clip.width - from.x,
                     from.y - clip.y, clip.y + clip.height - from.y};
  float start = 0.f;
  float end = 1.f;
  for (int index = 0; index < 4; ++index) {
    if (p[index] == 0.f) {
      if (q[index] < 0.f) return std::nullopt;
      continue;
    }
    const auto ratio = q[index] / p[index];
    if (p[index] < 0.f)
      start = std::max(start, ratio);
    else
      end = std::min(end, ratio);
    if (start > end) return std::nullopt;
  }
  return std::pair{Point{from.x + start * dx, from.y + start * dy},
                   Point{from.x + end * dx, from.y + end * dy}};
}

void clipped_stroke(DrawList &out, UiRect bounds, UiRect clip, Color color) {
  const Point top_left{bounds.x, bounds.y};
  const Point top_right{bounds.x + bounds.width, bounds.y};
  const Point bottom_left{bounds.x, bounds.y + bounds.height};
  const Point bottom_right{bounds.x + bounds.width,
                           bounds.y + bounds.height};
  for (const auto [from, to] :
       std::array{std::pair{top_left, top_right},
                  std::pair{top_right, bottom_right},
                  std::pair{bottom_right, bottom_left},
                  std::pair{bottom_left, top_left}}) {
    if (const auto segment = clipped_line(from, to, clip))
      out.overlay.emplace_back(Line{segment->first, segment->second, color});
  }
}

[[nodiscard]] std::string fixed(double value, int precision = 1) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(precision) << value;
  return out.str();
}

[[nodiscard]] std::string maturity(stellar::core::ResearchMaturity value) {
  using stellar::core::ResearchMaturity;
  switch (value) {
  case ResearchMaturity::rumored: return "RUMORED";
  case ResearchMaturity::hypothesized: return "HYPOTHESIZED";
  case ResearchMaturity::investigable: return "INVESTIGABLE";
  case ResearchMaturity::experimental: return "EXPERIMENTAL";
  case ResearchMaturity::demonstrated: return "DEMONSTRATED";
  case ResearchMaturity::engineering: return "ENGINEERING";
  case ResearchMaturity::mature: return "MATURE";
  case ResearchMaturity::archived: return "ARCHIVED";
  }
  return "KNOWN";
}

[[nodiscard]] std::string node_state(const NativeResearchNode &node) {
  if (node.active) return node.paused ? "PAUSED PROGRAM" : "ACTIVE PROGRAM";
  return maturity(node.maturity);
}

[[nodiscard]] std::string action_label(NativeResearchIntent intent) {
  switch (intent) {
  case NativeResearchIntent::Start: return "BEGIN RESEARCH";
  case NativeResearchIntent::Pause: return "PAUSE PROGRAM";
  case NativeResearchIntent::Resume: return "RESUME PROGRAM";
  default: return {};
  }
}

void erase_last_utf8(std::string &value) {
  if (value.empty()) return;
  auto index = value.size() - 1;
  while (index > 0 &&
         (static_cast<unsigned char>(value[index]) & 0xc0u) == 0x80u) {
    --index;
  }
  value.erase(index);
}

[[nodiscard]] std::string fold_ascii(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char character) {
    return character < 0x80u
               ? static_cast<char>(std::tolower(character))
               : static_cast<char>(character);
  });
  return value;
}

[[nodiscard]] bool matches_query(const NativeResearchNode &node,
                                 const NativeResearchQuery &query) {
  if (query.domain_id && node.domain_id != *query.domain_id) return false;
  if (query.search.empty()) return true;
  auto visible_text = node.display_name + " " + node.domain_label + " " +
                      node.solution_family;
  for (const auto &capability : node.known_capabilities)
    visible_text += " " + capability.display_name;
  return fold_ascii(std::move(visible_text)).contains(
      fold_ascii(query.search));
}

} // namespace

ResearchWorkspaceLayout ResearchWorkspaceLayout::for_viewport(
    int width, int height, std::size_t tab_count) {
  const auto screen_width = static_cast<float>(width);
  const auto screen_height = static_cast<float>(height);
  const auto scale = std::min(std::max(1.f, screen_height / 900.f),
                              std::max(1.f, screen_width / 900.f));
  const auto inset = 18.f * scale;
  const auto top = 64.f * scale;
  const auto search_width = std::clamp(screen_width * .28f, 180.f * scale,
                                       330.f * scale);
  const auto close_width = 88.f * scale;
  const auto gap = 8.f * scale;
  const auto tabs_y = top + 62.f * scale;
  const auto tab_gap = 5.f * scale;
  const auto available = screen_width - inset * 2.f;
  const auto columns = std::max<std::size_t>(
      1, static_cast<std::size_t>(available / (128.f * scale)));
  const auto rows = std::max<std::size_t>(
      1, (tab_count + columns - 1) / columns);
  const auto tab_width = (available - tab_gap * static_cast<float>(columns - 1)) /
                         static_cast<float>(columns);
  const auto tab_height = 34.f * scale;
  std::vector<ResearchTabLayout> tabs;
  tabs.reserve(tab_count);
  for (std::size_t index = 0; index < tab_count; ++index) {
    const auto column = index % columns;
    const auto row = index / columns;
    tabs.push_back({index,
                    {inset + static_cast<float>(column) * (tab_width + tab_gap),
                     tabs_y + static_cast<float>(row) * (tab_height + tab_gap),
                     tab_width, tab_height}});
  }
  const auto body_y = tabs_y + static_cast<float>(rows) *
                                   (tab_height + tab_gap) +
                      7.f * scale;
  const auto body_height = std::max(80.f * scale,
                                    screen_height - body_y - inset);
  const auto inspector_width = std::clamp(screen_width * .29f, 285.f * scale,
                                          390.f * scale);
  const auto graph_width = std::max(180.f * scale,
                                    available - inspector_width - gap);
  const UiRect inspector{inset + graph_width + gap, body_y, inspector_width,
                         body_height};
  const UiRect action{inspector.x + 12.f * scale,
                      inspector.y + inspector.height - 50.f * scale,
                      inspector.width - 24.f * scale, 38.f * scale};
  return {
      scale,
      static_cast<int>(std::lround(22.f * scale)),
      static_cast<int>(std::lround(14.f * scale)),
      static_cast<int>(std::lround(11.f * scale)),
      {0, top, screen_width, screen_height - top},
      {inset, top + 12.f * scale, 280.f * scale, 30.f * scale},
      {inset, top + 39.f * scale, 340.f * scale, 20.f * scale},
      {screen_width - inset - close_width - gap - search_width,
       top + 10.f * scale, search_width, 38.f * scale},
      {screen_width - inset - close_width, top + 10.f * scale, close_width,
       38.f * scale},
      std::move(tabs),
      {inset, body_y, graph_width, body_height},
      inspector,
      {action.x, action.y - 44.f * scale, action.width, 38.f * scale},
      action};
}

void NativeResearchWorkspace::open() {
  visible_ = true;
  refresh_requested_ = true;
}

void NativeResearchWorkspace::close() {
  visible_ = false;
  search_focused_ = false;
  dragging_ = false;
}

bool NativeResearchWorkspace::visible() const noexcept { return visible_; }

bool NativeResearchWorkspace::wants_text_input() const noexcept {
  return visible_ && search_focused_;
}

void NativeResearchWorkspace::set_window(NativeResearchWindow window) {
  const auto generation_changed =
      window_ && window_->campaign_generation != window.campaign_generation;
  if (generation_changed) {
    selected_node_id_.reset();
    query_.domain_id.reset();
    pan_ = {24.f, 30.f};
    notice_.clear();
    notice_accepted_ = false;
  }
  if (query_.domain_id) {
    const auto known = std::ranges::any_of(
        window.domain_tabs, [&](const auto &tab) { return tab.id == *query_.domain_id; });
    if (!known) query_.domain_id.reset();
  }
  if (window.selected_node_id) selected_node_id_ = window.selected_node_id;
  window_ = std::move(window);
  if (selected_node_id_) {
    const auto known = std::ranges::any_of(
        window_->nodes,
        [&](const auto &node) { return node.id == *selected_node_id_; });
    if (!known) selected_node_id_.reset();
  }
  if (!selected_node_id_ && !window_->nodes.empty()) {
    const auto preferred = std::ranges::find_if(window_->nodes, [](const auto &node) {
      return node.active || node.primary_action.enabled;
    });
    selected_node_id_ = preferred == window_->nodes.end()
                            ? window_->nodes.front().id
                            : preferred->id;
  }
  rebuild_topology();
  refresh_requested_ = false;
}

void NativeResearchWorkspace::discard_campaign() {
  window_.reset();
  selected_node_id_.reset();
  placements_.clear();
  domains_.clear();
  topology_signature_.clear();
  query_.domain_id.reset();
  pan_ = {24.f, 30.f};
  notice_.clear();
  notice_accepted_ = false;
  refresh_requested_ = visible_;
}

const NativeResearchQuery &NativeResearchWorkspace::query() const noexcept {
  return query_;
}

bool NativeResearchWorkspace::take_refresh_request() noexcept {
  return std::exchange(refresh_requested_, false);
}

const std::optional<NativeResearchWindow> &
NativeResearchWorkspace::window() const noexcept {
  return window_;
}

const std::optional<std::string> &
NativeResearchWorkspace::selected_id() const noexcept {
  return selected_node_id_;
}

void NativeResearchWorkspace::rebuild_topology() {
  std::string signature;
  if (window_) {
    for (const auto &node : window_->nodes) {
      if (!matches_query(node, query_)) continue;
      signature += node.id + ':' + node.domain_id + ':' +
                   std::to_string(node.graph_depth) + '|';
    }
  }
  if (signature == topology_signature_) {
    reconcile_selection();
    return;
  }
  topology_signature_ = std::move(signature);
  placements_.clear();
  domains_.clear();
  if (!window_) return;

  std::vector<const NativeResearchNode *> ordered;
  ordered.reserve(window_->nodes.size());
  for (const auto &node : window_->nodes)
    if (matches_query(node, query_)) ordered.push_back(&node);
  std::ranges::sort(ordered, [](const auto *left, const auto *right) {
    if (left->domain_id != right->domain_id)
      return left->domain_id < right->domain_id;
    if (left->graph_depth != right->graph_depth)
      return left->graph_depth < right->graph_depth;
    return left->id < right->id;
  });

  std::string domain;
  float domain_y = 42.f;
  int maximum_slot{};
  std::map<int, int> slots;
  for (const auto *node : ordered) {
    if (node->domain_id != domain) {
      if (!domain.empty()) domain_y += static_cast<float>(maximum_slot + 1) * 102.f + 48.f;
      domain = node->domain_id;
      slots.clear();
      maximum_slot = 0;
      domains_.push_back({node->domain_label, domain_y - 25.f});
    }
    const auto slot = slots[node->graph_depth]++;
    maximum_slot = std::max(maximum_slot, slot);
    placements_.push_back({node->id, node->domain_id,
                           {30.f + static_cast<float>(node->graph_depth) * 218.f,
                            domain_y + static_cast<float>(slot) * 102.f,
                            190.f, 82.f}});
  }
  reconcile_selection();
}

void NativeResearchWorkspace::reconcile_selection() {
  if (selected_node_id_ &&
      std::ranges::none_of(placements_, [&](const auto &placement) {
        return placement.id == *selected_node_id_;
      }))
    selected_node_id_.reset();
  if (!selected_node_id_ && !placements_.empty() && window_) {
    const auto actionable = std::ranges::find_if(placements_, [&](const auto &placement) {
      const auto node = std::ranges::find(window_->nodes, placement.id,
                                          &NativeResearchNode::id);
      return node != window_->nodes.end() &&
             (node->active || node->primary_action.enabled);
    });
    selected_node_id_ = actionable == placements_.end()
                            ? placements_.front().id
                            : actionable->id;
  }
}

void NativeResearchWorkspace::select(std::string node_id) {
  selected_node_id_ = std::move(node_id);
}

const NativeResearchNode *NativeResearchWorkspace::selected_node() const noexcept {
  if (!window_ || !selected_node_id_) return nullptr;
  const auto found = std::ranges::find(window_->nodes, *selected_node_id_,
                                       &NativeResearchNode::id);
  return found == window_->nodes.end() ? nullptr : &*found;
}

UiRect NativeResearchWorkspace::transformed_card(
    const NodePlacement &placement,
    const ResearchWorkspaceLayout &layout) const {
  return {layout.graph.x + pan_.x * layout.scale +
              placement.world_bounds.x * layout.scale,
          layout.graph.y + pan_.y * layout.scale +
              placement.world_bounds.y * layout.scale,
          placement.world_bounds.width * layout.scale,
          placement.world_bounds.height * layout.scale};
}

std::optional<UiRect> NativeResearchWorkspace::card_bounds(
    std::string_view node_id, int width, int height) const {
  if (!window_) return std::nullopt;
  const auto layout = ResearchWorkspaceLayout::for_viewport(
      width, height, window_->domain_tabs.size());
  const auto found = std::ranges::find(placements_, node_id, &NodePlacement::id);
  if (found == placements_.end()) return std::nullopt;
  return transformed_card(*found, layout);
}

std::optional<UiRect> NativeResearchWorkspace::first_actionable_card(
    int width, int height) const {
  if (!window_) return std::nullopt;
  const auto layout = ResearchWorkspaceLayout::for_viewport(
      width, height, window_->domain_tabs.size());
  for (const auto &placement : placements_) {
    const auto node = std::ranges::find(window_->nodes, placement.id,
                                        &NativeResearchNode::id);
    if (node == window_->nodes.end() || !node->primary_action.enabled) continue;
    const auto bounds = transformed_card(placement, layout);
    if (intersection(layout.graph, bounds)) return bounds;
  }
  return std::nullopt;
}

WorkspaceCommand NativeResearchWorkspace::handle(const InputEvent &event,
                                                  int width, int height) {
  if (!visible_) return {};
  pointer_ = event.position;
  const auto tab_count = window_ ? window_->domain_tabs.size() : 0;
  const auto layout = ResearchWorkspaceLayout::for_viewport(width, height,
                                                             tab_count);
  WorkspaceCommand result;
  result.captured = layout.surface.contains(event.position) || dragging_ ||
                    event.type == InputEventType::TextEntered ||
                    event.type == InputEventType::BackspacePressed;

  if (event.type == InputEventType::PointerCancelled) {
    dragging_ = false;
    return result;
  }
  if (event.type == InputEventType::TextEntered && search_focused_) {
    if (query_.search.size() + event.text.size() <= 256) {
      query_.search += event.text;
      rebuild_topology();
    }
    return result;
  }
  if (event.type == InputEventType::BackspacePressed && search_focused_) {
    erase_last_utf8(query_.search);
    rebuild_topology();
    return result;
  }
  if (event.type == InputEventType::PointerMove && dragging_) {
    pan_.x += event.delta.x / layout.scale;
    pan_.y += event.delta.y / layout.scale;
    return result;
  }
  if (event.type == InputEventType::LeftReleased) {
    dragging_ = false;
    return result;
  }
  if (event.type == InputEventType::Wheel && layout.graph.contains(event.position)) {
    pan_.y += event.wheel_y * 52.f;
    return result;
  }
  if (event.type != InputEventType::LeftPressed) return result;

  if (layout.close.contains(event.position)) {
    close();
    return {WorkspaceCommandKind::Close, true};
  }
  if (layout.search.contains(event.position)) {
    search_focused_ = true;
    return result;
  }
  search_focused_ = false;
  if (window_) {
    for (const auto &tab : layout.tabs) {
      if (!tab.bounds.contains(event.position)) continue;
      const auto &domain = window_->domain_tabs.at(tab.index).id;
      query_.domain_id = domain.empty() ? std::nullopt
                                        : std::optional<std::string>(domain);
      selected_node_id_.reset();
      rebuild_topology();
      return result;
    }
    if (layout.action.contains(event.position)) {
      if (const auto *node = selected_node()) {
        if (node->primary_action.enabled &&
            node->primary_action.intent != NativeResearchIntent::None) {
          return {WorkspaceCommandKind::Execute, true, node->id,
                  node->primary_action.intent};
        }
        if (!node->primary_action.reason.empty())
          set_notice(node->primary_action.reason, false);
      }
      return result;
    }
    for (const auto &placement : placements_) {
      const auto bounds = transformed_card(placement, layout);
      const auto clipped = intersection(layout.graph, bounds);
      if (!clipped || !clipped->contains(event.position))
        continue;
      select(placement.id);
      return {WorkspaceCommandKind::Select, true, placement.id};
    }
  }
  if (layout.graph.contains(event.position)) dragging_ = true;
  return result;
}

void NativeResearchWorkspace::set_notice(std::string message, bool accepted) {
  notice_ = std::move(message);
  notice_accepted_ = accepted;
}

void NativeResearchWorkspace::render(DrawList &out, int width,
                                     int height) const {
  if (!visible_) return;
  const auto tab_count = window_ ? window_->domain_tabs.size() : 0;
  const auto layout = ResearchWorkspaceLayout::for_viewport(width, height,
                                                             tab_count);
  fill(out, layout.surface, background);
  fill(out, {layout.surface.x, layout.surface.y, 4.f, layout.surface.height},
       native_ui::color::science);
  text(out, layout.title, "RESEARCH NETWORK", bright,
       layout.title_font_pixels, TextAlign::Left, FontFace::Heading);
  if (window_) {
    auto summary = fixed(window_->free_effective_labs) + " / " +
                   fixed(window_->total_effective_labs) +
                   " effective labs free";
    if (window_->formatted_treasury)
      summary = *window_->formatted_treasury + " treasury  |  " + summary;
    text(out, layout.labs,
         std::move(summary),
         muted, layout.small_font_pixels);
  }
  fill(out, layout.search, raised);
  stroke(out, layout.search, search_focused_ ? positive : border);
  if (search_focused_)
    fill(out, {layout.search.x, layout.search.y, 3.f, layout.search.height},
         positive);
  text(out,
       {layout.search.x + 10.f * layout.scale,
        layout.search.y + 9.f * layout.scale,
        layout.search.width - 20.f * layout.scale,
        layout.search.height - 10.f * layout.scale},
       query_.search.empty() ? "Search known research" : query_.search,
       query_.search.empty() ? muted : bright, layout.body_font_pixels);
  native_ui::button(out, layout.close, "CLOSE", pointer_,
                    layout.body_font_pixels);

  if (window_) {
    for (const auto &tab : layout.tabs) {
      const auto &domain = window_->domain_tabs.at(tab.index);
      const auto active = domain.id.empty() ? !query_.domain_id
                                            : query_.domain_id == domain.id;
      native_ui::button(
          out, tab.bounds,
          domain.label + " " + std::to_string(domain.known_node_count),
          pointer_, layout.small_font_pixels, native_ui::Tone::Science,
          active);
    }
  }

  native_ui::panel(out, layout.graph, native_ui::Tone::Science);
  native_ui::panel(out, layout.inspector, native_ui::Tone::Science);

  if (!window_) {
    text(out,
         {layout.graph.x + 24.f * layout.scale,
          layout.graph.y + 24.f * layout.scale,
          layout.graph.width - 48.f * layout.scale, 40.f * layout.scale},
         "Building the known research view...", muted,
         layout.body_font_pixels);
    return;
  }
  if (placements_.empty()) {
    text(out,
         {layout.graph.x + 24.f * layout.scale,
          layout.graph.y + 24.f * layout.scale,
          layout.graph.width - 48.f * layout.scale, 60.f * layout.scale},
         "No known research matches this view.", muted,
         layout.body_font_pixels);
  }

  struct VisibleCard {
    UiRect bounds;
    UiRect clipping;
  };
  std::unordered_map<std::string, VisibleCard> visible_cards;
  for (const auto &placement : placements_) {
    const auto bounds = transformed_card(placement, layout);
    if (const auto clipped = intersection(layout.graph, bounds))
      visible_cards.emplace(placement.id, VisibleCard{bounds, *clipped});
  }
  for (const auto &edge : window_->edges) {
    const auto from = visible_cards.find(edge.from_id);
    const auto to = visible_cards.find(edge.to_id);
    if (from == visible_cards.end() || to == visible_cards.end()) continue;
    const Point edge_from{from->second.bounds.x + from->second.bounds.width,
                          from->second.bounds.y +
                              from->second.bounds.height * .5f};
    const Point edge_to{to->second.bounds.x,
                        to->second.bounds.y +
                            to->second.bounds.height * .5f};
    if (const auto segment = clipped_line(edge_from, edge_to, layout.graph))
      out.overlay.emplace_back(
          Line{segment->first, segment->second, {56, 104, 145, 190}});
  }
  for (const auto &domain : domains_) {
    const auto y = layout.graph.y +
                   (pan_.y + domain.world_y) * layout.scale;
    clipped_text(out,
                 {layout.graph.x + 14.f * layout.scale, y,
                  layout.graph.width - 28.f * layout.scale,
                  20.f * layout.scale},
                 layout.graph, domain.label, muted,
                 layout.small_font_pixels);
  }
  for (const auto &node : window_->nodes) {
    const auto visible = visible_cards.find(node.id);
    if (visible == visible_cards.end()) continue;
    const auto bounds = visible->second.bounds;
    const auto clipping = visible->second.clipping;
    const auto chosen = selected_node_id_ == node.id;
    fill(out, clipping,
         chosen ? selected
                : clipping.contains(pointer_) ? hover : raised);
    if (chosen)
      fill(out, {clipping.x, clipping.y, 3.f, clipping.height}, positive);
    clipped_stroke(out, bounds, layout.graph, chosen ? positive : border);
    clipped_text(out,
                 {bounds.x + 9.f * layout.scale,
                  bounds.y + 8.f * layout.scale,
                  bounds.width - 18.f * layout.scale, 42.f * layout.scale},
                 clipping, node.display_name, bright,
                 layout.body_font_pixels);
    clipped_text(out,
                 {bounds.x + 9.f * layout.scale,
                  bounds.y + 55.f * layout.scale,
                  bounds.width - 18.f * layout.scale, 17.f * layout.scale},
                 clipping, node_state(node), node.active ? positive : muted,
                 layout.small_font_pixels);
    const UiRect progress_bounds{
        bounds.x + 8.f * layout.scale,
        bounds.y + bounds.height - 7.f * layout.scale,
        bounds.width - 16.f * layout.scale, 3.f * layout.scale};
    if (const auto progress_clip = intersection(progress_bounds, layout.graph))
      native_ui::progress(out, *progress_clip, node.total_progress,
                          native_ui::Tone::Science);
  }

  const auto inspector_x = layout.inspector.x + 14.f * layout.scale;
  const auto inspector_width = layout.inspector.width - 28.f * layout.scale;
  auto inspector_y = layout.inspector.y + 14.f * layout.scale;
  native_ui::section_header(
      out, {inspector_x, inspector_y, inspector_width, 22.f * layout.scale},
      "PROGRAM INSPECTOR", layout.small_font_pixels,
      native_ui::Tone::Science);
  inspector_y += 30.f * layout.scale;
  const auto *node = selected_node();
  if (!node) {
    text(out, {inspector_x, inspector_y, inspector_width, 60.f * layout.scale},
         "Select a known program to inspect its current details.", muted,
         layout.body_font_pixels);
    return;
  }
  text(out, {inspector_x, inspector_y, inspector_width, 56.f * layout.scale},
       node->display_name, bright, layout.title_font_pixels);
  inspector_y += 57.f * layout.scale;
  text(out, {inspector_x, inspector_y, inspector_width, 22.f * layout.scale},
       node->domain_label + "  |  " + node_state(*node),
       node->active ? positive : muted, layout.small_font_pixels);
  inspector_y += 27.f * layout.scale;
  native_ui::progress(
      out, {inspector_x, inspector_y, inspector_width, 7.f * layout.scale},
      node->total_progress, native_ui::Tone::Science);
  inspector_y += 17.f * layout.scale;
  auto progress_text =
      "Progress " + fixed(node->total_progress * 100., 0) + "%";
  if (node->active)
    progress_text += "  |  " + fixed(node->assigned_effective_labs) + " labs";
  else if (node->cost)
    progress_text += "  |  Planned staffing " +
                     fixed(node->cost->assigned_effective_labs) + " labs";
  text(out, {inspector_x, inspector_y, inspector_width, 20.f * layout.scale},
       std::move(progress_text), muted, layout.small_font_pixels);
  inspector_y += 28.f * layout.scale;

  if (node->cost) {
    const auto &cost = *node->cost;
    auto cost_text =
        "COST & TIME\nAuthorization " + cost.formatted_authorization +
        "\nMilestones " + cost.formatted_milestone_commitment +
        "\nOperations " + cost.formatted_operating_cost_rate +
        "\nEstimated total " + cost.formatted_estimated_total +
        "\nReserve to start " + cost.formatted_credits_needed_to_start + "\n";
    cost_text += std::isfinite(cost.estimated_years_at_full_funding)
                     ? "At full funding " +
                           fixed(cost.estimated_years_at_full_funding, 2) +
                           " years"
                     : "Staffed duration unavailable";
    text(out, {inspector_x, inspector_y, inspector_width, 132.f * layout.scale},
         cost_text, bright, layout.small_font_pixels);
    inspector_y += 138.f * layout.scale;
  }
  const auto details_end=layout.feedback.y-6.f*layout.scale;
  if (!node->known_capabilities.empty() && inspector_y<details_end) {
    std::string capabilities = "KNOWN CAPABILITIES\n";
    for (const auto &capability : node->known_capabilities)
      capabilities += capability.display_name + "\n";
    const auto available=details_end-inspector_y;
    const auto capability_height=std::min(
        72.f*layout.scale,node->blockers.empty()?available:available*.5f);
    if(capability_height>0.f){
      text(out,{inspector_x,inspector_y,inspector_width,capability_height},
           capabilities,bright,layout.small_font_pixels);
      inspector_y+=capability_height+6.f*layout.scale;
    }
  }
  if (!node->blockers.empty() && inspector_y<details_end) {
    std::string blockers = "REQUIREMENTS / STATUS\n";
    for (const auto &blocker : node->blockers) blockers += blocker + "\n";
    text(out,{inspector_x,inspector_y,inspector_width,
              details_end-inspector_y},blockers,warning,
         layout.small_font_pixels);
  }
  const auto action_text = action_label(node->primary_action.intent);
  if (!action_text.empty()) {
    const auto enabled = node->primary_action.enabled;
    native_ui::button(out, layout.action, action_text, pointer_,
                      layout.body_font_pixels, native_ui::Tone::Science,
                      false, enabled);
  }
  const auto feedback_text=!notice_.empty()
                               ?notice_
                               :!node->primary_action.enabled
                                    ?node->primary_action.reason
                                    :std::string{};
  if(!feedback_text.empty())
    text(out,layout.feedback,feedback_text,
         !notice_.empty()?(notice_accepted_?positive:failure):warning,
         layout.small_font_pixels);
  for (const auto &candidate : window_->nodes) {
    const auto placement = std::ranges::find(placements_, candidate.id,
                                             &NodePlacement::id);
    if (placement == placements_.end()) continue;
    const auto bounds = transformed_card(*placement, layout);
    if (!layout.graph.contains(pointer_) || !bounds.contains(pointer_)) continue;
    native_ui::tooltip(
        out, {pointer_.x + 12.f * layout.scale,
              pointer_.y + 12.f * layout.scale},
        candidate.display_name,
        candidate.domain_label + " · " + node_state(candidate) +
            ". Select for costs, staffing, capabilities and requirements.",
        width, height, layout.scale, native_ui::Tone::Science);
    break;
  }
}

} // namespace stellar::native_research_ui
