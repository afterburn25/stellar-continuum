#include <stellar/core/survey_operations.hpp>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
using namespace stellar::core;
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
bool same(const SurveyOperationsProfile& a,const SurveyOperationsProfile& b){
  return a.system_id==b.system_id&&a.planet_count==b.planet_count&&a.moon_count==b.moon_count&&
    a.estimated_science_survey_days==b.estimated_science_survey_days&&a.operational_hazard==b.operational_hazard;
}
int main(int argc,char**)try{
  const int count=argc>1?50000:1000,queries=1000;
  std::vector<StellarSystem> systems(count);
  std::vector<PlanetaryBody> bodies;
  bodies.reserve(static_cast<std::size_t>(count)*7);
  for(int i=0;i<count;++i){
    systems[i].id=i*3+17;systems[i].archetype=i%3==0?StarArchetype::Dangerous:StarArchetype::Nebula;
    if(i%5==0)systems[i].primary=StellarClass::WhiteDwarf;
    for(int j=0;j<7;++j){PlanetaryBody b;b.id=i*7+j;b.system_id=systems[i].id;
      b.kind=j<3?PlanetaryBodyKind::Moon:PlanetaryBodyKind::Planet;
      b.has_anomaly=i%11==0;b.has_rare_resource=i%13==0;
      b.environment.radiation_hazard=(i%4==0||j==0)?std::numeric_limits<double>::quiet_NaN():i%7*.14;
      bodies.push_back(std::move(b));
    }
  }
  std::reverse(bodies.begin(),bodies.end());
  SurveyOperationsProfiler reference;
  using Clock=std::chrono::steady_clock;
  const auto start=Clock::now();
  std::vector<SurveyOperationsProfile> expected;
  for(int i=0;i<queries;++i)expected.push_back(reference.build(systems,bodies,systems[static_cast<std::size_t>(i)*count/queries].id));
  const auto reference_end=Clock::now();
  SurveyOperationsBatch batch(systems,bodies);
  for(int i=0;i<queries;++i)check(same(expected[i],batch.build(expected[i].system_id)),"Batch changed survey effort/hazard/counts");
  const auto batch_end=Clock::now();
  bool rejected=false;try{(void)batch.build(-999);}catch(const std::runtime_error&){rejected=true;}check(rejected,"Absent system accepted");
  // New scopes see mutations immediately. Duplicate systems retain first-match
  // behavior and orphan bodies are ignored, matching the existing profiler.
  bodies.front().environment.radiation_hazard=1.;bodies.front().kind=PlanetaryBodyKind::Moon;
  systems.push_back(systems.front());systems.back().primary=StellarClass::BlackHole;
  bodies.push_back(bodies.front());bodies.back().system_id=-999;
  SurveyOperationsBatch changed(systems,bodies);
  for(const auto id:{systems.front().id,systems[count-1].id})check(same(reference.build(systems,bodies,id),changed.build(id)),"Batch stale or changed first-match semantics");
  SurveyOperationsBatch empty(systems,{});check(same(reference.build(systems,{},systems.front().id),empty.build(systems.front().id)),"Empty system mismatch");
  std::cout<<"{\"systems\":"<<count<<",\"bodies\":"<<count*7<<",\"queries\":"<<queries
    <<",\"referenceMilliseconds\":"<<std::chrono::duration<double,std::milli>(reference_end-start).count()
    <<",\"batchMilliseconds\":"<<std::chrono::duration<double,std::milli>(batch_end-reference_end).count()<<",\"exactMatch\":true}\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
