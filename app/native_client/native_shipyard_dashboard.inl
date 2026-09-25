// Shared workspace helpers and canonical controller projections are defined by
// the including translation unit. This file owns presentation, never costs.
ShipyardWorkspaceLayout ShipyardWorkspaceLayout::for_viewport(int width,int height)noexcept{
  const float w=static_cast<float>(width),h=static_cast<float>(height);
  const float s=std::clamp(std::min(h/1080.f,w/1720.f),.6f,2.f);
  const float top=native_workspace_top(width,height);
  const float pw=std::min(w*(width<1500?.90f:.86f),1660*s),ph=std::min(h-top-14*s,900*s);
  ShipyardWorkspaceLayout l;l.scale=s;l.title_font_pixels=static_cast<int>(24*s);l.body_font_pixels=static_cast<int>(15*s);l.small_font_pixels=static_cast<int>(12*s);
  l.surface={(w-pw)*.5f,top+(h-top-ph)*.5f,pw,ph};const auto p=l.surface;
  l.title={p.x+18*s,p.y+14*s,pw-76*s,54*s};l.close={p.x+pw-48*s,p.y+12*s,32*s,32*s};
  const float x=p.x+14*s,y=p.y+82*s,gap=10*s,bottom=p.y+ph-14*s;
  const float side=146*s,right=344*s,inner=pw-28*s,center=inner-side-right-2*gap;
  l.orders={x,bottom-214*s,inner-right-gap,214*s};
  l.categories={x,y,side,l.orders.y-y-gap};
  l.search={x+side+gap,y,center-212*s,34*s};
  l.sort={l.search.x+l.search.width+8*s,y,112*s,34*s};l.filter={l.sort.x+l.sort.width+8*s,y,84*s,34*s};
  l.designs={l.search.x,y+44*s,center,l.orders.y-y-54*s};
  l.design_details={x+inner-right,y,right,bottom-y-238*s};
  l.action={l.design_details.x,bottom-42*s,right,42*s};
  l.feedback={l.action.x,l.action.y-49*s,right,43*s};
  l.minus={l.action.x,l.feedback.y-41*s,32*s,32*s};l.quantity={l.minus.x+36*s,l.minus.y,46*s,32*s};l.plus={l.quantity.x+l.quantity.width+4*s,l.minus.y,32*s,32*s};
  l.favorite={l.plus.x+l.plus.width+10*s,l.minus.y,right-130*s,32*s};
  l.readiness={l.action.x,l.design_details.y+l.design_details.height+8*s,right,l.minus.y-l.design_details.y-l.design_details.height-16*s};
  return l;
}

