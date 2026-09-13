#include <stellar/core/logistics.hpp>

#include <algorithm>
#include <cmath>
#include <list>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace stellar::core {
namespace {
struct Edge { int from, to, link; };
struct Key { int source, destination; bool operator==(const Key&) const = default; };
struct KeyHash { std::size_t operator()(const Key& k) const noexcept { return (static_cast<std::size_t>(static_cast<unsigned>(k.source)) << 32) ^ static_cast<unsigned>(k.destination); } };

// This is the relevant .NET 8 PriorityQueue quaternary min-heap behavior (MIT; see
// third_party/dotnet/LICENSE.TXT).  In particular equal priorities retain heap, rather
// than std::priority_queue's unrelated tie behavior.
class DotnetQueue {
    struct Item { int node; double priority; };
    std::vector<Item> items;
public:
    void push(int node, double priority) {
        std::size_t i = items.size(); items.push_back({node, priority});
        while (i) { const auto parent = (i - 1) / 4; if (!(priority < items[parent].priority)) break;
            items[i] = items[parent]; i = parent; }
        items[i] = {node, priority};
    }
    bool pop(int& node, double& priority) {
        if (items.empty()) return false;
        auto root = items.front(); auto last = items.back(); items.pop_back();
        if (!items.empty()) {
            std::size_t i = 0;
            while (true) { const auto child = i * 4 + 1; if (child >= items.size()) break;
                auto best = child; const auto limit = std::min(items.size(), child + 4);
                for (auto c = child + 1; c < limit; ++c) if (items[c].priority < items[best].priority) best = c;
                if (!(items[best].priority < last.priority)) break;
                items[i] = items[best]; i = best;
            }
            items[i] = last;
        }
        node=root.node; priority=root.priority; return true;
    }
};
void finite_nonnegative(double value, const char* field, int node) {
    if (!std::isfinite(value) || value < 0.0) throw std::out_of_range(std::string(field) + " has invalid logistics quantity for node " + std::to_string(node));
}
}

