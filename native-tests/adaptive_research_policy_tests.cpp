#include <stellar/core/adaptive_research_facilities.hpp>
#include <stellar/core/adaptive_research_progress_policy.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <string_view>
#include <tuple>

using Json = nlohmann::ordered_json;
using namespace stellar::core;

static Json read(const std::filesystem::path &path) { std::ifstream in(path); if(!in) throw std::runtime_error("fixture input missing: "+path.string()); return Json::parse(in); }
static bool close(double a,double b) { return std::isnan(a)?std::isnan(b):std::abs(a-b)<=1e-12; }
static void require(bool value,const std::string &message) { if(!value) throw std::runtime_error(message); }
static double named_number(const Json &value) { if(value.is_number()) return value.get<double>(); const auto text=value.get<std::string>(); if(text=="NaN") return std::numeric_limits<double>::quiet_NaN(); if(text=="Infinity") return std::numeric_limits<double>::infinity(); if(text=="-Infinity") return -std::numeric_limits<double>::infinity(); throw std::runtime_error("unexpected named floating-point literal"); }
static void copy_files(const std::filesystem::path &from,const std::filesystem::path &to) { for(const auto &entry:std::filesystem::directory_iterator(from)) if(entry.is_regular_file()) std::filesystem::copy_file(entry.path(),to/entry.path().filename()); }
static void replace_all(const std::filesystem::path &path,const std::string &from,const std::string &to) { std::ifstream input(path); require(bool(input),"generated-copy mutation input missing"); std::string value((std::istreambuf_iterator<char>(input)),{}); require(value.find(from)!=std::string::npos,"generated-copy mutation target missing: "+from); for(size_t at=0;(at=value.find(from,at))!=std::string::npos;at+=to.size()) value.replace(at,from.size(),to); std::ofstream output(path); output<<value; require(bool(output),"generated-copy mutation output failed"); }
static void clear_files(const std::filesystem::path &root) { for(const auto &entry:std::filesystem::directory_iterator(root)) { require(entry.is_regular_file(),"owned scratch unexpectedly contains a directory"); std::filesystem::remove(entry.path()); } }
static std::string input_fingerprint(const std::filesystem::path &root) {
  std::vector<std::filesystem::path> files; for(const auto &entry:std::filesystem::directory_iterator(root)) if(entry.is_regular_file()) files.push_back(entry.path());
  std::ranges::sort(files,{},[](const auto &path){return path.filename().string();});
  std::uint64_t hash=14695981039346656037ULL; const auto add=[&](unsigned char value){hash^=value;hash*=1099511628211ULL;};
  for(const auto &path:files) { const auto name=path.filename().string(); for(const auto value:name) add(static_cast<unsigned char>(value)); add(0); std::ifstream input(path,std::ios::binary); require(bool(input),"fingerprint input missing: "+path.string()); for(char value;input.get(value);) add(static_cast<unsigned char>(value)); add(255); }
  std::ostringstream text; text<<std::uppercase<<std::hex<<std::setw(16)<<std::setfill('0')<<hash; return text.str();
}
class OwnedScratch final {
public:
  OwnedScratch() { base_=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()); for(unsigned attempt=0;attempt!=100;++attempt) { path_=base_/("stellar-policy-parity-043-"+std::to_string(std::random_device{}())+"-"+std::to_string(attempt)); std::error_code error; if(std::filesystem::create_directory(path_,error)) { owned_=std::filesystem::canonical(path_); return; } } throw std::runtime_error("could not create owned parity scratch directory"); }
  ~OwnedScratch() { std::error_code error; const auto resolved=std::filesystem::weakly_canonical(path_,error); if(!error && resolved==owned_ && owned_.parent_path()==base_) std::filesystem::remove_all(owned_,error); }
  const std::filesystem::path &path() const noexcept{return path_;}
private: std::filesystem::path base_; std::filesystem::path path_; std::filesystem::path owned_;
};

static const Json &record(const Json &records,std::string_view name) { for(const auto &item:records) if(item.at("Name").get<std::string>()==name) return item; throw std::runtime_error("source fixture record missing: "+std::string(name)); }
static void require_source_error(const Json &item,std::string_view type) { require(item.at("Result").is_null(),"source row unexpectedly succeeded: "+item.at("Name").get<std::string>()); require(item.at("Error").at("Type").get<std::string>()==type,"source exception category differs for "+item.at("Name").get<std::string>()); }
static void require_input_fingerprint(const Json &item,const std::filesystem::path &root,std::string_view phase) { const auto field=phase=="before"?"InputFingerprintBefore":"InputFingerprintAfter"; require(input_fingerprint(root)==item.at(field).get<std::string>(),"input byte fingerprint differs "+std::string(phase)+" "+item.at("Name").get<std::string>()); }

