#include <stellar/engine/text_fit.hpp>
#include "native_campaign_calendar.hpp"
#include "native_research_workspace.hpp"
#include "native_ui_layout.hpp"
#include "native_research_presentation.hpp"
#include "native_ui_style.hpp"
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
namespace theme = stellar::native_ui;

constexpr Color raised = theme::color::surface_secondary;
constexpr Color hover = theme::color::surface_hover;
constexpr Color selected = theme::color::surface_raised;
constexpr Color border = theme::color::keyline_strong;
constexpr Color bright = theme::color::text_primary;
constexpr Color muted = theme::color::text_secondary;
constexpr Color positive = theme::color::success;
constexpr Color warning = theme::color::caution;
constexpr Color failure = theme::color::danger;

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
  if (right_edge <= x || bottom <= y)
    return std::nullopt;
  return UiRect{x, y, right_edge - x, bottom - y};
}

void fill(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(FilledRectangle{bounds, color});
}

UiRect research_art_source(const RgbaImage &image,UiRect destination){
  const float w=static_cast<float>(image.width()),h=static_cast<float>(image.height());
  const float aspect=destination.width/std::max(1.f,destination.height);
  if(w/h>aspect){const float crop=h*aspect;return {(w-crop)*.5f,0,crop,h};}
  const float crop=w/aspect;return {0,(h-crop)*.5f,w,crop};
}

void stroke(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(StrokedRectangle{bounds, color});
}

void text(DrawList &out, UiRect bounds, std::string value, Color color,
          int pixels, TextAlign align = TextAlign::Left,
          FontFace face = FontFace::Interface) {
  const auto x = align == TextAlign::Left     ? bounds.x
                 : align == TextAlign::Center ? bounds.x + bounds.width * .5f
                                              : bounds.x + bounds.width;
  out.overlay.emplace_back(Text{{x, bounds.y},
                                std::move(value),
                                color,
                                pixels,
                                bounds.width,
                                bounds,
                                align,
                                face});
}

void clipped_text(DrawList &out, UiRect bounds, UiRect clipping,
                  std::string value, Color color, int pixels,
                  TextAlign align = TextAlign::Left,
                  FontFace face = FontFace::Interface) {
  const auto visible = intersection(bounds, clipping);
  if (!visible)
    return;
  const auto x = align == TextAlign::Left     ? bounds.x
                 : align == TextAlign::Center ? bounds.x + bounds.width * .5f
                                              : bounds.x + bounds.width;
  out.overlay.emplace_back(Text{{x, bounds.y},
                                std::move(value),
                                color,
                                pixels,
                                bounds.width,
                                *visible,
                                align,
                                face});
}

[[nodiscard]] std::optional<std::pair<Point, Point>>
clipped_line(Point from, Point to, UiRect clip) noexcept {
  const auto dx = to.x - from.x;
  const auto dy = to.y - from.y;
  const float p[] = {-dx, dx, -dy, dy};
  const float q[] = {from.x - clip.x, clip.x + clip.width - from.x,
                     from.y - clip.y, clip.y + clip.height - from.y};
  float start = 0.f;
  float end = 1.f;
  for (int index = 0; index < 4; ++index) {
    if (p[index] == 0.f) {
      if (q[index] < 0.f)
        return std::nullopt;
      continue;
    }
    const auto ratio = q[index] / p[index];
    if (p[index] < 0.f)
      start = std::max(start, ratio);
    else
      end = std::min(end, ratio);
    if (start > end)
      return std::nullopt;
  }
  return std::pair{Point{from.x + start * dx, from.y + start * dy},
                   Point{from.x + end * dx, from.y + end * dy}};
}

void clipped_stroke(DrawList &out, UiRect bounds, UiRect clip, Color color) {
  const Point top_left{bounds.x, bounds.y};
  const Point top_right{bounds.x + bounds.width, bounds.y};
  const Point bottom_left{bounds.x, bounds.y + bounds.height};
  const Point bottom_right{bounds.x + bounds.width, bounds.y + bounds.height};
  for (const auto [from, to] : std::array{std::pair{top_left, top_right},
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
  case ResearchMaturity::rumored:
    return "RUMORED";
  case ResearchMaturity::hypothesized:
    return "HYPOTHESIZED";
  case ResearchMaturity::investigable:
    return "INVESTIGABLE";
  case ResearchMaturity::experimental:
    return "EXPERIMENTAL";
  case ResearchMaturity::demonstrated:
    return "DEMONSTRATED";
  case ResearchMaturity::engineering:
    return "ENGINEERING";
  case ResearchMaturity::mature:
    return "MATURE";
  case ResearchMaturity::archived:
    return "ARCHIVED";
  }
  return "KNOWN";
}

[[nodiscard]] const char *maturity_key(stellar::core::ResearchMaturity value) {
  using stellar::core::ResearchMaturity;
  switch (value) {
  case ResearchMaturity::rumored:
    return "RESEARCH_MATURITY_RUMORED";
  case ResearchMaturity::hypothesized:
    return "RESEARCH_MATURITY_HYPOTHESIZED";
  case ResearchMaturity::investigable:
    return "RESEARCH_MATURITY_INVESTIGABLE";
  case ResearchMaturity::experimental:
    return "RESEARCH_MATURITY_EXPERIMENTAL";
  case ResearchMaturity::demonstrated:
    return "RESEARCH_MATURITY_DEMONSTRATED";
  case ResearchMaturity::engineering:
    return "RESEARCH_MATURITY_ENGINEERING";
  case ResearchMaturity::mature:
    return "RESEARCH_MATURITY_MATURE";
  case ResearchMaturity::archived:
    return "RESEARCH_MATURITY_ARCHIVED";
  }
  return "RESEARCH_MATURITY_KNOWN";
}

[[nodiscard]] std::string node_state(const NativeResearchNode &node) {
  if (node.cancelled) return "CANCELLED · WORK RETAINED";
  if (node.active)
    return node.paused ? "PAUSED PROGRAM" : "ACTIVE PROGRAM";
  return maturity(node.maturity);
}

[[nodiscard]] const char *node_state_key(const NativeResearchNode &node) {
  if (node.cancelled) return "RESEARCH_STATE_CANCELLED";
  if (node.active)
    return node.paused ? "RESEARCH_STATE_PAUSED" : "RESEARCH_STATE_ACTIVE";
  return maturity_key(node.maturity);
}

[[nodiscard]] std::string action_label(NativeResearchIntent intent) {
  switch (intent) {
  case NativeResearchIntent::Start:
    return "BEGIN RESEARCH";
  case NativeResearchIntent::Pause:
    return "PAUSE PROGRAM";
  case NativeResearchIntent::Resume:
    return "RESUME PROGRAM";
  default:
    return {};
  }
}

void erase_last_utf8(std::string &value) {
  if (value.empty())
    return;
  auto index = value.size() - 1;
  while (index > 0 &&
         (static_cast<unsigned char>(value[index]) & 0xc0u) == 0x80u) {
    --index;
  }
  value.erase(index);
}

[[nodiscard]] std::string fold_ascii(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char character) {
    return character < 0x80u ? static_cast<char>(std::tolower(character))
                             : static_cast<char>(character);
  });
  return value;
}

