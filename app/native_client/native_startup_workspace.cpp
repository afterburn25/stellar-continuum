#include "native_startup_workspace.hpp"

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
void fill(DrawList&o,UiRect r,Color c){o.overlay.emplace_back(FilledRectangle{r,c});}
void stroke(DrawList&o,UiRect r,Color c){o.overlay.emplace_back(StrokedRectangle{r,c});}
void text(DrawList&o,UiRect r,std::string v,Color c,int p,TextAlign a=TextAlign::Left,FontFace f=FontFace::Interface){const float x=a==TextAlign::Left?r.x:a==TextAlign::Center?r.x+r.width*.5f:r.x+r.width;const float y=a==TextAlign::Center?r.y+std::max(0.f,(r.height-static_cast<float>(p))*.5f):r.y;o.overlay.emplace_back(Text{{x,y},std::move(v),c,p,r.width,r,a,f});}
Point center(UiRect r){return {r.x+r.width*.5f,r.y+r.height*.5f};}
UiRect intersect(UiRect a,UiRect b){const float x=std::max(a.x,b.x),y=std::max(a.y,b.y),right=std::min(a.x+a.width,b.x+b.width),bottom=std::min(a.y+a.height,b.y+b.height);return {x,y,std::max(0.f,right-x),std::max(0.f,bottom-y)};}
UiRect busy_status(int width,int height,float scale){const float w=std::min(760.f*scale,static_cast<float>(width)-36.f*scale);return {(width-w)*.5f,height-194.f*scale,w,130.f*scale};}
UiRect busy_cancel(int width,int height,float scale){return {width*.5f-70.f*scale,height-54.f*scale,140.f*scale,36.f*scale};}
UiRect entry_panel(const StartupLayout&layout,bool return_available){const auto bottom=return_available?layout.return_to_campaign:layout.exit;return {layout.panel.x,layout.panel.y,layout.panel.width,bottom.y+bottom.height-layout.panel.y+24.f*layout.scale};}
UiRect cover_artwork(const RgbaImage& image,UiRect frame){const float scale=std::max(frame.width/static_cast<float>(image.width()),frame.height/static_cast<float>(image.height()));const float width=image.width()*scale,height=image.height()*scale;return {frame.x+(frame.width-width)*.5f,frame.y+(frame.height-height)*.5f,width,height};}
}

StartupLayout StartupLayout::for_viewport(int width,int height) noexcept{
  const float s=std::clamp(static_cast<float>(height)/900.f,1.f,2.4f);
  const float margin=18.f*s,w=std::min(520.f*s,static_cast<float>(width)-2*margin);
  const float h=std::min(620.f*s,static_cast<float>(height)-2*margin);
  UiRect p{margin,(height-h)*.5f,w,h};const float pad=22.f*s;
  UiRect title{p.x+pad,p.y+pad,p.width-2*pad,44*s};
  UiRect subtitle{p.x+pad,p.y+72*s,p.width-2*pad,44*s};
  const float bw=p.width-2*pad,bx=p.x+pad;
  UiRect n{bx,p.y+150*s,bw,58*s},l{bx,p.y+220*s,bw,58*s},settings{bx,p.y+290*s,bw,58*s},e{bx,p.y+360*s,bw,58*s},return_to_campaign{bx,p.y+430*s,bw,58*s};
  UiRect list{p.x+pad,p.y+126*s,p.width-2*pad,p.height-220*s};
  UiRect back{p.x+pad,p.y+p.height-66*s,120*s,40*s};
  UiRect primary{p.x+p.width-pad-190*s,p.y+p.height-66*s,190*s,40*s};
  UiRect status{p.x+pad,p.y+130*s,p.width-2*pad,p.height-230*s};
  const float card_gap=16*s, card_width=(p.width-2*pad-card_gap)*.5f;
  UiRect story{p.x+pad,p.y+140*s,card_width,std::min(300*s,p.height-230*s)};
  UiRect sandbox{story.x+card_width+card_gap,story.y,card_width,story.height};
  return {s,static_cast<int>(28*s),static_cast<int>(16*s),static_cast<int>(13*s),p,title,subtitle,n,l,e,list,back,primary,status,settings,return_to_campaign,story,sandbox};
}

