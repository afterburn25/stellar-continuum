#include "native_construction_workspace.hpp"
#include "native_ui_layout.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <ranges>
#include <sstream>
#include <utility>

namespace stellar::native_construction_ui {
namespace {
using namespace stellar::native_construction;
using namespace stellar::native_map;

constexpr Color panel{7, 17, 32, 252};
constexpr Color inset{5, 14, 27, 250};
constexpr Color row{12, 31, 54, 248};
constexpr Color hover{24, 61, 94, 252};
constexpr Color selected{19, 73, 68, 252};
constexpr Color border{91, 151, 205, 235};
constexpr Color good{102, 232, 164, 255};
constexpr Color bright{235, 244, 255, 255};
constexpr Color muted{154, 181, 211, 240};
constexpr Color failure{255, 133, 123, 255};

void fill(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(FilledRectangle{bounds, color});
}
void stroke(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(StrokedRectangle{bounds, color});
}
void text(DrawList &out, UiRect bounds, std::string value, Color color,
          int pixels, FontFace face = FontFace::Interface,
          TextAlign align = TextAlign::Left) {
  const auto x = align == TextAlign::Center
                     ? bounds.x + bounds.width * .5f
                     : align == TextAlign::Right ? bounds.x + bounds.width
                                                 : bounds.x;
  out.overlay.emplace_back(Text{{x, bounds.y}, std::move(value), color, pixels,
                                bounds.width, bounds, align, face});
}
[[nodiscard]] std::optional<UiRect> intersection(UiRect left,
                                                 UiRect right) noexcept {
  const auto x = std::max(left.x, right.x);
  const auto y = std::max(left.y, right.y);
  const auto r = std::min(left.x + left.width, right.x + right.width);
  const auto b = std::min(left.y + left.height, right.y + right.height);
  if (r <= x || b <= y) return std::nullopt;
  return UiRect{x, y, r - x, b - y};
}
[[nodiscard]] std::string number(double value, int precision = 1) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(precision) << value;
  return out.str();
}
[[nodiscard]] std::string visible_message(std::string value) {
  constexpr std::size_t limit = 180;
  if (value.size() <= limit) return value;
  auto end = limit - 3;
  while (end > 0 &&
         (static_cast<unsigned char>(value[end]) & 0xc0u) == 0x80u)
    --end;
  value.resize(end);
  value += "...";
  return value;
}
[[nodiscard]] std::string category_name(stellar::core::ConstructionCategory c) {
  using stellar::core::ConstructionCategory;
  switch (c) {
  case ConstructionCategory::Science: return "Science";
  case ConstructionCategory::Industry: return "Industry";
  case ConstructionCategory::Orbital: return "Orbital";
  case ConstructionCategory::Ftl: return "FTL";
  }
  return "Infrastructure";
}
[[nodiscard]] std::string state_name(const NativeConstructionProject &project) {
  if (project.complete) return "COMPLETED";
  if (project.active) return "ACTIVE";
  if (project.queued)
    return "QUEUED " + std::to_string(project.queue_position);
  return "AVAILABLE";
}
[[nodiscard]] float progress_width(const NativeConstructionProject &project,
                                   float width) noexcept {
  const auto progress = std::isfinite(project.progress_fraction)
                            ? std::clamp(project.progress_fraction, 0., 1.)
                            : 0.;
  return width * static_cast<float>(progress);
}
} // namespace

