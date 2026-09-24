#include "native_startup_workspace.hpp"
#include "native_menu_style.hpp"
#include <stellar/engine/localization.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace stellar::native_startup_ui {
namespace {
using namespace stellar::native_map;
constexpr Color background{3,9,18,255},panel{8,20,36,252},raised{12,31,54,250};
constexpr Color hover{24,61,94,252},selected{23,67,102,255};
constexpr Color border{91,151,205,235},bright{235,244,255,255};
constexpr Color muted{154,181,211,240},accent{122,230,190,255};
constexpr Color gold{241,195,105,255};
constexpr Color warning{255,190,112,255};
constexpr std::array<std::string_view,5> loading_tips{
  "Tip: Space pauses or resumes time.",
  "Tip: Use the navigation rail to open research, construction, fleets, and relations.",
  "Tip: Survey nearby systems before sending civilian expeditions.",
  "Tip: Save before changing a major plan.",
  "Tip: Select a fleet to review its orders and current readiness."};
constexpr std::array<std::string_view,5> loading_tip_keys{
  "STARTUP_TIP_PAUSE","STARTUP_TIP_RAIL","STARTUP_TIP_SURVEY",
  "STARTUP_TIP_SAVE","STARTUP_TIP_FLEET"};
void fill(DrawList&o,UiRect r,Color c){o.overlay.emplace_back(FilledRectangle{r,c});}
void stroke(DrawList&o,UiRect r,Color c){o.overlay.emplace_back(StrokedRectangle{r,c});}
void text(DrawList&o,UiRect r,std::string v,Color c,int p,TextAlign a=TextAlign::Left,FontFace f=FontFace::Interface){const float x=a==TextAlign::Left?r.x:a==TextAlign::Center?r.x+r.width*.5f:r.x+r.width;const float y=a==TextAlign::Center?r.y+std::max(0.f,(r.height-static_cast<float>(p))*.5f):r.y;o.overlay.emplace_back(Text{{x,y},std::move(v),c,p,r.width,r,a,f});}

}

std::string NativeStartupWorkspace::tr(std::string_view key,
                                       std::string_view fallback) const {
  if (locale_ && locale_->contains(key))
    return std::string(locale_->translate(key));
  return std::string(fallback);
}

namespace {
Point center(UiRect r){return {r.x+r.width*.5f,r.y+r.height*.5f};}
UiRect intersect(UiRect a,UiRect b){const float x=std::max(a.x,b.x),y=std::max(a.y,b.y),right=std::min(a.x+a.width,b.x+b.width),bottom=std::min(a.y+a.height,b.y+b.height);return {x,y,std::max(0.f,right-x),std::max(0.f,bottom-y)};}
UiRect busy_status(int width,int height,float scale){const float w=std::min(760.f*scale,static_cast<float>(width)-36.f*scale);return {(width-w)*.5f,height-194.f*scale,w,130.f*scale};}
UiRect busy_cancel(int width,int height,float scale){return {width*.5f-70.f*scale,height-54.f*scale,140.f*scale,36.f*scale};}
UiRect entry_panel(const StartupLayout&layout,bool return_available){
  const auto bottom=return_available?layout.exit:layout.exit;
  const float left=std::min({layout.title.x,layout.new_campaign.x,layout.load_campaign.x,
                             layout.settings.x,layout.exit.x});
  const float top=layout.title.y;
  return {left,top,layout.title.width,bottom.y+bottom.height-top+24.f*layout.scale};
}
UiRect cover_artwork(const RgbaImage& image,UiRect frame){const float scale=std::max(frame.width/static_cast<float>(image.width()),frame.height/static_cast<float>(image.height()));const float width=image.width()*scale,height=image.height()*scale;return {frame.x+(frame.width-width)*.5f,frame.y+(frame.height-height)*.5f,width,height};}
}

