#pragma once
#include <stellar/core/campaign_frame.hpp>
#include <stellar/core/campaign_diagnostics.hpp>
#include <stellar/core/campaign_calendar.hpp>
#include <set>
#include <tuple>
#include <cmath>

namespace stellar::app_diagnostics {
// Consumes completed canonical results only. Never advances or repairs the
// world. Inspectors/exports use bounded history; normal sessions do no work.
class CampaignDiagnosticMonitor {
public:
  stellar::engine::DiagnosticBuffer &history(){return history_;}
  const stellar::engine::DiagnosticBuffer &history()const{return history_;}
  void reset(){history_.clear();last_inspected_day_.reset();first_critical_.reset();active_findings_.clear();checks_=0;}
  const std::optional<stellar::engine::DiagnosticRecord>& first_critical()const{return first_critical_;}
  std::uint64_t invariant_checks()const{return checks_;}
  void observe(stellar::core::CampaignFrame &frame,const stellar::core::CampaignFrameResult &result,std::string_view timestamp){
    using namespace stellar::core;using namespace stellar::engine;
    const auto &world=frame.runtime().world().campaign();
    if(!world.developer_provenance)return;
    const auto &sim=world.developer_provenance->simulation;
    const auto tick=sim.completed_ticks+sim.tactical_completed_ticks;
    const auto day=frame.clock().simulation_days();
    const auto add=[&](DiagnosticRecord record){
      record.real_timestamp=timestamp;
      // Capture before bounded history filtering/rotation. The host must not
      // miss a critical fault because the viewer changed its recording level.
      if(record.severity==DiagnosticSeverity::Critical&&!first_critical_)first_critical_=record;
      history_.append(std::move(record));
    };
    if(!last_inspected_day_){
      DiagnosticRecord start;start.tick=tick;start.game_date=format_campaign_date(day);start.subsystem="session";
      start.event_type="native_monitor_started";start.message="Observing the isolated developer campaign; prior history is not reconstructed.";add(std::move(start));
    }
    if(result.strategic_results.size()!=result.completed_end_days.size()||tick<result.strategic_results.size())
      throw std::logic_error("Completed diagnostic frame metadata is inconsistent.");
    for(std::size_t i=0;i<result.strategic_results.size();++i)
      for(auto record:campaign_step_diagnostics(result.strategic_results[i],tick-result.strategic_results.size()+i+1,result.completed_end_days[i]))add(std::move(record));
    for(const auto &event:result.tactical_events){
      DiagnosticRecord record;record.tick=tick;record.game_date=format_campaign_date(day);record.subsystem="combat";
      record.event_type="tactical_event";record.message=event.message;record.detail=DiagnosticDetail::Detailed;add(std::move(record));
    }
    if(!last_inspected_day_||std::floor(day)!=std::floor(*last_inspected_day_)){
      ++checks_;auto findings=inspect_campaign_invariants(world,tick,day);
      auto warnings=inspect_campaign_operations(world,tick,day);findings.insert(findings.end(),warnings.begin(),warnings.end());
      auto diplomatic=inspect_diplomacy_invariants(frame.runtime().diplomacy(),world,tick,day);
      findings.insert(findings.end(),diplomatic.begin(),diplomatic.end());
      auto research=inspect_research_invariants(frame.runtime().research(),frame.runtime().research_runtime(),world,tick,day);
      findings.insert(findings.end(),research.begin(),research.end());
      std::set<Key> current;
      for(auto &finding:findings){
        Key key{finding.subsystem,finding.event_type,finding.entity_id.value_or(-1),finding.message};current.insert(key);
        if(!active_findings_.contains(key))add(std::move(finding));
      }
      active_findings_=std::move(current);last_inspected_day_=day;
    }
  }
private:
  using Key=std::tuple<std::string,std::string,std::int64_t,std::string>;
  stellar::engine::DiagnosticBuffer history_;
  std::optional<double> last_inspected_day_;
  std::optional<stellar::engine::DiagnosticRecord> first_critical_;
  std::set<Key> active_findings_;
  std::uint64_t checks_{};
};
}