[[nodiscard]] const char *action_key(NativeResearchIntent intent) {
  switch (intent) {
  case NativeResearchIntent::Start:
    return "RESEARCH_ACTION_BEGIN";
  case NativeResearchIntent::Pause:
    return "RESEARCH_ACTION_PAUSE";
  case NativeResearchIntent::Resume:
    return "RESEARCH_ACTION_RESUME";
  default:
    return "";
  }
}

[[nodiscard]] bool matches_query(const NativeResearchNode &node,
                                 const NativeResearchQuery &query) {
  if (query.domain_id && !research_category_matches(*query.domain_id, node.domain_id))
    return false;
  if (query.search.empty())
    return true;
  auto visible_text =
      node.display_name + " " + node.domain_label + " " + node.solution_family +
      " " + node.purpose + " " + node.benefits;
  for (const auto &capability : node.known_capabilities)
    visible_text += " " + capability.display_name;
  return fold_ascii(std::move(visible_text)).contains(fold_ascii(query.search));
}

} // namespace

ResearchWorkspaceLayout
ResearchWorkspaceLayout::for_viewport(int width, int height,
                                      std::size_t tab_count) {
  ResearchWorkspaceLayout l;const float s=std::clamp(height/1080.f,.8f,2.f),g=10*s;
  l.scale=s;l.title_font_pixels=static_cast<int>(24*s);l.body_font_pixels=std::max(13,static_cast<int>(16*s));l.small_font_pixels=std::max(12,static_cast<int>(14*s));
  const auto chrome=NativeUiLayout::for_viewport(width,height);const float x=native_navigation_content_left*chrome.scale,top=native_workspace_top(width,height),right=width-12*s;
  l.surface={x,top,right-x,height-top-10*s};l.title={x,top,350*s,32*s};l.labs={x,top+36*s,right-x-115*s,25*s};
  l.close={right-80*s,top,80*s,34*s};
  const float body=top+124*s,sidebar_width=195*s,inspector_width=350*s;
  l.sidebar={x,top+78*s,sidebar_width,height-top-88*s};
  const float gx=x+sidebar_width+g,iw=right-inspector_width,gwidth=iw-g-gx;
  l.view_tabs={gx,top+74*s,gwidth*.55f,38*s};
  l.search={gx+gwidth*.55f+g,top+74*s,gwidth*.45f-g,38*s};
  l.inspector={iw,body,inspector_width,height-body-10*s};
  l.active={gx,height-138*s,gwidth,128*s};
  l.graph={gx,body,gwidth,l.active.y-body-g};
  l.toolbar={gx,body,gwidth,38*s};l.filter={gx+gwidth-150*s,body+6*s,144*s,30*s};l.sort={gx+gwidth-300*s,body+6*s,144*s,30*s};
  const float tab_h=std::min(50*s,(l.sidebar.height-94*s)/std::max<std::size_t>(1,tab_count));
  for(std::size_t i=0;i<tab_count;++i)l.tabs.push_back({i,{x+6*s,l.sidebar.y+6*s+i*tab_h,sidebar_width-12*s,tab_h-4*s}});
  l.completed={x+6*s,l.sidebar.y+l.sidebar.height-84*s,sidebar_width-12*s,36*s};l.queue={x+6*s,l.sidebar.y+l.sidebar.height-42*s,sidebar_width-12*s,36*s};
  l.action={iw+12*s,height-106*s,inspector_width-24*s,38*s};
  l.bookmark={iw+12*s,height-58*s,(inspector_width-32*s)*.5f,36*s};l.enqueue={l.bookmark.x+l.bookmark.width+8*s,l.bookmark.y,l.bookmark.width,36*s};
  l.feedback={iw+12*s,l.action.y-36*s,inspector_width-24*s,30*s};l.tree_focus={iw+12*s,body+34*s,inspector_width-24*s,20*s};return l;
}

std::string NativeResearchWorkspace::tr(std::string_view key,
                                        std::string_view fallback) const {
  if (locale_ && locale_->contains(key))
    return std::string(locale_->translate(key));
  return std::string(fallback);
}

std::string NativeResearchWorkspace::trf(
    std::string_view key, std::initializer_list<std::string> args,
    std::string_view fallback) const {
  if (locale_ && locale_->contains(key)) {
    const std::vector<std::string> values(args.begin(), args.end());
    return locale_->format(key, std::span<const std::string>(values));
  }
  std::string out{fallback};
  std::size_t index = 0;
  for (const auto &arg : args) {
    const std::string marker = "{" + std::to_string(index++) + "}";
    if (const auto at = out.find(marker); at != std::string::npos)
      out.replace(at, marker.size(), arg);
  }
  return out;
}

void NativeResearchWorkspace::open() {
  visible_ = true;
  refresh_requested_ = true;
  center_selection_ = true;
  inspector_scroll_ = {};
  
  focus_ = -1;
}

void NativeResearchWorkspace::close() {
  visible_ = false;
  search_focused_ = false;
  dragging_ = false;
  dropdown_.close();
  inspector_scroll_ = {};
  
  focus_ = -1;
}

bool NativeResearchWorkspace::visible() const noexcept { return visible_; }

void NativeResearchWorkspace::set_text_measurer(TextMeasurer measure) {
  text_measurer_ = std::move(measure);
  inspector_scroll_ = {};
  
}

void NativeResearchWorkspace::set_artwork_resolver(ArtworkResolver resolve) {
  artwork_resolver_ = std::move(resolve);
}

bool NativeResearchWorkspace::wants_text_input() const noexcept {
  return visible_ && search_focused_;
}