ConstructionWorkspaceLayout ConstructionWorkspaceLayout::for_viewport(
    int width, int height) noexcept {
  const auto w = static_cast<float>(width);
  const auto h = static_cast<float>(height);
  const auto requested = std::max(1.f, h / 900.f);
  const auto fit = std::max(.55f, std::min(w / 1040.f, h / 650.f));
  const auto scale = std::min(requested, fit);
  const auto margin = 14.f * scale;
  const auto left_margin = native_navigation_content_left * scale;
  const auto top = 60.f * scale;
  const UiRect surface{left_margin, top,
                       std::max(1.f, w - left_margin - margin),
                       std::max(1.f, h - top - margin)};
  const auto inner_x = surface.x + 14.f * scale;
  const auto inner_y = surface.y + 54.f * scale;
  const auto inner_w = surface.width - 28.f * scale;
  const auto inner_h = surface.height - 68.f * scale;
  const auto gap = 10.f * scale;
  const auto left_w = std::clamp(inner_w * .25f, 230.f * scale,
                                 340.f * scale);
  const auto right_w = std::clamp(inner_w * .29f, 270.f * scale,
                                  390.f * scale);
  const auto center_w = std::max(220.f * scale,
                                 inner_w - left_w - right_w - gap * 2.f);
  const UiRect projects{inner_x, inner_y, left_w, inner_h};
  const UiRect details{projects.x + projects.width + gap, inner_y, center_w,
                       inner_h - 276.f * scale};
  const UiRect orders{details.x + details.width + gap, inner_y, right_w,
                      inner_h};
  const auto action_y = inner_y + inner_h - 42.f * scale;
  const auto action_gap = 8.f * scale;
  const auto action_w = (details.width - action_gap) * .5f;
  const UiRect primary{details.x, action_y, action_w, 42.f * scale};
  const UiRect secondary{details.x + action_w + action_gap, action_y,
                         action_w, 42.f * scale};
  const UiRect feedback{details.x, action_y - 62.f * scale, details.width,
                        54.f * scale};
  const UiRect costs{details.x, details.y + details.height + 8.f * scale,
                     details.width,
                     std::max(0.f, feedback.y - details.y -
                                       details.height - 16.f * scale)};
  return {scale,
          static_cast<int>(std::lround(24.f * scale)),
          static_cast<int>(std::lround(15.f * scale)),
          static_cast<int>(std::lround(12.f * scale)),
          surface,
          {inner_x, surface.y + 14.f * scale,
           surface.width - 92.f * scale, 32.f * scale},
          {surface.x + surface.width - 48.f * scale,
           surface.y + 12.f * scale, 34.f * scale, 34.f * scale},
          projects,
          details,
          orders,
          costs,
          feedback,
          primary,
          secondary};
}

void NativeConstructionWorkspace::open() noexcept { visible_ = true; }
void NativeConstructionWorkspace::close() noexcept {
  visible_ = false;
  cancel_confirmation_id_.reset();
}
bool NativeConstructionWorkspace::visible() const noexcept { return visible_; }

void NativeConstructionWorkspace::set_view(NativeConstructionView view) {
  const auto generation_changed =
      view_ && view_->campaign_generation != view.campaign_generation;
  const auto revision_changed =
      view_ && view_->construction_revision != view.construction_revision;
  if (generation_changed) {
    selected_project_id_.reset();
    notice_.clear();
    project_scroll_ = 0.f;
    order_scroll_ = 0.f;
  }
  if (generation_changed || revision_changed) cancel_confirmation_id_.reset();
  view_ = std::move(view);
  rebuild_status_order();
  reconcile_selection();
}

void NativeConstructionWorkspace::discard_campaign() {
  view_.reset();
  selected_project_id_.reset();
  cancel_confirmation_id_.reset();
  notice_.clear();
  project_scroll_ = 0.f;
  order_scroll_ = 0.f;
  status_order_.clear();
}

void NativeConstructionWorkspace::set_notice(std::string message,
                                             bool accepted) {
  notice_ = std::move(message);
  notice_accepted_ = accepted;
  cancel_confirmation_id_.reset();
}

bool NativeConstructionWorkspace::arm_cancel_confirmation(
    std::string_view project_id) {
  if (!view_) return false;
  const auto found = std::ranges::find(view_->projects, project_id,
                                       &NativeConstructionProject::id);
  if (found == view_->projects.end() || (!found->active && !found->queued))
    return false;
  selected_project_id_ = found->id;
  cancel_confirmation_id_ = found->id;
  notice_ = "Confirm cancellation to return " +
            found->formatted_cancellation_refund + ".";
  notice_accepted_ = true;
  return true;
}

const std::optional<NativeConstructionView> &
NativeConstructionWorkspace::view() const noexcept {
  return view_;
}
const std::optional<std::string> &
NativeConstructionWorkspace::selected_project_id() const noexcept {
  return selected_project_id_;
}

