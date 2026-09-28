#include <stellar/engine/native_map_platform.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_render.h>
#include <chrono>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
namespace {
int failures{};
void check(bool passed,const char *message){if(!passed){std::cerr<<message<<'\n';++failures;}}
void push(SDL_Event event){check(SDL_PushEvent(&event),"SDL event injection failed");}
void require_sdl(bool success,const char *operation){if(!success)throw std::runtime_error(std::string(operation)+": "+SDL_GetError());}
[[nodiscard]] std::string utf8_path(const std::filesystem::path &path){const auto value=path.u8string();return {reinterpret_cast<const char *>(value.data()),value.size()};}
void capture_legacy_circles(const std::vector<stellar::native_map::Circle> &circles,const std::filesystem::path &capture){
  SDL_Window *window=SDL_CreateWindow("Stellar legacy circle reference",640,360,SDL_WINDOW_HIDDEN|SDL_WINDOW_HIGH_PIXEL_DENSITY);if(!window)throw std::runtime_error(std::string("Legacy circle reference window: ")+SDL_GetError());
  SDL_GPUDevice *device{};SDL_Renderer *renderer{};
  try{
    device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV,false,"vulkan");if(!device)throw std::runtime_error(std::string("Legacy circle reference GPU device: ")+SDL_GetError());
    renderer=SDL_CreateGPURenderer(device,window);if(!renderer)throw std::runtime_error(std::string("Legacy circle reference renderer: ")+SDL_GetError());
    require_sdl(SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND),"Legacy circle reference blend setup failed");
    require_sdl(SDL_SetRenderDrawColor(renderer,5,9,19,255),"Legacy circle reference clear color failed");require_sdl(SDL_RenderClear(renderer),"Legacy circle reference clear failed");
    for(const auto &circle:circles){
      constexpr int segments=20;std::vector<SDL_Vertex> vertices;std::vector<int> indices;vertices.reserve(segments+2);indices.reserve(segments*3);
      const SDL_FColor center_color{circle.color.r/255.f,circle.color.g/255.f,circle.color.b/255.f,circle.color.a/255.f};const SDL_FColor edge_color{center_color.r,center_color.g,center_color.b,0.f};
      vertices.push_back({{circle.center.x,circle.center.y},center_color,{0,0}});
      for(int index=0;index<=segments;++index){const auto angle=2.f*std::numbers::pi_v<float>*static_cast<float>(index)/static_cast<float>(segments);vertices.push_back({{circle.center.x+std::cos(angle)*circle.radius,circle.center.y+std::sin(angle)*circle.radius},edge_color,{0,0}});}
      for(int index=0;index<segments;++index){indices.push_back(0);indices.push_back(index+1);indices.push_back(index+2);}
      require_sdl(SDL_RenderGeometry(renderer,nullptr,vertices.data(),static_cast<int>(vertices.size()),indices.data(),static_cast<int>(indices.size())),"Legacy soft circle draw failed");
    }
    SDL_Surface *surface=SDL_RenderReadPixels(renderer,nullptr);if(!surface)throw std::runtime_error(std::string("Legacy circle reference readback: ")+SDL_GetError());
    const auto saved=SDL_SaveBMP(surface,utf8_path(capture).c_str());SDL_DestroySurface(surface);require_sdl(saved,"Legacy circle reference write failed");
  }catch(...){if(renderer)SDL_DestroyRenderer(renderer);if(device)SDL_DestroyGPUDevice(device);SDL_DestroyWindow(window);throw;}
  SDL_DestroyRenderer(renderer);SDL_DestroyGPUDevice(device);SDL_DestroyWindow(window);
}
class FixtureDirectory final {
 public:
  FixtureDirectory(){
    const auto seed=std::chrono::high_resolution_clock::now().time_since_epoch().count();
    for(int attempt=0;attempt<100;++attempt){
      path_=std::filesystem::temp_directory_path()/(L"stellar-native-image-"+std::to_wstring(seed)+L"-"+std::to_wstring(attempt));
      if(std::filesystem::create_directory(path_))return;
    }
    throw std::runtime_error("Could not create an isolated image test directory.");
  }
  ~FixtureDirectory(){std::error_code ignored;std::filesystem::remove(path_/L"source-测试.png",ignored);std::filesystem::remove(path_/L"corrupt-测试.png",ignored);std::filesystem::remove(path_/L"overlay-order.bmp",ignored);std::filesystem::remove(path_/L"circle-order.bmp",ignored);std::filesystem::remove(path_/L"legacy-circle-reference.bmp",ignored);std::filesystem::remove(path_,ignored);}
  [[nodiscard]] const std::filesystem::path &path()const noexcept{return path_;}
 private:std::filesystem::path path_;
};
}
int main(int argc,char **argv){
  using namespace stellar::native_map;
  try{
    if(argc!=4)throw std::invalid_argument("Usage: platform_input_tests <Rajdhani font> <JPEG image> <PNG image>");
    const auto jpeg=decode_rgba_image(argv[2]);const auto png=decode_rgba_image(argv[3]);check(jpeg->width()>0&&jpeg->height()>0&&jpeg->byte_size()<=maximum_rgba_image_bytes,"WIC JPEG decode did not return bounded RGBA pixels");check(png->width()>0&&png->height()>0&&png->byte_size()<=maximum_rgba_image_bytes,"WIC PNG decode did not return bounded RGBA pixels");
    FixtureDirectory fixtures;const auto unicode_image=fixtures.path()/L"source-测试.png";std::filesystem::copy_file(argv[3],unicode_image);const auto unicode_decoded=decode_rgba_image(unicode_image);check(unicode_decoded->width()==png->width()&&unicode_decoded->height()==png->height(),"WIC decoder did not preserve a Unicode native path");
    bool missing_rejected{};try{(void)decode_rgba_image(fixtures.path()/L"missing.png");}catch(const std::exception&){missing_rejected=true;}check(missing_rejected,"missing image was not rejected");
    const auto corrupt_path=fixtures.path()/L"corrupt-测试.png";{std::ofstream corrupt(corrupt_path,std::ios::binary);corrupt<<"not a PNG";}bool corrupt_rejected{};try{(void)decode_rgba_image(corrupt_path);}catch(const std::exception&){corrupt_rejected=true;}check(corrupt_rejected,"corrupt image was not rejected");
    bool dimensions_rejected{};try{(void)RgbaImage::create(maximum_rgba_image_dimension+1,1,{});}catch(const std::length_error&){dimensions_rejected=true;}check(dimensions_rejected,"oversized image dimension was not rejected before allocation");
    bool bytes_rejected{};try{(void)RgbaImage::create(maximum_rgba_image_dimension,2049,{});}catch(const std::length_error&){bytes_rejected=true;}check(bytes_rejected,"oversized decoded image was not rejected before allocation");
    bool storage_rejected{};try{(void)RgbaImage::create(2,2,{1,2,3});}catch(const std::invalid_argument&){storage_rejected=true;}check(storage_rejected,"mismatched RGBA storage was accepted");
    bool rejected_font{};try{Window rejected("Rejected font",320,180,false,"missing-font.ttf");}catch(const std::exception &){rejected_font=true;}check(rejected_font,"missing private font silently fell back");
    Window window("Stellar native input replay",640,360,false,argv[1]);
    (void)window.poll(); // Discard ordinary create/show notifications.
    SDL_Event down{};down.type=SDL_EVENT_MOUSE_BUTTON_DOWN;down.button.button=SDL_BUTTON_LEFT;down.button.clicks=2;down.button.x=20;down.button.y=30;push(down);
    SDL_Event move{};move.type=SDL_EVENT_MOUSE_MOTION;move.motion.x=180;move.motion.y=140;push(move);
    SDL_Event up{};up.type=SDL_EVENT_MOUSE_BUTTON_UP;up.button.button=SDL_BUTTON_LEFT;up.button.x=180;up.button.y=140;push(up);
    const auto ordered=window.poll();
    check(ordered.events.size()==3,"same-poll pointer sequence was collapsed");
    if(ordered.events.size()==3){check(ordered.events[0].type==InputEventType::LeftPressed,"press was not first");check(ordered.events[0].click_count==2,"SDL click count was not retained");check(ordered.events[1].type==InputEventType::PointerMove,"move was not second");check(ordered.events[2].type==InputEventType::LeftReleased,"release was not third");check(ordered.events[0].position.x!=ordered.events[2].position.x,"press position was replaced by release position");}
    SDL_Event right_down{};right_down.type=SDL_EVENT_MOUSE_BUTTON_DOWN;right_down.button.button=SDL_BUTTON_RIGHT;right_down.button.x=240;right_down.button.y=160;push(right_down);SDL_Event right_up{};right_up.type=SDL_EVENT_MOUSE_BUTTON_UP;right_up.button.button=SDL_BUTTON_RIGHT;right_up.button.x=240;right_up.button.y=160;push(right_up);const auto right_click=window.poll();check(right_click.events.size()==2,"right-click events were dropped");if(right_click.events.size()==2){check(right_click.events[0].type==InputEventType::RightPressed,"right press was not ordered");check(right_click.events[1].type==InputEventType::RightReleased,"right release was not ordered");}
    SDL_Event leave{};leave.type=SDL_EVENT_WINDOW_MOUSE_LEAVE;push(leave);
    const auto outside=window.poll();
    check(outside.pointer.x<0.f&&outside.pointer.y<0.f,"mouse leave retained stale hover coordinates");
    check(outside.events.size()==1&&outside.events.front().type==InputEventType::PointerCancelled,"mouse leave did not cancel hover");
    push(move);const auto returned=window.poll();
    check(returned.pointer.x>=0.f&&returned.pointer.y>=0.f,"pointer motion did not restore hover after returning");
    push(down);push(leave);push(move);push(up);const auto captured=window.poll();
    check(captured.events.size()==3&&captured.events.back().type==InputEventType::LeftReleased,"cross-window drag was cancelled by mouse leave");
    SDL_Event focus{};focus.type=SDL_EVENT_WINDOW_FOCUS_LOST;push(focus);
    const auto unfocused=window.poll();
    check(!unfocused.focused&&unfocused.pointer.x<0.f&&unfocused.pointer.y<0.f,"focus loss retained stale hover coordinates");
    push(down);push(focus);push(move);
    const auto cancelled=window.poll();
    check(cancelled.events.size()==3,"focus loss and hover motion were not preserved");
    if(cancelled.events.size()==3){check(cancelled.events[1].type==InputEventType::PointerCancelled,"focus loss did not emit cancellation");check(cancelled.events[2].type==InputEventType::PointerMove,"post-cancel hover motion was dropped");check(cancelled.pointer.x==cancelled.events[2].position.x,"snapshot did not retain final drawable pointer");}
    SDL_Event minimized{};minimized.type=SDL_EVENT_WINDOW_MINIMIZED;push(minimized);
    const auto inactive=window.poll();check(!inactive.renderable(),"minimized window remained renderable");
    check(inactive.pointer.x<0.f&&inactive.pointer.y<0.f,"minimize retained stale hover coordinates");
    SDL_Event restored{};restored.type=SDL_EVENT_WINDOW_RESTORED;push(restored);
    const auto active=window.poll();check(active.renderable(),"restored window did not become renderable");
    window.set_text_input(true);SDL_Event text_input{};text_input.type=SDL_EVENT_TEXT_INPUT;text_input.text.text="research";push(text_input);SDL_Event backspace{};backspace.type=SDL_EVENT_KEY_DOWN;backspace.key.key=SDLK_BACKSPACE;push(backspace);const auto editing=window.poll();check(editing.events.size()==2,"ordered text editing events were lost");if(editing.events.size()==2){check(editing.events[0].type==InputEventType::TextEntered&&editing.events[0].text=="research","UTF-8 text event was not preserved");check(editing.events[1].type==InputEventType::BackspacePressed,"Backspace event was not preserved");}window.set_text_input(false);
    DrawList repeated;repeated.overlay.emplace_back(FilledRectangle{{8,8,180,54},{8,18,34,240}});repeated.overlay.emplace_back(Text{{18,12},"CACHE STABLE TEXT WRAPS",{235,245,255,255},18,120.f,UiRect{12,10,150,44}});window.draw(repeated);const auto stable_entries=window.text_cache_entries(),stable_bytes=window.text_cache_bytes();for(int frame=0;frame<8;++frame)window.draw(repeated);check(window.text_cache_entries()==stable_entries&&window.text_cache_bytes()==stable_bytes,"identical frames rebuilt cached text resources");
    DrawList pressure;for(int index=0;index<700;++index)pressure.overlay.emplace_back(Text{{-200,-200},"bounded-cache-"+std::to_string(index),{255,255,255,255},14});window.draw(pressure);check(window.text_cache_entries()<=640,"text cache exceeded entry capacity");check(window.text_cache_bytes()<=32u*1024u*1024u,"text cache exceeded byte capacity");
    DrawList invalid;invalid.overlay.emplace_back(Text{{0,0},"invalid",{255,255,255,255},14,std::numeric_limits<float>::quiet_NaN()});bool rejected_bounds{};try{window.draw(invalid);}catch(const std::invalid_argument &){rejected_bounds=true;}check(rejected_bounds,"non-finite UI text bounds reached native conversion");
    DrawList offscreen;offscreen.lines.push_back({{-10000000.f,180.f},{10000000.f,180.f},{80,120,180,100}});offscreen.world.emplace_back(Line{{20000000.f,-5000000.f},{21000000.f,-6000000.f},{80,120,180,100}});offscreen.world.emplace_back(Circle{{10000000.f,10000000.f},5000.f,{120,160,220,100}});window.draw(offscreen);DrawList invalid_geometry;invalid_geometry.world.emplace_back(Line{{std::numeric_limits<float>::infinity(),0},{0,0},{255,255,255,255}});bool nonfinite_rejected{};try{window.draw(invalid_geometry);}catch(const std::invalid_argument&){nonfinite_rejected=true;}check(nonfinite_rejected,"non-finite ordered geometry was accepted");
    const auto rejects_circle=[&](float radius,const char *diagnostic){DrawList invalid_circle;invalid_circle.world.emplace_back(Circle{{0,0},radius,{255,255,255,255}});try{window.draw(invalid_circle);}catch(const std::invalid_argument &error){return std::string(error.what())==diagnostic;}return false;};
    check(rejects_circle(-1.f,"Circle bounds must be finite and nonnegative."),"negative circle radius did not retain its diagnostic");
    check(rejects_circle(std::numeric_limits<float>::quiet_NaN(),"Circle bounds must be finite and nonnegative."),"NaN circle radius did not retain its diagnostic");
    check(rejects_circle(std::numeric_limits<float>::infinity(),"Circle bounds must be finite and nonnegative."),"infinite circle radius did not retain its diagnostic");
    DrawList overflowing_circle;overflowing_circle.world.emplace_back(Circle{{std::numeric_limits<float>::max(),0},std::numeric_limits<float>::max(),{255,255,255,255}});bool overflow_rejected{};std::string overflow_error;try{window.draw(overflowing_circle);}catch(const std::invalid_argument &error){overflow_rejected=true;overflow_error=error.what();}check(overflow_rejected&&overflow_error=="Circle projection produced non-finite geometry.","overflowing circle projection did not retain its diagnostic");
    {
      // A portrait must follow its panel and precede later controls. Both
      // image layers share resource ownership, clipping and upload limits.
      const auto portrait=RgbaImage::create(1,1,{255,255,255,255});
      DrawList layers;
      layers.world.emplace_back(Image{portrait,{0,0,8,8}});
      layers.overlay.emplace_back(FilledRectangle{{0,0,160,160},{10,30,90,255}});
      layers.overlay.emplace_back(Image{portrait,{20,20,80,80},std::nullopt,
                                       {20,200,40,255},UiRect{40,40,40,40}});
      layers.overlay.emplace_back(FilledRectangle{{60,60,20,20},{220,180,20,255}});
      layers.overlay.emplace_back(FilledRectangle{{20,110,20,20},{200,20,40,255}});
      const auto before=window.image_upload_count();
      const auto capture=fixtures.path()/L"overlay-order.bmp";
      FrameTiming capture_timing{-1.,-1.,-1.,-1.};
      window.draw(layers,capture,&capture_timing);
      const auto valid_timing=[](double value){return std::isfinite(value)&&value>=0.;};
      check(valid_timing(capture_timing.submission_ms)&&valid_timing(capture_timing.readback_ms)&&valid_timing(capture_timing.throttle_ms)&&valid_timing(capture_timing.present_ms),"frame timing did not reset to finite nonnegative CPU phase values");
      check(capture_timing.readback_ms>0.,"screenshot frame did not record its readback/write phase");
      window.draw(layers,std::nullopt,&capture_timing);
      check(capture_timing.readback_ms==0.,"frame timing retained screenshot readback on a subsequent ordinary draw");
      check(window.image_upload_count()==before+1,"world/UI layers duplicated image uploads");
      window.draw(layers);
      check(window.image_upload_count()==before+1,"stable UI portrait uploaded again");
      const auto pixels=decode_rgba_image(capture);
      const auto matches=[&](int x,int y,int red,int green,int blue){
        const auto offset=(static_cast<std::size_t>(y)*pixels->width()+x)*4u;
        const auto &rgba=pixels->pixels();
        return rgba[offset]==red&&rgba[offset+1]==green&&rgba[offset+2]==blue;
      };
      check(matches(50,50,20,200,40),"UI image was hidden behind its panel or lost tint");
      check(matches(30,30,10,30,90)&&matches(90,90,10,30,90),"UI image escaped its clip");
      check(matches(65,65,220,180,20),"UI image covered a later control");
      check(matches(25,115,200,20,40),"UI image clip leaked into a later control");
      DrawList invalid_overlay;
      invalid_overlay.overlay.emplace_back(Image{portrait,{0,0,20,20},UiRect{0,0,2,2}});
      bool invalid_overlay_rejected{};
      try{window.draw(invalid_overlay);}catch(const std::invalid_argument&){invalid_overlay_rejected=true;}
      check(invalid_overlay_rejected,"UI image bypassed source-bound validation");
    }
    {
      // Compare every pixel from overlapping translucent circles with the
      // pre-migration fan generated on the same SDL Vulkan renderer.
      const std::vector<Circle> legacy_circles{{{64,64},36.f,{220,40,30,180}},{{76,64},24.f,{30,210,70,160}},{{70,76},18.f,{40,100,240,200}}};
      DrawList native_circles;for(const auto &circle:legacy_circles)native_circles.world.emplace_back(circle);
      const auto native_capture=fixtures.path()/L"circle-order.bmp",legacy_capture=fixtures.path()/L"legacy-circle-reference.bmp";
      window.draw(native_circles,native_capture);capture_legacy_circles(legacy_circles,legacy_capture);
      const auto native_pixels=decode_rgba_image(native_capture),legacy_pixels=decode_rgba_image(legacy_capture);
      check(native_pixels->width()==legacy_pixels->width()&&native_pixels->height()==legacy_pixels->height()&&native_pixels->pixels()==legacy_pixels->pixels(),"cached soft-circle fan diverged from legacy 20-segment geometry");
    }
    {
      // Ordered world image/circles must still precede the text-bearing UI.
      const auto swatch=RgbaImage::create(1,1,{20,40,80,255});
      DrawList circles;
      circles.world.emplace_back(Image{swatch,{0,0,140,140}});
      circles.world.emplace_back(Circle{{44,64},36.f,{220,40,30,180}});
      circles.world.emplace_back(Circle{{76,64},24.f,{30,210,70,160}});
      circles.overlay.emplace_back(FilledRectangle{{60,60,20,20},{230,190,30,255}});
      circles.overlay.emplace_back(Text{{66,60},"I",{255,255,255,255},18});
      const auto capture=fixtures.path()/L"circle-order.bmp";
      window.draw(circles,capture);
      const auto pixels=decode_rgba_image(capture);
      const auto matches=[&](int x,int y,int red,int green,int blue){const auto offset=(static_cast<std::size_t>(y)*pixels->width()+x)*4u;const auto &rgba=pixels->pixels();return rgba[offset]==red&&rgba[offset+1]==green&&rgba[offset+2]==blue;};
      check(matches(12,12,20,40,80),"circle fan changed the preceding image layer");
      check(matches(64,64,230,190,30),"UI overlay was not drawn after circles");
      bool text_over_panel{};for(int y=60;y<80;++y)for(int x=60;x<80;++x)if(!matches(x,y,230,190,30))text_over_panel=true;
      check(text_over_panel,"text layer was skipped after circle rendering");
    }

    {
      const auto texture=RgbaImage::create(1,1,{255,255,255,255});
      TriangleMesh mesh{{{10,10},{110,10},{110,110},{10,110}},{0,1,2,0,2,3},{255,255,255,255},UiRect{30,30,60,60}};
      mesh.texture=texture;mesh.texture_coordinates={{0,0},{1,0},{1,1},{0,1}};mesh.vertex_colors.assign(4,{40,180,220,255});
      DrawList draw;draw.overlay.emplace_back(FilledRectangle{{0,0,120,120},{3,7,11,255}});
      // Prior image tint must not bleed into the globe's per-vertex lighting.
      draw.overlay.emplace_back(Image{texture,{0,0,5,5},std::nullopt,{255,0,0,40}});draw.overlay.emplace_back(mesh);
      const auto before=window.image_upload_count();const auto capture=fixtures.path()/L"textured-mesh.bmp";
      window.draw(draw,capture);window.draw(draw);check(window.image_upload_count()==before+1,"Textured mesh re-uploaded stable globe texture");
      const auto rgba=decode_rgba_image(capture);const auto channel=[&](int x,int y,int c){return rgba->pixels()[(y*rgba->width()+x)*4+c];};
      check(channel(50,50,0)==40&&channel(50,50,1)==180&&channel(50,50,2)==220,"Textured mesh UV/tint rendering failed");
      check(channel(20,20,0)==3&&channel(100,100,2)==11,"Textured globe mesh escaped its clip");
    }
    auto first_image=RgbaImage::create(2,2,std::vector<std::uint8_t>(16,255));std::weak_ptr<const RgbaImage> first_weak=first_image;DrawList image_frame;image_frame.world.emplace_back(Image{first_image,{24,24,48,48}});window.draw(image_frame);const auto first_uploads=window.image_upload_count();window.draw(image_frame);check(window.image_upload_count()==first_uploads,"identical image resource was uploaded more than once");image_frame.world.clear();first_image.reset();check(!first_weak.expired(),"texture cache did not retain strong immutable image ownership");
    auto never_uploaded=RgbaImage::create(2,2,std::vector<std::uint8_t>(16,128));DrawList invalid_image;invalid_image.world.emplace_back(Image{never_uploaded,{20,20,20,20},UiRect{1,1,4,4}});const auto uploads_before_invalid=window.image_upload_count();bool invalid_source_rejected{};try{window.draw(invalid_image);}catch(const std::invalid_argument&){invalid_source_rejected=true;}check(invalid_source_rejected&&window.image_upload_count()==uploads_before_invalid,"invalid source bounds uploaded or rendered a resource");
    DrawList image_pressure;for(std::size_t index=0;index<maximum_image_cache_entries;++index){auto pixels=std::vector<std::uint8_t>(16,static_cast<std::uint8_t>(index));pixels[3]=255;pixels[7]=255;pixels[11]=255;pixels[15]=255;image_pressure.world.emplace_back(Image{RgbaImage::create(2,2,std::move(pixels)),{-100,-100,2,2}});}window.draw(image_pressure);image_pressure.world.clear();check(first_weak.expired(),"evicted image cache entry retained its resource");check(window.image_cache_entries()<=maximum_image_cache_entries,"image cache exceeded entry capacity");check(window.image_cache_resident_bytes()<=maximum_image_cache_resident_bytes,"image cache exceeded resident byte capacity");
    constexpr int pressure_side=2048;std::shared_ptr<const RgbaImage> first_large;std::weak_ptr<const RgbaImage> first_large_weak;DrawList byte_pressure;for(int index=0;index<7;++index){auto pixels=std::vector<std::uint8_t>(static_cast<std::size_t>(pressure_side)*pressure_side*4u,static_cast<std::uint8_t>(index));auto resource=RgbaImage::create(pressure_side,pressure_side,std::move(pixels));if(index==0){first_large=resource;first_large_weak=resource;}byte_pressure.world.emplace_back(Image{std::move(resource),{-300,-300,2,2}});}window.draw(byte_pressure);byte_pressure.world.clear();first_large.reset();check(first_large_weak.expired(),"image byte-pressure did not evict the oldest large resource");check(window.image_cache_entries()<maximum_image_cache_entries,"byte-pressure test reached only the entry ceiling");check(window.image_cache_resident_bytes()<=maximum_image_cache_resident_bytes,"image cache exceeded the 192 MiB resident ceiling");
  }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
  if(failures==0)std::cout<<"Native ordered input, WIC image limits, ordered rendering ownership and bounded caches passed\n";
  return failures==0?0:1;
}
