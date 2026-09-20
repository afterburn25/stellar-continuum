#pragma once
#include <stellar/core/stellar_object.hpp>
#include <stellar/engine/stochastic_timeline.hpp>
#include <array>
#include <queue>
#include <tuple>
#include <span>
#include <string_view>

namespace stellar::core {
enum class StellarActivityLevel { Quiet,Normal,Active,VeryActive,FlareStar };
enum class StellarEruptionType { SmallProminence,Flare,MajorFlare,Superflare,Cme };
enum class EruptionSpectralClass { O,B,A,F,G,K,M,Unsupported };
struct EruptionTiming {std::array<double,4> minimum_minutes{},maximum_minutes{};double min_display_seconds{};};
struct StellarActivityConfiguration {
  int version{1};double base_events_per_day{},m_cme_escape{},giant_rate{},supergiant_rate{},history_days{};
  std::array<double,7> spectral_rates{},retention_myr{},rotation_reference_days{};
  std::array<double,5> activity_multipliers{},cme_probabilities{};
  std::array<int,5> simultaneous_caps{};
  std::array<std::array<double,5>,5> type_weights{};
  std::array<EruptionTiming,5> timings{};
};
const StellarActivityConfiguration& stellar_activity_configuration();
StellarActivityConfiguration parse_stellar_activity_configuration(std::string_view);
std::string_view stellar_activity_name(StellarActivityLevel);
std::string_view stellar_eruption_name(StellarEruptionType);
std::string_view eruption_spectral_name(EruptionSpectralClass);
EruptionSpectralClass eruption_spectral_class(StellarObjectType);

struct StellarActivityProfile {
  int version{1};EruptionSpectralClass spectral{EruptionSpectralClass::Unsupported};
  StellarActivityLevel level{StellarActivityLevel::Normal};
  double score{},magnetic_activity{},rotation_days{},age_modifier{},rotation_modifier{},evolution_modifier{1};
  double flare_rate_multiplier{1},prominence_rate_multiplier{1},superflare_rate_multiplier{1},cme_rate_multiplier{1};
  bool inferred_rotation{true};
  bool operator==(const StellarActivityProfile&)const=default;
};
struct StellarEruptionEvent {
  std::uint64_t id{},seed{},parent_event_id{};int system_id{},component{},visual_variant{};
  StellarEruptionType type{};StellarActivityLevel activity_source{};
  double magnitude{1},start_day{};std::array<double,4> stage_days{};
  double latitude{},longitude{},orientation{},scale{1},brightness{1},cme_probability{};
  bool cme_associated{},cme_escaped{},forced{},paused{};double paused_elapsed_days{};
  bool operator==(const StellarEruptionEvent&)const=default;
};
struct StellarActivityState {
  StellarActivityProfile profile;std::uint64_t seed{},counter{},revision{};
  double next_event_day{},last_day{};
  std::array<std::uint64_t,5> generated_counts{};std::uint64_t cme_opportunities{},escaping_cmes{},suppressed_events{};
  // Active plus bounded recent authoritative events support perceptible high-speed
  // presentation. Never creates view-local eruptions or stores GPU resources.
  std::vector<StellarEruptionEvent> events;
  bool operator==(const StellarActivityState&)const=default;
};
struct StellarSystem;struct FreshCampaignState;
StellarActivityProfile make_stellar_activity_profile(const StellarPhysicalProperties&,std::uint64_t,const StellarActivityConfiguration& = stellar_activity_configuration());
double stellar_eruption_rate(const StellarActivityProfile&,const StellarActivityConfiguration& = stellar_activity_configuration());
double stellar_cme_probability(const StellarActivityProfile&,StellarEruptionType,const StellarActivityConfiguration& = stellar_activity_configuration());
double stellar_eruption_duration(const StellarEruptionEvent&);
stellar::engine::TimelineSample stellar_eruption_sample(const StellarEruptionEvent&,double day);
void initialize_stellar_activity(std::int64_t,std::span<StellarSystem>,double day=0);
void validate_stellar_activity(const StellarSystem&);
void validate_stellar_activity_clock(std::optional<double>);
// Clean future space-weather hook; no unimplemented ship/colony damage implied.
struct TravelingCmeLaunch {std::uint64_t event_id{};int system_id{},component{};double launch_day{},latitude{},longitude{},speed_km_s{},magnitude{};};
class StellarActivityScheduler {
 public:
  void rebuild(std::span<StellarSystem>);
  std::vector<TravelingCmeLaunch> advance(std::span<StellarSystem>,double day,const StellarActivityConfiguration& = stellar_activity_configuration());
  void reschedule(std::span<StellarSystem>,int system_id,int component);
  std::size_t scheduled_stars()const{return queue_.size();}
 private:
  struct Due {double day{};std::size_t index{};int component{};std::uint64_t revision{};bool operator>(const Due& b)const{return std::tie(day,index,component)>std::tie(b.day,b.index,b.component);}};
  std::priority_queue<Due,std::vector<Due>,std::greater<Due>> queue_;
  const StellarSystem* base_{};std::size_t size_{};
};
enum class StellarActivityAction { Force,SetLevel,SetMagnitude,SetVariant,TogglePause,Scrub,Restart,MoveRegion,ClearForced };
struct StellarActivityCommand {
  StellarActivityAction action{};int system_id{},component{};StellarEruptionType type{};
  StellarActivityLevel level{};std::uint64_t event_id{};int variant{};
  double magnitude{1},fraction{},latitude{},longitude{},orientation{};
  bool randomize_position{}; // Force only; explicit sites remain available for QA.
};
std::uint64_t apply_developer_stellar_activity(FreshCampaignState&,StellarActivityScheduler&,double day,const StellarActivityCommand&);
} // namespace stellar::core
