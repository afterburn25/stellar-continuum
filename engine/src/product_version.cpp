#include "stellar/engine/product_version.hpp"
#include <charconv>
#include <stdexcept>
namespace stellar::engine {
ProductVersion ProductVersion::parse(std::string_view value) {
  ProductVersion result;
  const auto dash=value.find('-');
  if(dash==value.npos) throw std::runtime_error("Version requires four numbers and a channel.");
  auto digits=value.substr(0,dash); const auto channel=value.substr(dash+1);
  if(channel=="stable") result.channel=Channel::stable;
  else if(channel=="beta") result.channel=Channel::beta;
  else if(channel=="dev") result.channel=Channel::dev;
  else throw std::runtime_error("Unknown release channel.");
  for(std::size_t i=0;i<4;++i) {
    auto end=digits.find('.');
    if((i<3 && end==digits.npos)||(i==3 && end!=digits.npos)) throw std::runtime_error("Version requires major.minor.patch.build.");
    auto part=digits.substr(0,end);
    if(part.empty()||(part.size()>1 && part.front()=='0')) throw std::runtime_error("Invalid version number.");
    const auto parsed=std::from_chars(part.data(),part.data()+part.size(),result.number[i]);
    if(parsed.ec!=std::errc{}||parsed.ptr!=part.data()+part.size()||result.number[i]>65535) throw std::runtime_error("Version number out of range.");
    if(end!=digits.npos) digits.remove_prefix(end+1);
  }
  return result;
}
std::string ProductVersion::channel_name() const {return channel==Channel::stable?"stable":channel==Channel::beta?"beta":"dev";}
std::string ProductVersion::string() const {return std::to_string(number[0])+"."+std::to_string(number[1])+"."+std::to_string(number[2])+"."+std::to_string(number[3])+"-"+channel_name();}
}
