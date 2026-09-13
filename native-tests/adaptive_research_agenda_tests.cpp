#include <stellar/core/adaptive_research_agenda.hpp>
#include <stellar/core/detail/adaptive_research_expertise_state_writer.hpp>
#include <stellar/core/detail/adaptive_research_state_writer.hpp>

#include <nlohmann/json.hpp>

#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>

using Json = nlohmann::json;
using namespace stellar::core;
using TestAccess = detail::AdaptiveResearchAgendaRuntimeTestAccess;
using CoreWriter = detail::AdaptiveResearchStateWriter;
using ExpertiseWriter = detail::AdaptiveResearchExpertiseStateWriter;

static_assert(std::is_move_constructible_v<AdaptiveResearchAgendaRuntime>);
static_assert(!std::is_copy_constructible_v<AdaptiveResearchAgendaRuntime>);
static_assert(!std::is_constructible_v<AdaptiveResearchAgendaRuntime,
                                       AdaptiveResearchAuthority &&,
                                       const AdaptiveResearchAgendaCatalog &>);
static_assert(!std::is_constructible_v<AdaptiveResearchAgendaRuntime,
                                       const AdaptiveResearchAuthority &,
                                       AdaptiveResearchAgendaCatalog &&>);

namespace {
std::string bytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Could not open '" + path.string() + "'.");
  return {std::istreambuf_iterator<char>(input), {}};
}
void require(bool condition, std::string message) {
  if (!condition) throw std::runtime_error(std::move(message));
}
std::string sha256(std::span<const unsigned char> value) {
  BCRYPT_ALG_HANDLE algorithm{};
  BCRYPT_HASH_HANDLE hash{};
  DWORD object_size{}, ignored{};
  std::vector<unsigned char> object, digest(32);
  require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                      nullptr, 0) >= 0,
          "Could not initialize SHA-256.");
  require(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                            reinterpret_cast<PUCHAR>(&object_size),
                            sizeof(object_size), &ignored, 0) >= 0,
          "Could not query SHA-256.");
  object.resize(object_size);
  require(BCryptCreateHash(algorithm, &hash, object.data(), object_size,
                           nullptr, 0, 0) >= 0 &&
              BCryptHashData(hash, const_cast<PUCHAR>(value.data()),
                             static_cast<ULONG>(value.size()), 0) >= 0 &&
              BCryptFinishHash(hash, digest.data(),
                               static_cast<ULONG>(digest.size()), 0) >= 0,
          "Could not compute SHA-256.");
  BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  constexpr char digits[]="0123456789ABCDEF";
  std::string result;
  for(const auto byte:digest){result.push_back(digits[byte>>4]);result.push_back(digits[byte&15]);}
  return result;
}
std::string fingerprint(const std::filesystem::path &root) {
  std::vector<std::filesystem::path> files;
  for(const auto &entry:std::filesystem::directory_iterator(root))
    if(entry.is_regular_file()&&entry.path().extension()==".json")files.push_back(entry.path());
  std::ranges::sort(files,[](const auto &a,const auto &b){return a.filename().string()<b.filename().string();});
  std::string combined;
  for(const auto &path:files){combined+=path.filename().string();combined+=bytes(path);}
  return sha256({reinterpret_cast<const unsigned char *>(combined.data()),combined.size()});
}
class OwnedScratch final {
public:
 explicit OwnedScratch(const std::filesystem::path &root) {
  parent_=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()/"stellar-gate056-owned");
  std::filesystem::create_directories(parent_); parent_=std::filesystem::weakly_canonical(parent_);
  std::random_device random;
  for(int i=0;i<32;++i){path_=parent_/("native-"+std::to_string(random())+"-"+std::to_string(random()));if(std::filesystem::create_directory(path_)){path_=std::filesystem::weakly_canonical(path_);break;}path_.clear();}
  require(!path_.empty()&&path_.parent_path()==parent_,"Could not establish exclusive scratch ownership.");
  for(const auto *name:{"research_agenda_model.json","scientific_culture_model.json","research_ai_planning_contract.json","research_agenda_runtime_policy.json"})
    std::filesystem::copy_file(root/name,path_/name);
 }
 ~OwnedScratch(){std::error_code error;if(!path_.empty()&&path_.is_absolute()&&std::filesystem::weakly_canonical(path_,error).parent_path()==parent_&&!error)std::filesystem::remove_all(path_,error);}
 const std::filesystem::path &path()const noexcept{return path_;}