namespace {
constexpr std::array ship_categories{"All ships","Military","Civilian","Science","Colony","Transport"};
constexpr std::array ship_category_keys{"SHIPYARD_CAT_ALL","SHIPYARD_CAT_MILITARY","SHIPYARD_CAT_CIVILIAN","SHIPYARD_CAT_SCIENCE","SHIPYARD_CAT_COLONY","SHIPYARD_CAT_TRANSPORT"};
constexpr std::array ship_sorts{"Name","Cost","Build time"};
constexpr std::array ship_sort_keys{"SHIPYARD_SORT_NAME","SHIPYARD_SORT_COST","SHIPYARD_SORT_TIME"};
constexpr std::array ship_filters{"All known","Ready","Favorites"};
constexpr std::array ship_filter_keys{"SHIPYARD_FILTER_ALL","SHIPYARD_FILTER_READY","SHIPYARD_FILTER_FAVORITES"};
std::string lower_ship(std::string v){for(auto& c:v)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return v;}
bool has_ship(const std::vector<std::string>& values,std::string_view id){return std::ranges::find(values,id)!=values.end();}
}
std::vector<const NativeShipDesign*> NativeShipyardWorkspace::filtered_designs()const{
  std::vector<const NativeShipDesign*> result;if(!view_)return result;const auto query=lower_ship(search_);
  for(const auto& d:view_->available_designs){
    using stellar::core::FleetRole;
    const bool category=category_==0||(category_==1&&d.role==FleetRole::Military)||(category_==2&&d.role!=FleetRole::Military)||
        (category_==3&&(d.role==FleetRole::Scout||d.role==FleetRole::Science))||(category_==4&&d.role==FleetRole::Colony)||(category_==5&&d.role==FleetRole::Logistics);
    if(!category||(filter_==1&&!d.can_start)||(filter_==2&&!has_ship(favorites_,d.id)))continue;
    if(!query.empty()&&lower_ship(d.name+" "+role_name(d.role,locale_)+" "+d.description).find(query)==std::string::npos)continue;
    result.push_back(&d);
  }
  std::ranges::stable_sort(result,[&](auto a,auto b){if(sort_==1&&a->credit_cost!=b->credit_cost)return a->credit_cost<b->credit_cost;if(sort_==2&&a->minimum_build_days_at_full_shipyard_rate!=b->minimum_build_days_at_full_shipyard_rate)return a->minimum_build_days_at_full_shipyard_rate<b->minimum_build_days_at_full_shipyard_rate;return a->name<b->name;});
  return result;
}
UiRect NativeShipyardWorkspace::card(std::size_t i,const ShipyardWorkspaceLayout& l)const{
  const int columns=std::clamp(static_cast<int>(l.designs.width/(214*l.scale)),2,4);
  const float gap=8*l.scale,w=(l.designs.width-gap*(columns-1))/columns;
  return {l.designs.x+(i%columns)*(w+gap),l.designs.y-design_scroll_.scroll_offset+(i/columns)*268*l.scale,w,260*l.scale};
}
std::optional<UiRect> NativeShipyardWorkspace::design_bounds(std::string_view id,int w,int h)const{
  const auto l=ShipyardWorkspaceLayout::for_viewport(w,h);const auto designs=filtered_designs();
  for(std::size_t i=0;i<designs.size();++i)if(designs[i]->id==id)return intersection(card(i,l),l.designs);return {};
}
std::vector<NativeShipyardWorkspace::FocusItem> NativeShipyardWorkspace::focusables(const ShipyardWorkspaceLayout& l)const{
  std::vector<FocusItem> items;const float s=l.scale;
  const auto push=[&](UiRect r,std::uint64_t t,std::string label,
                      std::optional<UiRect> unclipped=std::nullopt,int lane=0){
    if(r.width>0&&r.height>0)items.push_back({r,t,std::move(label),unclipped,lane});};
  push(l.close,1,tr("SHIPYARD_CLOSE","Close shipyard"));
  for(int i=0;i<static_cast<int>(ship_categories.size());++i)push({l.categories.x,l.categories.y+i*54*s,l.categories.width,48*s},10+i,tr(ship_category_keys[i],ship_categories[i]));
  push(l.search,20,tr("SHIPYARD_SEARCH","Search ships"));
  push(l.sort,21,trf("SHIPYARD_SORT_LABEL",{tr(ship_sort_keys[sort_],ship_sorts[sort_])},"Sort: {0}"));
  push(l.filter,22,trf("SHIPYARD_FILTER_LABEL",{tr(ship_filter_keys[filter_],ship_filters[filter_])},"Filter: {0}"));
  const auto designs=filtered_designs();
  for(std::size_t i=0;i<designs.size();++i){const auto r=card(i,l);if(const auto clip=intersection(r,l.designs))push(*clip,100+i,designs[i]->name,r,1);}
  if(view_){
    const UiRect clip{l.orders.x,l.orders.y+30*s,l.orders.width,l.orders.height-30*s};
    const UiRect queue{l.orders.x+5*s,l.orders.y+30*s,l.orders.width-10*s,l.orders.height-35*s};
    for(std::size_t i=0;i<view_->orders.size();++i){const auto&o=view_->orders[i];
      const UiRect r{clip.x,clip.y-order_scroll_.scroll_offset+i*64*s,clip.width,60*s};
      const auto hit=intersection(r,clip);if(!hit)continue;
      push(*hit,200+i*10,o.design_name,r,2);
      const float bx=r.x+r.width-110*s;
      const auto push_order_button=[&](UiRect b,std::uint64_t t,std::string label){
        if(const auto c=intersection(b,clip))push(*c,t,std::move(label),b,2);};
      if(!o.active){push_order_button({bx,r.y+12*s,30*s,30*s},200+i*10+1,trf("SHIPYARD_ORDER_UP",{o.design_name},"Move {0} earlier"));push_order_button({bx+36*s,r.y+12*s,30*s,30*s},200+i*10+2,trf("SHIPYARD_ORDER_DOWN",{o.design_name},"Move {0} later"));}
      if(o.can_cancel)push_order_button({bx+72*s,r.y+12*s,30*s,30*s},200+i*10+3,trf("SHIPYARD_ORDER_CANCEL",{o.design_name},"Cancel {0}"));
    }
  }
  if(const auto* d=selected_design()){
    push(l.minus,30,tr("SHIPYARD_QTY_DOWN","Decrease quantity"));
    push(l.plus,31,tr("SHIPYARD_QTY_UP","Increase quantity"));
    push(l.favorite,32,has_ship(favorites_,d->id)?tr("SHIPYARD_UNFAVORITE","Remove from favorites"):tr("SHIPYARD_FAVORITE","Add to favorites"));
  }
  if(const auto* o=selected_order())push(l.action,33,cancel_confirmation_id_==o->order_id?tr("SHIPYARD_CONFIRM_REFUND","Confirm cancel and refund"):tr("SHIPYARD_CANCEL_ORDER","Cancel order"));
  else if(const auto* d=selected_design()){
    const bool enabled=batch_blocker().empty();
    push(l.action,33,enabled?(d->will_queue?tr("SHIPYARD_QUEUE_BUILD","Queue build"):tr("SHIPYARD_START_BUILD","Start build")):tr("SHIPYARD_BUILD_UNAVAILABLE","Build unavailable"));
  }
  std::stable_sort(items.begin(),items.end(),[](const auto&a,const auto&b){return a.rect.y!=b.rect.y?a.rect.y<b.rect.y:a.rect.x<b.rect.x;});
  return items;
}
std::string NativeShipyardWorkspace::focused_label(const ShipyardWorkspaceLayout& l)const{
  if(focus_<0)return {};
  const auto items=focusables(l);
  return focus_<static_cast<int>(items.size())?items[static_cast<std::size_t>(focus_)].label:std::string{};
}
std::optional<stellar::native_map::UiRect> NativeShipyardWorkspace::focused_bounds(const ShipyardWorkspaceLayout& l)const{
  if(focus_<0)return std::nullopt;
  const auto items=focusables(l);
  return focus_<static_cast<int>(items.size())?std::optional<stellar::native_map::UiRect>{items[static_cast<std::size_t>(focus_)].rect}:std::nullopt;
}
stellar::engine::AnnouncementControl NativeShipyardWorkspace::focused_control(const ShipyardWorkspaceLayout& l)const{
  const auto bounds=focused_bounds(l);
  return bounds&&bounds->x==l.search.x&&bounds->y==l.search.y&&bounds->width==l.search.width&&bounds->height==l.search.height?stellar::engine::AnnouncementControl::Edit:stellar::engine::AnnouncementControl::Custom;
}
std::optional<stellar::engine::AnnouncementValue> NativeShipyardWorkspace::focused_value(const ShipyardWorkspaceLayout& l)const{
  if(focused_control(l)!=stellar::engine::AnnouncementControl::Edit)return std::nullopt;
  return stellar::engine::AnnouncementValue{search_};
}
bool NativeShipyardWorkspace::set_focused_text(std::string text,const ShipyardWorkspaceLayout& l){
  if(focused_control(l)!=stellar::engine::AnnouncementControl::Edit)return false;
  // Same byte cap as the typed path, truncated on a code-point boundary.
  if(text.size()>128){std::size_t n=128;while(n>0&&(static_cast<unsigned char>(text[n])&0xc0)==0x80)--n;text.resize(n);}
  search_=std::move(text);
  return true;
}
std::string NativeShipyardWorkspace::batch_blocker()const{
  const auto* d=selected_design();if(!d)return tr("SHIPYARD_SELECT_DESIGN","Select a ship design.");
  const auto q=std::ranges::find(d->batch_quotes,quantity_,&stellar::core::ShipbuildingBatchAssessment::quantity);
  if(q!=d->batch_quotes.end())return q->can_start?std::string{}:q->blocker.value_or(tr("SHIPYARD_UNAVAILABLE","Construction unavailable."));
  // Older projection clients retain their single-order contract.
  if(quantity_==1)return d->can_start?std::string{}:d->start_blocker.value_or(tr("SHIPYARD_UNAVAILABLE","Construction unavailable."));
  return tr("SHIPYARD_AWAIT_QUOTE","Waiting for construction quote.");
}
void NativeShipyardWorkspace::bind_preferences(std::filesystem::path path){
  if(path==preferences_path_)return;preferences_path_=std::move(path);favorites_.clear();
  try{if(!std::filesystem::exists(preferences_path_))return;if(std::filesystem::file_size(preferences_path_)>32768)throw std::runtime_error("Oversized preferences");
    std::ifstream file(preferences_path_);std::string value;std::getline(file,value);if(value!="stellar-shipyard-favorites-v1")throw std::runtime_error("Unknown preference version");
    while(std::getline(file,value)){if(value.empty()||value.size()>128||favorites_.size()>=128||has_ship(favorites_,value))throw std::runtime_error("Invalid favorite");favorites_.push_back(value);}
  }catch(const std::exception&){favorites_.clear();notice_=tr("SHIPYARD_FAVORITES_LOAD_FAIL","Ship favorites could not be loaded.");notice_accepted_=false;}
}
bool NativeShipyardWorkspace::save_preferences(){
  if(preferences_path_.empty())return true;
  try{std::string data="stellar-shipyard-favorites-v1\n";for(const auto& id:favorites_)data+=id+"\n";
    stellar::engine::write_file_atomically(preferences_path_,std::span{reinterpret_cast<const std::byte*>(data.data()),data.size()});return true;
  }catch(const std::exception&){notice_=tr("SHIPYARD_FAVORITES_SAVE_FAIL","Ship favorites could not be saved.");notice_accepted_=false;return false;}
}

