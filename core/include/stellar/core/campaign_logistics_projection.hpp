#pragma once

#include <stellar/core/logistics.hpp>
#include <stellar/engine/logistics.hpp>

namespace stellar::core {

// Read-only projection of authoritative home-system logistics into the
// engine freight-network framework. Core logistics rules stay
// authoritative — the input HomeSystemLogisticsNetwork is produced by
// the same `home_system_logistics` computation the logistics workspace
// and diagnostics consume; this adapter only re-shapes the result into
// a LogisticsNetwork snapshot. Nothing here writes back.
//
// Unit mapping (engine capacity is max in-flight quantity; Core link
// capacity is a per-day rate):
//   node ids          = LogisticsNode::id
//   route per link    = path {from, to}, leg_days {transit_days}
//   route capacity    = link.capacity_per_day * link.transit_days
//                       (the committed tonnage the corridor can hold)
//   shipment per link = each daily_flow allocation traversing the link,
//                       quantity = allocated_per_day * transit_days
//                       (real cargo mass in transit on that leg)
//   route.in_flight   = sum of shipment quantities on the link
//
// The projected network therefore reports route_utilization() ==
// allocated_per_day / capacity_per_day per link — per-corridor
// saturation, which Core's colony/civilization snapshots do not
// expose. in_transit() lists the committed manifest with per-leg
// etas; restore_state keeps the clock at 0 so shipment_progress()
// reads 0 for the freshly observed state.
[[nodiscard]] engine::LogisticsNetwork
project_home_logistics_network(const HomeSystemLogisticsNetwork& network);

} // namespace stellar::core
