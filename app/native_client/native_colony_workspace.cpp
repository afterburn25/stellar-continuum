#include "native_colony_workspace.hpp"
#include "native_ui_layout.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace stellar::native_colony_ui {
namespace {
using namespace stellar::native_colony;
using namespace stellar::native_map;

constexpr Color bright{235, 244, 255, 255};
void fill(DrawList &out, UiRect bounds, Color color) {
  out.overlay.emplace_back(FilledRectangle{bounds, color});
}
void clipped_text(DrawList &out, UiRect bounds, UiRect clip,
                  std::string value, Color color, int pixels) {
  out.overlay.emplace_back(Text{{bounds.x, bounds.y}, std::move(value), color,
                                pixels, bounds.width, clip, TextAlign::Left,
                                FontFace::Interface});
}
[[nodiscard]] std::string number(double value, int precision = 1) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(precision) << value;
  return stream.str();
}
} // namespace

ColonyWorkspaceLayout ColonyWorkspaceLayout::for_viewport(int width,
                                                           int height) noexcept {
  const auto w = static_cast<float>(width);
  const auto h = static_cast<float>(height);
  const auto requested = std::max(1.f, h / 900.f);
  const auto fit = std::max(.55f, std::min(w / 1020.f, h / 650.f));
  const auto scale = std::min(requested, fit);
  const auto margin = 14.f * scale;
  const auto left_margin = native_navigation_content_left * scale;
  const auto top = native_workspace_top(width,height);
  const UiRect surface{left_margin, top,
                       std::max(1.f, w - left_margin - margin),
                       std::max(1.f, h - top - margin)};
  ColonyWorkspaceLayout result;
  result.scale=scale;result.surface=surface;
  result.title_font_pixels=static_cast<int>(24*scale);
  result.body_font_pixels=static_cast<int>(15*scale);
  result.small_font_pixels=static_cast<int>(12*scale);
  const float mw = std::min(surface.width - 30.f * scale, 700.f * scale);
  const float mh = std::min(surface.height - 30.f * scale, 440.f * scale);
  result.freight_review = {surface.x + (surface.width - mw) * .5f,
      surface.y + (surface.height - mh) * .5f, mw, mh};
  const auto modal = result.freight_review;
  result.freight_text = {modal.x + 18.f * scale, modal.y + 56.f * scale,
      mw - 36.f * scale, mh - 124.f * scale};
  result.freight_cancel = {modal.x + 18.f * scale, modal.y + mh - 52.f * scale,
      (mw - 50.f * scale) * .5f, 34.f * scale};
  result.freight_confirm = {result.freight_cancel.x + result.freight_cancel.width + 14.f * scale,
      result.freight_cancel.y, result.freight_cancel.width, result.freight_cancel.height};
  return result;
}

std::string NativeColonyWorkspace::tr(std::string_view key,
                                      std::string_view fallback) const {
  if (locale_ && locale_->contains(key))
    return std::string(locale_->translate(key));
  return std::string(fallback);
}