std::optional<UiRect> NativeConstructionWorkspace::project_bounds(
    std::string_view project_id, int width, int height) const {
  if (!view_) return std::nullopt;
  const auto found = std::ranges::find(view_->projects, project_id,
                                       &NativeConstructionProject::id);
  if (found == view_->projects.end()) return std::nullopt;
  const auto layout = ConstructionWorkspaceLayout::for_viewport(width, height);
  const UiRect rows{layout.projects.x, layout.projects.y + 27.f * layout.scale,
                    layout.projects.width,
                    layout.projects.height - 27.f * layout.scale};
  const auto index = static_cast<std::size_t>(found - view_->projects.begin());
  const UiRect bounds{rows.x, rows.y + project_scroll_ +
                                  static_cast<float>(index) * 58.f * layout.scale,
                      rows.width, 54.f * layout.scale};
  return intersection(bounds, rows);
}

void NativeConstructionWorkspace::reconcile_selection() {
  if (!view_) return;
  if (selected_project_id_ &&
      std::ranges::none_of(view_->projects, [&](const auto &project) {
        return project.id == *selected_project_id_;
      }))
    selected_project_id_.reset();
  if (!selected_project_id_ && !view_->projects.empty())
    selected_project_id_ = view_->projects.front().id;
  if (cancel_confirmation_id_ != selected_project_id_)
    cancel_confirmation_id_.reset();
}

void NativeConstructionWorkspace::rebuild_status_order() {
  status_order_.clear();
  if (!view_) return;
  for (std::size_t index = 0; index < view_->projects.size(); ++index)
    if (view_->projects[index].active) status_order_.push_back(index);
  std::vector<std::size_t> queued;
  for (std::size_t index = 0; index < view_->projects.size(); ++index)
    if (!view_->projects[index].active && view_->projects[index].queued)
      queued.push_back(index);
  std::ranges::sort(queued, [&](std::size_t left, std::size_t right) {
    const auto &a = view_->projects[left];
    const auto &b = view_->projects[right];
    if (a.queue_position != b.queue_position)
      return a.queue_position < b.queue_position;
    return a.id < b.id;
  });
  status_order_.insert(status_order_.end(), queued.begin(), queued.end());
  for (std::size_t index = 0; index < view_->projects.size(); ++index)
    if (!view_->projects[index].active && !view_->projects[index].queued &&
        view_->projects[index].complete)
      status_order_.push_back(index);
}

