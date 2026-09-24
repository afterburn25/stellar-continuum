#pragma once
#include "native_planet_materials.hpp"
#include "native_menu_style.hpp"
#include <stellar/core/developer_planet_index.hpp>
#include <stellar/engine/accessibility.hpp>
#include <sstream>
#include <iomanip>
namespace stellar::native_map {
class NativeGiantTestPanel {
 stellar::core::DeveloperGiantTestRequest request_;
 std::optional<stellar::core::DeveloperGiantLaboratory> laboratory_;
 bool visible_{},ring_shadow_{true},planet_shadow_{true};int pressed_{-1},ring_{-1};
 std::pair<int,int> target_{};float yaw_{},pitch_{.15f};double phase_{};Point pointer_{};std::string notice_;
 static std::string number(double n){std::ostringstream s;s<<std::fixed<<std::setprecision(2)<<n;return s.str();}
 struct Layout{UiRect panel,view;float s;};
 static Layout layout(int w,int h){const float s=std::min(w/1280.f,h/720.f);return {{w*.5f-608*s,h*.5f-335*s,1216*s,670*s},{w*.5f-590*s,h*.5f-267*s,594*s,565*s},s};}
 static UiRect button(const Layout& l,int i){return {l.panel.x+(630+(i%2)*285)*l.s,l.panel.y+(66+(i/2)*43)*l.s,274*l.s,35*l.s};}
 static std::vector<const stellar::core::PlanetSubclassDefinition*> subclasses(stellar::core::PlanetClass type){std::vector<const stellar::core::PlanetSubclassDefinition*> list;for(const auto& s:stellar::core::planet_subclass_definitions())if(s.primary==type&&s.generation_enabled)list.push_back(&s);return list;}
 void apply(stellar::core::CampaignFrame& frame,const stellar::core::DeveloperGiantTestRequest& next){target_=stellar::core::apply_developer_giant_test(frame.runtime().world().campaign(),next);request_=next;laboratory_=stellar::core::build_developer_giant_test(frame.runtime().world().campaign());notice_="Saved test planet updated. Thermal and geometry checks passed.";}
 std::array<std::string,20> button_labels()const{return {"GAS GIANTS","ICE GIANTS","NEXT SUBCLASS","NEXT PLANET IMAGE",request_.rings?"RINGS: ON":"RINGS: OFF","NEXT RING FAMILY","NEXT RING IMAGE","TILT +15°","CAMERA LEFT","CAMERA RIGHT","CAMERA LOWER","CAMERA HIGHER","STAR DIRECTION +45°","STAR SPECTRUM","MOVE CLOSER","MOVE FARTHER",ring_shadow_?"RING SHADOW: ON":"RING SHADOW: OFF",planet_shadow_?"PLANET SHADOW: ON":"PLANET SHADOW: OFF","RESET","CLOSE"};}
 // Keyboard-focus contract: the twenty rendered buttons ring in (y,x)
 // order and Return/Space replay the same press/release dispatch —
 // activate_button runs the identical switch a matched pointer
 // press+release takes. The 3D preview stays pointer-spatial by design.
 void activate_button(int action,stellar::core::CampaignFrame& frame){
  auto next=request_;using namespace stellar::core;
  try{switch(action){
   case 0:next.type=PlanetClass::GasGiant;next.subclass="cream-band";next.planet_variant=0;next.distance_scale=1;break;
   case 1:next.type=PlanetClass::IceGiant;next.subclass="cyan-haze";next.planet_variant=0;next.distance_scale=1;break;
   case 2:{const auto list=subclasses(next.type);auto i=std::ranges::find_if(list,[&](auto* s){return s->id==next.subclass;});next.subclass=list[(std::distance(list.begin(),i)+1)%list.size()]->id;next.planet_variant=0;next.distance_scale=1;break;}
   case 3:++next.planet_variant;break;
   case 4:next.rings=!next.rings;break;
   case 5:{const auto& list=ring_family_definitions();auto i=std::ranges::find(list,next.ring_family,&RingFamilyDefinition::id);next.ring_family=list[(std::distance(list.begin(),i)+1)%list.size()].id;next.ring_variant=0;next.rings=true;break;}
   case 6:++next.ring_variant;break;
   case 7:next.axial_tilt_degrees=std::fmod(next.axial_tilt_degrees+15,181.);break;
   case 8:yaw_-=.3f;return;case 9:yaw_+=.3f;return;
   case 10:pitch_=std::clamp(pitch_-.2f,-1.5f,1.5f);return;case 11:pitch_=std::clamp(pitch_+.2f,-1.5f,1.5f);return;
   case 12:phase_+=std::numbers::pi/4;return;
   case 13:next.star_type=next.star_type==StellarObjectType::GYellowStar?StellarObjectType::MRedDwarf:next.star_type==StellarObjectType::MRedDwarf?StellarObjectType::BBlueWhiteStar:StellarObjectType::GYellowStar;break;
   case 14:next.distance_scale=std::max(.1,next.distance_scale/1.2);break;case 15:next.distance_scale=std::min(10.,next.distance_scale*1.2);break;
   case 16:ring_shadow_=!ring_shadow_;return;case 17:planet_shadow_=!planet_shadow_;return;
   case 18:next={};yaw_=0;pitch_=.15f;phase_=0;ring_shadow_=planet_shadow_=true;break;
   case 19:close();return;
  }apply(frame,next);}catch(const std::exception& error){notice_=error.what();}
 }
 public:
 bool visible()const{return visible_;}void close(){visible_=false;pressed_=-1;ring_=-1;}
 // Keyboard-focus contract: ring index, announcement accessors.
 [[nodiscard]] bool wants_keyboard_focus()const noexcept{return ring_>=0;}
 [[nodiscard]] int focus()const noexcept{return ring_;}
 [[nodiscard]] std::string focused_label(int,int)const{
  return ring_>=0&&ring_<20?button_labels()[static_cast<std::size_t>(ring_)]:std::string{};
 }
 [[nodiscard]] std::optional<UiRect> focused_bounds(int w,int h)const{
  return ring_>=0&&ring_<20?std::optional<UiRect>{button(layout(w,h),ring_)}:std::nullopt;
 }
 [[nodiscard]] stellar::engine::AnnouncementControl focused_control(int,int)const{
  if(ring_<0||ring_>=20)return stellar::engine::AnnouncementControl::Custom;
  return ring_==4||ring_==16||ring_==17?stellar::engine::AnnouncementControl::CheckBox:stellar::engine::AnnouncementControl::Button;
 }
 void open(stellar::core::CampaignFrame& frame){visible_=true;ring_=-1;try{const auto& p=frame.runtime().world().campaign().developer_provenance;if(p&&p->giant_test)request_=p->giant_test->controls;apply(frame,request_);}catch(const std::exception& e){notice_=e.what();}}
 bool handle(const InputEvent& e,int w,int h,stellar::core::CampaignFrame& frame){
  if(!visible_)return false;pointer_=e.position;const auto l=layout(w,h);
  if(e.type==InputEventType::EscapePressed){if(ring_>=0){ring_=-1;return true;}close();return true;}
  if(e.type==InputEventType::PointerCancelled){pressed_=-1;ring_=-1;return true;}
  if(e.type==InputEventType::KeyPressed&&e.key){
   constexpr std::uint32_t kTab=9u,kReturn=13u,kSpace=32u;
   constexpr std::uint32_t kRight=0x4000004fu,kLeft=0x40000050u,kDown=0x40000051u,kUp=0x40000052u;
   constexpr std::uint32_t kHome=0x4000004au,kEnd=0x4000004du;
   const bool fwd=(e.key==kTab&&!e.shift)||e.key==kRight||e.key==kDown;
   const bool bwd=(e.key==kTab&&e.shift)||e.key==kLeft||e.key==kUp;
   if(fwd||bwd){
    if(ring_<0)ring_=bwd?19:0;else ring_=(ring_+(fwd?1:19))%20;return true;
   }
   if(e.key==kHome||e.key==kEnd){ring_=e.key==kHome?0:19;return true;}
   if((e.key==kReturn||e.key==kSpace)&&ring_>=0){activate_button(ring_,frame);return true;}
  }
  if(e.type==InputEventType::LeftPressed){ring_=-1;pressed_=-1;for(int i=0;i<20;++i)if(button(l,i).contains(e.position))pressed_=i;}
  if(e.type!=InputEventType::LeftReleased)return true;const int action=std::exchange(pressed_,-1);if(action<0||!button(l,action).contains(e.position))return true;
  activate_button(action,frame);return true;
 }
 void render(DrawList& out,int w,int h,stellar::core::CampaignFrame&,const stellar::native_planets::MaterialProvider& materials){
  if(!visible_)return;const auto l=layout(w,h);const int font=std::max(11,static_cast<int>(14*l.s));native_menu_style::panel(out,l.panel,l.s);
  const auto text=[&](UiRect r,std::string s,Color c=native_menu_style::ink){native_menu_style::text(out,r,std::move(s),font,c);};
  text({l.panel.x+20*l.s,l.panel.y+16*l.s,l.panel.width-40*l.s,32*l.s},"GIANT PLANET & RING TEST PANEL",native_menu_style::cyan);
  const auto labels=button_labels();
  for(int i=0;i<20;++i)native_menu_style::button(out,button(l,i),labels[i],font,button(l,i).contains(pointer_),true,l.s);
  if(ring_>=0&&ring_<20){
   const auto r=button(l,ring_);
   const UiRect outer{r.x-3*l.s,r.y-3*l.s,r.width+6*l.s,r.height+6*l.s};
   out.overlay.emplace_back(StrokedRectangle{outer,native_menu_style::cyan});
   out.overlay.emplace_back(StrokedRectangle{r,native_menu_style::cyan});
  }
  try{if(laboratory_){
   const auto& source=laboratory_->body;const auto& system=laboratory_->system;
   stellar::native_system::NativeSystemSnapshot snapshot;snapshot.system_id=system.id;snapshot.stellar_object=system.stellar_object;snapshot.primary_stellar_class=system.primary;
   stellar::native_system::NativeSystemBody body;body.id=source.id;body.appearance=source.appearance;body.orbit_au=source.stellar_exposure->orbit_au;body.stellar_orbit=stellar::core::planetary_stellar_orbit(system,source);
   const auto& a=*body.appearance;
   // Preview another point on this same authoritative Kepler orbit; no new
   // simulation or arbitrary light is invented by the panel.
   if(body.stellar_orbit)snapshot.simulation_days=(phase_-body.stellar_orbit->phase)/body.stellar_orbit->angular_speed;
   const auto light=stellar::native_planets::system_lighting(snapshot,body);auto maps=materials(a,1024);
   if(maps){std::vector<MeshInstance3D> objects;const auto view=compose_rotation(rotation_axis_angle({1,0,0},pitch_),rotation_axis_angle({0,1,0},yaw_));
    stellar::native_planets::append_instances(objects,a,*maps,{},1,1024,snapshot.simulation_days,light,view);
    for(auto& o:objects)if(o.material.shadow&&((o.material.shadow->shape==AnalyticShadowShape3D::Annulus&&!ring_shadow_)||(o.material.shadow->shape==AnalyticShadowShape3D::Ellipsoid&&!planet_shadow_)))o.material.shadow.reset();
    Camera3D camera;camera.position={0,0,8};camera.projection=Projection3D::Perspective;camera.vertical_fov_radians=static_cast<float>(2*std::atan((a.rings.enabled?a.rings.outer_radius:1)*1.2/8));camera.near_plane=.1f;camera.far_plane=20;
    out.overlay.emplace_back(Scene3DView{Scene3D::create(camera,std::move(objects)),l.view});
   }else text(l.view,"Loading reviewed material…");
   float y=l.panel.y+507*l.s;const auto line=[&](std::string s){text({l.panel.x+630*l.s,y,556*l.s,23*l.s},std::move(s));y+=24*l.s;};
   line(stellar::core::planet_appearance_display_name(a)+" · "+a.source_asset_id);line("Ring: "+(a.rings.enabled?a.rings.asset_id:"none"));
   line("Orbit "+number(body.orbit_au)+" AU · atmosphere "+number(a.climate.equilibrium_kelvin)+" K");
   line("Ring "+number(a.rings.equilibrium_kelvin)+" K · Roche "+number(a.rings.roche_radius)+" Rp");
   line("Radii "+number(a.rings.inner_radius)+"–"+number(a.rings.outer_radius)+" Rp · tilt "+number(request_.axial_tilt_degrees)+"°");
  }}catch(const std::exception& e){notice_=e.what();}
  text({l.panel.x+20*l.s,l.panel.y+638*l.s,l.panel.width-40*l.s,27*l.s},notice_,native_menu_style::muted);
 }
};
}