private: std::filesystem::path parent_,path_;
};
std::string decode_base64(std::string_view value){
 constexpr std::string_view alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 std::string result;std::uint32_t buffer{};int bits{};
 for(const char c:value){if(c=='=')break;const auto p=alphabet.find(c);require(p!=std::string_view::npos,"Invalid retained base64.");buffer=(buffer<<6)|static_cast<std::uint32_t>(p);bits+=6;if(bits>=8){bits-=8;result.push_back(static_cast<char>((buffer>>bits)&255));}}
 return result;
}
void apply_changes(const std::filesystem::path &scratch,const Json &row){
 for(const auto &change:row.at("ChangedFiles")){
  const auto relative=change.at("RelativePath").get<std::string>();const std::filesystem::path path(relative);
  require(!path.is_absolute()&&relative!="."&&relative!=".."&&path.filename()==path,"Changed path escaped scratch.");
  if(change.at("Deleted").get<bool>()){require(std::filesystem::remove(scratch/path),"Could not delete retained file.");continue;}
  std::ofstream output(scratch/path,std::ios::binary|std::ios::trunc);require(output.is_open(),"Could not open retained file.");output<<decode_base64(change.at("ContentBase64").get<std::string>());output.flush();require(output.good(),"Could not write retained file.");
 }
}
Json number(double value) {
  if (std::isnan(value)) return "NaN";
  if (value == std::numeric_limits<double>::infinity()) return "Infinity";
  if (value == -std::numeric_limits<double>::infinity()) return "-Infinity";
  return value;
}
Json optional_string(const std::optional<std::string> &value) {
  return value ? Json(*value) : Json(nullptr);
}
Json blocker(const ResearchBlocker &value) {
  return {{"Code", static_cast<int>(value.code)},
          {"SubjectId", optional_string(value.subject_id)},
          {"RequiredValue", value.required_value ? number(*value.required_value) : Json(nullptr)},
          {"ActualValue", value.actual_value ? number(*value.actual_value) : Json(nullptr)},
          {"Message", value.message}};
}
Json component(const ResearchProjectUtilityComponent &value) {
  return {{"Id", value.id}, {"Score", number(value.score)},
          {"Explanation", value.explanation}};
}
Json candidate(const ResearchVisibleProjectCandidate &value) {
  Json components=Json::array(); for(const auto &item:value.components) components.push_back(component(item));
  Json blockers=Json::array(); for(const auto &item:value.blockers) blockers.push_back(blocker(item));
  return {{"NodeId",value.node_id},{"UtilityScore",number(value.utility_score)},
          {"CanStart",value.can_start},{"RequestedEffectiveLabs",number(value.requested_effective_labs)},
          {"EstimatedYearsToMature",number(value.estimated_years_to_mature)},
          {"Components",std::move(components)},{"Blockers",std::move(blockers)},
          {"Explanation",value.explanation}};
}
Json candidates(const std::vector<ResearchVisibleProjectCandidate> &values) {
  Json result=Json::array(); for(const auto &value:values) result.push_back(candidate(value)); return result;
}
Json recommendation(const ResearchAgendaReviewRecommendation &value) {
  return {{"DomainId",value.domain_id},{"RecommendedPriorityId",value.recommended_priority_id},
          {"RelevantPressure",number(value.relevant_pressure)},
          {"ComplacencyIndex",number(value.complacency_index)},
          {"ChallengeIndex",number(value.challenge_index)},{"Explanation",value.explanation}};
}
Json priority_entries(std::span<const ResearchAgendaPriorityEntry> values) {
  Json result=Json::array();
  for(const auto &value:values)
    result.push_back({{"Key",value.key},{"Value",value.priority_id}});
  return result;
}
Json state_json(const AdaptiveResearchAgendaState &state) {
  Json axes=Json::array();for(const auto &v:state.culture_axes())axes.push_back({{"Key",v.axis_id},{"Value",number(v.value)}});
  const auto &o=state.orientations();
  return {{"Revision",state.revision()},
          {"Orientations",{{"BasicVsAppliedOrientation",number(o.basic_vs_applied_orientation)},
                           {"CompetencePreservationPolicy",number(o.competence_preservation_policy)},
                           {"PortfolioDiversityPolicy",number(o.portfolio_diversity_policy)},
                           {"ForeignScienceEngagement",number(o.foreign_science_engagement)}}},
          {"LastMajorReviewYear",number(state.last_major_review_year())},
          {"PolicyProvenance",state.policy_provenance()},
          {"DomainPriorities",priority_entries(state.domain_priorities())},
          {"FieldPriorities",priority_entries(state.field_priorities())},
          {"ProblemPriorities",priority_entries(state.problem_priorities())},
          {"CapabilityPriorities",priority_entries(state.capability_priorities())},
          {"CultureAxes",std::move(axes)}};
}
Json catalog_json(const AdaptiveResearchAgendaCatalog &catalog) {
  Json priorities=Json::array();for(const auto &v:catalog.priorities())priorities.push_back({{"Id",v.id},{"Rank",v.rank},{"Score",number(v.score)}});
  Json axes=Json::array();for(const auto &v:catalog.culture_axes())axes.push_back({{"Id",v.id},{"Name",v.name}});
  const auto &p=catalog.runtime_policy();
  Json policy={{"DefaultPriorityId",p.default_priority_id},{"DefaultBasicVsAppliedOrientation",p.default_basic_vs_applied_orientation},
    {"DefaultCompetencePreservation",p.default_competence_preservation},{"DefaultPortfolioDiversity",p.default_portfolio_diversity},
    {"DefaultForeignScienceEngagement",p.default_foreign_science_engagement},{"DefaultCultureAxis",p.default_culture_axis},
    {"BlockedProjectScoreMultiplier",p.blocked_project_score_multiplier},{"AlreadyCoveredSolutionValue",p.already_covered_solution_value},
    {"NovelSolutionValue",p.novel_solution_value},{"TimeToEffectHalfValueYears",p.time_to_effect_half_value_years},
    {"LabCostHalfValueFraction",p.lab_cost_half_value_fraction},{"MinimumAdequacyConfidence",p.minimum_adequacy_confidence},
    {"LowRelevantPressureMaximum",p.low_relevant_pressure_maximum},{"HighAdequacyMinimum",p.high_adequacy_minimum},
    {"ComplacencyDeprioritizeThreshold",p.complacency_deprioritize_threshold},{"ImportantChallengeThreshold",p.important_challenge_threshold},
    {"StrategicChallengeThreshold",p.strategic_challenge_threshold},{"CriticalChallengeThreshold",p.critical_challenge_threshold}};
  return {{"Priorities",std::move(priorities)},{"CultureAxes",std::move(axes)},
          {"UtilityComponentIds",std::vector<std::string>(catalog.utility_component_ids().begin(),catalog.utility_component_ids().end())},
          {"ShortlistBound",catalog.shortlist_bound()},{"RuntimePolicy",std::move(policy)}};
}
struct Error {std::string type;std::string message;};
template<class F> Error caught(F &&call) {try{call();return{};}catch(const std::out_of_range&e){return{"ArgumentOutOfRangeException",e.what()};}catch(const std::invalid_argument&e){return{"ArgumentException",e.what()};}}
template <class F> Error caught_key_not_found(F &&call) {
  try {
    call();
    return {};
  } catch (const std::out_of_range &error) {
    return {"KeyNotFoundException", error.what()};
  }
}
}