ConstructionWorkspaceCommand NativeConstructionWorkspace::handle(
    const InputEvent &event, int width, int height) {
  if (!visible_) return {};
  pointer_ = event.position;
  const auto layout = ConstructionWorkspaceLayout::for_viewport(width, height);
  if (event.type == InputEventType::PointerCancelled) {
    cancel_confirmation_id_.reset();
    return {ConstructionWorkspaceCommandKind::None, true};
  }
  const UiRect project_rows{layout.projects.x,
                            layout.projects.y + 27.f * layout.scale,
                            layout.projects.width,
                            layout.projects.height - 27.f * layout.scale};
  const UiRect order_rows{layout.orders.x,
                          layout.orders.y + 27.f * layout.scale,
                          layout.orders.width,
                          layout.orders.height - 27.f * layout.scale};
  if (event.type == InputEventType::Wheel) {
    const auto scroll = [&](float &offset, UiRect bounds, std::size_t count) {
      const auto content = static_cast<float>(count) * 58.f * layout.scale;
      offset = std::clamp(offset + event.wheel_y * 40.f * layout.scale,
                          std::min(0.f, bounds.height - content), 0.f);
    };
    if (layout.projects.contains(event.position)) {
      scroll(project_scroll_, project_rows, view_ ? view_->projects.size() : 0);
      return {ConstructionWorkspaceCommandKind::None, true};
    }
    if (layout.orders.contains(event.position)) {
      const auto content = static_cast<float>(status_order_.size()) *
                           72.f * layout.scale;
      order_scroll_ = std::clamp(
          order_scroll_ + event.wheel_y * 40.f * layout.scale,
          std::min(0.f, order_rows.height - content), 0.f);
      return {ConstructionWorkspaceCommandKind::None, true};
    }
    return {ConstructionWorkspaceCommandKind::None,
            layout.surface.contains(event.position)};
  }
  if (event.type != InputEventType::LeftPressed)
    return {ConstructionWorkspaceCommandKind::None,
            layout.surface.contains(event.position)};
  if (layout.close.contains(event.position)) {
    close();
    return {ConstructionWorkspaceCommandKind::None, true};
  }
  if (!layout.surface.contains(event.position)) return {};
  if (view_) {
    for (std::size_t index = 0; index < view_->projects.size(); ++index) {
      const UiRect bounds{project_rows.x,
                          project_rows.y + project_scroll_ +
                              static_cast<float>(index) * 58.f * layout.scale,
                          project_rows.width, 54.f * layout.scale};
      const auto clipped = intersection(bounds, project_rows);
      if (clipped && clipped->contains(event.position)) {
        selected_project_id_ = view_->projects[index].id;
        cancel_confirmation_id_.reset();
        notice_.clear();
        return {ConstructionWorkspaceCommandKind::None, true};
      }
    }
    for (std::size_t order_index = 0; order_index < status_order_.size();
         ++order_index) {
      const auto &project = view_->projects[status_order_[order_index]];
      const UiRect bounds{order_rows.x,
                          order_rows.y + order_scroll_ +
                              static_cast<float>(order_index) * 72.f * layout.scale,
                          order_rows.width, 68.f * layout.scale};
      const auto clipped = intersection(bounds, order_rows);
      if (clipped && clipped->contains(event.position)) {
        selected_project_id_ = project.id;
        cancel_confirmation_id_.reset();
        notice_.clear();
        return {ConstructionWorkspaceCommandKind::None, true};
      }
    }
  }
  const auto *project = selected_project();
  if (!project) return {ConstructionWorkspaceCommandKind::None, true};
  if (project->active || project->queued) {
    if (layout.secondary_action.contains(event.position)) {
      if (cancel_confirmation_id_ == project->id)
        return {ConstructionWorkspaceCommandKind::Cancel, true, project->id};
      return {ConstructionWorkspaceCommandKind::PrepareCancel, true,
              project->id};
    }
  } else if (!project->complete) {
    if (layout.primary_action.contains(event.position) &&
        project->start.enabled)
      return {ConstructionWorkspaceCommandKind::Start, true, project->id};
    if (layout.secondary_action.contains(event.position) &&
        project->queue.enabled)
      return {ConstructionWorkspaceCommandKind::Queue, true, project->id};
  }
  return {ConstructionWorkspaceCommandKind::None, true};
}

