#include "stellar/core/interstellar_distance.hpp"
#include "stellar/engine/foundation.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
int main(int argc,char**argv) {
 try {
  using stellar::engine::require; require(argc==2,"Pass the preserved C# golden CSV path");
  std::ifstream file(argv[1]); require(file.good(),"Cannot open legacy distance fixtures");
  std::string line; std::getline(file,line); std::size_t rows=0;
  while(std::getline(file,line)) {
   if(line.empty()) continue;
   std::stringstream in(line); std::vector<std::string> fields; std::string cell;
   while(std::getline(in,cell,',')) fields.push_back(cell);
   require(fields.size()==8,"Malformed distance fixture row");
   const auto depth=[](const std::string& s)->std::optional<double>{return s=="null"?std::nullopt:std::optional<double>(std::stod(s));};
   stellar::core::StarPosition a{std::stof(fields[0]),std::stof(fields[1]),depth(fields[2])};
   stellar::core::StarPosition b{std::stof(fields[3]),std::stof(fields[4]),depth(fields[5])};
   const double expected=std::stod(fields[6]), square=std::stod(fields[7]);
   const double tolerance=(!a.depth_light_years&&!b.depth_light_years)?2e-7:2e-14;
   require(std::abs(stellar::core::distance_light_years(a,b)-expected)<=std::max(1.0,std::abs(expected))*tolerance,"Native distance differs from preserved C# oracle");
   require(std::abs(stellar::core::squared_distance_light_years(a,b)-square)<=std::max(1.0,std::abs(square))*tolerance,"Native squared distance differs from preserved C# oracle"); ++rows;
  }
  require(rows>=500,"Parity fixture is incomplete"); bool rejected=false;
  try{(void)stellar::core::distance_light_years({std::numeric_limits<float>::quiet_NaN(),0,{}},{});}catch(const std::invalid_argument&){rejected=true;}
  require(rejected,"Nonfinite native coordinates accepted");
  std::cout<<"PASS: "<<rows<<" preserved C# distance cases and invalid input\n"; return 0;
 }catch(const std::exception& error){std::cerr<<"Distance parity: "<<error.what()<<'\n';return 1;}
}
