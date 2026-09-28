#include <stellar/engine/asset_audit.hpp>
#include <stellar/engine/asset_cooker.hpp>
#include <stellar/engine/asset_registry.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <set>
#include <nlohmann/json.hpp>
int main(int argc,char**argv){try{
  std::filesystem::path root=std::filesystem::current_path(),report;unsigned threads=4;bool audit=false,validate_only=false;
  stellar::engine::AssetCookOptions options;
  for(int i=1;i<argc;++i){std::string arg=argv[i];auto value=[&](){if(++i>=argc)throw std::runtime_error("Missing value for "+arg);return std::string(argv[i]);};
    if(arg=="--help"){std::cout<<"StellarCooker --root ROOT --profile development|qa|release --output DIR [--cache DIR] [--clean|--incremental] [--threads 1..16] [--category GROUP] [--report JSON] [--validate] [--no-package]\nStellarCooker --validate-only --output DIR [--report JSON]\nStellarCooker --audit --root ROOT --report JSON\n";return 0;}
    if(arg=="--audit")audit=true;else if(arg=="--root")root=value();else if(arg=="--report")report=value();else if(arg=="--threads")threads=static_cast<unsigned>(std::stoul(value()));
    else if(arg=="--platform"){if(value()!="windows")throw std::runtime_error("Only the Windows backend is implemented");}
    else if(arg=="--profile")options.profile=value();else if(arg=="--output")options.output=value();else if(arg=="--cache")options.cache=value();
    else if(arg=="--clean")options.clean=true;else if(arg=="--incremental")options.clean=false;else if(arg=="--validate")options.validate=true;else if(arg=="--validate-only")validate_only=true;
    else if(arg=="--package")options.package=true;else if(arg=="--no-package")options.package=false;else if(arg=="--category")options.category=value();else throw std::runtime_error("Unknown argument: "+arg);
  }
  if(audit){if(report.empty())report=root/"work/cooker/baseline-audit.json";stellar::engine::audit_asset_repository(root,report,threads);return 0;}
  options.root=std::filesystem::absolute(root);options.threads=threads;
  if(options.output.empty())options.output=root/"work/cooker"/options.profile;
  if(options.cache.empty())options.cache=root/"work/cooker/cache";
  options.report=report.empty()?root/"work/cooker/cook-report.json":report;
  if(validate_only){stellar::engine::AssetRegistry registry(options.output/"Content/runtime.stmanifest");registry.validate_all();
    std::set<std::string> packages;for(const auto&r:registry.records())for(const auto&c:r.chunks)packages.insert(c.package);
    if(!report.empty()){std::filesystem::create_directories(report.parent_path());std::ofstream f(report);f<<nlohmann::json{{"valid",true},{"assets",registry.records().size()},{"chunks",registry.diagnostics().reads},{"packages",packages}}.dump(2);if(!f)throw std::runtime_error("Cannot write validation report");}
    std::cout<<"Validated "<<registry.diagnostics().reads<<" chunks\n";return 0;}
  stellar::engine::cook_asset_repository(options);return 0;
}catch(const std::exception&e){std::cerr<<"StellarCooker: "<<e.what()<<'\n';return 1;}}