std::string NativeColonyWorkspace::trf(
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

void NativeColonyWorkspace::open(NativeColonyView view) {
  cancel_freight();
  visible_ = true;
  planetary_.reset();planetary_.set_view(view);
  view_ = std::move(view);
}

void NativeColonyWorkspace::set_view(NativeColonyView view) {
  planetary_.set_view(view);
  if (view_ && (view_->campaign_generation != view.campaign_generation ||
                view_->player_civilization_id != view.player_civilization_id ||
                view_->colony_id != view.colony_id || view_->body_id != view.body_id ||
                view_->system_id != view.system_id || view_->resource_outpost != view.resource_outpost)) {
    cancel_freight();
    pointer_ = {};
  }
  view_ = std::move(view);
}

void NativeColonyWorkspace::close() noexcept { cancel_freight(); planetary_.reset(); visible_ = false; }

void NativeColonyWorkspace::discard_campaign() noexcept {
  cancel_freight();
  planetary_.reset();
  visible_ = false;
  view_.reset();
  pointer_ = {};
}

void NativeColonyWorkspace::cancel_freight() noexcept {
  freight_preview_.reset(); freight_text_.clear(); freight_scroll_ = {};
  freight_pressed_ = ColonyWorkspaceCommandKind::None;
  focus_ = -1;
}

void NativeColonyWorkspace::set_freight_preview(NativeOutpostFreightPreview preview) {
  cancel_freight();
  if (!visible_ || !view_ || !view_->resource_outpost ||
      preview.campaign_generation != view_->campaign_generation ||
      preview.player_civilization_id != view_->player_civilization_id ||
      preview.colony_id != view_->colony_id || preview.body_id != view_->body_id ||
      preview.system_id != view_->system_id) return;
  if (preview.accepted) {
    freight_text_ = trf("COLONY_FREIGHT_PREVIEW",
        {preview.fleet_name, preview.home_name, preview.outpost_name,
         number(preview.cargo_capacity, 1), number(preview.stored_materials, 1),
         number(preview.extraction_per_day, 2)},
        "Ship: {0}\nDeparture: {1}\nCollection: {2}\nCargo capacity: {3} material units\nStored for pickup: {4} | Extraction: {5} / day\n\nNo upfront dispatch fee. Ongoing ship upkeep still applies.\nThe ship travels, loads cargo over time, then returns to its departure colony. Materials reach your stores only after unloading. Unpause to begin.");
  } else freight_text_ = preview.message;
  freight_preview_ = std::move(preview);
}

float NativeColonyWorkspace::freight_content_height(const ColonyWorkspaceLayout& layout) const {
  const Text label{{}, freight_text_, bright, layout.body_font_pixels, layout.freight_text.width};
  if (measure_) return static_cast<float>(std::max(1, measure_(label).height));
  const auto columns = std::max(1.f, layout.freight_text.width / (layout.body_font_pixels * .56f));
  float lines = 1.f, column = 0.f;
  for (const unsigned char c : freight_text_) {
    if (c == '\n') { ++lines; column = 0.f; }
    else if (++column > columns) { ++lines; column = 1.f; }
  }
  return lines * (layout.body_font_pixels + 4.f * layout.scale);
}

ColonyWorkspaceCommand NativeColonyWorkspace::handle(const InputEvent &event,
                                                       int width, int height) {
  if (!visible_) return {};
  if(!freight_preview_){
    auto command=planetary_.handle(event,width,height);
    if(command.action==PlanetaryAction::Back){close();return {ColonyWorkspaceCommandKind::Close,true};}
    if(command.action==PlanetaryAction::Freight)return {ColonyWorkspaceCommandKind::ReviewFreight,true};
    return {ColonyWorkspaceCommandKind::Planetary,true,0,std::move(command)};
  }
  pointer_ = event.position;
  const auto layout = ColonyWorkspaceLayout::for_viewport(width, height);
  if (event.type == InputEventType::PointerCancelled) {
    const bool reviewing = freight_preview_.has_value(); cancel_freight();
    return {reviewing ? ColonyWorkspaceCommandKind::CancelFreight : ColonyWorkspaceCommandKind::None, reviewing};
  }
  if (freight_preview_) {
    if (event.type == InputEventType::EscapePressed) {
      cancel_freight(); return {ColonyWorkspaceCommandKind::CancelFreight, true};
    }
    if (event.type == InputEventType::KeyPressed && event.key) {
      // SDL_Keycode: Tab/arrows move the ring over [cancel, confirm];
      // Return/Space replay the press+release gesture. Confirm stays
      // unreachable while the preview rejects the dispatch.
      constexpr std::uint32_t kTab=9u,kReturn=13u,kSpace=32u;
      constexpr std::uint32_t kRight=0x4000004fu,kLeft=0x40000050u,kDown=0x40000051u,kUp=0x40000052u;
      constexpr std::uint32_t kHome=0x4000004au,kEnd=0x4000004du;
      const int count=freight_preview_->accepted?2:1;
      const bool fwd=(event.key==kTab&&!event.shift)||event.key==kRight||event.key==kDown;
      const bool bwd=(event.key==kTab&&event.shift)||event.key==kLeft||event.key==kUp;
      if(event.key==kHome||event.key==kEnd){focus_=event.key==kHome?0:count-1;return {ColonyWorkspaceCommandKind::None,true};}
      if(fwd||bwd){focus_=focus_<0?(bwd?count-1:0):(focus_+(bwd?-1:1)+count)%count;return {ColonyWorkspaceCommandKind::None,true};}
      if((event.key==kReturn||event.key==kSpace)&&focus_>=0){
        const auto&rect=focus_==0?layout.freight_cancel:layout.freight_confirm;
        InputEvent press{InputEventType::LeftPressed},release{InputEventType::LeftReleased};
        press.position=release.position={rect.x+rect.width*.5f,rect.y+rect.height*.5f};
        const int keep=focus_;static_cast<void>(handle(press,width,height));
        auto command=handle(release,width,height);
        if(freight_preview_)focus_=keep;return command;
      }
      return {ColonyWorkspaceCommandKind::None,true};
    }
    const auto target = layout.freight_cancel.contains(event.position)
        ? ColonyWorkspaceCommandKind::CancelFreight
        : freight_preview_->accepted && layout.freight_confirm.contains(event.position)
        ? ColonyWorkspaceCommandKind::ConfirmFreight : ColonyWorkspaceCommandKind::None;
    if (event.type == InputEventType::LeftPressed) { freight_pressed_ = target; focus_ = -1; }
    if (event.type == InputEventType::LeftReleased) {
      const auto pressed = std::exchange(freight_pressed_, ColonyWorkspaceCommandKind::None);
      if (pressed == target && target != ColonyWorkspaceCommandKind::None) {
        const auto revision = freight_preview_->revision;
        if (target == ColonyWorkspaceCommandKind::CancelFreight) cancel_freight();
        return {target, true, revision};
      }
    }
    if (event.type == InputEventType::Wheel && layout.freight_text.contains(event.position)) {
      freight_scroll_.sync(freight_content_height(layout),
                           layout.freight_text.height);
      freight_scroll_.scroll_by(-event.wheel_y * 44.f * layout.scale);
    }
    return {ColonyWorkspaceCommandKind::None, true};
  }
  return {};
}

void NativeColonyWorkspace::render(DrawList &out, int width, int height) const {
  if (!visible_ || !view_) return;
    planetary_.render(out,*view_,width,height);
    if(freight_preview_){
      const auto l=ColonyWorkspaceLayout::for_viewport(width,height);
      fill(out,l.surface,{0,4,10,185});stellar::native_menu_style::panel(out,l.freight_review,l.scale);
      const auto clip=l.freight_text;clipped_text(out,{clip.x,clip.y-freight_scroll_.scroll_offset,clip.width,freight_content_height(l)},clip,freight_text_,bright,l.body_font_pixels);
      stellar::native_menu_style::button(out,l.freight_cancel,tr("COLONY_FREIGHT_CANCEL","Cancel"),l.body_font_pixels,l.freight_cancel.contains(pointer_));
      stellar::native_menu_style::button(out,l.freight_confirm,tr("COLONY_FREIGHT_CONFIRM","Confirm dispatch"),l.body_font_pixels,l.freight_confirm.contains(pointer_),freight_preview_->accepted);
      if(focus_>=0)out.overlay.emplace_back(stellar::native_map::StrokedRectangle{focus_==0?l.freight_cancel:l.freight_confirm,{160,210,255,255}});
    }
}

} // namespace stellar::native_colony_ui