void NativeResearchWorkspace::set_window(NativeResearchWindow window) {
  plan_=window.plan;
  const auto prior_selection = selected_node_id_;
  const auto generation_changed =
      window_ && window_->campaign_generation != window.campaign_generation;
  if (generation_changed) {
    selected_node_id_.reset();
    query_.domain_id.reset();
    pan_ = {24.f, 30.f};
    zoom_ = 1.f;
    notice_.clear();
    notice_accepted_ = false;
    inspector_scroll_ = {};
    focus_ = -1;
  }
  if (query_.domain_id) {
    const auto known =
        std::ranges::any_of(window.domain_tabs, [&](const auto &tab) {
          return tab.id == *query_.domain_id;
        });
    if (!known)
      query_.domain_id.reset();
  }
  if (window.selected_node_id)
    selected_node_id_ = window.selected_node_id;
  window_ = std::move(window);
  if (selected_node_id_) {
    const auto known =
        std::ranges::any_of(window_->nodes, [&](const auto &node) {
          return node.id == *selected_node_id_;
        });
    if (!known)
      selected_node_id_.reset();
  }
  if (!selected_node_id_ && !window_->nodes.empty()) {
    const auto preferred =
        std::ranges::find_if(window_->nodes, [](const auto &node) {
          return node.active || node.primary_action.enabled;
        });
    selected_node_id_ = preferred == window_->nodes.end()
                            ? window_->nodes.front().id
                            : preferred->id;
  }
  rebuild_topology();
  if (generation_changed || prior_selection != selected_node_id_) {
    center_selection_ = true;
    inspector_scroll_ = {};
    
  }
  refresh_requested_ = false;
}

void NativeResearchWorkspace::discard_campaign() {
  plan_={};dropdown_.close();why_open_=false;
  window_.reset();
  selected_node_id_.reset();
  placements_.clear();
  domains_.clear();
  topology_signature_.clear();
  query_.domain_id.reset();
  pan_ = {24.f, 30.f};
  zoom_ = 1.f;
  notice_.clear();
  notice_accepted_ = false;
  inspector_scroll_ = {};
  
  inspector_viewport_width_ = 0;
  inspector_viewport_height_ = 0;
  focus_ = -1;
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
      if (!matches_query(node, query_))
        continue;
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
  if (!window_)
    return;

  std::vector<const NativeResearchNode *> ordered;
  ordered.reserve(window_->nodes.size());
  for (const auto &node : window_->nodes)
    if (matches_query(node, query_))
      ordered.push_back(&node);
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
      if (!domain.empty())
        domain_y += static_cast<float>(maximum_slot + 1) * 150.f + 48.f;
      domain = node->domain_id;
      slots.clear();
      maximum_slot = 0;
      domains_.push_back({node->domain_label, domain_y - 25.f});
    }
    const auto slot = slots[node->graph_depth]++;
    maximum_slot = std::max(maximum_slot, slot);
    placements_.push_back(
        {node->id,
         node->domain_id,
         {30.f + static_cast<float>(node->graph_depth) * 286.f,
          domain_y + static_cast<float>(slot) * 150.f, 260.f, 128.f}});
  }
  reconcile_selection();
}

void NativeResearchWorkspace::reconcile_selection() {
  const auto prior_selection = selected_node_id_;
  if (selected_node_id_ &&
      std::ranges::none_of(placements_, [&](const auto &placement) {
        return placement.id == *selected_node_id_;
      }))
    selected_node_id_.reset();
  if (!selected_node_id_ && !placements_.empty() && window_) {
    const auto actionable =
        std::ranges::find_if(placements_, [&](const auto &placement) {
          const auto node = std::ranges::find(window_->nodes, placement.id,
                                              &NativeResearchNode::id);
          return node != window_->nodes.end() &&
                 (node->active || node->primary_action.enabled);
        });
    selected_node_id_ = actionable == placements_.end() ? placements_.front().id
                                                        : actionable->id;
  }
  if (prior_selection != selected_node_id_) {
    inspector_scroll_ = {};
    
  }
}

void NativeResearchWorkspace::select(std::string node_id) {
  selected_node_id_ = std::move(node_id);
  inspector_scroll_ = {};
  
}

const NativeResearchNode *
NativeResearchWorkspace::selected_node() const noexcept {
  if (!window_ || !selected_node_id_)
    return nullptr;
  const auto found = std::ranges::find(window_->nodes, *selected_node_id_,
                                       &NativeResearchNode::id);
  return found == window_->nodes.end() ? nullptr : &*found;
}

UiRect NativeResearchWorkspace::transformed_card(
    const NodePlacement &placement,
    const ResearchWorkspaceLayout &layout) const {
  return {layout.graph.x + pan_.x * layout.scale +
              placement.world_bounds.x * layout.scale * zoom_,
          layout.graph.y + pan_.y * layout.scale +
              placement.world_bounds.y * layout.scale * zoom_,
          placement.world_bounds.width * layout.scale * zoom_,
          placement.world_bounds.height * layout.scale * zoom_};
}

std::optional<UiRect>
NativeResearchWorkspace::card_bounds(std::string_view node_id, int width,
                                     int height) const {
  if (!window_)
    return std::nullopt;
  const auto layout = ResearchWorkspaceLayout::for_viewport(
      width, height, window_->domain_tabs.size());
  if(mode_!=ResearchViewMode::Tree){for(const auto& card:guided_cards(layout))if(card.id==node_id)return card.bounds;return {};}
  const auto found =
      std::ranges::find(placements_, node_id, &NodePlacement::id);
  if (found == placements_.end())
    return std::nullopt;
  return transformed_card(*found, layout);
}

std::optional<UiRect>
NativeResearchWorkspace::first_actionable_card(int width, int height) const {
  if (!window_)
    return std::nullopt;
  const auto layout = ResearchWorkspaceLayout::for_viewport(
      width, height, window_->domain_tabs.size());
  if(mode_!=ResearchViewMode::Tree){for(const auto& card:guided_cards(layout)){const auto n=std::ranges::find(window_->nodes,card.id,&NativeResearchNode::id);if(n!=window_->nodes.end()&&n->primary_action.enabled&&intersection(card.bounds,layout.graph))return card.bounds;}return {};}
  for (const auto &placement : placements_) {
    const auto node = std::ranges::find(window_->nodes, placement.id,
                                        &NativeResearchNode::id);
    if (node == window_->nodes.end() || !node->primary_action.enabled)
      continue;
    const auto bounds = transformed_card(placement, layout);
    if (intersection(layout.graph, bounds))
      return bounds;
  }
  return std::nullopt;
}

