#pragma once
#include <stellar/core/small_body_fields.hpp>
#include <nlohmann/json.hpp>
namespace stellar::core {
#define FIELD_ENUM(Type,Limit) \
inline void to_json(nlohmann::json& j,const Type& v){j=static_cast<int>(v);} \
inline void from_json(const nlohmann::json& j,Type& v){if(!j.is_number_integer()||j.get<std::int64_t>()<0||j.get<std::int64_t>()>=Limit)throw std::invalid_argument("Invalid saved " #Type);v=static_cast<Type>(j.get<int>());}
FIELD_ENUM(SmallBodyFieldType,9)
FIELD_ENUM(SmallBodyAssetPool,9)
#undef FIELD_ENUM
inline void require_field_integer(const nlohmann::json& j,std::int64_t min,std::uint64_t max){
  if(!j.is_number_integer()||(j.is_number_unsigned()?j.get<std::uint64_t>()>max:(j.get<std::int64_t>()<min||(j.get<std::int64_t>()>=0&&static_cast<std::uint64_t>(j.get<std::int64_t>())>max))))throw std::invalid_argument("Invalid saved field integer");
}
inline void require_field_array(const nlohmann::json& j,std::size_t size){if(!j.is_array()||j.size()!=size)throw std::invalid_argument("Invalid saved field array length");}
template<class Json> inline void to_json(Json& j,const SmallBodyExtraction& v){j={{"body_index",v.body_index},{"extracted",v.extracted}};}
inline void from_json(const nlohmann::json& j,SmallBodyExtraction& v){require_field_integer(j.at("body_index"),0,999999);require_field_array(j.at("extracted"),small_body_resource_count);j.at("body_index").get_to(v.body_index);j.at("extracted").get_to(v.extracted);}
#define FIELD_MEMBERS(X) X(version) X(id) X(type) X(seed) X(inner_radius_au) X(outer_radius_au) X(thickness_au) X(density) X(body_count) X(visible_count) X(composition) X(tilt) X(eccentricity) X(clustering) X(arc_fraction) X(phase_origin) X(epoch_days) X(central_mass_solar) X(irregular_fraction) X(spin_min) X(spin_max) X(retrograde_fraction) X(asset_variants) X(body_pool) X(dust_pool) X(far_pool) X(associated_planet_id) X(associated_belt_parent_id) X(planet_centered) X(rarity_profile) X(extraction)
template<class Json> inline void to_json(Json& j,const SmallBodyField& v){
#define SAVE_FIELD(name) j[#name]=v.name;
  FIELD_MEMBERS(SAVE_FIELD)
#undef SAVE_FIELD
}
inline void from_json(const nlohmann::json& j,SmallBodyField& v){
  for(const auto name:{"version","id","body_count","visible_count"})require_field_integer(j.at(name),0,2147483647);
  for(const auto name:{"associated_planet_id","associated_belt_parent_id"})require_field_integer(j.at(name),-1,2147483647);
  require_field_integer(j.at("seed"),0,UINT64_MAX);
  require_field_array(j.at("composition"),8);
  require_field_array(j.at("asset_variants"),4);
  for(const auto& id:j.at("asset_variants"))require_field_integer(id,1,4);
#define READ_FIELD(name) j.at(#name).get_to(v.name);
  FIELD_MEMBERS(READ_FIELD)
#undef READ_FIELD
  validate_small_body_field(v);
}
#undef FIELD_MEMBERS
}
