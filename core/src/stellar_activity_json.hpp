#pragma once
#include <stellar/core/stellar_activity.hpp>
#include <nlohmann/json.hpp>
namespace stellar::core {
#define ACTIVITY_ENUM(Type, Limit) \
inline void to_json(nlohmann::json& j,const Type& v){j=static_cast<int>(v);} \
inline void from_json(const nlohmann::json& j,Type& v){ \
 if(!j.is_number_integer()||j<0||j>=Limit)throw std::invalid_argument("Invalid saved " #Type);v=static_cast<Type>(j.get<int>());}
ACTIVITY_ENUM(StellarActivityLevel,5)
ACTIVITY_ENUM(StellarEruptionType,5)
ACTIVITY_ENUM(EruptionSpectralClass,8)
#undef ACTIVITY_ENUM
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StellarActivityProfile,version,spectral,level,score,magnetic_activity,rotation_days,age_modifier,rotation_modifier,evolution_modifier,flare_rate_multiplier,prominence_rate_multiplier,superflare_rate_multiplier,cme_rate_multiplier,inferred_rotation)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StellarEruptionEvent,id,seed,parent_event_id,system_id,component,visual_variant,type,activity_source,magnitude,start_day,stage_days,latitude,longitude,orientation,scale,brightness,cme_probability,cme_associated,cme_escaped,forced,paused,paused_elapsed_days)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(StellarActivityState,profile,seed,counter,revision,next_event_day,last_day,generated_counts,cme_opportunities,escaping_cmes,suppressed_events,events)
}
