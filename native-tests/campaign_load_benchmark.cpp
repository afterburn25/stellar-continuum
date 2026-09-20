#include <stellar/core/adaptive_research_strategic_runtime.hpp>
#include <stellar/core/player_campaign_recovery.hpp>
#include <stellar/core/player_campaign_json.hpp>
#include <stellar/core/developer_campaign.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <vector>

int main(int argc, char **argv) try {
  using namespace stellar::core;
  if (argc < 3 || argc > 4 || (argc == 4 && std::string_view(argv[3]) != "--capture-profile"))
    throw std::invalid_argument("Usage: campaign_load_benchmark <save> <research-root> [--capture-profile]");
  const auto start = std::chrono::steady_clock::now();
  const auto factory = [&] { return load_adaptive_research_strategic_runtime(std::filesystem::path(argv[2])); };
  const bool developer = std::filesystem::path(argv[1]).filename().string().ends_with(".dev17.json");
  auto loaded = developer
      ? load_existing_developer_campaign(argv[1], factory)
      : load_existing_player_campaign_v17(argv[1], factory);
  const auto end = std::chrono::steady_clock::now();
  const auto systems = loaded.campaign.galaxy().systems.size();
  const auto bodies = loaded.campaign.galaxy().bodies.size();
  PlayerCampaignCaptureOptions options{loaded.campaign.simulation_days(),
      std::string(loaded.campaign.game_version()), std::string(loaded.campaign.saved_at_utc())};
  auto active = std::move(loaded.campaign).activate();
  if (argc == 4) {
    std::vector<double> samples;
    for (int iteration = 0; iteration < 7; ++iteration) {
      const auto begin = std::chrono::steady_clock::now();
      if (developer) {
        const auto payload = capture_developer_campaign(active, options);
        samples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-begin).count());
      } else {
        const auto payload = capture_player_campaign_v17(active, options);
        samples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-begin).count());
      }
    }
    std::cout << "capture_samples_ms=";
    for (const auto sample : samples) std::cout << sample << ',';
    std::ranges::sort(samples);
    std::cout << " capture_median_ms=" << samples[samples.size()/2] << '\n';
  }
  std::uint64_t hash = 14695981039346656037ULL;
  std::size_t bytes = 0;
  const auto sink = [&](std::string_view text) {
    bytes += text.size();
    for (const unsigned char byte : text) { hash ^= byte; hash *= 1099511628211ULL; }
  };
  if (developer) stream_developer_campaign_json(capture_developer_campaign(active, options), sink);
  else stream_player_campaign_v17_json(capture_player_campaign_v17(active, options), sink);
  std::cout << "systems=" << systems << " bodies=" << bodies
            << " load_ms=" << std::chrono::duration<double, std::milli>(end-start).count()
            << " recaptured_bytes=" << bytes << " fnv1a=" << hash << '\n';
} catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