static void compare_facilities(const Json &expected,const AdaptiveResearchFacilityCatalog &actual) {
  const auto &caps=expected.at("Capabilities"); require(caps.size()==actual.facility_capability_ids().size(),"facility capability count differs");
  for(size_t i=0;i<caps.size();++i) require(caps.at(i).get<std::string>()==actual.facility_capability_ids()[i],"facility capability order differs");
  const auto &institutions=expected.at("Institutions"); require(institutions.size()==actual.institutions().size(),"institution count differs");
  for(size_t i=0;i<institutions.size();++i) { const auto &e=institutions.at(i); const auto &a=actual.institutions()[i]; require(e.at("Key").get<std::string>()==a.id&&e.at("Id").get<std::string>()==a.id&&close(e.at("EffectiveLabUnits").get<double>(),a.effective_lab_units),"institution definition differs"); require(e.at("Capabilities").get<std::vector<std::string>>()==a.facility_capabilities,"institution capability order differs"); }
  for(const auto &query:expected.at("InstitutionQueries")) { const auto known=query.at("Known").get<std::string>(); require((actual.find_institution(known)!=nullptr)==query.at("KnownFound").get<bool>(),"known institution lookup differs"); require((actual.find_institution("unknown_institution")!=nullptr)==query.at("UnknownFound").get<bool>(),"unknown institution lookup differs"); }
  for(const auto &query:expected.at("StageQueries")) { const auto node=query.at("Node").get<std::string>(); const auto stage=static_cast<ResearchMaturity>(query.at("Stage").get<int>()); require((actual.get_stage_requirement(node,stage)!=nullptr)==query.at("Present").get<bool>(),"stage requirement presence differs for "+node+":"+std::to_string(static_cast<int>(stage))); }
  for(const auto &e:expected.at("Requirements")) { const auto *a=actual.get_stage_requirement(e.at("Node").get<std::string>(),static_cast<ResearchMaturity>(e.at("Stage").get<int>())); require(a!=nullptr,"missing facility requirement"); require(e.at("AllOf").get<std::vector<std::string>>()==a->all_of&&e.at("AnyOf").get<std::vector<std::string>>()==a->any_of,"facility requirement differs"); }
}

static void compare_policy(const Json &expected,const AdaptiveResearchProgressPolicy &actual,const AdaptiveResearchCatalog &catalog) {
  const auto &stages=expected.at("Stages"); require(stages.size()==actual.stage_bands().size(),"stage count differs"); for(size_t i=0;i<stages.size();++i){const auto&e=stages.at(i);const auto&a=actual.stage_bands()[i];require(e.at("Stage").get<int>()==static_cast<int>(a.stage)&&close(e.at("StartFraction").get<double>(),a.start_fraction)&&close(e.at("EndFraction").get<double>(),a.end_fraction)&&close(e.at("WorkFraction").get<double>(),a.work_fraction()),"stage band differs");}
  const auto &readiness=expected.at("Readiness"); require(readiness.size()==actual.readiness_bands().size(),"readiness count differs"); for(size_t i=0;i<readiness.size();++i){const auto&e=readiness.at(i);const auto&a=actual.readiness_bands()[i];require(close(e.at("MinimumScore").get<double>(),a.minimum_score)&&close(e.at("MaximumScore").get<double>(),a.maximum_score)&&close(e.at("Efficiency").get<double>(),a.efficiency),"readiness band differs");}
  for(const auto &e:expected.at("StageWork")){const auto &node=catalog.get_node(e.at("Id").get<std::string>());require(close(e.at("Experimental").get<double>(),actual.get_stage_work(node,ResearchMaturity::experimental))&&close(e.at("Demonstrated").get<double>(),actual.get_stage_work(node,ResearchMaturity::demonstrated))&&close(e.at("Engineering").get<double>(),actual.get_stage_work(node,ResearchMaturity::engineering)),"stage work differs");}
  for(const auto &e:expected.at("Efficiency")) require(close(e.at("Result").get<double>(),actual.get_readiness_efficiency(named_number(e.at("Input")))),"readiness threshold differs");
}

