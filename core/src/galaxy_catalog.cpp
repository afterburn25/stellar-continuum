#include <stellar/core/galaxy_catalog.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>
#include <regex>
#include <set>
#include <stdexcept>
namespace stellar::core {
namespace {
std::string lower(std::string text) {
    for(auto& c:text) if(c>='A' && c<='Z') c=static_cast<char>(c-'A'+'a');
    return text;
}
bool blank(const std::string& text) {
    return text.empty() || std::all_of(text.begin(),text.end(),[](unsigned char c){return std::isspace(c)!=0;});
}
int id(const nlohmann::json& value) {
    if(!value.is_number_integer() || value<0 || value>std::numeric_limits<int>::max())
        throw std::runtime_error("Invalid catalog integer identity");
    return value.get<int>();
}
}
std::optional<StellarClass> classify_spectral_type(const std::string& source) {
    auto type=source;
    const auto first=type.find_first_not_of(" \t\r\n");
    if(first==std::string::npos) return {};
    type=type.substr(first,type.find_last_not_of(" \t\r\n")-first+1);
    if(type.front()=='D') return StellarClass::WhiteDwarf;
    static const std::regex giants("I{1,3}(?!V)");
    if(std::regex_search(type,giants)) return StellarClass::Giant;
    const auto found=type.find_first_of("OBAFGKMLTY");
    if(found==std::string::npos) return {};
    switch(type[found]) {
        case 'O': case 'B': return StellarClass::HotBlueStar;
        case 'A': return StellarClass::AWhiteStar;
        case 'F': return StellarClass::FYellowWhiteDwarf;
        case 'G': return StellarClass::GYellowDwarf;
        case 'K': return StellarClass::KOrangeDwarf;
        default: return StellarClass::MRedDwarf;
    }
}
std::vector<CatalogStar> load_nearby_catalog(const std::filesystem::path& path) {
    try {
        std::ifstream stream(path);
        if(!stream) throw std::runtime_error("file is missing or unreadable");
        const auto doc=nlohmann::json::parse(stream);
        if(doc.at("catalogVersion")!="hyg-nearby-500-v1" || !doc.at("systems").is_array() || doc.at("systems").size()!=500)
            throw std::runtime_error("invalid catalog version or system count");
        std::vector<CatalogStar> stars; std::set<int> ids; std::set<std::string> names;
        for(const auto& j:doc.at("systems")) {
            CatalogStar star;
            star.hyg_id=id(j.at("hygId")); star.name=j.at("name"); star.name_kind=j.at("nameKind");
            star.spectral_type=j.at("spectralType"); star.distance_parsecs=j.at("distanceParsecs");
            star.x=j.at("xLightYears"); star.y=j.at("yLightYears"); star.z=j.at("zLightYears");
            if(!j.at("components").is_array()) throw std::runtime_error("components must be an array");
            for(const auto& c:j.at("components")) star.components.push_back({id(c.at("hygId")),c.at("name"),c.at("spectralType")});
            if(blank(star.name) || !std::isfinite(star.x) || !std::isfinite(star.y) || !std::isfinite(star.z) ||
               !std::isfinite(star.distance_parsecs) || star.distance_parsecs<0 || star.components.empty() ||
               !ids.insert(star.hyg_id).second || !names.insert(lower(star.name)).second)
                throw std::runtime_error("invalid or duplicate HYG record: "+std::to_string(star.hyg_id));
            stars.push_back(std::move(star));
        }
        if(stars.front().hyg_id!=0) throw std::runtime_error("first catalog record must be Sol");
        return stars;
    } catch(const std::exception& error) {
        throw std::runtime_error("Cannot load stellar catalog '"+path.string()+"': "+error.what());
    }
}
std::vector<CatalogStar> nearest_classified(std::span<const CatalogStar> catalog,int count) {
    if(count<1 || count>500) throw std::invalid_argument("Classified star count must be between 1 and 500");
    std::vector<CatalogStar> stars;
    for(const auto& star:catalog) if(classify_spectral_type(star.spectral_type)) stars.push_back(star);
    std::stable_sort(stars.begin(),stars.end(),[](const auto& a,const auto& b) {
        return a.distance_parsecs==b.distance_parsecs?a.hyg_id<b.hyg_id:a.distance_parsecs<b.distance_parsecs;
    });
    if(stars.size()<static_cast<std::size_t>(count)) throw std::runtime_error("Not enough classified catalog systems");
    stars.resize(static_cast<std::size_t>(count)); return stars;
}
void apply_catalog_star(StellarSystem& system,const CatalogStar& star) {
    system.name=star.name;
    system.position={star.hyg_id==0?0:static_cast<float>(star.x),star.hyg_id==0?0:static_cast<float>(star.y),star.hyg_id==0?0:star.z};
    system.stellar_catalog_id="hyg-v41:"+std::to_string(star.hyg_id);
    system.primary=classify_spectral_type(star.spectral_type); system.secondary.reset(); system.tertiary.reset();
    if(star.components.size()>1 && system.primary) system.secondary=classify_spectral_type(star.components[1].spectral_type);
    if(star.components.size()>2 && system.primary && system.secondary) system.tertiary=classify_spectral_type(star.components[2].spectral_type);
}
std::vector<std::string> procedural_system_names(std::int64_t seed,int count) {
    static constexpr std::string_view prefixes[]={"Al","An","Ar","Bel","Cael","Cer","Cor","Del","Eri","Gal","Hal","Io","Ka","Ke","Ly","Mar","Mer","Na","Nex","Ori","Pel","Pro","Qua","Rin","Sa","Ser","Tal","Tau","Ul","Va","Vel","Xi","Za"};
    static constexpr std::string_view suffixes[]={"bara","caris","dara","dos","dris","lia","lion","lora","maris","mora","nara","nor","phos","ra","rian","ris","ron","rus","sara","tar","thera","tis","tor","vara","vega","von","xis","yra","zen","zora"};
    static constexpr std::string_view infixes[]={"a","e","i","o","u","ae","ia","or"};
    if(count<1 || count>33*30*9) throw std::invalid_argument("Procedural name count is outside supported bounds");
    LegacyRandom random(population_seed(seed,0x4E414D45)); std::vector<std::string> names; std::set<std::string> used;
    while(names.size()<static_cast<std::size_t>(count)) {
        // Sequential draws are intentional: C++ operand evaluation order must not reorder the stream.
        std::string name(prefixes[random.next(33)]);
        if(names.size()>=990) name+=infixes[random.next(8)];
        name+=suffixes[random.next(30)];
        if(used.insert(lower(name)).second) names.push_back(std::move(name));
    }
    return names;
}
void apply_stellar_companions(std::int64_t seed,std::vector<StellarSystem>& systems) {
    std::vector<std::size_t> eligible;
    for(std::size_t i=0;i<systems.size();++i) {
        const auto& s=systems[i];
        if(!s.catalog_preset_id && !s.stellar_catalog_id && s.primary && static_cast<int>(*s.primary)<=static_cast<int>(StellarClass::Giant)) eligible.push_back(i);
    }
    std::stable_sort(eligible.begin(),eligible.end(),[&](auto a,auto b){return systems[a].id<systems[b].id;});
    LegacyRandom random(population_seed(seed,0x434F4D50));
    for(int i=static_cast<int>(eligible.size())-1;i>0;--i) std::swap(eligible[i],eligible[random.next(i+1)]);
    const auto binaries=static_cast<std::size_t>(std::round(eligible.size()*.20));
    const auto triples=static_cast<std::size_t>(std::round(eligible.size()*.05));
    auto companion=[&](StellarClass primary) {
        const int choices=primary==StellarClass::MRedDwarf?1:primary==StellarClass::KOrangeDwarf?2:3;
        const int choice=random.next(10);
        return choices>=3 && choice>=9?StellarClass::GYellowDwarf:choices>=2 && choice>=6?StellarClass::KOrangeDwarf:StellarClass::MRedDwarf;
    };
    for(std::size_t i=0;i<eligible.size();++i) {
        auto& s=systems[eligible[i]]; s.secondary.reset(); s.tertiary.reset();
        if(i<binaries+triples) s.secondary=companion(*s.primary);
        if(i<triples) s.tertiary=companion(*s.primary);
    }
}
std::vector<StellarSystem> generate_stellar_catalog(std::int64_t seed,int count,std::span<const CatalogStar> catalog) {
    const auto measured=nearest_classified(catalog,96);
    const auto classes=full_galaxy_stellar_classes(seed,count,measured);
    const auto positions=full_galaxy_generated_positions(seed,count,full_galaxy_core(count),measured);
    const auto names=procedural_system_names(seed,count);
    std::vector<StellarSystem> result; result.reserve(static_cast<std::size_t>(count));
    for(int i=0;i<count;++i) {
        StellarSystem system; system.id=i; system.name=names[i]; system.primary=classes[i];
        if(i<96) apply_catalog_star(system,measured[i]);
        else system.position={positions[i-96].x,positions[i-96].y,{}};
        if(i==0) system.catalog_preset_id="sol-v1";
        result.push_back(std::move(system));
    }
    apply_stellar_companions(seed,result);
    apply_full_galaxy_traits(seed,result);
    return result;
}
}
