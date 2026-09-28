#pragma once
#include "native_audio_director.hpp"
#include "native_voice_playback.hpp"
#include <stellar/engine/native_map_platform.hpp>
#include <algorithm>
#include <cmath>
#include <optional>

namespace stellar::native_audio {
template<class Measure>
void render_voice_caption(stellar::native_map::DrawList& out, NativeAudioDirector* audio,
                          int width,int height,Measure measure,
                          const stellar::native_voice::NativeVoicePlayback* playback=nullptr) {
  using namespace stellar::native_map;
  if(!audio)return;
  std::optional<VoiceCaption> current;
  if(playback&&playback->has_active_subtitle())
    current=VoiceCaption{playback->active_speaker_name(),playback->active_subtitle(),
                         std::chrono::steady_clock::now()+std::chrono::seconds(1)};
  if(!current)current=audio->caption();
  if(!current)return;
  const auto preferences=audio->voice_preferences();
  const float scale=std::clamp(height/1080.f,.8f,2.5f);
  const int pixels=std::max(12,static_cast<int>(std::lround(preferences.subtitle_size*scale)));
  const float content_width=std::min(900.f*scale,width-80.f*scale);
  const auto value=(preferences.speaker_labels?current->speaker+"\n":"")+current->text;
  const auto measured=measure(Text{{0,0},value,{239,248,255,255},pixels,content_width});
  const float panel_height=static_cast<float>(measured.height)+24.f*scale;
  const UiRect panel{(width-content_width)*.5f-16*scale,height-panel_height-64*scale,content_width+32*scale,panel_height};
  out.overlay.emplace_back(FilledRectangle{panel,{3,11,20,static_cast<std::uint8_t>(std::lround(preferences.subtitle_background_opacity*235.f))}});
  const UiRect clip{panel.x+16*scale,panel.y+12*scale,content_width,static_cast<float>(measured.height)};
  out.overlay.emplace_back(Text{{clip.x+clip.width*.5f,clip.y},value,{239,248,255,255},pixels,clip.width,clip,TextAlign::Center});
}
}