void NativeStartupWorkspace::set_setup(stellar::native_setup::NativeNewCampaignSetupView view){setup_.set_view(std::move(view));}
void NativeStartupWorkspace::set_return_to_campaign_available(bool available)noexcept{return_to_campaign_available_=available;}
void NativeStartupWorkspace::show_setup()noexcept{screen_=StartupScreen::Setup;setup_.randomize_seed();selected_slot_.reset();load_scroll_=0;failure_.clear();reset_pointer();}
void NativeStartupWorkspace::reset_pointer() noexcept{pointer_={};}
void NativeStartupWorkspace::show_entry() noexcept{screen_=StartupScreen::Entry;selected_slot_.reset();load_scroll_=0;failure_.clear();reset_pointer();}
void NativeStartupWorkspace::set_slots(stellar::native_startup::NativeStartupSaveSlots slots){slots_=std::move(slots);selected_slot_=slots_.slots.empty()?std::nullopt:std::optional<std::size_t>{0};load_scroll_=0;screen_=StartupScreen::LoadSlots;reset_pointer();}
void NativeStartupWorkspace::set_setup_message(std::string message,bool accepted){setup_.set_assessment_message(std::move(message),accepted);}
void NativeStartupWorkspace::begin_operation(stellar::native_startup::NativeStartupView view,StartupOperationOrigin origin){if(view.request_id==0)throw std::invalid_argument("Startup operation origin requires a valid request.");static std::atomic<std::uint64_t> sequence{};const auto moment=static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());auto candidate=static_cast<int>((moment^(++sequence*0x9e3779b97f4a7c15ULL))%loading_tips.size());if(last_loading_tip_>=0&&candidate==last_loading_tip_)candidate=(candidate+1)%static_cast<int>(loading_tips.size());last_loading_tip_=candidate;loading_tip_=std::string(loading_tips[static_cast<std::size_t>(candidate)]);busy_artwork_=origin==StartupOperationOrigin::NewCampaign?StartupArtworkKind::NewGalaxyGeneration:StartupArtworkKind::SaveRestoration;operation_=std::move(view);screen_=StartupScreen::Busy;reset_pointer();}
void NativeStartupWorkspace::set_operation(stellar::native_startup::NativeStartupView view){if(screen_!=StartupScreen::Busy||operation_.request_id==0||view.request_id!=operation_.request_id)throw std::logic_error("Startup operation update has no matching explicit origin.");operation_=std::move(view);reset_pointer();}
void NativeStartupWorkspace::show_failure(std::string value){failure_=std::move(value);screen_=StartupScreen::Failure;reset_pointer();}
bool NativeStartupWorkspace::wants_text_input()const noexcept{return screen_==StartupScreen::Setup&&setup_.seed_focused();}

StartupIntent NativeStartupWorkspace::handle(const InputEvent&e,int width,int height,const TextMeasurer&measure){
  if(e.type==InputEventType::PointerMove)pointer_=e.position;
  if(e.type==InputEventType::PointerCancelled){reset_pointer();if(screen_==StartupScreen::Setup)(void)setup_.handle(e,width,height,measure);return {StartupIntentKind::None,true};}
  const auto l=StartupLayout::for_viewport(width,height);
  if(screen_==StartupScreen::Setup){const auto child=setup_.handle(e,width,height,measure);switch(child.kind){case stellar::native_setup_ui::NativeNewGameIntentKind::Cancel:screen_=StartupScreen::ModeSelection;reset_pointer();return {StartupIntentKind::Back,true};case stellar::native_setup_ui::NativeNewGameIntentKind::Create:return {StartupIntentKind::Create,true,child.seed_text,child.species_id,child.system_count,child.pre_warp_civilization_count,child.ancient_civilization_count};default:return {StartupIntentKind::None,child.captured};}}
  if(e.type==InputEventType::EscapePressed){if(screen_==StartupScreen::Busy)return {StartupIntentKind::CancelOperation,true};if(screen_==StartupScreen::Entry&&return_to_campaign_available_)return {StartupIntentKind::ReturnToCampaign,true};show_entry();return {StartupIntentKind::Back,true};}
  if(screen_==StartupScreen::LoadSlots&&e.type==InputEventType::Wheel&&l.list.contains(e.position)){
    const float pitch=46.f*l.scale;
    const float maximum=std::max(0.f,pitch*static_cast<float>(slots_.slots.size())-l.list.height);
    load_scroll_=std::clamp(load_scroll_-e.wheel_y*pitch*3.f,0.f,maximum);
    return {StartupIntentKind::None,true};
  }
  if(e.type!=InputEventType::LeftPressed)return {StartupIntentKind::None,l.panel.contains(e.position)};
  if(screen_==StartupScreen::Entry){if(l.new_campaign.contains(e.position)){screen_=StartupScreen::ModeSelection;reset_pointer();return {StartupIntentKind::OpenModeSelection,true};}if(l.load_campaign.contains(e.position))return {StartupIntentKind::OpenLoad,true};if(l.settings.contains(e.position))return {StartupIntentKind::OpenSettings,true};if(l.exit.contains(e.position))return {StartupIntentKind::Exit,true};if(return_to_campaign_available_&&l.return_to_campaign.contains(e.position))return {StartupIntentKind::ReturnToCampaign,true};}
  else if(screen_==StartupScreen::ModeSelection){if(l.sandbox_campaign.contains(e.position)){show_setup();return {StartupIntentKind::OpenSetup,true};}if(l.back.contains(e.position)){show_entry();return {StartupIntentKind::Back,true};}}
  else if(screen_==StartupScreen::LoadSlots){if(l.back.contains(e.position)){show_entry();return {StartupIntentKind::Back,true};}const float pitch=46.f*l.scale;if(l.list.contains(e.position))for(std::size_t i=0;i<slots_.slots.size();++i){UiRect row{l.list.x,l.list.y+i*pitch-load_scroll_,l.list.width,pitch-4*l.scale};const auto visible=intersect(row,l.list);if(visible.width>0&&visible.height>0&&visible.contains(e.position)){selected_slot_=i;return {StartupIntentKind::None,true};}}if(l.primary.contains(e.position)&&selected_slot_)return {StartupIntentKind::LoadSelected,true,{},{},0,6,1,slots_.slots[*selected_slot_].path};}
  else if(screen_==StartupScreen::Busy&&busy_cancel(width,height,l.scale).contains(e.position))return {StartupIntentKind::CancelOperation,true};
  else if(screen_==StartupScreen::Failure&&l.back.contains(e.position)){show_entry();return {StartupIntentKind::Back,true};}
  return {StartupIntentKind::None,(screen_==StartupScreen::Entry?entry_panel(l,return_to_campaign_available_):l.panel).contains(e.position)};
}

