#include <stellar/core/adaptive_research_applicability_catalog.hpp>
#include <windows.h>
#include <bcrypt.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <vector>
using Json = nlohmann::ordered_json;
using namespace stellar::core;
namespace {
[[noreturn]] void fail(const std::string &m) { throw std::runtime_error(m); }
void require(bool ok, const std::string &m) { if (!ok) fail(m); }
std::string fingerprint(const std::filesystem::path &dir) {
  struct Algorithm {
    BCRYPT_ALG_HANDLE value{};
    ~Algorithm() { if (value) BCryptCloseAlgorithmProvider(value, 0); }
  } algorithm;
  struct Hash {
    BCRYPT_HASH_HANDLE value{};
    ~Hash() { if (value) BCryptDestroyHash(value); }
  } hash;
  if (BCryptOpenAlgorithmProvider(&algorithm.value, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
    fail("SHA-256 provider");
  DWORD size{}, written{};
  if (BCryptGetProperty(algorithm.value, BCRYPT_OBJECT_LENGTH,
                        reinterpret_cast<PUCHAR>(&size), sizeof(size), &written, 0) != 0 ||
      written != sizeof(size) || size == 0)
    fail("SHA-256 object length");
  std::vector<unsigned char> object(size);
  std::array<unsigned char, 32> result{};
  if (BCryptCreateHash(algorithm.value, &hash.value, object.data(), size, nullptr, 0, 0) != 0)
    fail("SHA-256 hash");
  const auto append = [&](const char *data, std::size_t count) {
    require(count <= static_cast<std::size_t>((std::numeric_limits<ULONG>::max)()),
            "SHA-256 input too large");
    if (BCryptHashData(hash.value, reinterpret_cast<PUCHAR>(const_cast<char *>(data)),
                       static_cast<ULONG>(count), 0) != 0)
      fail("SHA-256 append");
  };
  std::vector<std::filesystem::path> files; for (const auto &e : std::filesystem::directory_iterator(dir)) if(e.is_regular_file()) files.push_back(e.path());
  std::ranges::sort(files, {}, [](const auto &p) { return p.filename().string(); });
  for (const auto &p : files) {
    const auto n=p.filename().string(); append(n.data(), n.size());
    std::ifstream s(p,std::ios::binary); require(static_cast<bool>(s), "fingerprint file read");
    std::array<char,4096> b{};
    while (s.read(b.data(), static_cast<std::streamsize>(b.size())) || s.gcount() != 0)
      append(b.data(), static_cast<std::size_t>(s.gcount()));
    require(s.eof() && !s.bad(), "fingerprint file read");
  }
  if (BCryptFinishHash(hash.value,result.data(),static_cast<ULONG>(result.size()),0) != 0)
    fail("SHA-256 finish");
  constexpr char d[]="0123456789ABCDEF"; std::string text; for(auto x:result){text+=d[x>>4U];text+=d[x&15U];} return text;
}
std::vector<unsigned char> decode64(const std::string &text) {
  const auto value=[](unsigned char c)->int{if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;if(c>='0'&&c<='9')return c-'0'+52;if(c=='+')return 62;if(c=='/')return 63;return -1;};
  require(text.size()%4==0,"fixture base64 length");std::vector<unsigned char> out;out.reserve(text.size()/4*3);
  for(std::size_t i=0;i<text.size();i+=4){const bool last=i+4==text.size();const bool pad2=text[i+2]=='=';const bool pad3=text[i+3]=='=';require(text[i]!='='&&text[i+1]!='='&&(!pad2||pad3)&&(!pad2&&!pad3||last),"fixture base64 padding");const auto a=value(static_cast<unsigned char>(text[i])),b=value(static_cast<unsigned char>(text[i+1]));const auto c=pad2?0:value(static_cast<unsigned char>(text[i+2]));const auto d=pad3?0:value(static_cast<unsigned char>(text[i+3]));require(a>=0&&b>=0&&c>=0&&d>=0,"fixture base64 alphabet");require(!pad2||(b&15)==0,"fixture base64 trailing bits");require(pad2||!pad3||(c&3)==0,"fixture base64 trailing bits");const auto bits=(static_cast<std::uint32_t>(a)<<18U)|(static_cast<std::uint32_t>(b)<<12U)|(static_cast<std::uint32_t>(c)<<6U)|static_cast<std::uint32_t>(d);out.push_back(static_cast<unsigned char>((bits>>16U)&255U));if(!pad2)out.push_back(static_cast<unsigned char>((bits>>8U)&255U));if(!pad3)out.push_back(static_cast<unsigned char>(bits&255U));}return out;
}
class Scratch {
public:
 Scratch(){parent_=std::filesystem::weakly_canonical(std::filesystem::absolute(std::filesystem::temp_directory_path()));for(unsigned n=0;n<100;++n){const auto candidate=std::filesystem::absolute(parent_/("stellar-applicability-045-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64())+"-"+std::to_string(n))).lexically_normal();require(candidate.parent_path()==parent_&&candidate.filename().string().starts_with("stellar-applicability-045-"),"scratch root path");if(std::filesystem::create_directory(candidate)){path_=candidate;owned_=true;return;}}fail("unique scratch");}
 ~Scratch() noexcept {try{if(owned_&&valid_root(path_))std::filesystem::remove_all(path_);}catch(...){}} const std::filesystem::path& path()const noexcept{return path_;}
 void remove_case(const std::filesystem::path &value){const auto normalized=std::filesystem::absolute(value).lexically_normal();require(owned_&&valid_root(path_)&&normalized.parent_path()==path_&&normalized.filename().string().starts_with("case-"),"scratch case path");std::filesystem::remove_all(normalized);}
private: bool valid_root(const std::filesystem::path &value)const{return value.is_absolute()&&value.lexically_normal()==value&&value.parent_path()==parent_&&value.filename().string().starts_with("stellar-applicability-045-");}std::filesystem::path parent_,path_;bool owned_{};
};
void copy_root(const std::filesystem::path &from,const std::filesystem::path &to){require(std::filesystem::create_directory(to),"exclusive scratch case");for(const auto&e:std::filesystem::directory_iterator(from))if(e.is_regular_file())std::filesystem::copy_file(e.path(),to/e.path().filename());}
Json trait(const ResearchApplicabilityTraitDefinition &v){return{{"Id",v.id},{"Scope",static_cast<int>(v.scope)},{"Mutable",v.is_mutable}};}
Json catalog_json(const AdaptiveResearchApplicabilityCatalog &c){Json a=Json::array();for(const auto&v:c.traits())a.push_back(trait(v));return{{"Traits",std::move(a)}};}
void equal(const Json&a,const Json&b,const std::string&where){if(a.is_array()&&b.is_array()){require(a.size()==b.size(),where+" count");for(std::size_t i=0;i<a.size();++i)equal(a[i],b[i],where+"["+std::to_string(i)+"]");return;}if(a.is_object()&&b.is_object()){require(a.size()==b.size(),where+" keys");for(const auto&[k,v]:b.items()){require(a.contains(k),where+" missing "+k);equal(a.at(k),v,where+"."+k);}return;}require(a==b,where+" mismatch");}
enum class ErrorCategory { none, semantic, lookup, parser, file, missing_property, wrong_property_type };
ErrorCategory expected_category(const Json&r){if(r.at("Error").is_null())return ErrorCategory::none;const auto type=r.at("Error").at("Type").get<std::string>();if(type=="InvalidDataException")return ErrorCategory::semantic;if(type=="KeyNotFoundException"&&r.at("Kind")=="GetTrait")return ErrorCategory::lookup;if(type=="JsonReaderException")return ErrorCategory::parser;if(type=="FileNotFoundException")return ErrorCategory::file;if(type=="KeyNotFoundException")return ErrorCategory::missing_property;if(type=="InvalidOperationException")return ErrorCategory::wrong_property_type;fail("unmapped source exception "+type);}
}
int main(int argc,char**argv){try{
 if(argc!=3)fail("usage: applicability_tests <research-root> <oracle-fixture>");const auto root=std::filesystem::absolute(argv[1]).lexically_normal();const auto fixture_path=std::filesystem::absolute(argv[2]).lexically_normal();std::ifstream in(fixture_path);require(static_cast<bool>(in),"fixture read");Json fixture;in>>fixture;require(fixture.at("Schema")=="stellar-adaptive-research-applicability-oracle-v1","fixture schema");const auto& rows=fixture.at("Cases");require(rows.is_array()&&rows.size()==26,"fixture row count");require(rows.at(0).at("Result").at("Traits").size()==14,"canonical trait count");
 static_assert(!std::is_copy_constructible_v<AdaptiveResearchApplicabilityCatalog>);static_assert(std::is_nothrow_move_constructible_v<AdaptiveResearchApplicabilityCatalog>);static_assert(static_cast<int>(ResearchApplicabilityTraitScope::civilization)==0);static_assert(static_cast<int>(ResearchApplicabilityTraitScope::population_or_species)==1);
 const auto base=load_adaptive_research_catalog(root);Scratch scratch;unsigned success=0,semantic=0,lookup=0,parser=0,file=0,missing_property=0,wrong_property_type=0;
 for(std::size_t i=0;i<rows.size();++i){const auto&r=rows[i];const auto case_dir=scratch.path()/("case-"+std::to_string(i));copy_root(root,case_dir);const auto traits=case_dir/"applicability_traits.json";if(r.at("TraitFileUtf8Base64").is_null())std::filesystem::remove(traits);else{const auto b=decode64(r.at("TraitFileUtf8Base64").get<std::string>());std::ofstream out(traits,std::ios::binary|std::ios::trunc);require(static_cast<bool>(out),"trait fixture open");out.write(reinterpret_cast<const char*>(b.data()),static_cast<std::streamsize>(b.size()));out.close();require(static_cast<bool>(out),"trait fixture write");}
 require(fingerprint(case_dir)==r.at("InputFingerprintBefore").get<std::string>(),"before fingerprint row "+std::to_string(i));const auto expected=expected_category(r);const auto expected_message=r.at("Error").is_null()?std::string{}:r.at("Error").at("Message").get<std::string>();const auto kind=r.at("Kind").get<std::string>();require(kind=="Load"||kind=="GetTrait","fixture operation kind");ErrorCategory observed=ErrorCategory::none;std::string observed_message;std::optional<AdaptiveResearchApplicabilityCatalog> loaded;const ResearchApplicabilityTraitDefinition* found=nullptr;
 if(kind=="GetTrait"){const auto catalog=load_adaptive_research_applicability_catalog(case_dir,base);const auto trait_id=r.at("Arguments").at("TraitId").get<std::string>();try{found=&catalog.get_trait(trait_id);}catch(const std::out_of_range&e){observed=ErrorCategory::lookup;observed_message=e.what();}Json actual=found?trait(*found):Json(nullptr);require(observed==expected,"native/source exception category row "+std::to_string(i));if(observed==ErrorCategory::lookup)require(observed_message==expected_message,"lookup message row "+std::to_string(i));if(observed==ErrorCategory::none){equal(actual,r.at("Result"),"result row "+std::to_string(i));++success;}else ++lookup;
 }else{try{loaded.emplace(load_adaptive_research_applicability_catalog(case_dir,base));}catch(const AdaptiveResearchApplicabilityCatalogError&e){observed=ErrorCategory::semantic;observed_message=e.what();}catch(const nlohmann::json::parse_error&e){observed=ErrorCategory::parser;observed_message=e.what();}catch(const nlohmann::json::out_of_range&e){observed=ErrorCategory::missing_property;observed_message=e.what();}catch(const nlohmann::json::type_error&e){observed=ErrorCategory::wrong_property_type;observed_message=e.what();}catch(const std::ios_base::failure&e){observed=ErrorCategory::file;observed_message=e.what();}require(observed==expected,"native/source exception category row "+std::to_string(i));if(observed==ErrorCategory::semantic)require(observed_message==expected_message,"semantic message row "+std::to_string(i));Json actual=loaded?catalog_json(*loaded):Json(nullptr);if(observed==ErrorCategory::none){equal(actual,r.at("Result"),"result row "+std::to_string(i));++success;}else if(observed==ErrorCategory::semantic)++semantic;else if(observed==ErrorCategory::parser)++parser;else if(observed==ErrorCategory::file)++file;else if(observed==ErrorCategory::missing_property)++missing_property;else if(observed==ErrorCategory::wrong_property_type)++wrong_property_type;else fail("invalid load error category");}
 require(fingerprint(case_dir)==r.at("InputFingerprintAfter").get<std::string>(),"after fingerprint row "+std::to_string(i));scratch.remove_case(case_dir);}
 std::cout<<"adaptive applicability fixture consumer: rows="<<rows.size()<<" success="<<success<<" semantic="<<semantic<<" lookup="<<lookup<<" parser="<<parser<<" file="<<file<<" missing-property="<<missing_property<<" wrong-property-type="<<wrong_property_type<<"\n";return EXIT_SUCCESS;
 }catch(const std::exception&e){std::cerr<<"applicability fixture consumer failed: type="<<typeid(e).name()<<" message="<<e.what()<<" cwd="<<std::filesystem::current_path().string();if(argc>=2)std::cerr<<" research-root="<<argv[1];if(argc>=3)std::cerr<<" fixture="<<argv[2];std::cerr<<"\n";return EXIT_FAILURE;}}