std::vector<NativeResearchWorkspace::FocusRect>
NativeResearchWorkspace::focusables(
    const ResearchWorkspaceLayout &l) const {
  std::vector<FocusRect> out;
  const auto node_label=[&](const std::string &id){
    if(!window_)return id;
    const auto found=std::ranges::find(window_->nodes,id,&NativeResearchNode::id);
    return found!=window_->nodes.end()?found->display_name:id;
  };
  out.push_back({l.close, tr("RESEARCH_CLOSE", "Close research")});
  out.push_back({l.search, tr("RESEARCH_SEARCH", "Search research")});
  for (const auto &tab : l.tabs)
    out.push_back({tab.bounds,
                   window_ && tab.index < window_->domain_tabs.size()
                       ? window_->domain_tabs[tab.index].label
                       : std::string{}});
  for (const auto &hit : interface_hits_)
    out.push_back({hit.bounds, hit.label});
  if (window_) {
    if (mode_ != ResearchViewMode::Tree) {
      const UiRect content{l.graph.x, l.graph.y + 80.f * l.scale,
                           l.graph.width, l.graph.height - 80.f * l.scale};
      for (const auto &card : guided_cards(l))
        if (const auto clip = intersection(card.bounds, content))
          out.push_back({*clip, node_label(card.id), card.bounds,
                         /*scroll_lane=*/1});
    } else {
      for (const auto &placement : placements_)
        if (const auto clip =
                intersection(l.graph, transformed_card(placement, l)))
          out.push_back({*clip, node_label(placement.id),
                         transformed_card(placement, l), /*scroll_lane=*/2});
    }
    if (const auto *node = selected_node()) {
      const auto intent = node->primary_action.intent;
      out.push_back({l.action,
                     node->cancelled && intent == NativeResearchIntent::Start
                         ? tr("RESEARCH_ACTION_RESTART", "Restart research")
                         : tr(action_key(intent), action_label(intent))});
    }
  }
  std::ranges::sort(out, [](const FocusRect &a, const FocusRect &b) {
    if (a.bounds.y != b.bounds.y)
      return a.bounds.y < b.bounds.y;
    return a.bounds.x < b.bounds.x;
  });
  return out;
}

std::string NativeResearchWorkspace::focused_label(int width,
                                                   int height) const {
  if (focus_ < 0) return {};
  const auto items = focusables(ResearchWorkspaceLayout::for_viewport(
      width, height, window_ ? window_->domain_tabs.size() : 0));
  return focus_ < static_cast<int>(items.size())
             ? items[static_cast<std::size_t>(focus_)].label
             : std::string{};
}

std::optional<stellar::native_map::UiRect>
NativeResearchWorkspace::focused_bounds(int width, int height) const {
  if (focus_ < 0) return std::nullopt;
  const auto items = focusables(ResearchWorkspaceLayout::for_viewport(
      width, height, window_ ? window_->domain_tabs.size() : 0));
  return focus_ < static_cast<int>(items.size())
             ? std::optional<stellar::native_map::UiRect>{
                   items[static_cast<std::size_t>(focus_)].bounds}
             : std::nullopt;
}

stellar::engine::AnnouncementControl
NativeResearchWorkspace::focused_control(int width, int height) const {
  const auto bounds = focused_bounds(width, height);
  const auto search = ResearchWorkspaceLayout::for_viewport(
                          width, height,
                          window_ ? window_->domain_tabs.size() : 0)
                          .search;
  return bounds && bounds->x == search.x && bounds->y == search.y &&
                 bounds->width == search.width &&
                 bounds->height == search.height
             ? stellar::engine::AnnouncementControl::Edit
             : stellar::engine::AnnouncementControl::Custom;
}
std::optional<stellar::engine::AnnouncementValue>
NativeResearchWorkspace::focused_value(int width, int height) const {
  if (focused_control(width, height) != stellar::engine::AnnouncementControl::Edit)
    return std::nullopt;
  return stellar::engine::AnnouncementValue{query_.search};
}
bool NativeResearchWorkspace::set_focused_text(std::string text, int width, int height) {
  if (focused_control(width, height) != stellar::engine::AnnouncementControl::Edit)
    return false;
  // Same byte cap as the typed path, truncated on a code-point boundary.
  if (text.size() > 256) {
    std::size_t n = 256;
    while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xc0) == 0x80) --n;
    text.resize(n);
  }
  query_.search = std::move(text);
  rebuild_topology();
  guided_scroll_ = {};
  return true;
}