StartupLayout StartupLayout::for_viewport(int width,int height) noexcept{
  const float s=std::clamp(static_cast<float>(height)/1080.f,.8f,2.5f);
  const float margin=18.f*s;
  // The menu's secondary surfaces are wide, centered dialogs.  Entry uses
  // the independent upper-left rectangles below so the artwork remains open.
  const float w=std::min(900.f*s,static_cast<float>(width)-2*margin);
  const float h=std::min(500.f*s,static_cast<float>(height)-2*margin);
  UiRect p{(width-w)*.5f,(height-h)*.5f,w,h};const float pad=22.f*s;
  const float entry_x=64.f*s,entry_y=44.f*s;
  const float entry_w=std::min(390.f*s,std::max(160.f,static_cast<float>(width)-entry_x-margin));
  UiRect title{entry_x,entry_y,entry_w,52*s};
  UiRect subtitle{entry_x,entry_y+57*s,entry_w,28*s};
  const float bw=entry_w,bx=entry_x;
  UiRect return_to_campaign{bx,entry_y+250*s,bw,34*s};
  UiRect n{bx,entry_y+338*s,bw,34*s},l{bx,entry_y+294*s,bw,34*s},settings{bx,entry_y+382*s,bw,34*s},e{bx,entry_y+470*s,bw,34*s};
  UiRect list{p.x+pad,p.y+126*s,p.width-2*pad,p.height-220*s};
  UiRect back{p.x+p.width-pad-112*s,p.y+pad,112*s,36*s};
  UiRect primary{p.x+p.width-pad-190*s,p.y+p.height-66*s,190*s,40*s};
  UiRect status{p.x+pad,p.y+130*s,p.width-2*pad,p.height-230*s};
  const float card_gap=16*s, card_width=(p.width-2*pad-card_gap)*.5f;
  UiRect story{p.x+pad,p.y+118*s,card_width,std::min(310*s,p.height-148*s)};
  UiRect sandbox{story.x+card_width+card_gap,story.y,card_width,story.height};
  return {s,static_cast<int>(30*s),static_cast<int>(20*s),static_cast<int>(15*s),p,title,subtitle,n,l,e,list,back,primary,status,settings,return_to_campaign,story,sandbox,{bx,entry_y+426*s,bw,34*s}};
}

void NativeStartupWorkspace::set_setup(stellar::native_setup::NativeNewCampaignSetupView view){developer_mode_=view.developer_mode;setup_.set_view(std::move(view));}
void NativeStartupWorkspace::set_return_to_campaign_available(bool available)noexcept{return_to_campaign_available_=available;}
void NativeStartupWorkspace::show_setup()noexcept{screen_=StartupScreen::Setup;setup_.begin_sandbox();selected_slot_.reset();load_scroll_=0;failure_.clear();reset_pointer();}
void NativeStartupWorkspace::reset_pointer() noexcept{pointer_={};hover_feedback_.reset();focus_=-1;}
void NativeStartupWorkspace::show_entry() noexcept{screen_=StartupScreen::Entry;selected_slot_.reset();load_scroll_=0;failure_.clear();reset_pointer();}
void NativeStartupWorkspace::set_slots(stellar::native_startup::NativeStartupSaveSlots slots){slots_=std::move(slots);selected_slot_=slots_.slots.empty()?std::nullopt:std::optional<std::size_t>{0};load_scroll_=0;screen_=StartupScreen::LoadSlots;reset_pointer();}
void NativeStartupWorkspace::set_setup_message(std::string message,bool accepted){setup_.set_assessment_message(std::move(message),accepted);}
void NativeStartupWorkspace::begin_operation(stellar::native_startup::NativeStartupView view,StartupOperationOrigin origin){if(view.request_id==0)throw std::invalid_argument("Startup operation origin requires a valid request.");static std::atomic<std::uint64_t> sequence{};const auto moment=static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());auto candidate=static_cast<int>((moment^(++sequence*0x9e3779b97f4a7c15ULL))%loading_tips.size());if(last_loading_tip_>=0&&candidate==last_loading_tip_)candidate=(candidate+1)%static_cast<int>(loading_tips.size());last_loading_tip_=candidate;loading_tip_=tr(loading_tip_keys[static_cast<std::size_t>(candidate)],loading_tips[static_cast<std::size_t>(candidate)]);busy_artwork_=origin==StartupOperationOrigin::NewCampaign?StartupArtworkKind::NewGalaxyGeneration:StartupArtworkKind::SaveRestoration;operation_=std::move(view);screen_=StartupScreen::Busy;reset_pointer();}
void NativeStartupWorkspace::set_operation(stellar::native_startup::NativeStartupView view){if(screen_!=StartupScreen::Busy||operation_.request_id==0||view.request_id!=operation_.request_id)throw std::logic_error("Startup operation update has no matching explicit origin.");operation_=std::move(view);reset_pointer();}
void NativeStartupWorkspace::show_failure(std::string value){failure_=std::move(value);screen_=StartupScreen::Failure;reset_pointer();}
bool NativeStartupWorkspace::wants_text_input()const noexcept{return screen_==StartupScreen::Setup&&setup_.seed_focused();}