int main(int argc,char **argv) {
 try {
  if(argc!=3)throw std::invalid_argument("Expected fixture and research-root paths.");
  const auto fixture_path=std::filesystem::absolute(argv[1]);
  const auto root=std::filesystem::absolute(argv[2]);
  const auto fixture=Json::parse(bytes(fixture_path));
  auto authority=load_adaptive_research_authority(root);
  auto catalog=load_adaptive_research_agenda_catalog(root,authority.catalog(),authority.expertise_catalog());
  AdaptiveResearchAgendaRuntime initial(authority,catalog);
  AdaptiveResearchAgendaRuntime runtime(std::move(initial));
  auto composition=authority.compose_reference_profile("fixture:agenda","reference_humanlike_solar_2050","fixture:context",2050);
  auto state=std::move(composition.state);
  std::vector<ResearchVisibleProjectCandidate> first;
  std::optional<ResearchAgendaReviewRecommendation> saved_recommendation;
  std::string domain=authority.catalog().nodes().front().domain_id;
  std::string pressure=authority.catalog().pressure_ids().front();
  std::string field=authority.expertise_catalog().fields().front().id;
  std::string capability=authority.catalog().capabilities().front().id;
  std::string axis=catalog.culture_axes().front().id;
  std::vector<std::string> sparse_domains;
  for(const auto &node:authority.catalog().nodes())
   if(std::ranges::find(sparse_domains,node.domain_id)==sparse_domains.end())
    sparse_domains.push_back(node.domain_id);
  require(sparse_domains.size()>=4,"Agenda sparse-order fixture needs four domains.");
  std::size_t checked{};
  require(fingerprint(root)==fixture.at("CanonicalFingerprint").get<std::string>(),
          "Canonical research fingerprint differs from actual source fixture.");
  for(const auto &row:fixture.at("Rows")) {
   const auto name=row.at("Name").get<std::string>(); Json result=nullptr; Error error;
   if(row.value("Phase",std::string{})=="Load") {
    OwnedScratch scratch(root);
    apply_changes(scratch.path(),row);
    require(fingerprint(scratch.path())==row.at("BeforeFingerprint").get<std::string>(),name+" retained input fingerprint differed");
    std::optional<AdaptiveResearchAgendaCatalog> malformed;
    try {
     malformed.emplace(load_adaptive_research_agenda_catalog(
         scratch.path(),authority.catalog(),authority.expertise_catalog()));
    } catch(const AdaptiveResearchAgendaCatalogError &caught_error) {
     error={"InvalidDataException",caught_error.what()};
    } catch(const std::invalid_argument &caught_error) {
     error={"ArgumentException",caught_error.what()};
    }
    if(malformed) result=catalog_json(*malformed);
    require(fingerprint(scratch.path())==row.at("AfterFingerprint").get<std::string>(),name+" production call changed input bytes");
    const auto source_error=row.at("Error");
    require(!error.type.empty(),name+" unexpectedly succeeded");
    if(name=="load-missing-agenda"||name=="load-malformed-agenda-json"||
       name=="load-priorities-wrong-kind"||name=="load-culture-axes-null"||
       name=="load-priority-scores-wrong-kind") {
     require(!error.message.empty(),name+" native boundary omitted a diagnostic");
    } else {
     require(error.type==source_error.at("Type").get<std::string>(),name+" semantic category differed");
     require(error.message==source_error.at("Message").get<std::string>(),name+" semantic message differed: "+error.message);
    }
    ++checked;
    continue;
   }
   if(name=="catalog-canonical") result=catalog_json(catalog);
   else if(name=="state-default") result=state_json(runtime.state(state));
   else if(name=="shortlist-first"){first=runtime.build_visible_shortlist(state);result=candidates(first);}
   else if(name=="shortlist-cache-hit"){const auto before=TestAccess::shortlist_rebuild_count(runtime,state);auto value=runtime.build_visible_shortlist(state);require(TestAccess::shortlist_rebuild_count(runtime,state)==before,"cache hit rebuilt");result={{"SameReference",true},{"Candidates",candidates(value)}};}
   else if(name=="priority-default-noop"){runtime.set_domain_priority(state,domain,"routine");result=true;}
   else if(name=="shortlist-after-priority-noop")result=candidates(runtime.build_visible_shortlist(state));
   else if(name=="priority-important"){runtime.set_domain_priority(state,domain,"important");result=true;}
   else if(name=="shortlist-after-agenda-revision")result=candidates(runtime.build_visible_shortlist(state));
   else if(name=="core-revision-pressure"){(void)authority.set_pressure(state,pressure,17.5);result=Json::array();}
   else if(name=="shortlist-after-core-revision")result=candidates(runtime.build_visible_shortlist(state));
   else if(name=="shortlist-after-expertise-revision"){
    auto existing=state.expertise().get_field(field);existing.current.theoretical+=.25;
    ExpertiseWriter::set_field(CoreWriter::expertise(state),std::move(existing));
    result=candidates(runtime.build_visible_shortlist(state));
   }
   else if(name=="culture-noop"){runtime.set_scientific_culture_axis(state,axis,50);result=true;}
   else if(name=="culture-change"){runtime.set_scientific_culture_axis(state,axis,62.5);result=true;}
   else if(name=="orientations-change"){runtime.set_orientations(state,{35,65,75,45});result=true;}
   else if(name=="field-priority"){runtime.set_field_priority(state,field,"strategic");result=true;}
   else if(name=="problem-priority"){runtime.set_problem_priority(state,pressure,"critical");result=true;}
   else if(name=="capability-priority"){runtime.set_capability_priority(state,capability,"deprioritized");result=true;}
   else if(name.starts_with("adequacy-")){
    if(name=="adequacy-critical")runtime.set_scientific_culture_axis(state,"threat_sensitivity",100);
    if(name=="adequacy-deprioritize"){runtime.set_scientific_culture_axis(state,"threat_sensitivity",0);runtime.set_scientific_culture_axis(state,"complacency_tendency",100);runtime.set_scientific_culture_axis(state,"institutional_conservatism",100);}
    ResearchPerceivedAdequacyAssessment input{domain,50,100,.1};
    if(name=="adequacy-critical")input={domain,20,90,1};else if(name=="adequacy-strategic")input={domain,20,65,1};else if(name=="adequacy-important")input={domain,20,40,1};else if(name=="adequacy-deprioritize")input={domain,100,0,1};else if(name=="adequacy-unchanged")input={domain,40,0,1};
    auto value=runtime.evaluate_perceived_adequacy(state,input);if(name=="adequacy-deprioritize")saved_recommendation=value;result=recommendation(value);
   }
   else if(name=="apply-recommendation"){runtime.apply_recommendation(state,*saved_recommendation,2125.5,"fixture");result=true;}
   else if(name=="shortlist-active-excluded"){
    auto list=runtime.build_visible_shortlist(state);auto allowed=std::ranges::find_if(list,[](const auto&v){return v.can_start;});require(allowed!=list.end(),"no allowed candidate");const auto command=authority.start_directed_research(state,allowed->node_id,allowed->requested_effective_labs);require(command.accepted,command.message);result=candidates(runtime.build_visible_shortlist(state));
   }
   else if(name=="shortlist-paused-excluded"){const auto project=state.active_projects().front().node_id;require(authority.pause_directed_research(state,project).accepted,"pause failed");result=candidates(runtime.build_visible_shortlist(state));}
   else if(name=="error-domain")error=caught([&]{runtime.set_domain_priority(state,"unknown","routine");});
   else if(name=="error-field")error=caught([&]{runtime.set_field_priority(state,"unknown","routine");});
   else if(name=="error-problem")error=caught([&]{runtime.set_problem_priority(state,"unknown","routine");});
   else if(name=="error-capability")error=caught([&]{runtime.set_capability_priority(state,"unknown","routine");});
   else if(name=="error-priority")error=caught_key_not_found([&]{runtime.set_domain_priority(state,domain,"unknown");});
   else if(name=="error-priority-fresh-state") {
    auto fresh=authority.compose_reference_profile(
        "fixture:error-state","reference_humanlike_solar_2050",
        "fixture:error-state",2050).state;
    const auto before=TestAccess::state_count(runtime);
    error=caught_key_not_found([&]{runtime.set_domain_priority(fresh,domain,"unknown");});
    result={{"StateCreated",TestAccess::state_count(runtime)==before+1}};
   }
   else if(name=="error-culture-axis")error=caught([&]{runtime.set_scientific_culture_axis(state,"unknown",50);});
   else if(name=="error-culture-negative")error=caught([&]{runtime.set_scientific_culture_axis(state,axis,-1);});
   else if(name=="error-culture-1e6")error=caught([&]{runtime.set_scientific_culture_axis(state,axis,1e6);});
   else if(name=="error-culture-1e16")error=caught([&]{runtime.set_scientific_culture_axis(state,axis,1e16);});
   else if(name=="error-culture-1e17")error=caught([&]{runtime.set_scientific_culture_axis(state,axis,1e17);});
   else if(name=="error-culture-minus-1e4")error=caught([&]{runtime.set_scientific_culture_axis(state,axis,-1e-4);});
   else if(name=="error-culture-minus-1e5")error=caught([&]{runtime.set_scientific_culture_axis(state,axis,-1e-5);});
   else if(name=="error-orientation-nan")error=caught([&]{runtime.set_orientations(state,{std::numeric_limits<double>::quiet_NaN(),50,50,50});});
   else if(name=="error-assessment-confidence")error=caught([&]{(void)runtime.evaluate_perceived_adequacy(state,{domain,50,50,std::numeric_limits<double>::quiet_NaN()});});
   else if(name=="identity-two-states") {
    auto left=authority.compose_reference_profile("fixture:same","reference_humanlike_solar_2050","fixture:left",2050).state;
    auto right=authority.compose_reference_profile("fixture:same","reference_humanlike_solar_2050","fixture:right",2050).state;
    runtime.set_domain_priority(left,domain,"critical");
    const auto default_id=catalog.runtime_policy().default_priority_id;
    result={{"Left",runtime.state(left).get_domain_priority(domain,default_id)},
            {"Right",runtime.state(right).get_domain_priority(domain,default_id)}};
   }
   else if(name=="identity-two-runtimes") {
    AdaptiveResearchAgendaRuntime isolated(authority,catalog);
    isolated.set_domain_priority(state,domain,"critical");
    const auto default_id=catalog.runtime_policy().default_priority_id;
    result={{"Primary",runtime.state(state).get_domain_priority(domain,default_id)},
            {"Isolated",isolated.state(state).get_domain_priority(domain,default_id)}};
   }
   else if(name=="sparse-priority-seed") {
    runtime.set_domain_priority(state,sparse_domains[0],"important");
    runtime.set_domain_priority(state,sparse_domains[1],"strategic");
    runtime.set_domain_priority(state,sparse_domains[2],"critical");
    result=priority_entries(runtime.state(state).domain_priorities());
   }
   else if(name=="sparse-priority-remove-two") {
    runtime.set_domain_priority(state,sparse_domains[0],"routine");
    runtime.set_domain_priority(state,sparse_domains[1],"routine");
    result=priority_entries(runtime.state(state).domain_priorities());
   }
   else if(name=="sparse-priority-reinsert-lifo") {
    runtime.set_domain_priority(state,sparse_domains[3],"important");
    runtime.set_domain_priority(state,sparse_domains[0],"strategic");
    result=priority_entries(runtime.state(state).domain_priorities());
   }
   else throw std::runtime_error("Unhandled row "+name);
   const auto error_it = row.find("Error");
   if(error_it == row.end() || error_it->is_null())
    require(error.type.empty(),name+" unexpectedly failed: "+error.message);
   else {
    require(error.type == error_it->at("Type").get<std::string>(),
            name + " type differed: " + error.type + ": " + error.message);
    require(error.message == error_it->at("Message").get<std::string>(),
            name + " message differed: " + error.message);
   }
   require(result==row.at("Result"),name+" result differed");
   if(name!="catalog-canonical"){
    require(state.revision()==row.at("CoreRevision"),name+" core revision differed");
    require(state.materialized_view_revision()==row.at("MaterializedViewRevision"),name+" view revision differed");
    require(state.expertise().revision()==row.at("ExpertiseRevision"),name+" expertise revision differed");
    require(state_json(runtime.state(state))==row.at("Agenda"),name+" agenda state differed");
   }
   ++checked;
  }
  require(checked==60,"fixture count changed");
  const auto default_id=catalog.runtime_policy().default_priority_id;
  auto identity_source=authority.compose_reference_profile(
      "fixture:identity","reference_humanlike_solar_2050","fixture:identity",2050).state;
  runtime.set_domain_priority(identity_source,domain,"critical");
  const auto *agenda_address=&runtime.state(identity_source);
  auto identity_copy=identity_source;
  require(runtime.state(identity_copy).get_domain_priority(domain,default_id)=="routine",
          "Civilization-state copy reused Agenda identity.");
  auto identity_moved=std::move(identity_source);
  require(&runtime.state(identity_moved)==agenda_address,
          "Civilization-state move did not preserve Agenda storage.");
  require(runtime.state(identity_moved).get_domain_priority(domain,default_id)=="critical",
          "Civilization-state move lost Agenda values.");
  auto alias_state=authority.compose_reference_profile(
      "fixture:alias","reference_humanlike_solar_2050","fixture:alias",2050).state;
  runtime.set_domain_priority(alias_state,domain,"important");
  const std::string_view borrowed_provenance=
      runtime.state(alias_state).domain_priorities().front().priority_id;
  runtime.apply_recommendation(
      alias_state,{domain,"critical",0,0,0,"alias input"},2200,
      borrowed_provenance);
  require(runtime.state(alias_state).policy_provenance()=="important",
          "Recommendation did not own aliased provenance before mutation.");
  const auto state_count_before=TestAccess::state_count(runtime);
  {
   auto temporary=authority.compose_reference_profile(
       "fixture:expired","reference_humanlike_solar_2050","fixture:expired",2050).state;
   (void)runtime.state(temporary);
  }
  TestAccess::maintain(runtime,1000);
  require(TestAccess::state_count(runtime)<state_count_before+1,
          "Expired Agenda weak state was retained.");
  AdaptiveResearchAgendaRuntime moved_runtime(authority,catalog);
  moved_runtime=std::move(runtime);
  require(&moved_runtime.state(identity_moved)==agenda_address,
          "Runtime move assignment changed Agenda storage address.");
  (void)moved_runtime.build_visible_shortlist(identity_moved);
  std::cout<<"Adaptive Research agenda parity passed "<<checked<<" rows.\n";return 0;
 }catch(const std::exception &error){std::cerr<<typeid(error).name()<<": "<<error.what()<<'\n';std::cerr<<"cwd="<<std::filesystem::current_path().string()<<'\n';if(argc>1)std::cerr<<"fixture="<<std::filesystem::absolute(argv[1]).string()<<'\n';if(argc>2)std::cerr<<"research-root="<<std::filesystem::absolute(argv[2]).string()<<'\n';return 1;}
}