WorkspaceCommand NativeResearchWorkspace::handle(const InputEvent &event,
                                                 int width, int height) {
  if (!visible_)
    return {};
  pointer_ = event.position;
  const auto tab_count = window_ ? window_->domain_tabs.size() : 0;
  const auto layout =
      ResearchWorkspaceLayout::for_viewport(width, height, tab_count);
  if(auto extra=handle_controls(event,layout,width,height))return *extra;
  WorkspaceCommand result;
  result.captured = layout.surface.contains(event.position) || dragging_ ||
                    event.type == InputEventType::TextEntered ||
                    event.type == InputEventType::BackspacePressed;

  if (event.type == InputEventType::PointerCancelled) {
    dragging_ = false;
    focus_ = -1;
    return result;
  }
  if (event.type == InputEventType::TextEntered && search_focused_) {
    if (query_.search.size() + event.text.size() <= 256) {
      query_.search += event.text;
      rebuild_topology();guided_scroll_={};
    }
    return result;
  }
  if (event.type == InputEventType::BackspacePressed && search_focused_) {
    erase_last_utf8(query_.search);
    rebuild_topology();guided_scroll_={};
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
  if (event.type == InputEventType::Wheel &&
      layout.graph.contains(event.position)) {
    if(mode_!=ResearchViewMode::Tree){const auto cards=guided_cards(layout);const UiRect content{layout.graph.x,layout.graph.y+80*layout.scale,layout.graph.width,layout.graph.height-80*layout.scale};float extent=0;for(const auto& card:cards)extent=std::max(extent,card.bounds.y+card.bounds.height+guided_scroll_.scroll_offset-content.y);guided_scroll_.sync(extent,content.height);guided_scroll_.scroll_by(-event.wheel_y*58*layout.scale);return result;}
    const auto next = std::clamp(zoom_ * std::pow(1.12f, event.wheel_y), .58f, 1.55f);
    const auto x = (event.position.x - layout.graph.x) / layout.scale;
    const auto y = (event.position.y - layout.graph.y) / layout.scale;
    const auto ratio = next / zoom_;
    pan_ = {x - (x - pan_.x) * ratio, y - (y - pan_.y) * ratio};
    zoom_ = next;
    return result;
  }
  if (event.type == InputEventType::Wheel &&
      layout.inspector.contains(event.position)) {
    inspector_scroll_.scroll_by(-event.wheel_y * 46.f);
    return result;
  }
  if (event.type == InputEventType::KeyPressed && event.key) {
    if (search_focused_) {
      // While editing the search field it owns key input; Tab/Return commit
      // and leave edit mode, everything else is captured.
      if (event.key == 9u)
        search_focused_ = false;
      else if (event.key == 13u) {
        search_focused_ = false;
        return {WorkspaceCommandKind::None, true};
      } else
        return {WorkspaceCommandKind::None, true};
    }
    constexpr std::uint32_t kTab = 9u, kReturn = 13u, kSpace = 32u;
    constexpr std::uint32_t kRight = 0x4000004fu, kLeft = 0x40000050u,
                            kDown = 0x40000051u, kUp = 0x40000052u;
    constexpr std::uint32_t kHome = 0x4000004au, kEnd = 0x4000004du;
    const auto items = focusables(layout);
    const int count = static_cast<int>(items.size());
    const bool fwd = (event.key == kTab && !event.shift) ||
                     event.key == kRight || event.key == kDown;
    const bool bwd = (event.key == kTab && event.shift) ||
                     event.key == kLeft || event.key == kUp;
    // Cards clipped by their viewport stay in the ring; when focus lands
    // on one, snap its lane — guided list scroll or tree-graph pan — so
    // the card is fully visible and the next card stays reachable.
    const auto snap_focused = [&] {
      if (focus_ < 0 || focus_ >= count) return;
      const auto &target = items[static_cast<std::size_t>(focus_)];
      if (!target.unclipped) return;
      const auto &full = *target.unclipped;
      if (target.scroll_lane == 1) {
        const UiRect content{layout.graph.x, layout.graph.y + 80.f * layout.scale,
                             layout.graph.width,
                             layout.graph.height - 80.f * layout.scale};
        float extent = 0.f;
        for (const auto &card : guided_cards(layout))
          extent = std::max(extent, card.bounds.y + card.bounds.height +
                                        guided_scroll_.scroll_offset - content.y);
        guided_scroll_.sync(extent, content.height);
        guided_scroll_.scroll_interval_into_view(
            full.y, full.y + full.height, content.y, content.y + content.height);
      } else if (target.scroll_lane == 2) {
        float dx = 0.f, dy = 0.f;
        if (full.x < layout.graph.x) dx = layout.graph.x - full.x;
        else if (full.x + full.width > layout.graph.x + layout.graph.width)
          dx = layout.graph.x + layout.graph.width - full.x - full.width;
        if (full.y < layout.graph.y) dy = layout.graph.y - full.y;
        else if (full.y + full.height > layout.graph.y + layout.graph.height)
          dy = layout.graph.y + layout.graph.height - full.y - full.height;
        pan_.x += dx / layout.scale;
        pan_.y += dy / layout.scale;
      }
    };
    if (count > 0 && (event.key == kHome || event.key == kEnd)) {
      focus_ = event.key == kHome ? 0 : count - 1;
      snap_focused();
      return {WorkspaceCommandKind::None, true};
    }
    if (count > 0 && (fwd || bwd)) {
      focus_ = focus_ < 0 || focus_ >= count
                   ? (bwd ? count - 1 : 0)
                   : (focus_ + (bwd ? -1 : 1) + count) % count;
      snap_focused();
      return {WorkspaceCommandKind::None, true};
    }
    if ((event.key == kReturn || event.key == kSpace) && focus_ >= 0 &&
        focus_ < count) {
      const auto &r = items[static_cast<std::size_t>(focus_)].bounds;
      InputEvent press{InputEventType::LeftPressed};
      press.position = {r.x + r.width * .5f, r.y + r.height * .5f};
      const int keep = focus_;
      auto command = handle(press, width, height);
      if (visible_)
        focus_ = keep;
      return command;
    }
    return {};
  }
  if (event.type != InputEventType::LeftPressed)
    return result;

  focus_ = -1;
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
      if (!tab.bounds.contains(event.position))
        continue;
      const auto &domain = window_->domain_tabs.at(tab.index).id;
      query_.domain_id =
          domain.empty() ? std::nullopt : std::optional<std::string>(domain);
      selected_node_id_.reset();guided_scroll_={};
      pan_ = {24.f, 30.f};
      zoom_ = 1.f;
      rebuild_topology();
      center_selection_ = true;
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
    if(mode_!=ResearchViewMode::Tree){for(const auto& card:guided_cards(layout)){const auto clip=intersection(card.bounds,{layout.graph.x,layout.graph.y+80*layout.scale,layout.graph.width,layout.graph.height-80*layout.scale});if(clip&&clip->contains(event.position)){select(card.id);return {WorkspaceCommandKind::Select,true,card.id};}}return result;}
    for (const auto &placement : placements_) {
      const auto bounds = transformed_card(placement, layout);
      const auto clipped = intersection(layout.graph, bounds);
      if (!clipped || !clipped->contains(event.position))
        continue;
      select(placement.id);
      return {WorkspaceCommandKind::Select, true, placement.id};
    }
  }
  if (mode_==ResearchViewMode::Tree&&layout.graph.contains(event.position))
    dragging_ = true;
  return result;
}

void NativeResearchWorkspace::set_notice(std::string message, bool accepted) {
  notice_ = std::move(message);
  notice_accepted_ = accepted;
  inspector_scroll_ = {};
  
}