void NativeConstructionWorkspace::render(DrawList &out, int width,
                                         int height) const {
  if (!visible_) return;
  const auto layout = ConstructionWorkspaceLayout::for_viewport(width, height);
  fill(out, layout.surface, panel);
  stroke(out, layout.surface, border);
  text(out, layout.title, "PLAYER CONSTRUCTION", bright,
       layout.title_font_pixels, FontFace::Heading);
  fill(out, layout.close, layout.close.contains(pointer_) ? hover : row);
  stroke(out, layout.close, border);
  text(out, {layout.close.x, layout.close.y + 7.f * layout.scale,
             layout.close.width, layout.close.height - 8.f * layout.scale},
       "X", bright, layout.body_font_pixels, FontFace::Interface,
       TextAlign::Center);
  const auto section = [&](UiRect bounds, std::string heading) {
    fill(out, bounds, inset);
    stroke(out, bounds, border);
    text(out, {bounds.x + 8.f * layout.scale,
               bounds.y + 6.f * layout.scale,
               bounds.width - 16.f * layout.scale, 20.f * layout.scale},
         std::move(heading), muted, layout.small_font_pixels,
         FontFace::Heading);
  };
  section(layout.projects, "KNOWN PROJECTS");
  section(layout.details, "PROJECT DETAILS");
  section(layout.orders, "CONSTRUCTION STATUS");
  const UiRect project_rows{layout.projects.x,
                            layout.projects.y + 27.f * layout.scale,
                            layout.projects.width,
                            layout.projects.height - 27.f * layout.scale};
  if (!view_ || view_->projects.empty()) {
    text(out, {project_rows.x + 10.f * layout.scale,
               project_rows.y + 8.f * layout.scale,
               project_rows.width - 20.f * layout.scale,
               project_rows.height - 16.f * layout.scale},
         "No known projects are available. Research prerequisites remain "
         "locked.",
         muted, layout.body_font_pixels);
  } else {
    for (std::size_t index = 0; index < view_->projects.size(); ++index) {
      const auto &project = view_->projects[index];
      const UiRect bounds{project_rows.x,
                          project_rows.y + project_scroll_ +
                              static_cast<float>(index) * 58.f * layout.scale,
                          project_rows.width, 54.f * layout.scale};
      const auto clipped = intersection(bounds, project_rows);
      if (!clipped) continue;
      fill(out, *clipped,
           selected_project_id_ == project.id
               ? selected
               : clipped->contains(pointer_) ? hover : row);
      if (const auto line = intersection(
              *clipped, {bounds.x + 8.f * layout.scale,
                         bounds.y + 5.f * layout.scale,
                         bounds.width - 16.f * layout.scale,
                         20.f * layout.scale}))
        text(out, *line, project.name, bright, layout.body_font_pixels);
      if (const auto line = intersection(
              *clipped, {bounds.x + 8.f * layout.scale,
                         bounds.y + 29.f * layout.scale,
                         bounds.width - 16.f * layout.scale,
                         18.f * layout.scale}))
        text(out, *line,
             category_name(project.category) + "  |  " + state_name(project),
             project.complete || project.active ? good : muted,
             layout.small_font_pixels);
    }
  }

  const auto *project = selected_project();
  if (!project) {
    text(out, {layout.details.x + 10.f * layout.scale,
               layout.details.y + 34.f * layout.scale,
               layout.details.width - 20.f * layout.scale,
               layout.details.height - 44.f * layout.scale},
         "Select a known project to review its requirements.", muted,
         layout.body_font_pixels);
  } else {
    std::string details = project->name + "\n" +
                          category_name(project->category) + "  |  " +
                          state_name(*project) + "\n\n" + project->description;
    if (!project->requirements.empty()) {
      details += "\n\nRequirements";
      for (const auto &requirement : project->requirements)
        details += "\n" + requirement;
    }
    text(out, {layout.details.x + 10.f * layout.scale,
               layout.details.y + 32.f * layout.scale,
               layout.details.width - 20.f * layout.scale,
               layout.details.height - 42.f * layout.scale},
         std::move(details), bright, layout.small_font_pixels);
  }

  const UiRect order_rows{layout.orders.x,
                          layout.orders.y + 27.f * layout.scale,
                          layout.orders.width,
                          layout.orders.height - 27.f * layout.scale};
  if (view_)
    for (std::size_t order_index = 0; order_index < status_order_.size();
         ++order_index) {
      const auto &value = view_->projects[status_order_[order_index]];
      const UiRect bounds{order_rows.x,
                          order_rows.y + order_scroll_ +
                              static_cast<float>(order_index) * 72.f * layout.scale,
                          order_rows.width, 68.f * layout.scale};
      const auto clipped = intersection(bounds, order_rows);
      if (!clipped) continue;
      fill(out, *clipped,
           selected_project_id_ == value.id
               ? selected
               : clipped->contains(pointer_) ? hover : row);
      if (const auto line = intersection(
              *clipped, {bounds.x + 8.f * layout.scale,
                         bounds.y + 5.f * layout.scale,
                         bounds.width - 16.f * layout.scale,
                         19.f * layout.scale}))
        text(out, *line, value.name, bright, layout.body_font_pixels);
      if (const auto line = intersection(
              *clipped, {bounds.x + 8.f * layout.scale,
                         bounds.y + 28.f * layout.scale,
                         bounds.width - 16.f * layout.scale,
                         17.f * layout.scale}))
        text(out, *line, state_name(value), good, layout.small_font_pixels);
      if (const auto line = intersection(
              *clipped, {bounds.x + 8.f * layout.scale,
                         bounds.y + 47.f * layout.scale,
                         bounds.width - 16.f * layout.scale,
                         17.f * layout.scale}))
        text(out, *line,
             (std::isfinite(value.progress_fraction)
                  ? "Progress " +
                        number(std::clamp(value.progress_fraction, 0., 1.) *
                                   100.,
                               1) +
                        "%"
                  : "Progress unavailable") +
                 "  |  Remaining " + number(value.industry_remaining, 1),
             muted, layout.small_font_pixels);
      const UiRect track{bounds.x + 8.f * layout.scale,
                         bounds.y + 62.f * layout.scale,
                         bounds.width - 16.f * layout.scale,
                         3.f * layout.scale};
      if (const auto clipped_track = intersection(track, order_rows)) {
        fill(out, *clipped_track, {23, 45, 67, 255});
        const UiRect completed{track.x, track.y,
                               progress_width(value, track.width),
                               track.height};
        if (const auto clipped_completed = intersection(completed, order_rows))
          fill(out, *clipped_completed, good);
      }
    }
  if (status_order_.empty())
    text(out, {order_rows.x + 10.f * layout.scale,
               order_rows.y + 8.f * layout.scale,
               order_rows.width - 20.f * layout.scale,
               order_rows.height - 16.f * layout.scale},
         "No projects are active, queued, or completed.", muted,
         layout.body_font_pixels);

  if (project && view_) {
    std::string costs = "COST AND READINESS\nAuthorization " +
                        project->formatted_credit_cost + "  |  Materials " +
                        number(project->industry_cost, 1) + "\nUpkeep " +
                        project->formatted_upkeep_rate + "  |  Treasury " +
                        view_->formatted_treasury +
                        "\nAvailable industry " +
                        number(view_->available_industry, 1) +
                        "  |  Minimum remaining " +
                        number(project->minimum_days_remaining, 2) + " days";
    if (project->active || project->queued)
      costs += "\nAuthorized " + project->formatted_authorization +
               "  |  Refund now " +
               project->formatted_cancellation_refund;
    if (project->queue_blocker) costs += "\n" + *project->queue_blocker;
    text(out, layout.costs, std::move(costs), muted,
         layout.small_font_pixels);
  }

  std::string feedback = notice_;
  if (feedback.empty() && project && !project->complete && !project->active &&
      !project->queued) {
    feedback = "Start: " + project->start.message +
               "\nQueue: " + project->queue.message;
  }
  if (!feedback.empty())
    text(out, layout.feedback, visible_message(std::move(feedback)),
         notice_.empty() || notice_accepted_ ? muted : failure,
         layout.small_font_pixels);

  const auto action = [&](UiRect bounds, std::string label, bool enabled) {
    fill(out, bounds, enabled && bounds.contains(pointer_) ? hover : row);
    stroke(out, bounds, enabled ? good : border);
    text(out, {bounds.x + 6.f * layout.scale,
               bounds.y + 10.f * layout.scale,
               bounds.width - 12.f * layout.scale,
               bounds.height - 12.f * layout.scale},
         std::move(label), enabled ? bright : muted,
         layout.body_font_pixels, FontFace::Interface, TextAlign::Center);
  };
  if (project) {
    if (project->active || project->queued) {
      action(layout.primary_action, state_name(*project), false);
      action(layout.secondary_action,
             cancel_confirmation_id_ == project->id
                 ? "CONFIRM CANCEL"
                 : "CANCEL + REFUND",
             true);
    } else if (!project->complete) {
      action(layout.primary_action,
             project->start.will_queue ? "START / QUEUE" : "START NOW",
             project->start.enabled);
      action(layout.secondary_action, "QUEUE",
             project->queue.enabled);
    } else {
      action(layout.primary_action, "COMPLETED", false);
    }
  }
}

const NativeConstructionProject *
NativeConstructionWorkspace::selected_project() const noexcept {
  if (!view_ || !selected_project_id_) return nullptr;
  const auto found = std::ranges::find(view_->projects, *selected_project_id_,
                                       &NativeConstructionProject::id);
  return found == view_->projects.end() ? nullptr : &*found;
}

} // namespace stellar::native_construction_ui
