#include "map_camera.hpp"
#include "map_interaction.hpp"
#include <cmath>
#include <iostream>
#include <limits>
int main(){
  using namespace stellar::native_map;
  int failures=0;
  const auto check=[&](bool passed,const char *message){if(!passed){std::cerr<<message<<'\n';++failures;}};
  Camera camera{{10,20},2};
  const Point pointer{320,180};
  const auto world=camera.unproject(pointer,640,360);
  camera.zoom_at(3,pointer,640,360);
  const auto kept=camera.unproject(pointer,640,360);
  check(std::abs(world.x-kept.x)<.0001f&&std::abs(world.y-kept.y)<.0001f,"zoom did not preserve pointer anchor");
  camera.pan_pixels(20,-10);
  check(camera.center.x<10&&camera.center.y>20,"pixel pan moved camera in the wrong direction");
  const auto prior=camera.pixels_per_world;
  camera.zoom_at(std::numeric_limits<float>::quiet_NaN(),pointer,640,360);
  check(camera.pixels_per_world==prior,"non-finite zoom changed scale");
  camera.zoom_at(100,pointer,640,360);
  check(camera.pixels_per_world==100.f,"maximum zoom clamp failed");
  camera.zoom_at(-100,pointer,640,360);
  check(camera.pixels_per_world==.01,"minimum zoom clamp failed");
  for(const int height:{720,1080,1440,2160}){
    const int width=height*16/9;
    const Camera overview{{145.,-71.},height/1000.};
    auto view=overview;
    view.pan_pixels(900,-600);view.constrain_to_overview(overview,width,height);
    check(view.center.x==overview.center.x&&view.center.y==overview.center.y,"overview can be dragged off center");
    view.zoom_at(-20,{300,200},width,height);view.constrain_to_overview(overview,width,height);
    check(view.pixels_per_world==overview.pixels_per_world&&view.center.x==overview.center.x,"overview shrank below fitted galaxy");
    view.zoom_at(10,{width*.5f,height*.5f},width,height);view.constrain_to_overview(overview,width,height);
    const WorldPoint star{150.,-40.};const auto before=view.project(star,width,height);
    view.pan_pixels(90,-50);view.constrain_to_overview(overview,width,height);
    const auto after=view.project(star,width,height);
    check(std::abs(after.x-before.x-90)<.001&&std::abs(after.y-before.y+50)<.001,"zoomed drag did not pan through world coordinates");
    view.pan_pixels(100000,100000);view.constrain_to_overview(overview,width,height);
    const auto corner=view.unproject({0,0},width,height),bound=overview.unproject({0,0},width,height);
    check(corner.x>=bound.x-.0001&&corner.y>=bound.y-.0001,"pan escaped map navigation bounds");
  }
  PointerGesture gesture;
  gesture.begin(false);gesture.move({1,1});
  check(gesture.release_as_world_click(),"small pointer jitter was not a click");
  gesture.begin(false);gesture.move({4,0});
  check(!gesture.release_as_world_click(),"drag was incorrectly accepted as a click");
  // Same-poll UI press -> move outside -> release remains UI-owned.
  gesture.begin(true);gesture.move({40,20});
  check(gesture.captured_by_ui()&&!gesture.allows_world_drag(),"UI capture did not persist during drag");
  check(!gesture.release_as_world_click(),"same-frame UI gesture leaked to the world");
  // The inverse starts world-owned; moving over UI cannot transfer ownership.
  gesture.begin(false);gesture.move({40,20});
  check(!gesture.captured_by_ui()&&gesture.allows_world_drag(),"world gesture changed ownership over UI");
  check(!gesture.release_as_world_click(),"world drag ending over UI became a click");
  gesture.begin(false);gesture.move({8,0});gesture.cancel();
  check(!gesture.release_as_world_click(),"focus-loss cancellation left a live gesture");
  InputSnapshot activity;activity.drawable_width=1280;activity.drawable_height=720;
  check(activity.renderable(),"active drawable was rejected");activity.minimized=true;
  check(!activity.renderable(),"minimized drawable remained renderable");
  return failures==0?0:1;
}
