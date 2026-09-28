// Shared Sandbox workflow. All resolved settings come from Core's canonical config.
namespace {
constexpr std::array galaxy_card_order{stellar::core::GalaxyMorphology::Spiral,stellar::core::GalaxyMorphology::BarredSpiral,stellar::core::GalaxyMorphology::Elliptical,stellar::core::GalaxyMorphology::Lenticular,stellar::core::GalaxyMorphology::Irregular,stellar::core::GalaxyMorphology::Ring};
constexpr std::array galaxy_card_names{"Spiral Galaxy","Barred Spiral Galaxy","Elliptical Galaxy","Lenticular Galaxy","Irregular Galaxy","Ring Galaxy"};
constexpr std::array galaxy_card_name_keys{"SETUP_GALAXY_SPIRAL","SETUP_GALAXY_BARRED","SETUP_GALAXY_ELLIPTICAL","SETUP_GALAXY_LENTICULAR","SETUP_GALAXY_IRREGULAR","SETUP_GALAXY_RING"};
constexpr std::array galaxy_card_descriptions{"Sweeping arms around a bright central bulge.","Spiral arms extend from an elongated central bar.","A broad body dominated by older stellar populations.","A smooth disk and bulge with little spiral structure.","Asymmetric structure and patchy stellar nurseries.","A distinct ring surrounds a sparse interior."};
constexpr std::array galaxy_card_description_keys{"SETUP_GALAXY_SPIRAL_DESC","SETUP_GALAXY_BARRED_DESC","SETUP_GALAXY_ELLIPTICAL_DESC","SETUP_GALAXY_LENTICULAR_DESC","SETUP_GALAXY_IRREGULAR_DESC","SETUP_GALAXY_RING_DESC"};
constexpr std::array population_description_keys{"SETUP_POPSTATE_DETERMINED","SETUP_POPSTATE_INTENSE","SETUP_POPSTATE_ONGOING","SETUP_POPSTATE_BALANCED","SETUP_POPSTATE_REDUCED","SETUP_POPSTATE_MINIMAL"};
constexpr std::array population_descriptions{
 "Population state is determined from the galaxy seed and morphology. The same settings always give the same result.",
 "Intense star formation. More young massive stars, supergiants and Wolf-Rayet stars, with stronger stellar hazards.",
 "Strong ongoing star formation with elevated young, massive stellar populations.",
 "A balanced mixture of stellar ages with moderate ongoing star formation.",
 "Reduced star formation, with a greater relative presence of older stars and stellar remnants.",
 "Very little current star formation. Older, long-lived stars and remnants dominate."};
}
GalaxyChoiceLayout GalaxyChoiceLayout::for_viewport(int width,int height) noexcept {
  const float s=std::clamp(height/1080.f,.65f,2.f),pw=std::min(1520*s,width-48*s),ph=std::min(942*s,height-40*s);
  GalaxyChoiceLayout l;l.scale=s;l.panel={(width-pw)*.5f,(height-ph)*.5f,pw,ph};
  const float x=l.panel.x+28*s,y=l.panel.y+24*s,w=pw-56*s;
  l.heading={x,y,w,54*s};l.back={x,l.panel.y+ph-64*s,124*s,40*s};l.next={x+w-170*s,l.back.y,170*s,40*s};
  const float gap=20*s,cw=(w-gap*2)/3,ch=(l.back.y-y-90*s-gap)/2;
  for(int i=0;i<6;++i)l.cards[i]={x+(i%3)*(cw+gap),y+82*s+(i/3)*(ch+gap),cw,ch};
  l.preview={x,y+98*s,w*.57f,ph-210*s};
  const float dx=l.preview.x+l.preview.width+30*s,dw=x+w-dx;
  l.population={dx,y+146*s,dw,46*s};l.description={dx,y+215*s,dw,164*s};l.summary={dx,y+404*s,dw,ph-520*s};
  return l;
}
void NativeNewGameWorkspace::begin_sandbox(){restore_defaults();reset_interaction();}
std::optional<stellar::core::GalaxyGenerationConfig> NativeNewGameWorkspace::generation_configuration()const {
  const auto seed=stellar::native_setup::parse_campaign_seed(seed_text_);if(!seed)return {};
  stellar::core::GalaxyGenerationConfig c;c.base_seed=*seed;c.morphology=population_.morphology;c.requested_population=requested_population_;c.system_count=selected_system_count_;c.pre_warp_count=selected_pre_warp_civilization_count_;c.ancient_count=selected_ancient_civilization_count_;c.player_species_id=selected_species_id_;c.developer_full_coverage=developer_coverage_;
  return stellar::core::resolve_galaxy_configuration(std::move(c));
}
NativeNewGameIntent NativeNewGameWorkspace::handle_galaxy_page(const InputEvent& e,int width,int height){
  const auto l=GalaxyChoiceLayout::for_viewport(width,height);
  if(e.type==InputEventType::PointerMove)pointer_=e.position;
  if(dropdown_.visible()){
    hover_feedback_.update(e,dropdown_.hover_target(e.position,l.population,width,height));
    if(auto chosen=dropdown_.handle(e,l.population,width,height))requested_population_=static_cast<stellar::core::PopulationSelection>(*chosen);
    return {NativeNewGameIntentKind::None,true};
  }
  auto target=stellar::native_menu_audio::hit(e.position,{l.back,(page_==SandboxPage::Population||morphology_selected_)?l.next:UiRect{},page_==SandboxPage::Population?l.population:UiRect{}});
  if(page_==SandboxPage::GalaxyType)for(std::size_t i=0;i<l.cards.size();++i)if(l.cards[i].contains(e.position))target=100+i;
  hover_feedback_.update(e,target);
  if(e.type==InputEventType::PointerCancelled){reset_interaction();return {NativeNewGameIntentKind::None,true};}
  if(e.type==InputEventType::EscapePressed||(e.type==InputEventType::LeftPressed&&l.back.contains(e.position))){
    const bool first=page_==SandboxPage::GalaxyType;page_=SandboxPage::GalaxyType;reset_interaction();return {first?NativeNewGameIntentKind::Cancel:NativeNewGameIntentKind::None,true};
  }
  if(e.type!=InputEventType::LeftPressed)return {NativeNewGameIntentKind::None,true};
  pointer_=e.position;
  if(page_==SandboxPage::GalaxyType){
    for(std::size_t i=0;i<l.cards.size();++i)if(l.cards[i].contains(e.position)){population_.morphology=galaxy_card_order[i];morphology_selected_=true;return {NativeNewGameIntentKind::None,true};}
    if(l.next.contains(e.position)&&morphology_selected_){page_=SandboxPage::Population;reset_interaction();}
  }else{
    if(l.population.contains(e.position)){
      std::vector<std::string> labels;for(int i=0;i<6;++i)labels.emplace_back(stellar::core::population_selection_label(static_cast<stellar::core::PopulationSelection>(i)));
      dropdown_.open(0,std::move(labels),static_cast<int>(requested_population_));
    }else if(l.next.contains(e.position)){page_=SandboxPage::Configuration;reset_interaction();}
  }
  return {NativeNewGameIntentKind::None,true};
}
void NativeNewGameWorkspace::render_galaxy_page(DrawList& out,int width,int height,const PortraitProvider* provider,std::shared_ptr<const RgbaImage> backdrop)const{
  const auto l=GalaxyChoiceLayout::for_viewport(width,height);const float s=l.scale;const int title=static_cast<int>(27*s),body=std::max(13,static_cast<int>(18*s)),small=std::max(12,static_cast<int>(15*s));
  if(backdrop){const float k=std::max(width/static_cast<float>(backdrop->width()),height/static_cast<float>(backdrop->height()));out.overlay.emplace_back(Image{backdrop,{(width-backdrop->width()*k)*.5f,(height-backdrop->height()*k)*.5f,backdrop->width()*k,backdrop->height()*k},std::nullopt,{255,255,255,255}});}
  else fill(out,{0,0,static_cast<float>(width),static_cast<float>(height)},background);
  stellar::engine::ui_skin::surface(out,l.panel,s);
  text(out,l.heading,tr(page_==SandboxPage::GalaxyType?"SETUP_GALAXY_TYPE_TITLE":"SETUP_POPULATION_TITLE",page_==SandboxPage::GalaxyType?"CHOOSE GALAXY TYPE":"GALAXY POPULATION STATE"),{150,225,255,255},title,TextAlign::Left,FontFace::Heading);
  text(out,{l.heading.x,l.heading.y+43*s,l.heading.width,26*s},tr(page_==SandboxPage::GalaxyType?"SETUP_GALAXY_TYPE_HINT":"SETUP_POPULATION_HINT",page_==SandboxPage::GalaxyType?"Choose the shape of your civilization’s new home.":"Choose its stellar age and activity. Galaxy shape and population are independent."),muted,small);
  const auto image=[&](UiRect area,const std::string& path){if(!provider)return;const auto art=(*provider)(path);if(!art)return;const float k=std::min(area.width/art->width(),area.height/art->height());out.overlay.emplace_back(Image{art,{area.x+(area.width-art->width()*k)*.5f,area.y+(area.height-art->height()*k)*.5f,art->width()*k,art->height()*k},std::nullopt,{255,255,255,255},area});};
  if(page_==SandboxPage::GalaxyType){
    for(std::size_t i=0;i<l.cards.size();++i){const auto r=l.cards[i];const bool active=morphology_selected_&&population_.morphology==galaxy_card_order[i];stellar::engine::ui_skin::control(out,r,s,r.contains(pointer_),active);
      const float ih=std::min((r.width-16*s)*9/16,r.height-92*s);
      image({r.x+8*s,r.y+8*s,r.width-16*s,ih},stellar::core::galaxy_visual_pair(galaxy_card_order[i]).preview_path);
      text(out,{r.x+16*s,r.y+ih+20*s,r.width-32*s,28*s},tr(galaxy_card_name_keys[i],galaxy_card_names[i]),bright,body);
      text(out,{r.x+16*s,r.y+ih+52*s,r.width-32*s,45*s},tr(galaxy_card_description_keys[i],galaxy_card_descriptions[i]),muted,small);
      if(active)text(out,{r.x+r.width-120*s,r.y+12*s,100*s,24*s},tr("SETUP_SELECTED","✓ SELECTED"),{113,231,255,255},small,TextAlign::Right);
    }
  }else{
    const auto c=generation_configuration();const auto pair=stellar::core::galaxy_visual_pair(population_.morphology,c?std::optional{c->resolved_population}:std::optional{stellar::core::PopulationState::Mature});
    image(l.preview,pair.preview_path);
    text(out,{l.population.x,l.population.y-37*s,l.population.width,29*s},tr("SETUP_POPULATION_STATE","POPULATION STATE"),{150,225,255,255},body);
    native_menu_style::button(out,l.population,std::string(stellar::core::population_selection_label(requested_population_))+"  ▼",body,l.population.contains(pointer_),true,s);
    text(out,l.description,tr(population_description_keys[static_cast<std::size_t>(requested_population_)],population_descriptions[static_cast<std::size_t>(requested_population_)]),bright,body);
    if(c)text(out,l.summary,trf("SETUP_SUMMARY",{std::string(stellar::core::morphology_name(c->morphology)),std::string(stellar::core::population_state_name(c->resolved_population)),seed_text_},"Type: {0}\nResolved population: {1}\nSeed: {2}\n\nYou can change the seed and galaxy size on the next screen."),muted,body);
  }
  native_menu_style::button(out,l.back,tr("STARTUP_BACK","BACK"),body,l.back.contains(pointer_),true,s);
  native_menu_style::button(out,l.next,tr("SETUP_NEXT","NEXT"),body,l.next.contains(pointer_),page_==SandboxPage::Population||morphology_selected_,s);
  if(dropdown_.visible())dropdown_.render(out,l.population,width,height,body);
}