std::vector<NativeStartupWorkspace::Focusable> NativeStartupWorkspace::collect_focusables(
    const StartupLayout&l,int width,int height)const{
  std::vector<Focusable> out;
  const auto push=[&](UiRect rect,std::uint64_t cue){if(rect.width>0&&rect.height>0)out.push_back({rect,cue});};
  switch(screen_){
    case StartupScreen::Entry:
      // Visual order: Continue first when present, then the menu links;
      // cue ids match the hit() target space.
      if(return_to_campaign_available_||!continue_save_.empty())push(l.return_to_campaign,6);
      push(l.new_campaign,1);push(l.load_campaign,2);push(l.settings,3);push(l.development,4);push(l.exit,5);
      break;
    case StartupScreen::ModeSelection:push(l.sandbox_campaign,2);push(l.back,1);break;
    case StartupScreen::Development:push(l.primary,2);push(l.back,1);break;
    case StartupScreen::Failure:push(l.back,1);break;
    case StartupScreen::Busy:push(busy_cancel(width,height,l.scale),1);break;
    case StartupScreen::LoadSlots:{
      const float pitch=46.f*l.scale;
      for(std::size_t i=0;i<slots_.slots.size();++i)
        push(intersect({l.list.x,l.list.y+i*pitch-load_scroll_,l.list.width,pitch-4*l.scale},l.list),100+i);
      push(l.back,1);if(selected_slot_)push(l.primary,2);
      break;}
    default:break;
  }
  return out;
}