ShipyardWorkspaceCommand NativeShipyardWorkspace::handle(const InputEvent& e,int w,int h){
  if(!visible_)return {};pointer_=e.position;const auto l=ShipyardWorkspaceLayout::for_viewport(w,h);const float s=l.scale;
  if(dropdown_.visible()){const int id=dropdown_.id();if(auto choice=dropdown_.handle(e,id==1?l.sort:l.filter,w,h)){if(id==1)sort_=*choice;else filter_=*choice;design_scroll_={};}return {ShipyardWorkspaceCommandKind::None,true};}
  if(e.type==InputEventType::PointerCancelled){cancel_confirmation_id_.reset();search_focused_=false;return {ShipyardWorkspaceCommandKind::None,true};}
  if(e.type==InputEventType::EscapePressed){close();return {ShipyardWorkspaceCommandKind::None,true};}
  if(search_focused_&&(e.type==InputEventType::TextEntered||e.type==InputEventType::BackspacePressed)){
    if(e.type==InputEventType::BackspacePressed&&!search_.empty()){auto end=search_.size()-1;while(end>0&&(static_cast<unsigned char>(search_[end])&0xc0u)==0x80u)--end;search_.resize(end);}
    else if(e.type==InputEventType::TextEntered&&search_.size()+e.text.size()<=128)search_+=e.text;
    design_scroll_={};return {ShipyardWorkspaceCommandKind::None,true};
  }
  if(e.type==InputEventType::KeyPressed&&e.key){
    if(search_focused_){
      // While editing the search field it owns key input; Tab/Return commit
      // and leave edit mode, everything else is captured.
      if(e.key==9u)search_focused_=false;
      else if(e.key==13u){search_focused_=false;return {ShipyardWorkspaceCommandKind::None,true};}
      else return {ShipyardWorkspaceCommandKind::None,true};
    }
    constexpr std::uint32_t kTab=9u,kReturn=13u,kSpace=32u;
    constexpr std::uint32_t kRight=0x4000004fu,kLeft=0x40000050u,kDown=0x40000051u,kUp=0x40000052u;
    constexpr std::uint32_t kHome=0x4000004au,kEnd=0x4000004du;
    const auto items=focusables(l);const int count=static_cast<int>(items.size());
    const bool fwd=(e.key==kTab&&!e.shift)||e.key==kRight||e.key==kDown;
    const bool bwd=(e.key==kTab&&e.shift)||e.key==kLeft||e.key==kUp;
    // Rows/cards clipped by a scroll viewport stay in the ring; when focus
    // lands on one, snap its lane so it is fully visible — which exposes
    // the next entry and keeps the whole list keyboard-reachable.
    const auto snap_focused=[&]{
      if(focus_<0||focus_>=count)return;
      const auto&t=items[static_cast<std::size_t>(focus_)];
      if(!t.unclipped)return;
      const auto&full=*t.unclipped;
      if(t.scroll_lane==1){
        const int columns=std::clamp(static_cast<int>(l.designs.width/(214*s)),2,4);
        const float rows=static_cast<float>((filtered_designs().size()+columns-1)/columns);
        design_scroll_.sync(rows*268*s,l.designs.height);
        design_scroll_.scroll_interval_into_view(full.y,full.y+full.height,l.designs.y,l.designs.y+l.designs.height);
      }else if(t.scroll_lane==2){
        order_scroll_.sync(30*s+(view_?view_->orders.size():0)*64*s,l.orders.height);
        order_scroll_.scroll_interval_into_view(full.y,full.y+full.height,l.orders.y+30*s,l.orders.y+l.orders.height);
      }};
    if(count>0&&(e.key==kHome||e.key==kEnd)){focus_=e.key==kHome?0:count-1;snap_focused();return {ShipyardWorkspaceCommandKind::None,true};}
    if(count>0&&(fwd||bwd)){focus_=focus_<0||focus_>=count?(bwd?count-1:0):(focus_+(bwd?-1:1)+count)%count;snap_focused();return {ShipyardWorkspaceCommandKind::None,true};}
    if((e.key==kReturn||e.key==kSpace)&&focus_>=0&&focus_<count){
      const auto&r=items[static_cast<std::size_t>(focus_)].rect;
      InputEvent press{InputEventType::LeftPressed};press.position={r.x+r.width*.5f,r.y+r.height*.5f};
      const int keep=focus_;auto command=handle(press,w,h);
      if(visible_)focus_=keep;return command;
    }
    return {};
  }
  if(e.type==InputEventType::Wheel){
    if(l.designs.contains(e.position)){const auto designs=filtered_designs();const float content=designs.empty()?0:card(designs.size()-1,l).y-design_scroll_.scroll_offset-l.designs.y+268*s;design_scroll_.sync(content,l.designs.height);design_scroll_.scroll_by(-e.wheel_y*66*s);}
    else if(l.orders.contains(e.position)){order_scroll_.sync(30*s+(view_?view_->orders.size():0)*64*s,l.orders.height);order_scroll_.scroll_by(-e.wheel_y*50*s);}
    else if(l.design_details.contains(e.position))detail_scroll_.scroll_by(-e.wheel_y*48*s);
    return {ShipyardWorkspaceCommandKind::None,l.surface.contains(e.position)};
  }
  if(e.type!=InputEventType::LeftPressed)return {ShipyardWorkspaceCommandKind::None,l.surface.contains(e.position)};
  if(l.close.contains(e.position)){close();return {ShipyardWorkspaceCommandKind::None,true};}
  if(!l.surface.contains(e.position))return {};
  focus_=-1;
  search_focused_=l.search.contains(e.position);if(search_focused_)return {ShipyardWorkspaceCommandKind::None,true};
  if(l.sort.contains(e.position)||l.filter.contains(e.position)){const bool sort=l.sort.contains(e.position);std::vector<std::string> options;if(sort)for(std::size_t i=0;i<ship_sorts.size();++i)options.emplace_back(tr(ship_sort_keys[i],ship_sorts[i]));else for(std::size_t i=0;i<ship_filters.size();++i)options.emplace_back(tr(ship_filter_keys[i],ship_filters[i]));dropdown_.open(sort?1:2,std::move(options),sort?sort_:filter_);return {ShipyardWorkspaceCommandKind::None,true};}
  for(int i=0;i<static_cast<int>(ship_categories.size());++i){UiRect r{l.categories.x,l.categories.y+i*54*s,l.categories.width,48*s};if(r.contains(e.position)){category_=i;design_scroll_={};return {ShipyardWorkspaceCommandKind::None,true};}}
  const auto designs=filtered_designs();for(std::size_t i=0;i<designs.size();++i){const auto r=intersection(card(i,l),l.designs);if(r&&r->contains(e.position)){selected_design_id_=designs[i]->id;selected_order_id_.reset();cancel_confirmation_id_.reset();notice_.clear();detail_scroll_={};quantity_=1;return {ShipyardWorkspaceCommandKind::None,true};}}
  if(view_){const UiRect clip{l.orders.x,l.orders.y+30*s,l.orders.width,l.orders.height-30*s};
    for(std::size_t i=0;i<view_->orders.size();++i){const auto& o=view_->orders[i];const UiRect r{clip.x,clip.y-order_scroll_.scroll_offset+i*64*s,clip.width,60*s};const auto hit=intersection(r,clip);if(!hit||!hit->contains(e.position))continue;
      selected_order_id_=o.order_id;selected_design_id_.reset();cancel_confirmation_id_.reset();notice_.clear();detail_scroll_={};
      const float bx=r.x+r.width-110*s;
      if(!o.active&&UiRect{bx,r.y+12*s,30*s,30*s}.contains(e.position))return {ShipyardWorkspaceCommandKind::MoveUp,true,o.order_id};
      if(!o.active&&UiRect{bx+36*s,r.y+12*s,30*s,30*s}.contains(e.position))return {ShipyardWorkspaceCommandKind::MoveDown,true,o.order_id};
      if(o.can_cancel&&UiRect{bx+72*s,r.y+12*s,30*s,30*s}.contains(e.position))return {ShipyardWorkspaceCommandKind::PrepareCancel,true,o.order_id};
      return {ShipyardWorkspaceCommandKind::None,true};
    }
  }
  if(l.minus.contains(e.position))quantity_=std::max(1,quantity_-1);
  if(l.plus.contains(e.position))quantity_=std::min(view_?view_->maximum_pending_builds:1,quantity_+1);
  if(l.favorite.contains(e.position)&&selected_design_id_){const auto before=favorites_;const auto it=std::ranges::find(favorites_,*selected_design_id_);if(it!=favorites_.end())favorites_.erase(it);else if(favorites_.size()<128)favorites_.push_back(*selected_design_id_);if(!save_preferences())favorites_=before;}
  if(l.action.contains(e.position)){
    if(const auto* o=selected_order();o&&o->can_cancel)return {cancel_confirmation_id_==o->order_id?ShipyardWorkspaceCommandKind::Cancel:ShipyardWorkspaceCommandKind::PrepareCancel,true,o->order_id};
    if(const auto* d=selected_design();d&&batch_blocker().empty())return {ShipyardWorkspaceCommandKind::Start,true,d->id,quantity_};
  }
  return {ShipyardWorkspaceCommandKind::None,true};
}

