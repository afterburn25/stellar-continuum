#include <stellar/core/construction_projects.hpp>
#include <stdexcept>
#include <string>
#include <vector>
#include <iostream>
using namespace stellar::core;
namespace {void require(bool v,const char*m){if(!v)throw std::runtime_error(m);} Civilization civ(){Civilization c;c.id=1;c.name="Player";c.is_player=true;c.species_id="terran_baseline";return c;}}
int main()try{
 std::vector<Civilization> cs{civ()};std::vector<ConstructionState> states{{1,{},std::nullopt,0.,0.,{{"research_network",150.}}}};std::vector<CivilizationEconomy> economies{{1,1000.,1000.}};std::vector<Colony> colonies;std::vector<PlanetaryBody>bodies;std::vector<CivilizationConstructionCapabilities>caps;
 ConstructionWorld world{cs,bodies,states,colonies,economies,caps};
 const auto assessed=assess_construction_project_order(world.read(),1,"industrial_automation",ConstructionOrderIntent::Start);
 require(!assessed.accepted&&assessed.message=="A construction project is already in progress."&&states[0].queued_projects.size()==1&&!states[0].active_project_id,"assessment mutated or missed virtual promotion");
 const auto denied=start_construction_project(world,1,"industrial_automation");
 require(!denied.accepted&&denied.message==assessed.message&&states[0].active_project_id=="research_network"&&states[0].queued_projects.empty(),"live rejected order lost source promotion side effect");
 states[0]={1,{},std::nullopt,0.,0.,{{"research_network",150.}}};economies.clear();
 world={cs,bodies,states,colonies,economies,caps};
 bool assessment_threw{};try{(void)assess_construction_project_order(world.read(),1,"industrial_automation",ConstructionOrderIntent::Queue);}catch(const std::out_of_range&){assessment_threw=true;}
 require(assessment_threw&&states[0].queued_projects.size()==1&&!states[0].active_project_id,"assessment exception mutated live promotion state");
 bool live_threw{};try{(void)queue_construction_project(world,1,"industrial_automation");}catch(const std::out_of_range&){live_threw=true;}
 require(live_threw&&states[0].active_project_id=="research_network"&&states[0].queued_projects.empty(),"exception after promotion lost live source mutation order");
 return 0;
}catch(const std::exception &error){std::cerr<<"construction assessment test failed: "<<error.what()<<'\n';return 1;}