void NativeStartupWorkspace::render(DrawList&out,int width,int height,const TextMeasurer&measure,const PortraitProvider*portraits)const{
  render(out,width,height,measure,portraits,nullptr);
}

void NativeStartupWorkspace::render(DrawList&out,int width,int height,const TextMeasurer&measure,const PortraitProvider*portraits,const StartupArtworkProvider*artwork)const{
  std::shared_ptr<const RgbaImage> backdrop;
  if(artwork){const auto kind=screen_==StartupScreen::Busy?busy_artwork_:StartupArtworkKind::MainMenu;backdrop=(*artwork)(kind);}
  if(screen_==StartupScreen::Setup){setup_.render(out,width,height,measure,portraits,std::move(backdrop));return;}
  const auto l=StartupLayout::for_viewport(width,height);const float s=l.scale;
  if(backdrop){const auto destination=startup_artwork_destination(backdrop->width(),backdrop->height(),width,height);out.overlay.emplace_back(Image{std::move(backdrop),destination,std::nullopt,{255,255,255,255},UiRect{0,0,static_cast<float>(width),static_cast<float>(height)}});}else fill(out,{0,0,static_cast<float>(width),static_cast<float>(height)},background);
  if(screen_!=StartupScreen::Busy&&screen_!=StartupScreen::Entry){fill(out,l.panel,artwork?Color{8,20,36,210}:panel);stroke(out,l.panel,border);text(out,l.title,"STELLAR CONTINUUM",bright,l.heading_font,TextAlign::Center,FontFace::Heading);}
  if(screen_==StartupScreen::Entry){text(out,l.title,"STELLAR",bright,l.heading_font+16,TextAlign::Left,FontFace::Heading);text(out,l.subtitle,"C O N T I N U U M",bright,l.body_font,TextAlign::Left);for(const auto&item:{std::pair{l.new_campaign,"NEW GAME"},std::pair{l.load_campaign,"LOAD CAMPAIGN"},std::pair{l.settings,"SETTINGS"},std::pair{l.exit,"EXIT TO WINDOWS"}}){fill(out,item.first,item.first.contains(pointer_)?hover:raised);stroke(out,item.first,border);text(out,item.first,item.second,bright,l.body_font,TextAlign::Center);}if(return_to_campaign_available_){fill(out,l.return_to_campaign,l.return_to_campaign.contains(pointer_)?hover:selected);stroke(out,l.return_to_campaign,accent);text(out,l.return_to_campaign,"RETURN TO CAMPAIGN",bright,l.body_font,TextAlign::Center);}return;}
  if(screen_==StartupScreen::ModeSelection){
    text(out,l.subtitle,"CHOOSE YOUR GAME",bright,l.body_font,TextAlign::Center);
    const auto story_art=artwork?(*artwork)(StartupArtworkKind::ApplicationStartup):nullptr;
    const auto sandbox_art=artwork?(*artwork)(StartupArtworkKind::NewGalaxyGeneration):nullptr;
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
      text(out,{card.x+10*s,card.y+card.height-72*s,card.width-20*s,24*s},
           std::string(title),enabled?bright:muted,l.body_font,TextAlign::Center);
      text(out,{card.x+10*s,card.y+card.height-42*s,card.width-20*s,20*s},
           std::string(description),enabled?accent:gold,l.small_font,TextAlign::Center);
    };
    render_card(l.story_campaign,"STORY CAMPAIGN","COMING SOON",false,story_art);
    render_card(l.sandbox_campaign,"SANDBOX","Explore. Build. Discover.",true,sandbox_art);
    fill(out,l.back,raised);stroke(out,l.back,border);
    text(out,l.back,"BACK",bright,l.body_font,TextAlign::Center);
    return;
  }
  if(screen_==StartupScreen::LoadSlots){text(out,l.subtitle,"SELECT A SAVED CAMPAIGN",bright,l.body_font);if(!slots_.error.empty())text(out,l.list,slots_.error,warning,l.body_font);else if(slots_.slots.empty())text(out,l.list,"No saved campaigns are available.",muted,l.body_font);else{const float pitch=46.f*s;for(std::size_t i=0;i<slots_.slots.size();++i){UiRect row{l.list.x,l.list.y+i*pitch-load_scroll_,l.list.width,pitch-4*s};const auto visible=intersect(row,l.list);if(visible.width<=0||visible.height<=0)continue;fill(out,visible,selected_slot_==i?selected:raised);stroke(out,visible,selected_slot_==i?accent:border);UiRect label{row.x+10*s,row.y+11*s,row.width-20*s,row.height-12*s};const auto clip=intersect(label,l.list);if(clip.width>0&&clip.height>0)out.overlay.emplace_back(Text{{label.x,label.y},slots_.slots[i].filename,bright,l.body_font,label.width,clip,TextAlign::Left,FontFace::Interface});}}fill(out,l.back,raised);stroke(out,l.back,border);text(out,l.back,"BACK",bright,l.body_font,TextAlign::Center);fill(out,l.primary,selected_slot_?selected:raised);stroke(out,l.primary,selected_slot_?accent:muted);text(out,l.primary,"LOAD SELECTED",selected_slot_?bright:muted,l.body_font,TextAlign::Center);return;}
  if(screen_==StartupScreen::Busy){const auto status=busy_status(width,height,s),cancel=busy_cancel(width,height,s);fill(out,status,{5,14,27,210});stroke(out,status,border);text(out,{status.x+18*s,status.y+12*s,status.width-36*s,26*s},operation_.status,bright,l.body_font,TextAlign::Center);if(operation_.determinate_progress){UiRect track{status.x+50*s,status.y+50*s,status.width-100*s,12*s};fill(out,track,raised);fill(out,{track.x,track.y,track.width*static_cast<float>(std::clamp(*operation_.determinate_progress,0.,1.)),track.height},accent);}else {const UiRect track{status.x+50*s,status.y+50*s,status.width-100*s,12*s};fill(out,track,raised);const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();const float cycle=static_cast<float>(std::fmod(seconds,2.4)/2.4);const float travel=cycle<.5f?cycle*2.f:2.f-cycle*2.f;const float segment=track.width*.22f;fill(out,{track.x+(track.width-segment)*travel,track.y,segment,track.height},accent);}text(out,{status.x+20*s,status.y+82*s,status.width-40*s,34*s},loading_tip_,muted,l.small_font,TextAlign::Center);fill(out,cancel,{8,20,36,220});stroke(out,cancel,border);text(out,cancel,operation_.worker_running?"CANCEL":"BACK",bright,l.body_font,TextAlign::Center);return;}
  text(out,l.subtitle,"STARTUP COULD NOT COMPLETE",warning,l.body_font,TextAlign::Center);text(out,l.status,failure_,warning,l.body_font,TextAlign::Center);fill(out,l.back,raised);stroke(out,l.back,border);text(out,l.back,"BACK",bright,l.body_font,TextAlign::Center);
}
} // namespace stellar::native_startup_ui