int main(int argc,char **argv) {
  try {
    require(argc==3,"usage: policy_parity <research-dir> <source-fixture>");
    const auto source=read(argv[2]); require(source.at("Schema")=="stellar-adaptive-research-policy-oracle-v2","unexpected source fixture schema"); const auto &records=source.at("Records"); require(records.size()==28,"source fixture row count differs"); require(source.at("Metadata").at("ProductionRecords").get<size_t>()==records.size(),"source fixture metadata count differs");
    const auto catalog=load_adaptive_research_catalog(argv[1]);
    const auto &canonical_facility_record=record(records,"canonical-facilities"); require_input_fingerprint(canonical_facility_record,argv[1],"before"); const auto facilities=load_adaptive_research_facility_catalog(argv[1],catalog); require_input_fingerprint(canonical_facility_record,argv[1],"after"); compare_facilities(canonical_facility_record.at("Result"),facilities);
    const auto &canonical_policy_record=record(records,"canonical-progress"); require_input_fingerprint(canonical_policy_record,argv[1],"before"); const auto policy=load_adaptive_research_progress_policy(argv[1],catalog); require_input_fingerprint(canonical_policy_record,argv[1],"after"); compare_policy(canonical_policy_record.at("Result"),policy,catalog);
    OwnedScratch scratch; const auto &temporary=scratch.path(); auto reset=[&](){clear_files(temporary);copy_files(argv[1],temporary);}; auto mutate=[&](const char *file,const char *from,const char *to){replace_all(temporary/file,from,to);};
    auto facility_error=[&](std::string_view name,auto &&invoke){const auto &expected=record(records,name);require_source_error(expected,"InvalidDataException");require_input_fingerprint(expected,temporary,"before");std::string message;try{invoke();}catch(const AdaptiveResearchFacilityCatalogError &error){message=error.what();}require_input_fingerprint(expected,temporary,"after");require(!message.empty(),"expected mapped facility validation error was not thrown: "+std::string(name));require(message==expected.at("Error").at("Message").get<std::string>(),"facility validation message differs for "+std::string(name));};
    auto policy_error=[&](std::string_view name,auto &&invoke,std::string_view source_type="InvalidDataException"){const auto &expected=record(records,name);require_source_error(expected,source_type);require_input_fingerprint(expected,temporary,"before");std::string message;try{invoke();}catch(const AdaptiveResearchProgressPolicyError &error){message=error.what();}require_input_fingerprint(expected,temporary,"after");require(!message.empty(),"expected mapped progress validation error was not thrown: "+std::string(name));require(message==expected.at("Error").at("Message").get<std::string>(),"progress validation message differs for "+std::string(name));};
    auto load_facilities=[&](){auto loaded=load_adaptive_research_facility_catalog(temporary,catalog);(void)loaded;}; auto load_policy=[&](){auto loaded=load_adaptive_research_progress_policy(temporary,catalog);(void)loaded;};

    reset(); mutate("project_readiness_model.json","\"min\":0","\"min\":1"); policy_error("progress-first-band-not-zero",load_policy);
    reset(); mutate("biochemical_research_facilities.json","{\"id\":\"alternative_biochemistry_experimentation\"","{\"id\":\"general_experimentation\""); facility_error("facility-duplicate-declared-capability",load_facilities);
    reset(); mutate("research_facility_model.json","[\"general_experimentation\"]","[\"not_declared\"]"); facility_error("facility-unknown-institution-capability",load_facilities);
    reset(); mutate("research_facility_model.json","\"effective_lab_units\":1,","\"effective_lab_units\":0,"); facility_error("facility-nonpositive-labs",load_facilities);
    reset(); mutate("research_facility_model.json","\"prototype_warp_drive\"","\"unknown_policy_node\""); facility_error("facility-unknown-node",load_facilities);
    reset(); mutate("research_facility_model.json","\"experimental\":{\"all_of\":[\"high_energy_experimentation\"","\"mature\":{\"all_of\":[\"high_energy_experimentation\""); facility_error("facility-unsupported-stage",load_facilities);
    reset(); mutate("research_facility_model.json","\"large_scale_prototyping\"]}","\"not_declared\"]}"); facility_error("facility-unknown-required-capability",load_facilities);
    reset(); mutate("biochemical_research_facilities.json","\"facility_capabilities\": [","\"facility_capabilities\": [{\"id\":\"orphan_capability\"},"); mutate("research_facility_model.json","\"large_scale_prototyping\"]}","\"orphan_capability\"]}"); facility_error("facility-unprovided-required-capability",load_facilities);
    for(const auto &[name,file,from,to]:std::initializer_list<std::tuple<const char*,const char*,const char*,const char*>>{{"progress-stage-gap","maturation_model.json","\"typical_rp_fraction_start\": 0.45","\"typical_rp_fraction_start\": 0.46"},{"progress-stage-overlap","maturation_model.json","\"typical_rp_fraction_start\": 0.45","\"typical_rp_fraction_start\": 0.44"},{"progress-stage-end-overflow","maturation_model.json","\"typical_rp_fraction_end\": 1.0","\"typical_rp_fraction_end\": 1.1"},{"progress-readiness-duplicate-threshold","project_readiness_model.json","\"min\":20","\"min\":0"},{"progress-readiness-insufficient-coverage","project_readiness_model.json","\"max\":100","\"max\":99"},{"progress-readiness-transition-gap","project_readiness_model.json","\"min\":20","\"min\":21"},{"progress-readiness-bad-efficiency","project_readiness_model.json","\"efficiency\":0.35","\"efficiency\":0"}}){reset();mutate(file,from,to);policy_error(name,load_policy);}
    reset(); for(const auto *file:{"research_facility_model.json","biochemical_research_facilities.json"}) mutate(file,"\"general_experimentation\"","\"\""); {const auto &expected=record(records,"facility-empty-capability-id");require_input_fingerprint(expected,temporary,"before");auto loaded=load_adaptive_research_facility_catalog(temporary,catalog);require_input_fingerprint(expected,temporary,"after");compare_facilities(expected.at("Result"),loaded);}
    reset(); mutate("research_facility_model.json","\"id\":\"general_research_laboratory\"","\"id\":\"\""); {const auto &expected=record(records,"facility-empty-institution-id");require_input_fingerprint(expected,temporary,"before");auto loaded=load_adaptive_research_facility_catalog(temporary,catalog);require_input_fingerprint(expected,temporary,"after");compare_facilities(expected.at("Result"),loaded);}
    reset(); mutate("biochemical_research_facilities.json","\"id\":\"alternative_biochemistry_institute\"","\"id\":\"general_research_laboratory\""); facility_error("facility-duplicate-institution",load_facilities);
    reset(); mutate("biochemical_research_facilities.json","\"stage_requirements\": {","\"stage_requirements\": {\n    \"prototype_warp_drive\":{\"experimental\":{\"all_of\":[\"high_energy_experimentation\",\"field_physics_experimentation\",\"precision_measurement\"],\"any_of\":[]}},"); facility_error("facility-duplicate-stage-requirement",load_facilities);
    reset(); mutate("research_facility_model.json","\"effective_lab_units\":1,","\"effective_lab_units\":-0.125,"); facility_error("facility-negative-fractional-labs",load_facilities);
    reset(); mutate("research_facility_model.json","\"effective_lab_units\":1,","\"effective_lab_units\":-1e-7,"); facility_error("facility-negative-scientific-labs",load_facilities);
    for(const auto maturity:{ResearchMaturity::mature,ResearchMaturity::archived}) { const auto name=maturity==ResearchMaturity::mature?"progress-get-stage-band-mature":"progress-get-stage-band-archived"; const auto &expected=record(records,name);require_source_error(expected,"InvalidOperationException");require_input_fingerprint(expected,argv[1],"before");std::string message;try{(void)policy.get_stage_band(maturity);}catch(const AdaptiveResearchProgressPolicyError &error){message=error.what();}require_input_fingerprint(expected,argv[1],"after");require(!message.empty(),"expected mapped stage-band error was not thrown");require(message==expected.at("Error").at("Message").get<std::string>(),"stage-band validation message differs"); }

    reset(); mutate("research_facility_index.json","research_facility_model.json","missing_facility.json"); {const auto &expected=record(records,"facility-missing-listed-file");require_source_error(expected,"FileNotFoundException");require_input_fingerprint(expected,temporary,"before");std::string diagnostic;try{load_facilities();}catch(const AdaptiveResearchFacilityCatalogError &error){diagnostic=error.what();}require_input_fingerprint(expected,temporary,"after");const auto path=(temporary/"missing_facility.json").string();require(diagnostic=="Unable to read Adaptive Research file: "+path,"native missing-file boundary diagnostic differs");}
    reset(); {std::ofstream append(temporary/"research_facility_model.json",std::ios::app);append<<"{";} {const auto &expected=record(records,"facility-json-syntax-boundary");require_source_error(expected,"JsonReaderException");require_input_fingerprint(expected,temporary,"before");std::string diagnostic;try{load_facilities();}catch(const AdaptiveResearchFacilityCatalogError &error){diagnostic=error.what();}require_input_fingerprint(expected,temporary,"after");require(diagnostic=="Malformed Adaptive Research JSON in "+(temporary/"research_facility_model.json").string(),"native JSON syntax boundary diagnostic differs");}
    reset(); mutate("research_facility_model.json","\"effective_lab_units\":1,","\"effective_lab_units\":\"bad\","); {const auto &expected=record(records,"facility-type-boundary");require_source_error(expected,"InvalidOperationException");require_input_fingerprint(expected,temporary,"before");std::string diagnostic;try{load_facilities();}catch(const AdaptiveResearchFacilityCatalogError &error){diagnostic=error.what();}require_input_fingerprint(expected,temporary,"after");require(diagnostic=="research_facility_model.json.effective_lab_units must be numeric.","native JSON type boundary diagnostic differs");}

    std::cout<<"policy parity passed: "<<records.size()<<" source rows, "<<record(records,"canonical-facilities").at("Result").at("StageQueries").size()<<" facility queries, "<<record(records,"canonical-progress").at("Result").at("StageWork").size()<<" nodes\n";
  } catch(const std::exception &error) { std::cerr<<error.what()<<'\n';return 1; }
}