void NativeShipyardWorkspace::render(DrawList& out,int w,int h,stellar::native_ship_ui::NativeShipArtAssets* art)const{
  last_ship_art_rows_=0;if(!visible_)return;const auto l=ShipyardWorkspaceLayout::for_viewport(w,h);const float s=l.scale;
  stellar::native_ui_style::menu_panel(out,l.surface);const auto button=[&](UiRect r,std::string label,bool active=false,bool enabled=true){theme::button(out,r,std::move(label),pointer_,l.small_font_pixels,theme::Tone::Neutral,active,enabled);};
  text(out,l.title,tr("SHIPYARD_TITLE","SHIP CONSTRUCTION"),bright,l.title_font_pixels,FontFace::Heading);
  text(out,{l.title.x,l.title.y+31*s,l.title.width,24*s},view_?trf("SHIPYARD_SUBTITLE",{view_->yard_name,view_->formatted_treasury,number(view_->available_industry,0)},"{0}  ·  Treasury {1}  ·  Industry {2}"):tr("SHIPYARD_LOADING","Loading shipyard…"),muted,l.small_font_pixels);
  button(l.close,"×");
  for(int i=0;i<static_cast<int>(ship_categories.size());++i)button({l.categories.x,l.categories.y+i*54*s,l.categories.width,48*s},tr(ship_category_keys[i],ship_categories[i]),category_==i);
  button(l.search,search_.empty()?tr("SHIPYARD_SEARCH","Search ships…"):search_,search_focused_);button(l.sort,tr(ship_sort_keys[sort_],ship_sorts[sort_])+" ▾");button(l.filter,tr(ship_filter_keys[filter_],ship_filters[filter_])+" ▾");
  const auto designs=filtered_designs();
  const auto clipped_text=[&](UiRect r,UiRect clip,std::string value,Color color,int size){if(intersection(r,clip))out.overlay.emplace_back(Text{{r.x,r.y},std::move(value),color,size,r.width,clip});};
  if(designs.empty())theme::empty_state(out,l.designs,search_.empty()?tr("SHIPYARD_EMPTY_FILTER","No known designs match these filters."):tr("SHIPYARD_EMPTY_SEARCH","No known designs match your search."),tr("SHIPYARD_EMPTY_HINT","Clear the search or change the filters."),l.body_font_pixels);
  for(std::size_t i=0;i<designs.size();++i){const auto& d=*designs[i];const auto r=card(i,l);const auto clip=intersection(r,l.designs);if(!clip)continue;
    fill(out,*clip,theme::color::surface_secondary);
    if(clip->x==r.x&&clip->y==r.y&&clip->width==r.width&&clip->height==r.height)stroke(out,r,theme::color::keyline);
    if(selected_design_id_==d.id)fill(out,{clip->x,clip->y,3*s,clip->height},theme::color::selected);
    const UiRect pic{r.x+4*s,r.y+4*s,r.width-8*s,136*s};if(art){if(auto image=art->image_for(d.id,d.role)){out.overlay.emplace_back(Image{image,pic,cover_source(*image,pic),{255,255,255,255},l.designs});++last_ship_art_rows_;}}
    clipped_text({r.x+8*s,r.y+147*s,r.width-16*s,38*s},*clip,d.name,bright,l.body_font_pixels);
    clipped_text({r.x+8*s,r.y+188*s,r.width-16*s,20*s},*clip,role_name(d.role,locale_),muted,l.small_font_pixels);
    clipped_text({r.x+8*s,r.y+210*s,r.width-16*s,20*s},*clip,d.formatted_credit_cost,d.can_start?good:warning,l.small_font_pixels);
    clipped_text({r.x+8*s,r.y+234*s,r.width-16*s,20*s},*clip,trf("SHIPYARD_BUILD_MINIMUM",{stellar::native_campaign::format_campaign_duration(d.minimum_build_days_at_full_shipyard_rate)},"{0} minimum"),muted,l.small_font_pixels);
  }
  stellar::native_ui_style::menu_panel(out,l.orders);theme::section_header(out,{l.orders.x+10*s,l.orders.y+6*s,l.orders.width-20*s,22*s},trf("SHIPYARD_ORDERS_HEADING",{std::to_string(view_?view_->orders.size():0)},"BUILD ORDERS  /  {0}"),l.small_font_pixels);
  const UiRect queue{l.orders.x+5*s,l.orders.y+30*s,l.orders.width-10*s,l.orders.height-35*s};
  if(!view_||view_->orders.empty())theme::empty_state(out,queue,tr("SHIPYARD_NO_ORDERS","No ships are under construction."),tr("SHIPYARD_NO_ORDERS_HINT","Queued builds appear here."),l.body_font_pixels);
  if(view_)for(std::size_t i=0;i<view_->orders.size();++i){const auto& o=view_->orders[i];const UiRect r{l.orders.x,queue.y-order_scroll_.scroll_offset+i*64*s,l.orders.width,60*s};const auto clip=intersection(r,queue);if(!clip)continue;
    fill(out,*clip,selected_order_id_==o.order_id?selected:inset);
    if(selected_order_id_==o.order_id)fill(out,{clip->x,clip->y,3*s,clip->height},theme::color::selected);float x=r.x+12*s;
    if(art){if(auto image=art->image_for(o.design_id,stellar::core::FleetRole::Military)){const UiRect p{x,r.y+5*s,70*s,49*s};out.overlay.emplace_back(Image{image,p,cover_source(*image,p),{255,255,255,255},queue});}x+=80*s;}
    clipped_text({x,r.y+4*s,r.width-(x-r.x)-122*s,22*s},queue,o.design_name,bright,l.body_font_pixels);
    clipped_text({x,r.y+27*s,r.width-(x-r.x)-122*s,18*s},queue,trf(o.active?"SHIPYARD_ORDER_ACTIVE":"SHIPYARD_ORDER_QUEUED",{number(o.progress_fraction*100,0),stellar::native_campaign::format_campaign_duration(o.industry_remaining/stellar::core::shipbuilding_industry_per_day)},o.active?"ACTIVE · {0}%  ·  {1} at full production":"QUEUED · {0}%  ·  {1} at full production"),muted,l.small_font_pixels);
    const UiRect track{x,r.y+51*s,r.width-(x-r.x)-122*s,4*s};if(auto c=intersection(track,queue))fill(out,*c,row);if(auto c=intersection({track.x,track.y,progress_width(o.progress_fraction,track.width),track.height},queue))fill(out,*c,good);
    const float bx=r.x+r.width-110*s;
    if(queue.contains({bx,r.y+12*s})&&queue.contains({bx+102*s,r.y+42*s})){
      if(!o.active){button({bx,r.y+12*s,30*s,30*s},"↑");button({bx+36*s,r.y+12*s,30*s,30*s},"↓");}
      if(o.can_cancel)button({bx+72*s,r.y+12*s,30*s,30*s},"×");
    }
  }
  stellar::native_ui_style::menu_panel(out,l.design_details);const auto* d=selected_design();const auto* o=selected_order();
  const UiRect details{l.design_details.x+10*s,l.design_details.y+8*s,l.design_details.width-20*s,l.design_details.height-16*s};float y=details.y-detail_scroll_.scroll_offset;
  const auto write=[&](std::string value,Color color=bright,int pixels=0){if(!pixels)pixels=l.small_font_pixels;Text item{{details.x,y},std::move(value),color,pixels,details.width,details};const auto extent=measure_?measure_(item):TextExtent{0,static_cast<int>((1+item.value.size()/std::max(1,static_cast<int>(details.width/(pixels*.53f))))*(pixels+4))};out.overlay.emplace_back(std::move(item));y+=std::max(pixels+3,extent.height)+6*s;};
  if(d){write(d->name,bright,l.body_font_pixels);if(art){if(auto image=art->image_for(d->id,d->role)){const UiRect p{details.x,y,details.width,152*s};out.overlay.emplace_back(Image{image,p,cover_source(*image,p),{255,255,255,255},details});y+=160*s;}}
    write(d->description);write(tr("SHIPYARD_COMBAT","COMBAT"),theme::accent(theme::Tone::Construction));write(trf("SHIPYARD_STATS_DEFENSE",{number(d->hull,0),number(d->armor,0),number(d->shields,0)},"Hull  {0}     Armor  {1}     Shields  {2}"));
    write(trf("SHIPYARD_STAT_DAMAGE",{number(d->weapon_damage,1)},"Weapon damage  {0}"));write(tr("SHIPYARD_MOBILITY","MOBILITY"),theme::accent(theme::Tone::Construction));write(trf("SHIPYARD_STAT_SPEED",{stellar::core::format_interstellar_metric_speed(d->strategic_speed)},"Speed  {0}"));write(trf("SHIPYARD_STAT_LEG",{stellar::core::format_interstellar_metric_primary(d->maximum_leg_range_light_years)},"Maximum leg  {0}"));write(trf("SHIPYARD_STAT_FUEL",{stellar::core::format_interstellar_metric_primary(d->fuel_endurance_light_years)},"Fuel endurance  {0}"));write(trf("SHIPYARD_STAT_SENSORS",{number(d->sensor_range,1)},"Sensors  {0}"));write(trf("SHIPYARD_STAT_PROPULSION",{d->propulsion_generation},"Propulsion  {0}"));write(trf("SHIPYARD_STATS_CREW_CARGO",{std::to_string(d->crew),number(d->cargo_capacity,0)},"Crew  {0}     Cargo  {1}"));
  }else if(o){write(o->design_name,bright,l.body_font_pixels);
    if(art){const auto design=std::ranges::find(view_->available_designs,o->design_id,&NativeShipDesign::id);const auto role=design==view_->available_designs.end()?stellar::core::FleetRole::Military:design->role;if(auto image=art->image_for(o->design_id,role)){const UiRect p{details.x,y,details.width,152*s};out.overlay.emplace_back(Image{image,p,cover_source(*image,p),{255,255,255,255},details});y+=160*s;}}
    write(o->active?tr("SHIPYARD_STATE_ACTIVE","Active construction"):tr("SHIPYARD_STATE_WAITING","Waiting for the production line"),good);write(trf("SHIPYARD_INDUSTRY_REMAINING",{number(o->industry_remaining,1)},"Industry remaining  {0}"));write(trf("SHIPYARD_POPULATION_RESERVED",{number(o->reserved_population_millions,3)},"Population reserved  {0} million"));write(tr("SHIPYARD_CANCEL_NOTE","Cancelling releases the unused authorization and reserved population under the normal construction rules."));}
  else write(tr("SHIPYARD_SELECT_HINT","Select an available design to review its requirements."),muted);
  detail_scroll_.sync(y+detail_scroll_.scroll_offset-details.y,details.height);
  std::string readiness;
  if(d&&view_){const auto q=std::ranges::find(d->batch_quotes,quantity_,&stellar::core::ShipbuildingBatchAssessment::quantity);
    const auto credit=q==d->batch_quotes.end()?d->formatted_credit_cost:view_->currency.format(q->credit_cost);
    const double industry=q==d->batch_quotes.end()?d->industry_cost:q->industry_cost;
    readiness=trf(quantity_==1?"SHIPYARD_READINESS_ONE":"SHIPYARD_READINESS_MANY",{credit,number(industry,0),std::to_string(quantity_)},quantity_==1?"Authorization {0}\nIndustry {1} · {2} vessel\n":"Authorization {0}\nIndustry {1} · {2} vessels\n");
    const auto reason=batch_blocker();readiness+=reason.empty()?tr("SHIPYARD_READY","Ready to build."):reason;
  }else if(o)readiness=trf("SHIPYARD_REFUND",{o->formatted_refund},"Refund if cancelled now  {0}")+"\n"+o->cancellation_blocker.value_or(tr("SHIPYARD_REFUND_NOTE","Unused authorization will be returned."));
  text(out,l.readiness,std::move(readiness),muted,l.small_font_pixels);
  if(d){button(l.minus,"−");button(l.quantity,std::to_string(quantity_));button(l.plus,"+");button(l.favorite,has_ship(favorites_,d->id)?tr("SHIPYARD_FAVORITED","★ Favorite"):tr("SHIPYARD_FAVORITE","☆ Favorite"),has_ship(favorites_,d->id));}
  if(!notice_.empty())text(out,l.feedback,visible_message(notice_),notice_accepted_?muted:failure,l.small_font_pixels);
  std::string action;bool enabled=false;
  if(o){enabled=o->can_cancel;action=cancel_confirmation_id_==o->order_id?tr("SHIPYARD_CONFIRM_REFUND","CONFIRM CANCEL + REFUND"):tr("SHIPYARD_CANCEL_ORDER","CANCEL ORDER");}
  else if(d){enabled=batch_blocker().empty();action=enabled?(d->will_queue?tr("SHIPYARD_QUEUE_BUILD","QUEUE BUILD"):tr("SHIPYARD_START_BUILD","START BUILD")):tr("SHIPYARD_BUILD_UNAVAILABLE","BUILD UNAVAILABLE");}
  if(!action.empty())button(l.action,action,enabled,enabled);
  // Disabled state carries the authoritative blocker as a hover tooltip so the
  // reason is discoverable at the point of interaction, not only in the
  // readiness text above.
  if(!action.empty()&&!enabled){
    const std::string reason=o?o->cancellation_blocker.value_or(tr("SHIPYARD_UNAVAILABLE","Construction unavailable.")):batch_blocker();
    theme::hover_tooltip(out,l.action,pointer_,action,reason,w,h,l.scale);
  }
  const auto focus_items=focusables(l);
  if(focus_>=0&&focus_<static_cast<int>(focus_items.size()))theme::focus_ring(out,focus_items[static_cast<std::size_t>(focus_)].rect);
  dropdown_.render(out,dropdown_.id()==1?l.sort:l.filter,w,h,l.body_font_pixels);
}