StartupIntent NativeStartupWorkspace::handle(const InputEvent&e,int width,int height,const TextMeasurer&measure){
  if(e.type==InputEventType::PointerMove)pointer_=e.position;
  if(e.type==InputEventType::PointerCancelled){reset_pointer();if(screen_==StartupScreen::Setup)(void)setup_.handle(e,width,height,measure);return {StartupIntentKind::None,true};}
  const auto l=StartupLayout::for_viewport(width,height);
  if(screen_==StartupScreen::Setup){const auto child=setup_.handle(e,width,height,measure);switch(child.kind){case stellar::native_setup_ui::NativeNewGameIntentKind::Cancel:screen_=StartupScreen::ModeSelection;reset_pointer();return {StartupIntentKind::Back,true};case stellar::native_setup_ui::NativeNewGameIntentKind::CopySetup:return {StartupIntentKind::CopySetup,true,child.seed_text,child.species_id,child.system_count,child.pre_warp_civilization_count,child.ancient_civilization_count,child.stellar_population,{},child.developer_research,child.developer_full_coverage,child.requested_population,child.developer_full_exploration};case stellar::native_setup_ui::NativeNewGameIntentKind::Create:return {StartupIntentKind::Create,true,child.seed_text,child.species_id,child.system_count,child.pre_warp_civilization_count,child.ancient_civilization_count,child.stellar_population,{},child.developer_research,child.developer_full_coverage,child.requested_population,child.developer_full_exploration};default:return {StartupIntentKind::None,child.captured};}}
  std::uint64_t hover_target{};
  using stellar::native_menu_audio::hit;
  if(screen_==StartupScreen::Entry)hover_target=hit(e.position,{l.new_campaign,l.load_campaign,l.settings,l.development,l.exit,(return_to_campaign_available_||!continue_save_.empty())?l.return_to_campaign:UiRect{}});
  else if(screen_==StartupScreen::ModeSelection)hover_target=hit(e.position,{l.back,l.sandbox_campaign});
  else if(screen_==StartupScreen::Development)hover_target=hit(e.position,{l.back,l.primary});
  else if(screen_==StartupScreen::Failure)hover_target=hit(e.position,{l.back});
  else if(screen_==StartupScreen::Busy)hover_target=hit(e.position,{busy_cancel(width,height,l.scale)});
  else if(screen_==StartupScreen::LoadSlots){
    hover_target=hit(e.position,{l.back,selected_slot_?l.primary:UiRect{}});
    if(l.list.contains(e.position))for(std::size_t i=0;i<slots_.slots.size();++i){
      const float pitch=46.f*l.scale;
      const auto row=intersect({l.list.x,l.list.y+i*pitch-load_scroll_,l.list.width,pitch-4*l.scale},l.list);
      if(row.width>0&&row.height>0&&row.contains(e.position)){hover_target=100+i;break;}
    }
  }
  hover_feedback_.update(e,hover_target);
  if(e.type==InputEventType::EscapePressed){if(screen_==StartupScreen::Busy)return {StartupIntentKind::CancelOperation,true};if(screen_==StartupScreen::Entry&&return_to_campaign_available_)return {StartupIntentKind::ReturnToCampaign,true};show_entry();return {StartupIntentKind::Back,true};}
  if(screen_==StartupScreen::LoadSlots&&e.type==InputEventType::Wheel&&l.list.contains(e.position)){
    const float pitch=46.f*l.scale;
    const float maximum=std::max(0.f,pitch*static_cast<float>(slots_.slots.size())-l.list.height);
    load_scroll_=std::clamp(load_scroll_-e.wheel_y*pitch*3.f,0.f,maximum);
    return {StartupIntentKind::None,true};
  }
  if(e.type==InputEventType::KeyPressed){
    // SDL_Keycode: Tab/arrows ring the screen's controls, Home/End jump to
    // the ends, Return/Space re-enter pointer dispatch at the rect center.
    constexpr std::uint32_t kTab=9u,kReturn=13u,kSpace=32u;
    constexpr std::uint32_t kRight=0x4000004fu,kLeft=0x40000050u,kDown=0x40000051u,kUp=0x40000052u;
    constexpr std::uint32_t kHome=0x4000004au,kEnd=0x4000004du;
    const auto focusables=collect_focusables(l,width,height);
    const int count=static_cast<int>(focusables.size());
    const bool fwd=(e.key==kTab&&!e.shift)||e.key==kRight||e.key==kDown;
    const bool bwd=(e.key==kTab&&e.shift)||e.key==kLeft||e.key==kUp;
    if(count>0&&(e.key==kHome||e.key==kEnd||fwd||bwd)){
      if(e.key==kHome)focus_=0;
      else if(e.key==kEnd)focus_=count-1;
      else if(focus_<0)focus_=bwd?count-1:0;
      else focus_=(focus_+(bwd?-1:1)+count)%count;
      if(focus_>=count)focus_=0;
      hover_feedback_.cue(focusables[static_cast<std::size_t>(focus_)].cue);
      return {StartupIntentKind::None,true};
    }
    if((e.key==kReturn||e.key==kSpace)&&focus_>=0&&focus_<count){
      const auto r=focusables[static_cast<std::size_t>(focus_)].rect;
      const int keep=focus_;
      InputEvent click{};click.type=InputEventType::LeftPressed;
      click.position={r.x+r.width*.5f,r.y+r.height*.5f};
      const auto before=screen_;
      auto intent=handle(click,width,height,measure);
      // Keep focus for non-navigating results so an action can repeat;
      // screen changes and navigation intents take it back.
      const bool non_navigating=intent.kind==StartupIntentKind::None||
                                intent.kind==StartupIntentKind::CopyDiagnostics;
      focus_=non_navigating&&screen_==before?keep:-1;
      return intent;
    }
    return {StartupIntentKind::None,true};
  }
  if(e.type!=InputEventType::LeftPressed)return {StartupIntentKind::None,l.panel.contains(e.position)};
  focus_=-1;
  if(screen_==StartupScreen::Entry&&l.development.contains(e.position)){screen_=StartupScreen::Development;reset_pointer();return {StartupIntentKind::None,true};}
  if(screen_==StartupScreen::Development){
    if(l.back.contains(e.position)){show_entry();return {StartupIntentKind::Back,true};}
    if(l.primary.contains(e.position))return {StartupIntentKind::CopyDiagnostics,true};
    return {StartupIntentKind::None,true};
  }
  if(screen_==StartupScreen::Entry){if(l.new_campaign.contains(e.position)){screen_=StartupScreen::ModeSelection;reset_pointer();return {StartupIntentKind::OpenModeSelection,true};}if(l.load_campaign.contains(e.position))return {StartupIntentKind::OpenLoad,true};if(l.settings.contains(e.position))return {StartupIntentKind::OpenSettings,true};if(l.exit.contains(e.position))return {StartupIntentKind::Exit,true};if(l.return_to_campaign.contains(e.position)){if(return_to_campaign_available_)return {StartupIntentKind::ReturnToCampaign,true};if(!continue_save_.empty())return {StartupIntentKind::LoadSelected,true,{},{},0,6,1,{},continue_save_};}}
  else if(screen_==StartupScreen::ModeSelection){if(l.sandbox_campaign.contains(e.position)){show_setup();return {StartupIntentKind::OpenSetup,true};}if(l.back.contains(e.position)){show_entry();return {StartupIntentKind::Back,true};}}
  else if(screen_==StartupScreen::LoadSlots){if(l.back.contains(e.position)){show_entry();return {StartupIntentKind::Back,true};}const float pitch=46.f*l.scale;if(l.list.contains(e.position))for(std::size_t i=0;i<slots_.slots.size();++i){UiRect row{l.list.x,l.list.y+i*pitch-load_scroll_,l.list.width,pitch-4*l.scale};const auto visible=intersect(row,l.list);if(visible.width>0&&visible.height>0&&visible.contains(e.position)){selected_slot_=i;return {StartupIntentKind::None,true};}}if(l.primary.contains(e.position)&&selected_slot_)return {StartupIntentKind::LoadSelected,true,{},{},0,6,1,{},slots_.slots[*selected_slot_].path};}
  else if(screen_==StartupScreen::Busy&&busy_cancel(width,height,l.scale).contains(e.position))return {StartupIntentKind::CancelOperation,true};
  else if(screen_==StartupScreen::Failure&&l.back.contains(e.position)){show_entry();return {StartupIntentKind::Back,true};}
  return {StartupIntentKind::None,(screen_==StartupScreen::Entry?entry_panel(l,return_to_campaign_available_):l.panel).contains(e.position)};
}