struct LogisticsRoutePlanner::Impl {
    struct Entry { std::optional<LogisticsRoutePlan> plan; std::list<Key>::iterator lru; };
    std::unordered_map<int, LogisticsNode> nodes;
    std::unordered_map<int, LogisticsLink> links;
    std::unordered_map<int, std::vector<Edge>> adjacency;
    int capacity;
    std::unordered_map<Key, Entry, KeyHash> cache;
    std::list<Key> lru;
    std::thread::id owner{std::this_thread::get_id()};
    Impl(std::span<const LogisticsNode> ns, std::span<const LogisticsLink> ls, int c) : capacity(c) {
        if (c < 1) throw std::out_of_range("cacheCapacity");
        for (const auto& n : ns) { if (!nodes.emplace(n.id, n).second) throw std::invalid_argument("Duplicate logistics node id."); adjacency.emplace(n.id, std::vector<Edge>{}); }
        for (const auto& l : ls) {
            if (!links.emplace(l.id, l).second) throw std::invalid_argument("Duplicate logistics link id.");
            const auto from=nodes.find(l.from_node_id), to=nodes.find(l.to_node_id);
            if (from==nodes.end()) throw std::runtime_error("Logistics link references unknown source node.");
            if (to==nodes.end()) throw std::runtime_error("Logistics link references unknown destination node.");
            if (from->second.civilization_id != l.civilization_id || to->second.civilization_id != l.civilization_id) throw std::runtime_error("Logistics link crosses civilization ownership.");
            if (!std::isfinite(l.capacity_per_day) || l.capacity_per_day < 0) throw std::runtime_error("Logistics link has invalid cargo capacity.");
            if (!std::isfinite(l.transit_days) || l.transit_days < 0) throw std::runtime_error("Logistics link has invalid transit time.");
            adjacency[l.from_node_id].push_back({l.from_node_id,l.to_node_id,l.id});
            if (l.bidirectional) adjacency[l.to_node_id].push_back({l.to_node_id,l.from_node_id,l.id});
        }
        for (auto& [_, edges] : adjacency) std::sort(edges.begin(), edges.end(), [](auto a, auto b){ return a.link < b.link; });
    }
    std::optional<LogisticsRoutePlan> route(int source, int destination, int civ) const {
        std::unordered_map<int,double> distance{{source,0}}; std::unordered_map<int,Edge> previous; DotnetQueue queue; queue.push(source,0);
        int current; double current_distance;
        while (queue.pop(current,current_distance)) {
            if (current == destination) break;
            if (auto best=distance.find(current); best != distance.end() && current_distance > best->second) continue;
            for (const auto& edge : adjacency.at(current)) {
                const auto& link=links.at(edge.link);
                if (!link.enabled || link.civilization_id != civ || link.capacity_per_day <= 0 || link.transit_days < 0) continue;
                const double next=current_distance+link.transit_days; auto old=distance.find(edge.to);
                if (old != distance.end() && next >= old->second) continue;
                distance[edge.to]=next; previous[edge.to]=edge; queue.push(edge.to,next);
            }
        }
        auto found=distance.find(destination); if (found == distance.end()) return std::nullopt;
        LogisticsRoutePlan plan; plan.source_node_id=source; plan.destination_node_id=destination; plan.transit_days=found->second; plan.bottleneck_capacity_per_day=INFINITY;
        plan.node_ids.push_back(destination); int cursor=destination;
        while (cursor != source) { auto edge=previous.find(cursor); if(edge==previous.end()) return std::nullopt; const auto& link=links.at(edge->second.link);
            plan.link_ids.push_back(link.id); plan.bottleneck_capacity_per_day=std::min(plan.bottleneck_capacity_per_day,link.capacity_per_day); cursor=edge->second.from; plan.node_ids.push_back(cursor); }
        std::reverse(plan.node_ids.begin(),plan.node_ids.end()); std::reverse(plan.link_ids.begin(),plan.link_ids.end()); return plan;
    }
};
LogisticsRoutePlanner::LogisticsRoutePlanner(std::span<const LogisticsNode> n, std::span<const LogisticsLink> l, int c) : impl_(std::make_unique<Impl>(n,l,c)) {}
LogisticsRoutePlanner::~LogisticsRoutePlanner() = default;
LogisticsRoutePlanner::LogisticsRoutePlanner(LogisticsRoutePlanner&&) noexcept = default;
LogisticsRoutePlanner& LogisticsRoutePlanner::operator=(LogisticsRoutePlanner&&) noexcept = default;
std::optional<LogisticsRoutePlan> LogisticsRoutePlanner::find_route(int source, int destination) {
    if (std::this_thread::get_id() != impl_->owner) throw std::runtime_error("Logistics route queries must run on the owner thread.");
    auto src=impl_->nodes.find(source), dst=impl_->nodes.find(destination); if(src==impl_->nodes.end()) throw std::out_of_range("Unknown logistics source node."); if(dst==impl_->nodes.end()) throw std::out_of_range("Unknown logistics destination node.");
    if(src->second.civilization_id != dst->second.civilization_id) return std::nullopt;
    if(source == destination) return LogisticsRoutePlan{source,destination,{source},{},0,INFINITY};
    Key key{source,destination}; if(auto hit=impl_->cache.find(key);hit!=impl_->cache.end()) { impl_->lru.erase(hit->second.lru); hit->second.lru=impl_->lru.insert(impl_->lru.end(),key); return hit->second.plan; }
    auto plan=impl_->route(source,destination,src->second.civilization_id);
    while (static_cast<int>(impl_->cache.size()) >= impl_->capacity) { const auto old=impl_->lru.front(); impl_->cache.erase(old); impl_->lru.pop_front(); }
    auto position=impl_->lru.insert(impl_->lru.end(),key); impl_->cache.emplace(key,Impl::Entry{plan,position}); return plan;
}
std::size_t LogisticsRoutePlanner::cached_route_count() const { return impl_->cache.size(); }
int LogisticsRoutePlanner::cache_capacity() const { return impl_->capacity; }