void NativeResearchWorkspace::render(DrawList &out, int width, int height) {
  if (!visible_)
    return;
  if (width != inspector_viewport_width_ ||
      height != inspector_viewport_height_) {
    inspector_scroll_ = {};
    
    inspector_viewport_width_ = width;
    inspector_viewport_height_ = height;
  }
  const auto tab_count = window_ ? window_->domain_tabs.size() : 0;
  const auto layout =
      ResearchWorkspaceLayout::for_viewport(width, height, tab_count);
  if (center_selection_ && selected_node_id_) {
    const auto selected_placement = std::ranges::find(
        placements_, *selected_node_id_, &NodePlacement::id);
    if (selected_placement != placements_.end()) {
      // Center once when this surface/filter first appears.  Subsequent
      // progress-only window updates retain the player's camera exactly.
      pan_.x = layout.graph.width / (2.f * layout.scale) -
               (selected_placement->world_bounds.x +
                selected_placement->world_bounds.width * .5f) * zoom_;
      pan_.y = layout.graph.height / (2.f * layout.scale) -
               (selected_placement->world_bounds.y +
                selected_placement->world_bounds.height * .5f) * zoom_;
    }
    center_selection_ = false;
  }
  interface_hits_.clear();
  native_ui_style::menu_panel(out, layout.surface);
  fill(out,{layout.title.x,layout.title.y+2*layout.scale,3*layout.scale,50*layout.scale},theme::color::science);
  auto research_title=layout.title;research_title.x+=16*layout.scale;
  text(out, research_title, tr("RESEARCH_TITLE", "RESEARCH"),
       theme::color::science, layout.title_font_pixels,
       TextAlign::Left, FontFace::Heading);
  if (window_) {
    auto summary = trf(
        "RESEARCH_LABS_SUMMARY",
        {fixed(window_->free_effective_labs), fixed(window_->total_effective_labs),
         std::to_string(window_->active_program_count),
         window_->lab_capacity_only
             ? tr("RESEARCH_LAB_LIMITED",
                  " active programs · lab capacity limited")
             : trf("RESEARCH_RESEARCH_SLOTS",
                   {std::to_string(
                       window_->maximum_programs.value_or(0))},
                   " / {0} research slots")},
        "{0} / {1} effective labs free  ·  {2}{3}");
    if (window_->formatted_treasury)
      summary = trf("RESEARCH_TREASURY_PREFIX", {*window_->formatted_treasury},
                    "{0} treasury  |  ") +
                summary;
    text(out, layout.labs, std::move(summary), muted, layout.small_font_pixels);
  }
  fill(out,layout.search,theme::color::surface_secondary);
  stroke(out,layout.search,
         search_focused_ ? theme::color::focus : theme::color::keyline_strong);
  text(out,
       {layout.search.x + 10.f * layout.scale,
        layout.search.y + 9.f * layout.scale,
        layout.search.width - 20.f * layout.scale,
        layout.search.height - 10.f * layout.scale},
       query_.search.empty()
           ? tr("RESEARCH_SEARCH_HINT",
                "Search technology, effects, unlocks…")
           : query_.search,
       query_.search.empty() ? muted : bright, layout.body_font_pixels);
  theme::button(out,layout.close,tr("RESEARCH_CLOSE","CLOSE"),pointer_,
                layout.body_font_pixels);

  if (window_) {
    for (const auto &tab : layout.tabs) {
      const auto &domain = window_->domain_tabs.at(tab.index);
      const auto active =
          domain.id.empty() ? !query_.domain_id : query_.domain_id == domain.id;
      const bool tab_hovered=tab.bounds.contains(pointer_);
      fill(out,tab.bounds,active?theme::color::surface_raised:tab_hovered?theme::color::surface_hover:theme::color::surface_secondary);
      stroke(out,tab.bounds,active?theme::color::science:theme::color::keyline);
      if(active)fill(out,{tab.bounds.x,tab.bounds.y,3.f,tab.bounds.height},theme::color::science);
      const float icon=std::min(34.f*layout.scale,tab.bounds.height-8.f*layout.scale);
      if(artwork_resolver_){
        const auto node=std::ranges::find_if(window_->nodes,[&](const auto &n){return domain.id.empty()||research_category_matches(domain.id,n.domain_id);});
        if(node!=window_->nodes.end())if(const auto image=artwork_resolver_(node->id,false)){
          const UiRect r{tab.bounds.x+5*layout.scale,tab.bounds.y+5*layout.scale,icon,icon};
          out.overlay.emplace_back(Image{image,r,research_art_source(*image,r),{210,235,255,255},tab.bounds});
        }
      }
      const UiRect tab_label{tab.bounds.x + icon+12.f * layout.scale,
                             tab.bounds.y + 5.f * layout.scale,
                             tab.bounds.width - icon-18.f * layout.scale,
                             tab.bounds.height - 8.f * layout.scale};
      clipped_text(out, tab_label, tab.bounds,
                   domain.label,
                   active ? bright : muted, layout.small_font_pixels,
                   TextAlign::Left);
    }
  }

  fill(out,layout.graph,theme::color::canvas);
  stroke(out,layout.graph,theme::color::keyline);
  fill(out,layout.inspector,theme::color::surface);
  stroke(out,layout.inspector,theme::color::keyline);

  if (!window_) {
    text(out,
         {layout.graph.x + 24.f * layout.scale,
          layout.graph.y + 24.f * layout.scale,
          layout.graph.width - 48.f * layout.scale, 40.f * layout.scale},
         tr("RESEARCH_BUILDING_VIEW", "Building the known research view..."),
         muted, layout.body_font_pixels);
    return;
  }
  if (mode_==ResearchViewMode::Tree&&placements_.empty()) {
    theme::empty_state(
        out, layout.graph,
        tr("RESEARCH_NO_MATCH", "No known research matches this view."),
        tr("RESEARCH_NO_MATCH_HINT",
           "Adjust the category, search, or view tab."),
        layout.body_font_pixels);
  }

  if(mode_==ResearchViewMode::Tree){
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
    if (from == visible_cards.end() || to == visible_cards.end())
      continue;
    const Point edge_from{from->second.bounds.x + from->second.bounds.width,
                          from->second.bounds.y +
                              from->second.bounds.height * .5f};
    const Point edge_to{to->second.bounds.x,
                        to->second.bounds.y + to->second.bounds.height * .5f};
    if (const auto segment = clipped_line(edge_from, edge_to, layout.graph))
      out.overlay.emplace_back(
          Line{segment->first, segment->second, theme::color::keyline_strong});
  }
  for (const auto &domain : domains_) {
    const auto y = layout.graph.y + (pan_.y + domain.world_y * zoom_) * layout.scale;
    clipped_text(out,
                 {layout.graph.x + 14.f * layout.scale, y,
                  layout.graph.width - 28.f * layout.scale,
                  20.f * layout.scale},
                 layout.graph, domain.label, muted,
                 std::max(8, static_cast<int>(std::lround(layout.small_font_pixels * zoom_))));
  }
  for (const auto &node : window_->nodes) {
    const auto visible = visible_cards.find(node.id);
    if (visible == visible_cards.end())
      continue;
    const auto bounds = visible->second.bounds;
    const auto clipping = visible->second.clipping;
    const auto chosen = selected_node_id_ == node.id;
    fill(out, clipping,
         chosen                        ? selected
         : clipping.contains(pointer_) ? hover
                                       : raised);
    clipped_stroke(out, bounds, layout.graph, chosen ? theme::color::selected : border);
    const auto card_scale = layout.scale * zoom_;
    const auto thumbnail_side = std::min(bounds.height - 14.f * card_scale,
                                         76.f * card_scale);
    const UiRect thumbnail{bounds.x + 8.f * card_scale, bounds.y + 8.f * card_scale,
                           thumbnail_side, thumbnail_side};
    if (artwork_resolver_) {
      // `node` is from the already projected known window; never resolve art
      // while laying out hidden or filtered-out catalog entries.
      if (const auto image = artwork_resolver_(node.id, false))
        out.overlay.emplace_back(Image{image, thumbnail, std::nullopt,
                                       {255, 255, 255, 235}, clipping});
    }
    clipped_text(out,
                 {thumbnail.x + thumbnail.width + 9.f * card_scale,
                  bounds.y + 9.f * card_scale,
                  bounds.width - thumbnail.width - 26.f * card_scale,
                  76.f * card_scale},
                 clipping, node.display_name, bright,
                 std::max(9, static_cast<int>(std::lround(layout.body_font_pixels * zoom_))));
    clipped_text(out,
                 {thumbnail.x + thumbnail.width + 9.f * card_scale,
                  bounds.y + 92.f * card_scale,
                  bounds.width - thumbnail.width - 26.f * card_scale, 20.f * card_scale},
                 clipping, tr(node_state_key(node), node_state(node)),
                 node.active ? positive : muted,
                 std::max(8, static_cast<int>(std::lround(layout.small_font_pixels * zoom_))));
    const auto progress =
        static_cast<float>(std::clamp(node.total_progress, 0., 1.));
    const UiRect progress_bounds{
        bounds.x + 8.f * card_scale,
        bounds.y + bounds.height - 6.f * card_scale,
        (bounds.width - 16.f * card_scale) * progress, 2.f * card_scale};
    if (const auto progress_clip = intersection(progress_bounds, layout.graph))
      fill(out, *progress_clip, theme::color::science);
  }

  }else render_dashboard(out,layout);
  render_controls(out,layout);

  const auto inspector_x = layout.inspector.x + 14.f * layout.scale;
  const auto inspector_width = layout.inspector.width - 28.f * layout.scale;
  auto inspector_y = layout.inspector.y + 14.f * layout.scale;
  theme::section_header(out,
       {inspector_x, inspector_y, inspector_width, 18.f * layout.scale},
       tr("RESEARCH_SELECTED_TECHNOLOGY", "SELECTED TECHNOLOGY"),
       layout.small_font_pixels, theme::Tone::Science);
  inspector_y += 42.f * layout.scale;
  const auto *node = selected_node();
  if (!node) {
    text(out, {inspector_x, inspector_y, inspector_width, 60.f * layout.scale},
         tr("RESEARCH_SELECT_HINT",
            "Select a known program to inspect its current details."),
         muted,
         layout.body_font_pixels);
    dropdown_.render(out,dropdown_.id()==1?layout.filter:dropdown_.id()==2?layout.sort:layout.toolbar,width,height,layout.small_font_pixels);
    return;
  }
  const float art_height=150.f*layout.scale;
  if(artwork_resolver_)if(const auto image=artwork_resolver_(node->id,true)){
    const UiRect hero{inspector_x,inspector_y,inspector_width,art_height};
    out.overlay.emplace_back(Image{image,hero,research_art_source(*image,hero),{255,255,255,255},layout.inspector});
  }
  inspector_y+=art_height+9*layout.scale;
  const Text name_probe{{},node->display_name,bright,layout.title_font_pixels,inspector_width};
  const float name_height=text_measurer_?static_cast<float>(text_measurer_(name_probe).height):56*layout.scale;
  text(out,{inspector_x,inspector_y,inspector_width,std::max(56*layout.scale,name_height)},node->display_name,bright,layout.title_font_pixels);inspector_y+=std::max(56*layout.scale,name_height)+6*layout.scale;
  text(out, {inspector_x, inspector_y, inspector_width, 22.f * layout.scale},
       node->domain_label + "  |  " + tr(node_state_key(*node), node_state(*node)),
       node->active ? positive : muted, layout.small_font_pixels);
  inspector_y += 27.f * layout.scale;
  theme::progress(out,
       {inspector_x, inspector_y, inspector_width, 7.f * layout.scale},
       node->total_progress, theme::Tone::Science);
  inspector_y += 17.f * layout.scale;
  auto progress_text = trf("RESEARCH_PROGRESS",
                           {fixed(node->total_progress * 100., 0)},
                           "Progress {0}%");
  if (node->active)
    progress_text += trf("RESEARCH_LABS_SUFFIX",
                         {fixed(node->assigned_effective_labs)},
                         "  |  {0} labs");
  else if (node->cost)
    progress_text += trf("RESEARCH_PLANNED_STAFFING",
                         {fixed(node->cost->assigned_effective_labs)},
                         "  |  Planned staffing {0} labs");
  text(out, {inspector_x, inspector_y, inspector_width, 20.f * layout.scale},
       std::move(progress_text), muted, layout.small_font_pixels);
  inspector_y += 28.f * layout.scale;

  struct InspectorBlock {
    std::string value;
    Color color;
    float height{};
    std::string node_id;
  };
  std::vector<InspectorBlock> details;
  if (node->cancelled) details.push_back({tr("RESEARCH_RESTARTING","RESTARTING THIS PROGRAM\nRetained progress resumes in its original context. New authorization and milestone funding are required."), muted});
  if (!node->purpose.empty()) details.push_back({tr("RESEARCH_WHAT_IT_DOES","WHAT IT DOES") + "\n" + node->purpose, bright});
  if (!node->benefits.empty()) details.push_back({tr("RESEARCH_WHAT_CHANGES","WHAT THIS CHANGES") + "\n" + node->benefits, bright});
  if (node->active) details.push_back({tr("RESEARCH_CANCELLATION","CANCELLATION") + "\n" + node->cancel_action.reason, muted});
  if(!node->recommendation_reasons.empty()){std::string why=tr("RESEARCH_RECOMMENDED","RECOMMENDED BECAUSE");for(const auto& reason:node->recommendation_reasons)why+="\n• "+reason;details.push_back({std::move(why),warning});}
  for(const auto& edge:window_->edges)if(edge.to_id==node->id||edge.from_id==node->id){const bool prerequisite=edge.to_id==node->id;const auto id=prerequisite?edge.from_id:edge.to_id;const auto other=std::ranges::find(window_->nodes,id,&NativeResearchNode::id);if(other!=window_->nodes.end())details.push_back({(prerequisite?tr("RESEARCH_REQUIRES","REQUIRES"):tr("RESEARCH_LEADS_TO","LEADS TO"))+"\n"+std::string(other->maturity>=stellar::core::ResearchMaturity::mature?"✓ ":"› ")+other->display_name,prerequisite&&other->maturity<stellar::core::ResearchMaturity::mature?warning:positive,0,id});}
  if (node->research_points > 0.)
    details.push_back({trf("RESEARCH_WORK", {fixed(node->research_points, 0)},
                           "RESEARCH WORK\n{0} RP"),
                       muted});
  if (!node->active && node->maturity >= stellar::core::ResearchMaturity::mature)
    details.push_back({tr("RESEARCH_COMPLETED",
                          "COMPLETED\nEstablished knowledge has no ongoing "
                          "program cost and is not charged again."),
                       positive});
  if (node->cost) {
    const auto &cost = *node->cost;
    auto value = trf(
        "RESEARCH_COST_TIME",
        {cost.formatted_authorization, cost.formatted_milestone_commitment,
         cost.formatted_operating_cost_rate, cost.formatted_estimated_total,
         cost.formatted_credits_needed_to_start},
        "COST & TIME\nAuthorization {0}\nMilestones {1}\nOperations {2}\n"
        "Estimated total {3}\nReserve to start {4}\n");
    value += std::isfinite(cost.estimated_years_at_full_funding)
                 ? trf("RESEARCH_FULL_FUNDING",
                       {stellar::native_campaign::format_campaign_duration(
                           cost.estimated_years_at_full_funding * 365.25)},
                       "At full funding {0}")
                 : tr("RESEARCH_DURATION_UNAVAILABLE",
                      "Staffed duration unavailable");
    details.push_back({std::move(value), bright});
  }
  if (!node->known_capabilities.empty()) {
    std::string value = tr("RESEARCH_KNOWN_CAPABILITIES", "KNOWN CAPABILITIES") + "\n";
    for (const auto &capability : node->known_capabilities)
      value += capability.display_name + "\n";
    details.push_back({std::move(value), bright});
  }
  if (!node->blockers.empty()) {
    std::string value = tr("RESEARCH_REQUIREMENTS_STATUS", "REQUIREMENTS / STATUS") + "\n";
    for (const auto &blocker : node->blockers)
      value += blocker + "\n";
    details.push_back({std::move(value), warning});
  }
  if (!node->primary_action.enabled && !node->primary_action.reason.empty() &&
      std::ranges::find(node->blockers, node->primary_action.reason) ==
          node->blockers.end()) {
    details.push_back(
        {tr("RESEARCH_ACTION_STATUS", "ACTION STATUS") + "\n" +
             node->primary_action.reason,
         warning});
  }
  if (!notice_.empty()) {
    details.push_back(
        {tr("RESEARCH_PROGRAM_NOTICE", "PROGRAM NOTICE") + "\n" + notice_,
         notice_accepted_ ? positive : failure});
  }
  const auto details_end = layout.feedback.y - 6.f * layout.scale;
  const UiRect details_clip{inspector_x, inspector_y, inspector_width,
                            std::max(1.f, details_end - inspector_y)};
  const auto fallback_height = [&](std::string_view value) {
    const auto character_width = std::max(1.f, layout.small_font_pixels * .58f);
    const auto columns = std::max(
        1, static_cast<int>(std::floor(details_clip.width / character_width)));
    int lines = 1;
    std::size_t line_length{};
    for (const char character : value) {
      if (character == '\n') {
        ++lines;
        line_length = 0;
      } else if (++line_length > static_cast<std::size_t>(columns)) {
        ++lines;
        line_length = 1;
      }
    }
    return static_cast<float>(lines) *
           (layout.small_font_pixels + 3.f * layout.scale);
  };
  float content_height{};
  const auto block_gap = 9.f * layout.scale;
  for (auto &block : details) {
    const Text probe{{details_clip.x, 0.f},    block.value,        block.color,
                     layout.small_font_pixels, details_clip.width, std::nullopt,
                     TextAlign::Left,          FontFace::Interface};
    const auto measured =
        text_measurer_
            ? text_measurer_(probe)
            : TextExtent{0, static_cast<int>(fallback_height(block.value))};
    if (measured.width < 0 || measured.height < 0)
      throw std::runtime_error(
          "Renderer returned invalid research inspector text bounds.");
    block.height = std::max(static_cast<float>(layout.small_font_pixels),
                            static_cast<float>(measured.height));
    content_height += block.height + block_gap;
  }
  if (!details.empty())
    content_height -= block_gap;
  content_height += 4.f * layout.scale;
  inspector_scroll_.sync(content_height, details_clip.height);
  auto block_y = details_clip.y - inspector_scroll_.scroll_offset;
  for (auto &block : details) {
    if(!block.node_id.empty())if(const auto visible=intersection({details_clip.x,block_y,details_clip.width,block.height},details_clip)){
      const auto target=std::ranges::find(window_->nodes,block.node_id,&NativeResearchNode::id);
      interface_hits_.push_back({*visible,10,block.node_id,target!=window_->nodes.end()?target->display_name:block.node_id});
    }
    clipped_text(out,
                 {details_clip.x, block_y, details_clip.width, block.height},
                 details_clip, std::move(block.value), block.color,
                 layout.small_font_pixels);
    block_y += block.height + block_gap;
  }
  if (const auto thumb = inspector_scroll_.thumb(details_clip.height,
                                                 18.f * layout.scale);
      thumb.size > 0.f) {
    fill(out,
         {details_clip.x + details_clip.width - 3.f * layout.scale,
          details_clip.y + thumb.offset, 2.f * layout.scale, thumb.size},
         muted);
  }
  const auto action_text = node->cancelled && node->primary_action.intent == NativeResearchIntent::Start ? tr("RESEARCH_ACTION_RESTART", "Restart research") : tr(action_key(node->primary_action.intent), action_label(node->primary_action.intent));
  if (!action_text.empty()) {
    const auto enabled = node->primary_action.enabled;
    theme::button(out,layout.action,action_text,pointer_,
                  layout.body_font_pixels,theme::Tone::Science,true,enabled);
  }
  const auto feedback_text =
      !notice_.empty() ? notice_accepted_
                             ? tr("RESEARCH_PROGRAM_UPDATED",
                                  "Program updated. Scroll for details.")
                             : tr("RESEARCH_ACTION_UNAVAILABLE",
                                  "Action unavailable. Scroll for details.")
      : !node->primary_action.enabled && !node->primary_action.reason.empty()
          ? tr("RESEARCH_ACTION_UNAVAILABLE",
               "Action unavailable. Scroll for details.")
          : std::string{};
  if (!feedback_text.empty())
    text(out, layout.feedback, feedback_text,
         notice_accepted_ ? positive : warning, layout.small_font_pixels);
  if (focus_ >= 0) {
    const auto items = focusables(layout);
    if (focus_ < static_cast<int>(items.size()))
      theme::focus_ring(out, items[static_cast<std::size_t>(focus_)].bounds);
  }
  dropdown_.render(out,dropdown_.id()==1?layout.filter:dropdown_.id()==2?layout.sort:layout.toolbar,width,height,layout.small_font_pixels);
}

#include "native_research_dashboard.inl"

} // namespace stellar::native_research_ui