void NativeStartupWorkspace::render(DrawList&out,int width,int height,const TextMeasurer&measure,const PortraitProvider*portraits)const{
  render(out,width,height,measure,portraits,nullptr);
}

void NativeStartupWorkspace::render(DrawList&out,int width,int height,const TextMeasurer&measure,const PortraitProvider*portraits,const StartupArtworkProvider*artwork,bool backdrop_only)const{
  std::shared_ptr<const RgbaImage> backdrop;
  if(artwork){const auto kind=screen_==StartupScreen::Busy?busy_artwork_:StartupArtworkKind::MainMenu;backdrop=(*artwork)(kind);}
  if(screen_==StartupScreen::Setup){setup_.render(out,width,height,measure,portraits,std::move(backdrop));return;}
  const auto l=StartupLayout::for_viewport(width,height);const float s=l.scale;
  const auto focus_ring=[&]{
    if(focus_<0)return;
    const auto fs=collect_focusables(l,width,height);
    if(focus_<static_cast<int>(fs.size()))
      out.overlay.emplace_back(StrokedRectangle{fs[static_cast<std::size_t>(focus_)].rect,{160,210,255,255}});
  };
  if(backdrop){const auto destination=startup_artwork_destination(backdrop->width(),backdrop->height(),width,height);out.overlay.emplace_back(Image{std::move(backdrop),destination,std::nullopt,{255,255,255,255},UiRect{0,0,static_cast<float>(width),static_cast<float>(height)}});}else fill(out,{0,0,static_cast<float>(width),static_cast<float>(height)},background);
  if(backdrop_only)return;
  if(screen_!=StartupScreen::Busy&&screen_!=StartupScreen::Entry){
    native_menu_style::panel(out,l.panel,s);
    if(screen_!=StartupScreen::ModeSelection)text(out,{l.panel.x+22*s,l.panel.y+22*s,l.panel.width-170*s,34*s},"STELLAR CONTINUUM",bright,l.heading_font,TextAlign::Left,FontFace::Heading);
  }
  if(screen_==StartupScreen::Entry){
    const auto logo=artwork?(*artwork)(StartupArtworkKind::TitleLogo):nullptr;
    if(logo)out.overlay.emplace_back(Image{logo,{l.title.x-12*s,l.title.y,330*s,220*s},std::nullopt,{255,255,255,255},std::nullopt});
    else {text(out,l.title,"STELLAR",bright,l.heading_font+16,TextAlign::Left,FontFace::Heading);text(out,l.subtitle,"C O N T I N U U M",bright,l.body_font,TextAlign::Left);}
    text(out,{l.title.x,l.title.y+224*s,l.title.width,18*s},tr("STARTUP_TAGLINE","THE FIRST LIGHT OF AN INTERSTELLAR AGE"),gold,l.small_font,TextAlign::Left);
    const auto link=[&](UiRect rect,std::string_view label,bool active=false){
      const bool hovered=rect.contains(pointer_);
      if(active||hovered) fill(out,{rect.x-7*s,rect.y+4*s,2*s,rect.height-8*s},active?accent:border);
      text(out,rect,std::string(label),hovered?accent:bright,l.body_font,TextAlign::Left);
    };
    if(return_to_campaign_available_||!continue_save_.empty()) link(l.return_to_campaign,tr("STARTUP_CONTINUE","Continue"),true);
    else text(out,l.return_to_campaign,tr("STARTUP_CONTINUE","Continue"),muted,l.body_font);
    link(l.new_campaign,tr("STARTUP_NEW_GAME","New Game"));
    link(l.load_campaign,tr("STARTUP_LOAD","Load saved campaign"));
    link(l.settings,tr("STARTUP_SETTINGS","Settings"));
    link(l.development,tr("STARTUP_DEVELOPMENT","Development"));
    link(l.exit,tr("STARTUP_EXIT","Exit to Windows"));
    const UiRect footer{l.title.x,std::max(l.exit.y+l.exit.height+30*s,static_cast<float>(height)-74*s),l.title.width,16*s};
    text(out,footer,tr(developer_mode_?"STARTUP_DEV_MODE":"STARTUP_PLAYER_MODE",developer_mode_?"DEV MODE · ISOLATED SAVES":"PLAYER MODE"),accent,l.small_font,TextAlign::Left);
    if(!build_label_.empty()) text(out,{footer.x,footer.y+17*s,footer.width,16*s},build_label_,muted,l.small_font,TextAlign::Left);
    focus_ring();return;
  }
  if(screen_==StartupScreen::Development){
    text(out,{l.panel.x+22*s,l.panel.y+70*s,l.panel.width-44*s,30*s},tr("STARTUP_DEV_TITLE","DEVELOPMENT & DIAGNOSTICS"),gold,l.body_font);
    text(out,{l.panel.x+22*s,l.panel.y+116*s,l.panel.width-44*s,126*s},build_label_+"\n"+diagnostics_,bright,l.body_font);
    text(out,{l.panel.x+22*s,l.panel.y+266*s,l.panel.width-44*s,116*s},
      tr("STARTUP_DEV_HELP","F12 captures the screen. Export diagnostics from the pause menu.\n\nOpen the Developer Game launcher, then choose New Game > Sandbox. The footer shows DEV MODE. Saves stay separate.\n\nManual launch: --dev-game enters developer mode directly. Ctrl+Shift+F12 toggles mode only in a --devtools launch."),muted,l.small_font);
    native_menu_style::button(out,l.primary,tr("STARTUP_COPY_SYSINFO","Copy system info"),l.small_font,l.primary.contains(pointer_),true,s);
    native_menu_style::button(out,l.back,tr("SETTINGS_BACK","Back"),l.body_font,l.back.contains(pointer_),true,s);
    focus_ring();return;
  }
  if(screen_==StartupScreen::ModeSelection){
    text(out,{l.panel.x+22*s,l.panel.y+22*s,l.panel.width-170*s,34*s},tr("STARTUP_CHOOSE_GAME","CHOOSE YOUR GAME"),bright,l.heading_font,TextAlign::Left,FontFace::Heading);
    text(out,{l.panel.x+22*s,l.panel.y+62*s,l.panel.width-44*s,24*s},tr("STARTUP_CHOOSE_HINT","Select how your civilization's journey will begin."),muted,l.small_font,TextAlign::Left);
    // These are generic art resources rather than loading-card captures; the
    // provider can supply the approved card artwork without coupling UI state.
    const auto story_art=artwork?(*artwork)(StartupArtworkKind::MainMenu):nullptr;
    const auto sandbox_art=artwork?(*artwork)(StartupArtworkKind::GalaxyCard):nullptr;
    const auto render_card=[&](UiRect card,std::string_view title,
                               std::string_view description,bool enabled,
                               const std::shared_ptr<const RgbaImage>& image){
      fill(out,card,enabled&&card.contains(pointer_)?hover:raised);
      const UiRect art{card.x+6*s,card.y+6*s,card.width-12*s,card.height*.56f};
      if(image){
        const Color tint=enabled?bright:Color{150,150,150,255};
        out.overlay.emplace_back(Image{image,cover_artwork(*image,art),
                                       std::nullopt,tint,art});
      }
      stroke(out,card,enabled?accent:muted);
      if(!enabled){
        const UiRect unavailable{card.x+card.width*.5f-66*s,card.y+card.height*.28f,132*s,32*s};
        fill(out,unavailable,{8,20,36,230});stroke(out,unavailable,gold);
        text(out,unavailable,tr("STARTUP_COMING_SOON","COMING SOON"),gold,l.small_font,TextAlign::Center);
      }
      text(out,{card.x+10*s,card.y+card.height-94*s,card.width-20*s,24*s},
           std::string(title),bright,l.body_font,TextAlign::Left);
      text(out,{card.x+10*s,card.y+card.height-64*s,card.width-20*s,48*s},
           std::string(description),bright,l.small_font,TextAlign::Left);
      if(enabled)text(out,{card.x+10*s,card.y+card.height-20*s,card.width-20*s,18*s},
                      tr("STARTUP_START_SANDBOX","START SANDBOX  >"),gold,std::max(11,l.small_font-2),TextAlign::Left);
    };
    render_card(l.story_campaign,tr("STARTUP_STORY_TITLE","STORY CAMPAIGN"),tr("STARTUP_STORY_DESC","A guided narrative with authored characters, conflicts and discoveries."),false,story_art);
    render_card(l.sandbox_campaign,tr("STARTUP_SANDBOX_TITLE","SANDBOX"),tr("STARTUP_SANDBOX_DESC","Set the galaxy scale, rivals, ancient empires, and your people."),true,sandbox_art);
    fill(out,l.back,raised);stroke(out,l.back,border);
    text(out,l.back,tr("STARTUP_BACK","BACK"),bright,l.body_font,TextAlign::Center);
    focus_ring();return;
  }
  if(screen_==StartupScreen::LoadSlots){text(out,{l.panel.x+22*s,l.panel.y+70*s,l.panel.width-44*s,28*s},tr("STARTUP_SELECT_SAVE","SELECT A SAVED CAMPAIGN"),bright,l.body_font);if(!slots_.error.empty())text(out,l.list,slots_.error,warning,l.body_font);else if(slots_.slots.empty())text(out,l.list,tr("STARTUP_NO_SAVES","No saved campaigns are available."),muted,l.body_font);else{const float pitch=46.f*s;for(std::size_t i=0;i<slots_.slots.size();++i){UiRect row{l.list.x,l.list.y+i*pitch-load_scroll_,l.list.width,pitch-4*s};const auto visible=intersect(row,l.list);if(visible.width<=0||visible.height<=0)continue;fill(out,visible,selected_slot_==i?selected:raised);stroke(out,visible,selected_slot_==i?accent:border);UiRect label{row.x+10*s,row.y+11*s,row.width-20*s,row.height-12*s};const auto clip=intersect(label,l.list);if(clip.width>0&&clip.height>0)out.overlay.emplace_back(Text{{label.x,label.y},slots_.slots[i].filename,bright,l.body_font,label.width,clip,TextAlign::Left,FontFace::Interface});}}fill(out,l.back,raised);stroke(out,l.back,border);text(out,l.back,tr("STARTUP_BACK","BACK"),bright,l.body_font,TextAlign::Center);fill(out,l.primary,selected_slot_?selected:raised);stroke(out,l.primary,selected_slot_?accent:muted);text(out,l.primary,tr("STARTUP_LOAD_SELECTED","LOAD SELECTED"),selected_slot_?bright:muted,l.body_font,TextAlign::Center);focus_ring();return;}
  if(screen_==StartupScreen::Busy){const auto status=busy_status(width,height,s),cancel=busy_cancel(width,height,s);fill(out,status,{5,14,27,210});stroke(out,status,border);text(out,{status.x+18*s,status.y+12*s,status.width-36*s,26*s},operation_.status,bright,l.body_font,TextAlign::Center);if(operation_.determinate_progress){UiRect track{status.x+50*s,status.y+50*s,status.width-100*s,12*s};fill(out,track,raised);fill(out,{track.x,track.y,track.width*static_cast<float>(std::clamp(*operation_.determinate_progress,0.,1.)),track.height},accent);}else {const UiRect track{status.x+50*s,status.y+50*s,status.width-100*s,12*s};fill(out,track,raised);const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();const float cycle=static_cast<float>(std::fmod(seconds,2.4)/2.4);const float travel=cycle<.5f?cycle*2.f:2.f-cycle*2.f;const float segment=track.width*.22f;fill(out,{track.x+(track.width-segment)*travel,track.y,segment,track.height},accent);}text(out,{status.x+20*s,status.y+82*s,status.width-40*s,34*s},loading_tip_,muted,l.small_font,TextAlign::Center);fill(out,cancel,{8,20,36,220});stroke(out,cancel,border);text(out,cancel,tr(operation_.worker_running?"SETTINGS_CANCEL":"STARTUP_BACK",operation_.worker_running?"CANCEL":"BACK"),bright,l.body_font,TextAlign::Center);focus_ring();return;}
  text(out,{l.panel.x+22*s,l.panel.y+70*s,l.panel.width-44*s,28*s},tr("STARTUP_FAILED","STARTUP COULD NOT COMPLETE"),warning,l.body_font,TextAlign::Center);text(out,l.status,failure_,warning,l.body_font,TextAlign::Center);fill(out,l.back,raised);stroke(out,l.back,border);text(out,l.back,tr("STARTUP_BACK","BACK"),bright,l.body_font,TextAlign::Center);
  focus_ring();
}
} // namespace stellar::native_startup_ui