LogisticsFlowPlan allocate_daily_logistics(LogisticsRoutePlanner& planner, std::span<const LogisticsLink> links, std::span<const LogisticsSupplyOffer> offers, std::span<const LogisticsDemand> demands) {
    struct Supply { int id; double value; }; std::vector<Supply> supply; std::unordered_map<int,std::size_t> supply_index; std::unordered_map<int,double> remaining_link;
    for(const auto& link:links) { if(!remaining_link.emplace(link.id,link.enabled?std::max(0.0,link.capacity_per_day):0.0).second) throw std::invalid_argument("Duplicate logistics link id."); }
    for(const auto& offer:offers) { finite_nonnegative(offer.available_per_day,"AvailablePerDay",offer.node_id); auto [it,new_item]=supply_index.emplace(offer.node_id,supply.size()); if(new_item) supply.push_back({offer.node_id,offer.available_per_day}); else supply[it->second].value += offer.available_per_day; }
    std::vector<LogisticsDemand> sorted(demands.begin(),demands.end()); std::stable_sort(sorted.begin(),sorted.end(),[](auto&a,auto&b){return a.priority!=b.priority?a.priority>b.priority:a.node_id<b.node_id;});
    LogisticsFlowPlan result;
    for(const auto& demand:sorted) { finite_nonnegative(demand.required_per_day,"RequiredPerDay",demand.node_id); double remaining=demand.required_per_day;
        if(remaining<=0){ auto found=std::find_if(result.unmet_demand_per_day.begin(),result.unmet_demand_per_day.end(),[&](auto&x){return x.node_id==demand.node_id;}); if(found==result.unmet_demand_per_day.end()) result.unmet_demand_per_day.push_back({demand.node_id,0}); else found->per_day=0; continue; }
        struct Candidate { std::size_t supply; LogisticsRoutePlan route; double capacity; }; std::vector<Candidate> candidates;
        for(std::size_t i=0;i<supply.size();++i) { if(supply[i].value<=0||supply[i].id==demand.node_id)continue; auto route=planner.find_route(supply[i].id,demand.node_id); if(!route)continue; double cap=INFINITY; for(int id:route->link_ids) {auto x=remaining_link.find(id);if(x==remaining_link.end())throw std::runtime_error("Route references logistics link not known by allocator.");cap=std::min(cap,x->second);} if(cap>0)candidates.push_back({i,*route,cap}); }
        std::stable_sort(candidates.begin(),candidates.end(),[](const auto&a,const auto&b){ if(a.route.transit_days!=b.route.transit_days)return a.route.transit_days<b.route.transit_days; if(a.capacity!=b.capacity)return a.capacity>b.capacity; return a.route.source_node_id<b.route.source_node_id; });
        for(const auto& candidate:candidates) { if(remaining<=0)break; auto& source=supply[candidate.supply]; if(source.value<=0)continue; double cap=INFINITY; for(int id:candidate.route.link_ids) cap=std::min(cap,remaining_link.at(id)); double amount=std::min(remaining,std::min(source.value,cap)); if(amount<=0)continue; source.value-=amount;remaining-=amount;for(int id:candidate.route.link_ids)remaining_link[id]=std::max(0.0,remaining_link[id]-amount); result.allocations.push_back({source.id,demand.node_id,amount,candidate.route.transit_days,candidate.route.link_ids}); }
        auto found=std::find_if(result.unmet_demand_per_day.begin(),result.unmet_demand_per_day.end(),[&](auto&x){return x.node_id==demand.node_id;}); if(found==result.unmet_demand_per_day.end()) result.unmet_demand_per_day.push_back({demand.node_id,std::max(0.0,remaining)}); else found->per_day=std::max(0.0,remaining);
    }
    for(const auto& item:supply) result.unused_supply_per_day.push_back({item.id,item.value}); for(const auto&a:result.allocations)result.total_allocated_per_day+=a.allocated_per_day; for(const auto&d:result.unmet_demand_per_day)result.total_unmet_demand_per_day+=d.per_day; return result;
}
} // namespace stellar::core
