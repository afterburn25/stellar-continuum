#include "stellar/engine/foundation.hpp"
#include "stellar/core/interstellar_distance.hpp"
#include "stellar/build_version.hpp"
#include "galaxy_main.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <typeinfo>

using namespace stellar::engine;
namespace {
struct Scenario { std::uint64_t seed{8374837}, systems{500}, completed_ticks{}; };
std::uint64_t number(std::string_view text) {
    std::uint64_t result{}; const auto parsed = std::from_chars(text.data(), text.data()+text.size(), result);
    require(parsed.ec == std::errc{} && parsed.ptr == text.data()+text.size(), "Expected an unsigned whole number"); return result;
}
std::uint64_t checksum(const Scenario& s) {
    std::uint64_t h=14695981039346656037ull;
    for (auto value : {s.seed,s.systems,s.completed_ticks}) for(int b=0;b<8;++b) { h^=value&255; h*=1099511628211ull; value>>=8; }
    return h;
}
Scenario load(const std::filesystem::path& path) {
    std::ifstream in(path); in.imbue(std::locale::classic());
    require(in.good(), "Cannot read foundation checkpoint"); std::string magic; std::getline(in,magic);
    require(magic=="STELLAR_FOUNDATION_V1", "Unsupported checkpoint: this is not a migrated game save");
    Scenario s; std::uint64_t recorded{}; require(bool(in>>s.seed>>s.systems>>s.completed_ticks>>recorded), "Truncated checkpoint");
    std::string extra; require(!(in>>extra), "Unexpected checkpoint data");
    require(s.systems>=2 && s.systems<=100000 && s.completed_ticks<=1000000000, "Checkpoint values outside supported bounds");
    require(recorded==checksum(s), "Checkpoint checksum mismatch"); return s;
}
void save(const Scenario& s, const std::filesystem::path& path) {
    require(!std::filesystem::exists(path), "Refusing to overwrite a checkpoint; choose a new save path");
    auto temp=path; temp += ".pending";
    require(!std::filesystem::exists(temp), "Pending checkpoint exists; preserve it and choose a new save path");
    { std::ofstream out(temp); out.imbue(std::locale::classic()); require(out.good(), "Cannot create checkpoint");
      out<<"STELLAR_FOUNDATION_V1\n"<<s.seed<<' '<<s.systems<<' '<<s.completed_ticks<<' '<<checksum(s)<<'\n';
      out.flush(); require(out.good(), "Could not finish checkpoint write"); }
    std::filesystem::rename(temp,path);
}
int run(int argc, char** argv) {
    bool catalog_mode=false;
    // Inspect option names only: file paths and other values may themselves begin
    // with '--'. Global help/version also apply to the campaign commands.
    for(int i=1;i<argc;++i) {
        const std::string_view arg=argv[i];
        if(arg=="--help") { std::cout<<"Stellar Engine native commands:\n  --headless [--systems N] [--ticks N] [--workers N] [--seed N] [--load file] [--save new-file]\n  --headless --generate-galaxy [--systems N] [--seed N] [--repeat N] [--asset-root directory] [--catalog-output new-file]\n  --headless --seed-campaign [--systems N] [--seed N] [--civilizations N] [--ancients N] [--player-species ID] [--repeat N] [--catalog-output new-file]\nFresh campaign output is diagnostic data, not a player save or a running campaign.\n"; return 0; }
        if(arg=="--version") { std::cout<<"Stellar Engine " STELLAR_ENGINE_VERSION "; Stellar Continuum " STELLAR_GAME_VERSION "; source " STELLAR_SOURCE_COMMIT "\n"; return 0; }
        if(arg=="--generate-galaxy" || arg=="--seed-campaign") catalog_mode=true;
        if(arg=="--systems" || arg=="--ticks" || arg=="--workers" || arg=="--seed" ||
           arg=="--load" || arg=="--save" || arg=="--repeat" || arg=="--asset-root" ||
           arg=="--catalog-output" || arg=="--civilizations" || arg=="--ancients" || arg=="--player-species") {
            require(i+1<argc,"Missing option value"); ++i;
        }
    }
    if(catalog_mode) return run_galaxy_catalog(argc,argv);
    Scenario scenario; std::uint64_t steps=10, workers=std::clamp(std::thread::hardware_concurrency(),1u,4u);
    bool headless=false; std::string save_path, load_path;
    for(int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        if(arg=="--version") { std::cout<<"Stellar Engine " STELLAR_ENGINE_VERSION "; Stellar Continuum " STELLAR_GAME_VERSION "; source " STELLAR_SOURCE_COMMIT "\n"; return 0; }
        if(arg=="--headless") { headless=true; continue; }
        if(arg=="--help") { std::cout<<"Native foundation. --headless [--systems 500] [--ticks 10] [--workers 4] [--seed 8374837] [--load file] [--save new-file]\nPhysical catalog: --headless --generate-galaxy [--systems 500] [--seed 8374837] [--repeat 1] [--asset-root directory] [--catalog-output new-file]\nFresh campaign: --headless --seed-campaign [--systems 500] [--seed 8374837] [--civilizations 6] [--ancients 1] [--player-species terran_baseline] [--repeat 1] [--catalog-output new-file]\nFresh campaign output is diagnostic data, not a player save or a running campaign.\n"; return 0; }
        require(i+1<argc, "Missing option value"); const std::string value=argv[++i];
        if(arg=="--systems") scenario.systems=number(value);
        else if(arg=="--ticks") steps=number(value);
        else if(arg=="--workers") workers=number(value);
        else if(arg=="--seed") scenario.seed=number(value);
        else if(arg=="--save") save_path=value;
        else if(arg=="--load") load_path=value;
        else throw std::invalid_argument("Unknown option: "+arg);
    }
    require(headless, "Graphical migration is not available yet. Use --headless or launch the preserved reference game.");
    if(!load_path.empty()) scenario=load(load_path);
    require(scenario.systems>=2 && scenario.systems<=100000 && steps<=1000000 && workers>=1 && workers<=64, "Scenario counts exceed supported bounds");
    require(scenario.completed_ticks<=1000000000-steps, "Checkpoint tick limit exceeded");
    DeterministicRandom rng(scenario.seed); EntityRegistry identities;
    std::vector<stellar::core::StarPosition> positions; positions.reserve(static_cast<std::size_t>(scenario.systems));
    for(std::uint64_t i=0;i<scenario.systems;++i) {
        const auto id=identities.create(); require(identities.contains(id), "Created entity missing");
        positions.push_back({static_cast<float>((rng.unit_double()-.5)*10000),static_cast<float>((rng.unit_double()-.5)*10000),(rng.unit_double()-.5)*1000});
    }
    JobSystem jobs(static_cast<std::size_t>(workers)); FixedClock clock(std::chrono::seconds(1));
    std::vector<double> distances(positions.size()); EventQueue<Tick> completed;
    // Drain before any captured result buffer unwinds, including submission errors.
    struct DrainBeforeBuffers { JobSystem& jobs; ~DrainBeforeBuffers() { jobs.wait_idle(); } } drain{jobs};
    const auto started=std::chrono::steady_clock::now(); double total=0;
    for(std::uint64_t tick=0;tick<steps;++tick) {
        std::vector<std::future<void>> work;
        // Stable partitions and an owner-ordered merge keep results independent of worker count.
        for(std::size_t start=0;start<positions.size();start+=64) work.push_back(jobs.submit([&,start] {
            for(auto i=start;i<std::min(start+64,positions.size());++i)
                distances[i]=stellar::core::distance_light_years(positions[i],positions[(i+1)%positions.size()]);
        }));
        for(auto& task:work) task.get();
        total=0; for(double value:distances) total+=value;
        require(clock.advance(std::chrono::seconds(1))==1, "Headless fixed tick diverged");
        completed.publish(++scenario.completed_ticks);
        require(completed.drain().size()==1, "Tick event duplicated or lost");
    }
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    if(!save_path.empty()) save(scenario,save_path);
    std::cout.imbue(std::locale::classic());
    std::cout<<std::setprecision(17)<<"{\"mode\":\"foundation-distance-benchmark\",\"gameplayParity\":false,\"engineVersion\":\"" STELLAR_ENGINE_VERSION "\",\"sourceCommit\":\"" STELLAR_SOURCE_COMMIT "\",\"systems\":"<<scenario.systems<<",\"steps\":"<<steps<<",\"completedTicks\":"<<scenario.completed_ticks<<",\"workers\":"<<workers<<",\"elapsedMs\":"<<ms<<",\"tickMeanMs\":"<<(steps?ms/static_cast<double>(steps):0)<<",\"distanceSum\":"<<total<<",\"checkpointHash\":"<<checksum(scenario)<<"}\n";
    return 0;
}
}
int main(int argc,char** argv) {
    try { return run(argc,argv); }
    catch(const std::exception& error) {
        std::cerr<<"Stellar Engine " STELLAR_ENGINE_VERSION " error ["<<typeid(error).name()<<"]: "<<error.what()<<"\nSource: " STELLAR_SOURCE_COMMIT "\n";
        std::error_code ec; const auto cwd=std::filesystem::current_path(ec);
        if(!ec) std::cerr<<"Working directory: "<<cwd.string()<<'\n';
        return 1;
    } catch(...) { std::cerr<<"Stellar Engine fatal: unknown exception; source " STELLAR_SOURCE_COMMIT "\n"; return 2; }
}
