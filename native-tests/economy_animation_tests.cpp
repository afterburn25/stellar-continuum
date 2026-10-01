#include <stellar/engine/accessibility.hpp>
#include <stellar/engine/animation.hpp>
#include <stellar/engine/replay.hpp>
#include <stellar/engine/resource_economy.hpp>

#include <cmath>
#include <iostream>
#include <string>

using namespace stellar::engine;

namespace {
int failures = 0;
void check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}
} // namespace

int main() {
  // --- Resource economy ---
  ResourceNetwork economy;
  economy.define({"res.ore", "RES_ORE", true});
  economy.define({"res.metal", "RES_METAL", true});
  economy.add_recipe(Recipe{"smelt",
                            {{"res.ore", 2.0}},
                            {{"res.metal", 1.0}},
                            2.0});

  auto &mine = economy.add_node(1);
  auto &forge = economy.add_node(2);
  mine.inventory.add("res.ore", 10.0);
  forge.inventory.add("res.ore", 4.0);

  const auto producer = economy.add_producer(2, "smelt");
  check(producer != 0, "producer registered");

  // Transfer ore mine -> forge at 3/day.
  economy.transfer(1, 2, "res.ore", 6.0, 3.0);
  economy.advance(1.0);
  check(std::abs(forge.inventory.quantity("res.ore") - 7.0) < 1e-9,
        "transfer moved 3 ore in one day");
  check(std::abs(mine.inventory.quantity("res.ore") - 7.0) < 1e-9,
        "source decremented");
  economy.advance(1.0);
  check(economy.transfers().empty(), "order completes after 2 days");

  // Production: each 2 days completes a run (2 ore -> 1 metal). The two
  // 1-day advances above already finished one run; this adds a second.
  economy.advance(2.0);
  check(std::abs(forge.inventory.quantity("res.metal") - 2.0) < 1e-9,
        "recipe produced metal");
  check(std::abs(forge.inventory.quantity("res.ore") - 6.0) < 1e-9,
        "recipe consumed ore");

  // Shortage: ore runs out, producer stalls and reports. Six ore support
  // three more runs (metal 2 -> 5), then the producer stalls.
  economy.advance(8.0);
  check(!economy.shortages().empty(), "shortage reported on empty input");
  check(std::abs(forge.inventory.quantity("res.metal") - 5.0) < 1e-9,
        "producer stalled after consuming available ore");

  // Capacity bound.
  auto &silo = economy.add_node(3, 5.0);
  check(std::abs(silo.inventory.add("res.ore", 8.0) - 5.0) < 1e-9,
        "capacity caps stored amount");

  // --- Replay ---
  ReplayRecorder recorder({20260908, "build-test", "0.1.0"});
  recorder.record(10, "set_speed", "5");
  recorder.record(10, "move_fleet", "fleet:3->sys:9");
  recorder.record(42, "pause", "");
  recorder.checkpoint(10, fnv1a64("state-abc"), "post-command");
  recorder.checkpoint(40, fnv1a64("state-def"));

  const auto doc = recorder.serialize();
  std::string error;
  const auto parsed = ReplayRecorder::parse(doc, &error);
  check(parsed.has_value(), error.c_str());
  check(parsed->header().seed == 20260908, "header seed round-trips");

  ReplayPlayer player(&*parsed);
  const auto at10 = player.commands_for(10);
  check(at10.size() == 2 && at10[0]->name == "set_speed" &&
            at10[1]->name == "move_fleet",
        "commands replay in record order");
  const auto expected = player.expected_checkpoint(10);
  check(expected && *expected == fnv1a64("state-abc"),
        "checkpoint hash matches");
  check(!player.expected_checkpoint(11), "no checkpoint at 11");

  // --- Animation ---
  FloatCurve curve;
  curve.add_key(0.0f, 0.0f);
  curve.add_key(2.0f, 10.0f);
  curve.add_key(4.0f, 0.0f, Easing::SmoothStep);
  check(std::abs(curve.evaluate(1.0f) - 5.0f) < 1e-5, "linear midpoint");
  check(std::abs(curve.evaluate(3.0f) - 5.0f) < 1e-5,
        "smoothstep midpoint symmetric");
  check(curve.evaluate(-1.0f) == 0.0f && curve.evaluate(99.0f) == 0.0f,
        "curve clamps at ends");

  Timeline timeline;
  timeline.duration = 4.0f;
  timeline.track("pulse").add_key(0.0f, 0.0f);
  timeline.track("pulse").add_key(4.0f, 1.0f);
  timeline.add_event(1.0f, "beat");
  timeline.add_event(3.0f, "crest");
  const auto crossed = timeline.events_crossed(0.5f, 1.5f, LoopMode::Once);
  check(crossed.size() == 1 && crossed[0].name == "beat",
        "event crossing detected");
  // 0.9 -> 5.2 under a 4s loop wraps once: (0.9,4] catches crest, [0,1.2]
  // catches beat.
  const auto wrapped =
      timeline.events_crossed(0.9f, 5.2f, LoopMode::Loop);
  check(wrapped.size() == 2, "loop wrap catches events on both sides");
  check(std::abs(wrap_time(5.0f, 4.0f, LoopMode::Loop) - 1.0f) < 1e-5,
        "loop wrap");
  check(std::abs(wrap_time(6.0f, 4.0f, LoopMode::PingPong) - 2.0f) < 1e-5,
        "pingpong mirror");

  // --- AnimationPlayer lifecycle ---
  AnimationPlayer clip;
  check(clip.advance(1.0f).values.empty(),
        "timeline-less player steps empty");
  clip.play(&timeline, LoopMode::Once);
  check(clip.playing() && !clip.finished(), "play starts");
  auto step = clip.advance(1.2f);
  check(step.events.size() == 1 && step.events[0].name == "beat",
        "player emits crossed event");
  check(std::abs(step.values.at("pulse") - 0.3f) < 1e-5,
        "player evaluates track at wrapped time");
  clip.set_speed(2.0f);
  step = clip.advance(0.4f); // playhead 2.0 — no event
  check(step.events.empty(), "no event before crest");
  step = clip.advance(1.0f); // playhead 4.0 — crest at 3.0 crossed, clamps
  check(step.events.size() == 1 && step.events[0].name == "crest",
        "speed-scaled advance crosses crest");
  check(clip.finished() && std::abs(clip.time() - 4.0f) < 1e-5,
        "once-mode clamps at duration");
  clip.set_paused(true);
  check(clip.advance(1.0f).values.empty(), "paused steps empty");
  clip.set_paused(false);
  clip.play(&timeline, LoopMode::Loop);
  step = clip.advance(5.0f); // wraps once: crest(3) + beat(1, next cycle)
  check(step.events.size() == 2, "player wrap emits both cycle events");
  check(std::abs(clip.time() - 5.0f) < 1e-5,
        "loop keeps unwrapped playhead");
  clip.seek(2.5f);
  check(std::abs(clip.time() - 2.5f) < 1e-5, "seek restores playhead");
  clip.stop();
  check(clip.timeline() == nullptr && clip.time() == 0.f,
        "stop detaches and resets");

  // --- Accessibility ---
  AccessibilitySettings settings;
  settings.ui_scale = 5.0f;     // out of range
  settings.text_scale = -1.0f;  // out of range
  settings.sanitize();
  check(settings.ui_scale == 2.0f && settings.text_scale == 0.75f,
        "scales clamp to supported range");
  const auto loaded = AccessibilitySettings::from_json(
      R"({"ui_scale":1.5,"color_blind":"deuteranopia","reduce_motion":true})");
  check(loaded.ui_scale == 1.5f &&
            loaded.color_blind == ColorBlindMode::Deuteranopia &&
            loaded.reduce_motion,
        "settings round-trip with enum parsing");
  check(AccessibilitySettings::from_json("not json").ui_scale == 1.0f,
        "malformed settings fall back to defaults");

  // --- Accessibility announcer ---
  AccessibilityAnnouncer announcer{3};
  check(announcer.empty() && !announcer.take().has_value(),
        "announcer starts drained");
  announcer.announce("first");
  announcer.announce("first");
  check(announcer.size() == 1, "consecutive duplicates collapse");
  announcer.announce("second");
  announcer.announce("");
  check(announcer.size() == 2, "empty text dropped");
  check(announcer.latest()->text == "second", "latest tracks newest");
  announcer.announce("third");
  announcer.announce("fourth");  // capacity 3 -> oldest polite evicted
  check(announcer.size() == 3 && announcer.take()->text == "second",
        "capacity evicts oldest polite first");
  announcer.announce("alert", AnnouncementPriority::Assertive);
  check(announcer.size() == 1 && announcer.latest()->text == "alert",
        "assertive preempts queued polite");
  const auto seq_a = announcer.latest()->sequence;
  announcer.announce("next", AnnouncementPriority::Assertive);
  check(announcer.size() == 2 && announcer.latest()->sequence > seq_a,
        "assertive does not preempt assertive; sequence increments");
  check(announcer.take()->text == "alert" && announcer.take()->text == "next" &&
            !announcer.take().has_value(),
        "take drains in publish order");
  announcer.announce("arrived");
  announcer.announce_focus("focused control",
                           stellar::engine::AnnouncementBounds{4.f, 8.f, 16.f,
                                                               24.f},
                           std::nullopt,
                           stellar::engine::AnnouncementControl::Button);
  const auto status_item = announcer.take();
  const auto focus_item = announcer.take();
  check(status_item && status_item->kind == AnnouncementKind::Status &&
            focus_item && focus_item->kind == AnnouncementKind::Focus,
        "announcement kind did not distinguish focus from status");
  check(focus_item->bounds && focus_item->bounds->x == 4.f &&
            focus_item->bounds->width == 16.f && !status_item->bounds,
        "focus announcement did not retain its control bounds");
  check(focus_item->control == stellar::engine::AnnouncementControl::Button &&
            status_item->control ==
                stellar::engine::AnnouncementControl::Custom,
        "focus announcement did not retain its control kind");
  check(!announcer.take().has_value(), "announcer did not drain fully");

  // Same-focus state changes must re-announce: identical labels with
  // different checked/range/value state are not duplicates.
  announcer.announce_focus("mute", std::nullopt, std::nullopt,
                           stellar::engine::AnnouncementControl::CheckBox,
                           false);
  announcer.announce_focus("mute", std::nullopt, std::nullopt,
                           stellar::engine::AnnouncementControl::CheckBox,
                           false);
  check(announcer.size() == 1, "identical checked state deduped");
  announcer.announce_focus("mute", std::nullopt, std::nullopt,
                           stellar::engine::AnnouncementControl::CheckBox,
                           true);
  check(announcer.size() == 2, "checked change did not re-announce");
  announcer.clear();
  announcer.announce_focus("volume", std::nullopt,
                           stellar::engine::AnnouncementRange{0.f, 1.f, 0.5f},
                           stellar::engine::AnnouncementControl::Slider);
  announcer.announce_focus("volume", std::nullopt,
                           stellar::engine::AnnouncementRange{0.f, 1.f, 0.75f},
                           stellar::engine::AnnouncementControl::Slider);
  check(announcer.size() == 2, "range change did not re-announce");
  announcer.clear();
  announcer.announce_focus("search", std::nullopt, std::nullopt,
                           stellar::engine::AnnouncementControl::Edit,
                           std::nullopt,
                           stellar::engine::AnnouncementValue{"alp"});
  announcer.announce_focus("search", std::nullopt, std::nullopt,
                           stellar::engine::AnnouncementControl::Edit,
                           std::nullopt,
                           stellar::engine::AnnouncementValue{"alph"});
  check(announcer.size() == 2, "edit-value change did not re-announce");
  announcer.clear();
  announcer.announce_focus("planets", std::nullopt, std::nullopt,
                           stellar::engine::AnnouncementControl::Group,
                           std::nullopt, std::nullopt, true);
  announcer.announce_focus("planets", std::nullopt, std::nullopt,
                           stellar::engine::AnnouncementControl::Group,
                           std::nullopt, std::nullopt, false);
  check(announcer.size() == 2, "expanded change did not re-announce");
  announcer.clear();

  if (failures == 0)
    std::cout << "Economy, replay, animation and accessibility tests passed\n";
  return failures == 0 ? 0 : 1;
}
