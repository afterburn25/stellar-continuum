// Included in the research workspace translation unit to share its clipping,
// typography and observer-safe projection helpers.
namespace {
bool established(const NativeResearchNode& n){return n.maturity>=stellar::core::ResearchMaturity::mature;}
bool includes(const std::vector<std::string>& values,std::string_view id){return std::ranges::find(values,id)!=values.end();}
std::string concise(std::string value,std::size_t limit){if(value.size()<=limit)return value;const auto at=value.rfind(' ',limit);return value.substr(0,at==std::string::npos?limit:at)+"…";}
}
std::vector<NativeResearchWorkspace::GuidedCard> NativeResearchWorkspace::guided_cards(const ResearchWorkspaceLayout& l)const{
  std::vector<GuidedCard> cards;if(!window_)return cards;
  std::vector<const NativeResearchNode*> nodes;
  for(const auto& n:window_->nodes){if(!matches_query(n,query_))continue;
    const bool favorite=includes(plan_.favorites,n.id),queued=includes(plan_.queue,n.id);
    if(mode_==ResearchViewMode::Recent&&(!established(n)||!n.recent_year))continue;
    if(mode_==ResearchViewMode::Favorites&&!favorite)continue;
    if(mode_==ResearchViewMode::Completed&&!established(n))continue;
    if(mode_==ResearchViewMode::Queue&&!queued)continue;
    if(mode_==ResearchViewMode::Guided){
      if(filter_==0&&query_.search.empty()&&!n.requirements_met)continue;
      if(filter_==0&&(n.active||established(n)))continue;
      if(filter_==2&&(n.recommendation_score<0||!n.requirements_met||n.active))continue;
      if(filter_==3&&!n.active)continue;
      if(filter_==4&&!queued)continue;
      if(filter_==5&&(n.active||established(n)||n.requirements_met))continue;
      if(filter_==6&&!established(n))continue;
      if(filter_==7&&!favorite)continue;
    }nodes.push_back(&n);
  }
  std::ranges::stable_sort(nodes,[&](const auto* a,const auto* b){
    if(mode_==ResearchViewMode::Queue)return std::ranges::find(plan_.queue,a->id)<std::ranges::find(plan_.queue,b->id);
    if(mode_==ResearchViewMode::Recent)return a->recent_year.value_or(0)>b->recent_year.value_or(0);
    if(sort_==1){const auto av=a->cost?a->cost->estimated_years_at_full_funding:INFINITY,bv=b->cost?b->cost->estimated_years_at_full_funding:INFINITY;if(av!=bv)return av<bv;}
    if(sort_==2&&a->graph_depth!=b->graph_depth)return a->graph_depth<b->graph_depth;
    if(sort_==3&&a->domain_label!=b->domain_label)return a->domain_label<b->domain_label;
    if(sort_==0&&a->recommendation_score!=b->recommendation_score)return a->recommendation_score>b->recommendation_score;
    return a->display_name<b->display_name;
  });
  const float s=l.scale,gap=10*s,x=l.graph.x+12*s,available=l.graph.width-24*s;float y=l.graph.y+82*s-guided_scroll_.scroll_offset;
  const bool rows=list_view_||mode_==ResearchViewMode::Queue;
  const int columns=rows?1:std::clamp(static_cast<int>(available/std::max(220*s,220.f)),2,4);
  const float cw=(available-gap*(columns-1))/columns;
  std::vector<const NativeResearchNode*> recommended;
  if(mode_==ResearchViewMode::Guided&&plan_.suggestions&&!rows&&(filter_==0||filter_==2)&&query_.search.empty())
    for(const auto* n:nodes)if(n->recommendation_score>=0&&n->requirements_met&&!n->active&&recommended.size()<static_cast<std::size_t>(columns))recommended.push_back(n);
  if(!recommended.empty()){
    for(int i=0;i<static_cast<int>(recommended.size());++i)cards.push_back({recommended[i]->id,{x+i*(cw+gap),y,cw,368*s},true});
    y+=408*s;
  }
  int i=0;const float card_height=rows?106*s:282*s;
  for(const auto* n:nodes){if(std::ranges::find(recommended,n)!=recommended.end())continue;cards.push_back({n->id,{x+(i%columns)*(cw+gap),y+(i/columns)*(card_height+gap),cw,card_height},false});++i;}
  return cards;
}
void NativeResearchWorkspace::render_dashboard(DrawList& out,const ResearchWorkspaceLayout& l){
  const float s=l.scale;const auto cards=guided_cards(l);
  const auto title=tr(mode_==ResearchViewMode::Recent?"RESEARCH_VIEW_RECENT":mode_==ResearchViewMode::Favorites?"RESEARCH_VIEW_FAVORITES":mode_==ResearchViewMode::Completed?"RESEARCH_VIEW_COMPLETED":mode_==ResearchViewMode::Queue?"RESEARCH_VIEW_QUEUE":plan_.suggestions?"RESEARCH_VIEW_RECOMMENDED":"RESEARCH_VIEW_AVAILABLE",mode_==ResearchViewMode::Recent?"RECENTLY COMPLETED":mode_==ResearchViewMode::Favorites?"FAVORITE TECHNOLOGIES":mode_==ResearchViewMode::Completed?"COMPLETED RESEARCH":mode_==ResearchViewMode::Queue?"RESEARCH QUEUE":plan_.suggestions?"RECOMMENDED & AVAILABLE":"AVAILABLE TECHNOLOGIES");
  text(out,{l.graph.x+12*s,l.graph.y+49*s,l.graph.width-24*s,26*s},title,bright,l.body_font_pixels);
  const UiRect content{l.graph.x,l.graph.y+80*s,l.graph.width,l.graph.height-80*s};float extent=0;bool section=false;
  for(const auto& card:cards){const auto clip=intersection(card.bounds,content);extent=std::max(extent,card.bounds.y+card.bounds.height+guided_scroll_.scroll_offset-content.y);if(!clip)continue;
    const auto n=std::ranges::find(window_->nodes,card.id,&NativeResearchNode::id);if(n==window_->nodes.end())continue;
    if(!card.recommended&&!section&&std::ranges::any_of(cards,[](const auto& c){return c.recommended;})){clipped_text(out,{card.bounds.x,card.bounds.y-30*s,l.graph.width-24*s,25*s},content,tr("RESEARCH_OTHER_AVAILABLE","OTHER AVAILABLE TECHNOLOGIES"),muted,l.small_font_pixels);section=true;}
    const auto r=card.bounds;stellar::engine::ui_skin::surface(out,r,s,selected_node_id_==n->id,content);
    const bool row=list_view_||mode_==ResearchViewMode::Queue;const UiRect art{r.x+6*s,r.y+6*s,row?88*s:r.width-12*s,row?r.height-12*s:102*s};
    if(artwork_resolver_)if(const auto image=artwork_resolver_(n->id,true))out.overlay.emplace_back(Image{image,art,research_art_source(*image,art), {255,255,255,255},*clip});
    if(card.recommended&&!row){const UiRect badge{art.x+4*s,art.y+art.height-20*s,104*s,20*s};stellar::engine::ui_skin::gradient(out,badge,{35,158,211,255},{8,81,119,255},3*s,content);clipped_text(out,{badge.x+5*s,badge.y+2*s,badge.width-10*s,badge.height},content,tr("RESEARCH_RECOMMENDED_BADGE","RECOMMENDED"),bright,std::max(10,l.small_font_pixels-2));}
    const float tx=row?art.x+art.width+10*s:r.x+10*s,tw=row?r.width-110*s:r.width-20*s;float y=row?r.y+8*s:r.y+114*s;
    const auto line=[&](std::string value,Color color,int font,float h){
      const auto fitted=stellar::engine::fit_text_to_box(value,tw,h,[&](std::string_view label){
        if(text_measurer_){const auto extent=text_measurer_(Text{{},std::string(label),color,font,tw});
          return stellar::engine::TextFitExtent{static_cast<float>(extent.width),static_cast<float>(extent.height)};}
        const float line_height=font*1.25f;
        const float raw_width=static_cast<float>(label.size())*font*.62f;
        return stellar::engine::TextFitExtent{std::min(tw,raw_width),std::ceil(raw_width/tw)*line_height};
      });
      clipped_text(out,{tx,y,tw,h},*clip,fitted,color,font);y+=h;
    };
    line(n->display_name,bright,l.body_font_pixels,row?23*s:40*s);
    line(n->domain_label+" · Tier "+std::to_string(n->graph_depth+1),muted,l.small_font_pixels,22*s);
    const std::string state=n->active?(n->paused?"PAUSED":"RESEARCHING"):established(*n)?"✓ COMPLETED":includes(plan_.queue,n->id)?"QUEUED":n->primary_action.enabled?(card.recommended?"RECOMMENDED":"AVAILABLE"):(n->requirements_met?"WAITING · Capacity / funding":"LOCKED · Requirements");
    line(state,n->active||established(*n)?positive:n->primary_action.enabled?Color{124,226,246,255}:warning,l.small_font_pixels,22*s);
    if(!row){line(n->cost&&std::isfinite(n->cost->estimated_years_at_full_funding)?trf("RESEARCH_DURATION_FUNDED",{stellar::native_campaign::format_campaign_duration(n->cost->estimated_years_at_full_funding*365.25)},"{0} at full funding"):tr("RESEARCH_DURATION_STAFFED","Duration requires staffed labs"),muted,l.small_font_pixels,35*s);line(concise(n->benefits,95),positive,l.small_font_pixels,card.recommended?48*s:36*s);
      if(card.recommended&&!n->recommendation_reasons.empty()){line(tr("RESEARCH_RECOMMENDED_BECAUSE","Recommended because:"),warning,l.small_font_pixels,20*s);line(concise(n->recommendation_reasons.front(),90),muted,l.small_font_pixels,44*s);}}
    if(mode_==ResearchViewMode::Recent&&n->recent_year)line(trf("RESEARCH_COMPLETED_YEAR",{fixed(*n->recent_year,2)},"Completed in year {0}"),muted,l.small_font_pixels,20*s);
    if(mode_==ResearchViewMode::Queue){
      const auto index=std::ranges::find(plan_.queue,n->id)-plan_.queue.begin();const float bx=r.x+r.width-143*s,by=r.y+r.height-32*s;
      for(int k=0;k<3;++k){const UiRect b{bx+k*46*s,by,42*s,26*s};if(const auto visible=intersection(b,content)){native_menu_style::button(out,*visible,k==0?"↑":k==1?"↓":"×",l.body_font_pixels,b.contains(pointer_),true,s);interface_hits_.push_back({*visible,20+k,n->id});}}
      clipped_text(out,{tx,r.y+r.height-28*s,tw-150*s,26*s},*clip,"#"+std::to_string(index+1)+" · "+(n->active?"Running":established(*n)?"Complete":n->primary_action.enabled?"Ready":concise(n->primary_action.reason,72)),n->primary_action.enabled?positive:warning,l.small_font_pixels);
    }
  }
  if(cards.empty())text(out,{content.x+18*s,content.y+18*s,content.width-36*s,96*s},tr(mode_==ResearchViewMode::Queue?"RESEARCH_EMPTY_QUEUE":mode_==ResearchViewMode::Recent?"RESEARCH_EMPTY_RECENT":"RESEARCH_EMPTY_VIEW",mode_==ResearchViewMode::Queue?"No technologies queued. Add a known program from the inspector. Queued programs wait for their requirements and funding; blocked entries are never skipped.":mode_==ResearchViewMode::Recent?"No completion records in the saved research history yet. Established starting knowledge is listed under Completed Research.":"No technologies match this view. Change the category, search or filter."),muted,l.body_font_pixels);
  guided_scroll_.sync(extent,content.height);
  if(const auto thumb=guided_scroll_.thumb(content.height,20*s);thumb.size>0)fill(out,{content.x+content.width-4*s,content.y+thumb.offset,2*s,thumb.size},border);
}
void NativeResearchWorkspace::render_controls(DrawList& out,const ResearchWorkspaceLayout& l){
  const float s=l.scale;
  const auto button=[&](UiRect r,std::string label,int action,std::string id={},bool enabled=true){native_menu_style::button(out,r,std::move(label),l.small_font_pixels,r.contains(pointer_),enabled,s);if(enabled)interface_hits_.push_back({r,action,std::move(id)});};
  for(int i=0;i<4;++i){UiRect r{l.view_tabs.x+i*l.view_tabs.width/4,l.view_tabs.y,l.view_tabs.width/4-4*s,l.view_tabs.height};button(r,tr(std::array{"RESEARCH_TAB_GUIDED","RESEARCH_TAB_TREE","RESEARCH_TAB_RECENT","RESEARCH_TAB_FAVORITES"}[i],std::array{"Guided","Tech Tree","Recent","Favorites"}[i]),100+i);if(static_cast<int>(mode_)==i)fill(out,{r.x,r.y+r.height-2*s,r.width,2*s},{108,223,251,255});}
  button(l.completed,tr("RESEARCH_TAB_COMPLETED","✓ Completed Research"),104);button(l.queue,trf("RESEARCH_TAB_QUEUE",{std::to_string(plan_.queue.size())},"Research Queue · {0}"),105);
  if(mode_==ResearchViewMode::Tree){button({l.graph.x+8*s,l.graph.y+6*s,100*s,30*s},tr("RESEARCH_RESET_VIEW","Reset view"),30);button({l.graph.x+116*s,l.graph.y+6*s,108*s,30*s},tr("RESEARCH_FOCUS_SELECTED","Focus selected"),31);button({l.graph.x+232*s,l.graph.y+6*s,100*s,30*s},tr("RESEARCH_FOCUS_ACTIVE","Focus active"),32);}
  else {
    button({l.toolbar.x+8*s,l.toolbar.y+6*s,90*s,30*s},tr("RESEARCH_WHY","Why?"),1);
    button({l.toolbar.x+104*s,l.toolbar.y+6*s,68*s,30*s},tr(list_view_?"RESEARCH_CARDS":"RESEARCH_LIST",list_view_?"Cards":"List"),2);
    button({l.toolbar.x+178*s,l.toolbar.y+6*s,84*s,30*s},tr("RESEARCH_ASSISTANCE","Assistance"),3);
    button(l.sort,tr(std::array{"RESEARCH_SORT_RELEVANCE","RESEARCH_SORT_TIME","RESEARCH_SORT_TIER","RESEARCH_SORT_CATEGORY","RESEARCH_SORT_NAME"}[sort_],std::array{"Relevance ▾","Time ▾","Tier ▾","Category ▾","Name ▾"}[sort_]),5);
    button(l.filter,tr(std::array{"RESEARCH_FILTER_AVAILABLE","RESEARCH_FILTER_ALL","RESEARCH_FILTER_RECOMMENDED","RESEARCH_FILTER_RESEARCHING","RESEARCH_FILTER_QUEUED","RESEARCH_FILTER_LOCKED","RESEARCH_FILTER_COMPLETED","RESEARCH_FILTER_FAVORITES"}[filter_],std::array{"Available ▾","All known ▾","Recommended ▾","Researching ▾","Queued ▾","Locked ▾","Completed ▾","Favorites ▾"}[filter_]),4);
  }
  if(const auto* n=selected_node()){
    button(l.bookmark,includes(plan_.favorites,n->id)?"★ Favorited":"☆ Favorite",6,n->id);
    button(l.enqueue,includes(plan_.queue,n->id)?"Remove queue":"Add to queue",7,n->id,(!established(*n)&&!n->active)||includes(plan_.queue,n->id));
    auto tree = l.tree_focus;
    if (n->active) {
      tree.width = (tree.width-6*s)*.5f;
      button({tree.x+tree.width+6*s,tree.y,tree.width,tree.height},tr("RESEARCH_CANCEL_PROGRAM","Cancel research"),11,n->id,n->cancel_action.enabled);
    }
    button(tree,tr("RESEARCH_VIEW_IN_TREE","View in tree"),8,n->id);
  }
  native_menu_style::panel(out,l.active,s);
  text(out,{l.active.x+12*s,l.active.y+8*s,l.active.width-24*s,24*s},trf("RESEARCH_ACTIVE_PROGRAMS",{std::to_string(window_->active_program_count),window_->lab_capacity_only?tr("RESEARCH_ACTIVE_LIMITED"," programs · laboratory capacity limited"):trf("RESEARCH_ACTIVE_SLOTS",{std::to_string(window_->maximum_programs.value_or(0))}," / {0} slots")},"ACTIVE RESEARCH  ·  {0}{1}"),bright,l.body_font_pixels);
  active_scroll_.sync((window_?window_->active_program_count+1:1)*264*s+16*s,l.active.width);
  float x=l.active.x+10*s-active_scroll_.scroll_offset;const UiRect area{l.active.x+8*s,l.active.y+38*s,l.active.width-16*s,l.active.height-44*s};
  for(const auto& n:window_->nodes)if(n.active){const UiRect r{x,area.y,255*s,area.height};x+=264*s;if(const auto clip=intersection(r,area)){
    stellar::engine::ui_skin::surface(out,r,s,false,area);clipped_text(out,{r.x+8*s,r.y+7*s,r.width-52*s,28*s},*clip,concise(n.display_name,26),bright,l.small_font_pixels);
    const float progress=static_cast<float>(std::clamp(n.total_progress,0.,1.));const UiRect bar{r.x+8*s,r.y+38*s,r.width-16*s,6*s};stellar::engine::ui_skin::progress(out,bar,progress,s,area);
    clipped_text(out,{r.x+8*s,r.y+52*s,r.width-16*s,25*s},*clip,(n.paused?"Paused · ":"")+fixed(progress*100,0)+"% · "+fixed(n.assigned_effective_labs,1)+" labs",n.paused?warning:muted,l.small_font_pixels);interface_hits_.push_back({*clip,10,n.id});
    const UiRect cancel{r.x+r.width-38*s,r.y+5*s,30*s,30*s};
    if (const auto c=intersection(cancel,area)) {
      fill(out,*c,raised);clipped_stroke(out,cancel,area,border);
      clipped_text(out,cancel,area,"×",n.cancel_action.enabled?warning:muted,l.body_font_pixels);
      if(n.cancel_action.enabled)interface_hits_.push_back({*c,11,n.id});
    }
  }}
  if(window_->free_effective_labs>0&&(window_->lab_capacity_only||window_->active_program_count<window_->maximum_programs.value_or(0))){UiRect r{x,area.y,245*s,area.height};if(const auto clip=intersection(r,area)){fill(out,*clip,raised);clipped_stroke(out,r,area,border);clipped_text(out,{r.x+8*s,r.y+10*s,r.width-16*s,area.height-16*s},*clip,trf("RESEARCH_AVAILABLE_CAPACITY",{fixed(window_->free_effective_labs,1)},"+  AVAILABLE CAPACITY\n{0} free labs · choose research"),positive,l.small_font_pixels);interface_hits_.push_back({*clip,100,{}});}}
  if(why_open_){native_menu_style::panel(out,l.graph,s);text(out,{l.graph.x+18*s,l.graph.y+16*s,l.graph.width-36*s,36*s},tr("RESEARCH_WHY_TITLE","WHY THESE RECOMMENDATIONS?"),bright,l.body_font_pixels);float y=l.graph.y+58*s;int count=0;for(const auto& c:guided_cards(l))if(c.recommended&&count++<3){const auto n=std::ranges::find(window_->nodes,c.id,&NativeResearchNode::id);if(n==window_->nodes.end())continue;text(out,{l.graph.x+18*s,y,l.graph.width-36*s,30*s},n->display_name,positive,l.body_font_pixels);y+=32*s;for(const auto& reason:n->recommendation_reasons){text(out,{l.graph.x+18*s,y,l.graph.width-36*s,38*s},"• "+reason,muted,l.small_font_pixels);y+=38*s;}y+=10*s;}
    if(!count)text(out,{l.graph.x+18*s,y,l.graph.width-36*s,100*s},tr("RESEARCH_WHY_EXPLANATION","Recommendations use the existing adaptive agenda: recognized pressures, readiness, capability gaps, research priorities and laboratory opportunity cost. No recommendation is available in this view."),muted,l.small_font_pixels);
    button({l.graph.x+l.graph.width-90*s,l.graph.y+10*s,76*s,30*s},tr("RESEARCH_WHY_CLOSE","Close"),1);
  }
}
std::optional<WorkspaceCommand> NativeResearchWorkspace::handle_controls(const InputEvent& e,const ResearchWorkspaceLayout& l,int width,int height){
  const WorkspaceCommand consumed{WorkspaceCommandKind::None,true};
  if(dropdown_.visible()){const int id=dropdown_.id();if(auto value=dropdown_.handle(e,id==1?l.filter:id==2?l.sort:l.toolbar,width,height)){if(id==1)filter_=*value;else if(id==2)sort_=*value;else return WorkspaceCommand{WorkspaceCommandKind::Execute,true,{},*value==1?NativeResearchIntent::SuggestionsOn:NativeResearchIntent::SuggestionsOff};guided_scroll_={};}return consumed;}
  if(e.type==InputEventType::EscapePressed&&why_open_){why_open_=false;return consumed;}
  if(e.type==InputEventType::Wheel&&l.active.contains(e.position)){active_scroll_.sync((window_?window_->active_program_count+1:1)*264*l.scale+16*l.scale,l.active.width);active_scroll_.scroll_by(-e.wheel_y*120*l.scale);return consumed;}
  if(e.type!=InputEventType::LeftPressed)return {};
  if(why_open_){why_open_=false;return consumed;}
  for(auto hit=interface_hits_.rbegin();hit!=interface_hits_.rend();++hit)if(hit->bounds.contains(e.position)){
    const auto action=hit->action;const auto id=hit->id;
    if(action>=100){mode_=static_cast<ResearchViewMode>(action-100);guided_scroll_={};search_focused_=false;return consumed;}
    if(action==1)why_open_=!why_open_;
    else if(action==2){list_view_=!list_view_;guided_scroll_={};}
    else if(action==3)dropdown_.open(3,{tr("RESEARCH_ASSIST_OFF","Off"),tr("RESEARCH_ASSIST_SUGGESTIONS","Suggestions only")},plan_.suggestions?1:0);
    else if(action==4)dropdown_.open(1,{tr("RESEARCH_DD_AVAILABLE","Available"),tr("RESEARCH_DD_ALL_KNOWN","All known"),tr("RESEARCH_DD_RECOMMENDED","Recommended"),tr("RESEARCH_DD_RESEARCHING","Researching"),tr("RESEARCH_DD_QUEUED","Queued"),tr("RESEARCH_DD_LOCKED","Locked"),tr("RESEARCH_DD_COMPLETED","Completed"),tr("RESEARCH_DD_FAVORITES","Favorites")},filter_);
    else if(action==5)dropdown_.open(2,{tr("RESEARCH_DD_RELEVANCE","Relevance"),tr("RESEARCH_DD_TIME","Research time"),tr("RESEARCH_DD_TIER","Tier"),tr("RESEARCH_DD_CATEGORY","Category"),tr("RESEARCH_DD_NAME","Name")},sort_);
    else if(action==6||action==7){const bool present=includes(action==6?plan_.favorites:plan_.queue,id);
      return WorkspaceCommand{WorkspaceCommandKind::Execute,true,id,action==6?(present?NativeResearchIntent::RemoveFavorite:NativeResearchIntent::AddFavorite):(present?NativeResearchIntent::RemoveQueued:NativeResearchIntent::Enqueue)};}
    else if(action==11&&window_){
      const auto n=std::ranges::find(window_->nodes,id,&NativeResearchNode::id);
      if(n!=window_->nodes.end()&&n->active&&n->cancel_action.enabled)
        return WorkspaceCommand{WorkspaceCommandKind::Execute,true,id,NativeResearchIntent::Cancel};
    }
    else if(action>=20&&action<=22)return WorkspaceCommand{WorkspaceCommandKind::Execute,true,id,action==20?NativeResearchIntent::MoveUp:action==21?NativeResearchIntent::MoveDown:NativeResearchIntent::RemoveQueued};
    else if(action==8||action==10){query_.domain_id.reset();query_.search.clear();rebuild_topology();select(id);if(action==8){mode_=ResearchViewMode::Tree;center_selection_=true;}return WorkspaceCommand{WorkspaceCommandKind::Select,true,id};}
    else if(action==30){pan_={24,30};zoom_=1;}
    else if(action==31)center_selection_=true;
    else if(action==32&&window_){const auto active=std::ranges::find_if(window_->nodes,[](const auto& n){return n.active;});if(active!=window_->nodes.end()){query_={};rebuild_topology();select(active->id);center_selection_=true;return WorkspaceCommand{WorkspaceCommandKind::Select,true,active->id};}}
    return consumed;
  }return {};
}
