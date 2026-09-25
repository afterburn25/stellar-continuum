#include <stellar/engine/memory_tracker.hpp>
#include <stellar/engine/native_scene3d.hpp>
#include <stellar/engine/native_geometry3d.hpp>
#include <stellar/engine/surface_attachment.hpp>
#include <stellar/engine/texture_compression.hpp>
#include <stellar/engine/texture_cook.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace stellar::native_map;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::shared_ptr<const Mesh3D> quad(float left,float right){
  return Mesh3D::create({{{-.9f,-.9f,left},{0,0,1},{0,1}},{{.9f,-.9f,right},{0,0,1},{1,1}},{{.9f,.9f,right},{0,0,1},{1,0}},{{-.9f,.9f,left},{0,0,1},{0,0}}},{0,1,2,0,2,3});
}
int main(int argc,char** argv)try{
  // The thin ring chain approaches twice the base, unlike square images.
  for(const auto& test:std::vector<std::array<int,4>>{{1,1,1,4},{7,5,3,168},{256,1,9,2044},{1,256,9,2044},{1024,512,11,2796204}}){
    const auto image=RgbaImage::create(test[0],test[1],std::vector<std::uint8_t>(static_cast<std::size_t>(test[0])*test[1]*4));
    const auto layout=texture_mip_layout3d(image.get());
    check(layout.levels==static_cast<std::uint32_t>(test[2])&&layout.gpu_bytes==static_cast<std::size_t>(test[3])&&layout.resident_bytes==image->byte_size()+layout.gpu_bytes,"Incorrect mip dimensions or resident texture charge");
  }
  check(texture_mip_layout3d(nullptr).levels==1&&texture_mip_layout3d(nullptr).resident_bytes==8,"White fallback has an incorrect texture charge");
  if(argc!=3)throw std::invalid_argument("Usage: scene3d_gpu_tests FONT OUTPUT_DIR");
  const std::filesystem::path folder=argv[2];std::filesystem::create_directories(folder);
  Window window("Stellar 3D renderer validation",640,360,false,argv[1]);window.set_vsync(0);window.set_frame_cap(0);
  const auto input=window.poll();check(input.drawable_width==640&&input.drawable_height==360,"3D test requires a 640 by 360 drawable");
  Camera3D camera;camera.projection=Projection3D::Orthographic;camera.orthographic_height=2;
  Material3D red;red.tint={230,30,20,255};red.ambient=1;red.diffuse=0;
  Material3D green=red;green.tint={20,210,40,255};
  MeshInstance3D a{quad(.5f,-.5f),{},{},1,red},b{quad(0,0),{},{},1,green};
  const auto capture=[&](std::vector<MeshInstance3D> objects,const char* name){
    DrawList list;list.world.emplace_back(Scene3DView{Scene3D::create(camera,std::move(objects)),{0,0,320,320}});
    list.overlay.emplace_back(FilledRectangle{{20,20,30,30},{40,50,240,255}});
    auto path=folder/name;window.draw(list,path);return decode_rgba_image(path);
  };
  const auto first=capture({a,b},"depth-forward.png"),second=capture({b,a},"depth-reverse.png");
  check(first->pixels()==second->pixels(),"3D occlusion depends on triangle submission order");
  const auto channel=[](const RgbaImage& p,int x,int y,int c){return p.pixels()[(static_cast<std::size_t>(y)*p.width()+x)*4+c];};
  check(channel(*first,80,160,0)>220&&channel(*first,240,160,1)>200,"Intersecting meshes failed the depth test");
  check(channel(*first,30,30,2)==240,"3D viewport covered a later 2D overlay");
  check(channel(*first,400,160,0)==5,"3D viewport escaped its rectangle");
  { // Window::draw attributes the 3D backend's VRAM residency to MemoryTracker.
    const auto snapshot=stellar::engine::MemoryTracker::instance().snapshot();
    const auto find=[&](std::string_view name){return std::find_if(snapshot.subsystems.begin(),snapshot.subsystems.end(),[&](const auto& s){return s.name==name;});};
    const auto residency=window.scene3d_statistics();
    const auto tex=find("scene3d-textures"),meshes=find("scene3d-meshes"),targets=find("scene3d-targets");
    check(tex!=snapshot.subsystems.end()&&meshes!=snapshot.subsystems.end()&&targets!=snapshot.subsystems.end(),"3D renderer VRAM is not attributed to MemoryTracker subsystems");
    check(tex->current_bytes==residency.texture_cache_bytes&&meshes->current_bytes==residency.mesh_cache_bytes&&targets->current_bytes==residency.target_bytes,"Attributed VRAM bytes do not match renderer residency");
    check(find("ui-image-cache")!=snapshot.subsystems.end()&&find("ui-text-cache")!=snapshot.subsystems.end(),"2D image/text caches are not attributed to MemoryTracker subsystems");
  }
  {
    std::vector<std::uint8_t> pixels(64*64*4);
    for(int y=0;y<64;++y)for(int x=0;x<64;++x){const auto i=(y*64+x)*4;pixels[i]=x<32?255:0;pixels[i+1]=x>=32?255:0;pixels[i+3]=255;}
    const auto rgba=RgbaImage::create(64,64,pixels),bc=compress_opaque_texture(*rgba);
    check(bc->bc1_mips().size()==7&&bc->bc1_mips().back().blocks.size()==8,"Compressed mip tails missing");
    auto compressed=a;compressed.mesh=quad(0,0);compressed.material.tint={255,255,255,255};compressed.material.texture=bc;
    const auto uploads=window.scene3d_statistics().texture_uploads,bytes=window.scene3d_statistics().texture_cache_bytes;
    const auto frame=capture({compressed},"bc1-base.png");
    check(channel(*frame,80,160,0)>240&&channel(*frame,240,160,1)>240,"Compressed channel or block upload is corrupt");
    check(window.scene3d_statistics().texture_uploads==uploads+1,"Compressed texture upload missing");
    const auto charged=window.scene3d_statistics().texture_cache_bytes-bytes;
    std::size_t packed=0;for(const auto& m:bc->bc1_mips())packed+=m.blocks.size();
    check(charged==bc->byte_size()+packed||charged==texture_mip_layout3d(bc.get()).resident_bytes,"Compressed/fallback cache charge invalid");
    compressed.scale=.1f;const auto small=capture({compressed},"bc1-mips.png");
    check(channel(*small,154,160,0)>180&&channel(*small,166,160,1)>180,"Compressed minified mip levels are blank");
    check(window.scene3d_statistics().texture_uploads==uploads+1,"Compressed texture reuploaded on minification");
    bool invalid=false;auto malformed=bc->bc1_mips();malformed.pop_back();try{(void)RgbaImage::create(64,64,pixels,malformed);}catch(const std::invalid_argument&){invalid=true;}
    check(invalid,"Incomplete compressed mip chain accepted");
    std::cout<<"bc1_mips=7 resident_bytes="<<charged<<" rgba_fallback_bytes="<<texture_mip_layout3d(bc.get()).resident_bytes<<'\n';
  }
  { // Cooked BC7 alpha/color and BC5 normal maps use the production GPU path.
    std::vector<std::uint8_t> color(64*64*4),normal(color.size());
    for(int y=0;y<64;++y)for(int x=0;x<64;++x){const auto i=(y*64+x)*4;
      color[i]=x<32?224:24;color[i+1]=x<32?40:208;color[i+2]=64;color[i+3]=x<32?255:128;
      normal[i]=x<32?160:96;normal[i+1]=128;normal[i+2]=251;normal[i+3]=255;
    }
    auto source=RgbaImage::create(64,64,color),normals=RgbaImage::create(64,64,normal);
    auto color_cook=cook_texture(*source,"critical"),normal_cook=cook_texture(*normals,"normal");
    check(color_cook.format==TextureFormat::Bc7&&normal_cook.format==TextureFormat::Bc5,"GPU codec fixtures unexpectedly fell back");
    auto object=a;object.mesh=quad(0,0);object.material.tint={255,255,255,255};object.material.transparent=true;object.material.texture=source;
    const auto reference=capture({object},"bc7-source.png");object.material.texture=RgbaImage::create_cooked(color_cook.format,color_cook.mips);
    const auto packed=capture({object},"bc7-cooked.png");
    for(const int x:{80,240})for(int c=0;c<3;++c)check(std::abs(int(channel(*reference,x,160,c))-channel(*packed,x,160,c))<=3,"Cooked GPU color/alpha differs from source");
    object.material.ambient=.18f;object.material.diffuse=.82f;object.material.surface_response=SurfaceResponse3D{};object.material.surface_response->normal=normals;object.material.surface_response->properties=RgbaImage::create(1,1,{255,0,0,128});object.material.surface_response->normal_strength=.7f;
    const auto normal_reference=capture({object},"bc5-source.png");object.material.surface_response->normal=RgbaImage::create_cooked(normal_cook.format,normal_cook.mips);
    const auto normal_packed=capture({object},"bc5-cooked.png");
    for(const int x:{80,240})for(int c=0;c<3;++c)check(std::abs(int(channel(*normal_reference,x,160,c))-channel(*normal_packed,x,160,c))<=3,"Cooked GPU normal map differs from source");
    object.scale=.1f;const auto tiny=capture({object},"bc7-cooked-mips.png");check(channel(*tiny,154,160,0)>40,"Cooked mip tail failed on GPU");
  }
  { // Magnified star cores must stay crisp without halo ringing. Minification
    // must retain the existing mip sampler instead of aliasing fine features.
    std::vector<std::uint8_t> stars(64*64*4,255);
    for(int y=0;y<64;++y)for(int x=0;x<64;++x)for(int c=0;c<3;++c)stars[(y*64+x)*4+c]=(x%8==3&&y%8==3)?240:3;
    auto sky=a;sky.mesh=quad(0,0);sky.material.tint={255,255,255,255};sky.material.texture=RgbaImage::create(64,64,std::move(stars));
    const auto soft=capture({sky},"star-cores-linear.png");sky.material.cubic_magnification=true;
    const auto crisp=capture({sky},"star-cores-cubic.png");
    const auto energy=[&](const auto& image){double e=0;for(int y=60;y<260;++y)for(int x=60;x<260;++x){double dx=int(channel(image,x,y,0))-int(channel(image,x+1,y,0)),dy=int(channel(image,x,y,0))-int(channel(image,x,y+1,0));e+=dx*dx+dy*dy;}return e;};
    check(energy(*crisp)>energy(*soft)*1.08,"Cubic magnification did not preserve star-core contrast");
    for(int y=60;y<260;++y)for(int x=60;x<260;++x)check(channel(*crisp,x,y,0)>=3&&channel(*crisp,x,y,0)<=240,"Cubic star filtering rang beyond source values");
    sky.scale=.1f;const auto small=capture({sky},"star-cores-small-cubic.png");sky.material.cubic_magnification=false;
    const auto reference=capture({sky},"star-cores-small-linear.png");check(small->pixels()==reference->pixels(),"Minification bypassed the mip sampler");
    std::cout<<"star_core_gradient_ratio="<<energy(*crisp)/energy(*soft)<<'\n';
  }
  {auto lit=a;lit.mesh=quad(0,0);lit.material.tint={255,255,255,255};lit.material.ambient=.25f;lit.material.diffuse=0;lit.material.linear_light=true;
    const auto linear=capture({lit},"linear-light.png");lit.material.linear_light=false;const auto legacy=capture({lit},"legacy-light.png");
    check(channel(*linear,160,160,0)>130&&channel(*linear,160,160,0)<142&&channel(*legacy,160,160,0)<70,"Planet lighting multiplied sRGB values instead of linear light");}
  {auto lit=a;lit.mesh=quad(0,0);lit.material.tint={255,255,255,255};lit.material.linear_light=true;
    lit.material.ambient=0;lit.material.diffuse=1;lit.material.light_intensity=0;
    lit.material.additional_lights[0]={{0,0,1},{1,0,0},.4f};
    const auto binary=capture({lit},"binary-light.png");
    check(channel(*binary,160,160,0)>100&&channel(*binary,160,160,1)<5,"Secondary stellar light did not illuminate the surface");
    lit.material.additional_lights[1]={{0,0,1},{0,0,1},.3f};
    const auto triple=capture({lit},"triple-light.png");
    check(channel(*triple,160,160,0)>100&&channel(*triple,160,160,2)>100,"Third stellar spectrum was lost");
    lit.material.additional_lights[0].direction={0,0,-1};
    const auto backside=capture({lit},"secondary-behind.png");
    check(channel(*backside,160,160,0)<5&&channel(*backside,160,160,2)>100,"Secondary light ignored its actual direction");
  }
  (void)capture({a,b},"depth-restored.png");
  const auto uploaded=window.scene3d_statistics();
  DrawList stable;stable.world.emplace_back(Scene3DView{Scene3D::create(camera,{a,b}),{0,0,320,320}});
  for(int i=0;i<20;++i)window.draw(stable);
  check(window.scene3d_statistics().mesh_uploads==uploaded.mesh_uploads&&window.scene3d_statistics().texture_uploads==uploaded.texture_uploads,"Stable 3D resources were uploaded again");
  auto transparent=a;transparent.position.z=1;transparent.material.transparent=true;transparent.material.opacity=.5f;
  const auto blended=capture({b,transparent},"transparent.png");
  check(std::abs(channel(*blended,160,160,0)-125)<=2&&std::abs(channel(*blended,160,160,1)-120)<=2,"Premultiplied transparency is incorrect");
  // A second scene must have its own target even before the 2D renderer submits.
  DrawList multi;multi.overlay.emplace_back(Scene3DView{Scene3D::create(camera,{a}),{0,0,320,320}});
  multi.overlay.emplace_back(Scene3DView{Scene3D::create(camera,{b}),{320,0,320,320}});
  window.draw(multi,folder/"two-views.png");const auto both=decode_rgba_image(folder/"two-views.png");
  check(channel(*both,160,160,0)>220&&channel(*both,480,160,1)>200,"3D views overwrote one another");
  auto offscreen=a;offscreen.position.x=1e12;const auto before_cull=window.scene3d_statistics().mesh_uploads;
  (void)capture({offscreen},"culled.png");check(window.scene3d_statistics().culled_instances==1&&window.scene3d_statistics().draw_calls==0&&window.scene3d_statistics().mesh_uploads==before_cull,"Offscreen geometry was submitted or uploaded");
  // Near-plane rejection and correct perspective size/depth are exercised on GPU.
  camera.projection=Projection3D::Perspective;auto behind=a;behind.position.z=6;
  (void)capture({behind},"behind.png");check(window.scene3d_statistics().draw_calls==0,"Behind-camera object was drawn");
  {
    // Identical mesh+material draws merge into a single instanced call whose
    // per-instance state comes from the storage buffers.
    camera.projection=Projection3D::Orthographic;
    auto left=a,right=a;left.position.x=-.55f;right.position.x=.55f;
    const auto instanced=capture({left,right},"instanced.png");
    check(window.scene3d_statistics().draw_calls==1,"Identical 3D objects were not batched into one instanced draw");
    check(channel(*instanced,40,160,0)>200&&channel(*instanced,280,160,0)>200,"Instanced copies did not each apply their own transform");
  }
  auto textured=b;textured.material.tint={255,255,255,255};textured.material.texture=RgbaImage::create(2,2,{255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255});
  camera.projection=Projection3D::Orthographic;const auto uv=capture({textured},"uv.png");
  check(channel(*uv,40,80,0)>200&&channel(*uv,280,80,1)>200&&channel(*uv,40,280,2)>200,"3D texture coordinates are flipped or ignored");
  {
    // Texture streaming under a tight byte budget: at this footprint the
    // ~5.6MB chains demand only their ~87KB mip-3 tails, so the budget is
    // sized to degrade the second texture to a coarser tail rather than
    // evict outright.
    window.set_scene3d_texture_budget(100000u);
    const auto big=[&](std::uint8_t shade){
      std::vector<std::uint8_t> pixels(1024u*1024u*4u);
      for(std::size_t i=0;i<pixels.size();i+=4){pixels[i]=pixels[i+1]=pixels[i+2]=shade;pixels[i+3]=255;}
      return RgbaImage::create(1024,1024,std::move(pixels));};
    Material3D dark_material=green;dark_material.tint={255,255,255,255};dark_material.texture=big(0);
    Material3D light_material=dark_material;light_material.texture=big(220);
    MeshInstance3D dark{quad(0,0),{-.55f,0,0},{},.3f,dark_material},light{quad(0,0),{.55f,0,0},{},.3f,light_material};
    const auto stream_base=window.scene3d_statistics().streamed_fallbacks;
    (void)capture({dark},"stream-dark.png");(void)capture({light},"stream-light.png");
    check(window.scene3d_statistics().texture_cache_entries<=2,"Texture streamer did not evict the unrequested texture under budget");
    const auto uploads=window.scene3d_statistics().texture_uploads;
    const auto pair=capture({dark,light},"stream-both.png");
    // Equal distances tie; the earlier-registered texture wins its mip-3
    // tail (~87KB), leaving ~12KB — the second degrades to its ~5KB mip-5
    // tail and still binds its own 220 texels rather than the white fallback.
    check(window.scene3d_statistics().streamed_partial_binds>0,"Streamer did not admit a degraded mip tail under budget pressure");
    check(window.scene3d_statistics().texture_uploads>uploads,"Evicted texture was not re-uploaded on re-admission");
    check(channel(*pair,72,160,0)<30&&channel(*pair,248,160,0)>150&&channel(*pair,248,160,0)<235,"Streaming degraded-bind pixels are wrong");
    // A zero budget denies even the smallest tail: the pinned white fallback
    // serves the bind, keeping the pop-in path reachable.
    window.set_scene3d_texture_budget(0);
    (void)capture({dark},"stream-denied.png");
    check(window.scene3d_statistics().streamed_fallbacks>stream_base,"Fully denied texture did not fall back to the pinned texture");
    window.set_scene3d_texture_budget(maximum_scene3d_texture_cache_bytes);
  }
  {
    std::vector<std::uint8_t> checks(1024u*1024u*4u,255);
    for(int y=0;y<1024;++y)for(int x=0;x<1024;++x)for(int c=0;c<3;++c)
      checks[(static_cast<std::size_t>(y)*1024+x)*4+c]=((x+y)%2)?255:0;
    auto fine=textured;fine.scale=.23f;fine.material.texture=RgbaImage::create(1024,1024,std::move(checks));
    const auto before=window.scene3d_statistics();
    const auto small=capture({fine},"mip-checker.png");const auto charged=window.scene3d_statistics();
    // At this footprint the checker demands its mip-3 tail: resident GPU
    // bytes are the 128..1 tail levels, not the full chain.
    std::size_t tail3=0;for(int w=128,h=128;;){tail3+=static_cast<std::size_t>(w)*h*4;if(w==1&&h==1)break;w=std::max(1,w/2);h=std::max(1,h/2);}
    check(charged.texture_cache_bytes-before.texture_cache_bytes+charged.streamed_evicted_bytes-before.streamed_evicted_bytes==fine.material.texture->byte_size()+tail3,"GPU cache did not account for the resident mip tail");
    fine.position.x=.00175;
    const auto moved=capture({fine},"mip-checker-moved.png");
    for(int y=140;y<180;++y)for(int x=140;x<180;++x){
      check(std::abs(channel(*small,x,y,0)-128)<=2,"Minified checker failed to converge to its area average");
      check(std::abs(channel(*small,x,y,0)-channel(*moved,x,y,0))<=2,"Subpixel motion made fine texture shimmer");
    }
    check(window.scene3d_statistics().texture_uploads==charged.texture_uploads,"Motion regenerated cached mip textures");
    // Thin radial opacity maps must average alpha, retain wide gaps and share
    // the same GPU resource when used for a shadow in an additional view.
    std::vector<std::uint8_t> bands(1024*4,255);
    for(int x=0;x<1024;++x)bands[x*4+3]=(x>=384&&x<640)?0:((x%2)?255:0);
    fine.position={};fine.material.texture=RgbaImage::create(1024,1,std::move(bands));fine.material.transparent=true;
    const auto ring=capture({fine},"mip-ring-alpha.png");
    check(channel(*ring,160,160,0)==5&&std::abs(channel(*ring,138,160,0)-130)<=3,"Thin mip texture lost ring gap or mean opacity");
    const auto uploads_before_promotion=window.scene3d_statistics().texture_uploads;
    fine.scale=1;const auto close_ring=capture({fine},"mip-ring-close.png");
    check(window.scene3d_statistics().texture_uploads==uploads_before_promotion+1,"Footprint growth did not promote the resident mip tail");
    check(channel(*close_ring,160,160,0)==5,"Magnification filled a transparent ring gap");
    // The 160px views demand a coarser mip than the 360px capture did —
    // warm the resident tail at their footprint so the two-view draw below
    // shares it without re-uploading.
    DrawList warm;warm.world.emplace_back(Scene3DView{Scene3D::create(camera,{fine}),{0,0,160,160}});window.draw(warm);
    const auto retained=window.scene3d_statistics();
    auto shadow=fine;shadow.material.texture.reset();shadow.material.shadow=AnalyticShadow3D{};shadow.material.shadow->shape=AnalyticShadowShape3D::Annulus;shadow.material.shadow->opacity_map=fine.material.texture;
    DrawList shared;shared.world.emplace_back(Scene3DView{Scene3D::create(camera,{fine}),{0,0,160,160}});
    shared.world.emplace_back(Scene3DView{Scene3D::create(camera,{shadow}),{160,0,160,160}});window.draw(shared);
    check(window.scene3d_statistics().texture_uploads==retained.texture_uploads&&window.scene3d_statistics().texture_cache_bytes==retained.texture_cache_bytes,"Shared surface/shadow mip texture was charged or uploaded twice");
    // Rectangular tails and vertical 1D maps must initialize every sampled level.
    for(const auto dimensions:std::vector<std::array<int,2>>{{1,1024},{511,7}}){
      std::vector<std::uint8_t> pixels(static_cast<std::size_t>(dimensions[0])*dimensions[1]*4,255);
      for(std::size_t n=0;n<pixels.size();n+=4){pixels[n]=80;pixels[n+1]=140;pixels[n+2]=200;}
      auto thin=textured;thin.scale=.1f;thin.material.texture=RgbaImage::create(dimensions[0],dimensions[1],std::move(pixels));
      const auto name="mip-tail-"+std::to_string(dimensions[0])+".png";const auto view=capture({thin},name.c_str());
      for(int x=150;x<170;++x)for(int c=0;c<3;++c)check(std::abs(channel(*view,x,160,c)-(80+c*60))<=2,"Rectangular/1D mip tail contains uninitialized pixels");
    }
  }
  std::cout<<"texture_mips=area_average_subpixel_motion_thin_alpha_gap_residency_reuse_passed\n";
  {
    // A polar ring map has unequal radial/angular texel footprints even before
    // tilt. Isotropic LOD must not erase resolvable bands on the shorter axis.
    std::vector<std::uint8_t> pixels(256u*4096u*4u,255);
    for(int y=0;y<4096;++y)for(int x=0;x<256;++x)for(int c=0;c<3;++c)
      pixels[(static_cast<std::size_t>(y)*256+x)*4+c]=(x/4)%2?230:30;
    auto bands=textured;bands.material.texture=RgbaImage::create(256,4096,std::move(pixels));
    const auto isotropic=capture({bands},"ring-isotropic.png");
    const auto entries=window.scene3d_statistics().texture_cache_entries;
    bands.material.anisotropic_texture=true;
    const auto detailed=capture({bands},"ring-anisotropic.png");
    const auto contrast=[&](const RgbaImage& im){double deviation=0;for(int x=70;x<250;++x)deviation+=std::abs(channel(im,x,160,0)-130);return deviation/180;};
    check(contrast(*detailed)>contrast(*isotropic)*2+20,"Anisotropic ring filtering failed to preserve resolvable radial contrast");
    // The flag promotes the texture to full-chain residency — one cache
    // entry is replaced in place, never duplicated.
    check(window.scene3d_statistics().texture_cache_entries==entries,"Changing the material sampler duplicated its texture entry");
    bands.scale=.05f;const auto distant=capture({bands},"ring-anisotropic-distant.png");
    for(int x=157;x<=162;++x)check(std::abs(channel(*distant,x,160,0)-130)<4,"Distant ring detail no longer converges to a stable average");
    std::cout<<"ring_filter_contrast="<<contrast(*isotropic)<<" -> "<<contrast(*detailed)<<"; distant average and cached upload passed\n";
  }
  camera.near_plane=3;
  const auto near_clip=capture({a},"near-clip.png");
  check(channel(*near_clip,80,160,0)==5&&channel(*near_clip,240,160,0)>220,"GPU near-plane clipping failed");
  camera.near_plane=.01f;camera.far_plane=3;
  const auto far_clip=capture({a},"far-clip.png");
  check(channel(*far_clip,80,160,0)>220&&channel(*far_clip,240,160,0)==5,"GPU far-plane clipping failed");
  camera.far_plane=1000;
  auto lit=b;lit.material.tint={255,255,255,255};lit.material.ambient=.2f;lit.material.diffuse=.8f;
  DrawList dark;dark.world.emplace_back(Scene3DView{Scene3D::create(camera,{lit},{0,0,-1}),{0,0,320,320}});
  window.draw(dark,folder/"lighting.png");const auto lighting=decode_rgba_image(folder/"lighting.png");
  check(std::abs(channel(*lighting,160,160,0)-51)<=1,"GPU light direction or ambient term is incorrect");
  lit.material.light_direction=Vec3{0,0,1};
  DrawList object_lit;object_lit.world.emplace_back(Scene3DView{Scene3D::create(camera,{lit},{0,0,-1}),{0,0,320,320}});
  window.draw(object_lit,folder/"object-lighting.png");const auto object_lighting=decode_rgba_image(folder/"object-lighting.png");
  check(channel(*object_lighting,160,160,0)>250,"Per-object lighting did not override shared scene light on GPU");
  if(window.scene3d_statistics().hdr)check(channel(*object_lighting,160,160,0)>=251&&channel(*object_lighting,160,160,0)<=254,"HDR resolve did not tonemap the fully lit surface");
  auto shadowed=lit;AnalyticShadow3D blocker;blocker.position={0,0,.6};blocker.radii={.3f,.15f,.1f};shadowed.material.shadow=blocker;
  const auto ellipse=capture({shadowed},"shadow-ellipsoid.png");
  check(std::abs(channel(*ellipse,160,160,0)-51)<=1&&channel(*ellipse,192,160,0)<55&&channel(*ellipse,160,192,0)>250,
      "Ellipsoid shadow lost its silhouette or shadowed ambient light");
  shadowed.material.light_direction=Vec3{1,0,1};
  const auto orbit_shadow=capture({shadowed},"shadow-star-direction.png");
  check(channel(*orbit_shadow,64,160,0)<55&&channel(*orbit_shadow,160,160,0)>185,"Shadow did not move with the star direction");
  shadowed.material.light_direction=Vec3{0,0,1};
  shadowed.material.shadow->rotation=rotation_axis_angle({0,0,1},1.570796327f);
  const auto rotated_shadow=capture({shadowed},"shadow-rotated.png");
  check(channel(*rotated_shadow,192,160,0)>250&&channel(*rotated_shadow,160,192,0)<55,"Blocker rotation did not rotate its shadow");
  shadowed.material.shadow->position.z=-.6;
  const auto wrong_side=capture({shadowed},"shadow-behind.png");
  check(channel(*wrong_side,160,160,0)>250,"Blocker behind receiver incorrectly obscured the star");
  blocker.shape=AnalyticShadowShape3D::Annulus;blocker.rotation=rotation_axis_angle({1,0,0},1.570796327f);
  blocker.inner_radius=.25f;blocker.outer_radius=.75f;
  std::vector<std::uint8_t> ring_alpha(256*4,255);
  for(int x=0;x<256;++x)ring_alpha[x*4+3]=x<90?255:x<160?0:128;
  blocker.opacity_map=RgbaImage::create(256,1,std::move(ring_alpha));shadowed.material.shadow=blocker;
  const auto annulus=capture({shadowed},"shadow-ring-gaps.png");
  check(channel(*annulus,160,160,0)>250&&channel(*annulus,216,160,0)<55&&channel(*annulus,240,160,0)>250&&
      std::abs(channel(*annulus,264,160,0)-153)<=2&&channel(*annulus,296,160,0)>250,
      "Ring hole, gap, opacity or outer edge did not control transmitted sunlight");
  const auto shadow_uploads=window.scene3d_statistics().texture_uploads;
  shadowed.scale=.01f;shadowed.material.shadow->scale=.01f;shadowed.material.shadow->position.z=.006;
  camera.position.z=.03;camera.near_plane=.0001f;camera.orthographic_height=.02f;
  const auto small_shadow=capture({shadowed},"shadow-scale.png");
  for(int x:{160,216,240,264,296})check(std::abs(channel(*annulus,x,160,0)-channel(*small_shadow,x,160,0))<=1,"Zoom/scale changed ring shadow geometry");
  check(window.scene3d_statistics().texture_uploads==shadow_uploads,"Shared ring opacity was uploaded again after zoom");
  camera.position.z=3;camera.near_plane=.01f;camera.orthographic_height=2;
  shadowed.scale=1;shadowed.material.shadow=blocker;shadowed.material.diffuse=0;shadowed.material.ambient=1;
  const auto emitted=capture({shadowed},"shadow-emission.png");
  check(channel(*emitted,216,160,0)>250,"Ring shadow incorrectly suppressed emitted light");
  shadowed=lit;shadowed.material.shadow=blocker;shadowed.material.shadow->rotation={};
  const auto parallel=capture({shadowed},"shadow-parallel.png");
  check(channel(*parallel,216,160,0)>250,"Light parallel to zero-thickness ring produced an invalid shadow");
  // Actual sphere/ring geometry shares depth, tilted planes and mutual shadows.
  {
    Material3D planet;planet.tint={177,159,124,255};planet.ambient=.07f;planet.diffuse=.9f;planet.light_direction=Vec3{-.8f,.3f,.7f};
    const auto tilt=rotation_axis_angle({1,0,0},.55f);
    auto ring_blocker=blocker;ring_blocker.position={};ring_blocker.rotation=tilt;ring_blocker.inner_radius=1.25f;ring_blocker.outer_radius=2.1f;
    planet.shadow=ring_blocker;
    Material3D rings=planet;rings.texture=blocker.opacity_map;rings.transparent=true;rings.double_sided=true;rings.two_sided_diffuse=true;rings.ambient=.12f;
    AnalyticShadow3D planet_blocker;planet_blocker.rotation=tilt;rings.shadow=planet_blocker;
    camera.orthographic_height=4.5f;
    const std::vector<MeshInstance3D> objects{{Mesh3D::uv_sphere(96,48),{},tilt,1,planet},{annulus_mesh(1.25f,2.1f,128),{},tilt,1,rings}};
    const auto mutual=capture(objects,"shadow-mutual.png");
    auto unshadowed=objects;for(auto& o:unshadowed)o.material.shadow.reset();
    const auto baseline=capture(unshadowed,"shadow-mutual-disabled.png");
    int differences=0;for(std::size_t n=0;n<mutual->pixels().size();n+=4)differences+=baseline->pixels()[n]>mutual->pixels()[n]+3;
    check(differences>150,"Mutual planet/ring shadows did not change the actual tilted geometry");
    camera.orthographic_height=2;
  }
  std::cout<<"analytic_shadows=ellipsoid_rotation_annulus_gaps_opacity_scale_emission_mutual_passed\n";
  auto thin_sheet=lit;thin_sheet.material.light_direction=Vec3{0,0,-1};thin_sheet.material.two_sided_diffuse=true;
  const auto underside=capture({thin_sheet},"ring-underside.png");
  check(channel(*underside,160,160,0)>250,"Opt-in thin ring underside lost direct illumination");
  auto response=lit;response.material.tint={125,125,125,255};response.material.ambient=.05f;response.material.diffuse=.85f;
  response.material.surface_response=SurfaceResponse3D{};
  auto& surface=*response.material.surface_response;
  surface.normal=RgbaImage::create(1,1,{128,128,255,255});surface.properties=RgbaImage::create(1,1,{255,0,0,128});surface.normal_strength=1;
  const auto matte=capture({response},"planet-matte.png");
  surface.normal=RgbaImage::create(1,1,{255,128,128,255});
  const auto normal=capture({response},"planet-normal.png");
  check(channel(*matte,160,160,0)>channel(*normal,160,160,0)+20,"Planet normal map did not change dynamic lighting");
  surface.normal=RgbaImage::create(1,1,{128,128,255,255});surface.cloud_shadow=RgbaImage::create(1,1,{255,255,255,255});surface.cloud_opacity=.5f;
  const auto clouded=capture({response},"planet-cloud-shadow.png");
  check(channel(*clouded,160,160,0)<channel(*matte,160,160,0)-30,"Separate cloud alpha did not cast surface shadow");
  {
    auto seam=response;seam.material.tint={255,255,255,255};seam.material.ambient=.2f;seam.material.diffuse=.8f;
    std::vector<std::uint8_t> pixels(1024*4*4,255);
    for(int y=0;y<4;++y)for(int x=0;x<1024;++x)pixels[(y*1024+x)*4+3]=(x<32||x>=992)?255:0;
    auto& clouds=*seam.material.surface_response;clouds.cloud_shadow=RgbaImage::create(1024,4,std::move(pixels));clouds.cloud_opacity=1;clouds.cloud_offset.x=.5f;
    const auto wrap=capture({seam},"mip-cloud-wrap.png");
    for(int x=157;x<=162;++x)check(channel(*wrap,x,160,0)<55,"Wrapped cloud longitude selected a blurred mip at its seam");
    clouds.cloud_offset.x=1.5f;const auto full_turn=capture({seam},"mip-cloud-full-turn.png");
    for(int x=70;x<260;++x)check(std::abs(channel(*wrap,x,160,0)-channel(*full_turn,x,160,0))<=1,"Cloud wrapping changed after a full turn");
  }
  {
    auto deck=lit;deck.material.tint={125,125,125,255};deck.material.ambient=.05f;deck.material.diffuse=.85f;
    deck.material.surface_response=SurfaceResponse3D{};
    auto& deck_surface=*deck.material.surface_response;
    deck_surface.cloud_shadow=RgbaImage::create(1,1,{255,255,255,255});
    deck_surface.cloud_opacity=1;deck_surface.cloud_albedo=1;
    const auto deck_lit=capture({deck},"planet-cloud-deck.png");
    check(channel(*deck_lit,160,160,0)>channel(*matte,160,160,0)+60,"Opaque cloud deck did not composite over the surface");
    deck_surface.cloud_albedo=.25f;
    const auto dim_deck=capture({deck},"planet-cloud-deck-dim.png");
    check(channel(*dim_deck,160,160,0)<channel(*deck_lit,160,160,0)-60,"Cloud albedo did not scale the deck brightness");
  }
  {
    auto wrapped=lit;wrapped.material.tint={255,255,255,255};wrapped.material.ambient=.05f;wrapped.material.diffuse=.95f;
    wrapped.material.light_direction=Vec3{1,0,0};
    const auto edge=capture({wrapped},"terminator-flat.png");
    wrapped.material.terminator_wrap=.8f;
    const auto wrapped_edge=capture({wrapped},"terminator-wrap.png");
    check(channel(*wrapped_edge,160,160,0)>channel(*edge,160,160,0)+80,"Wrap diffuse did not light the terminator");
    wrapped.material.light_direction=Vec3{0,0,1};
    const auto lit_wrap=capture({wrapped},"terminator-wrap-lit.png");
    wrapped.material.terminator_wrap=0;
    const auto lit_flat=capture({wrapped},"terminator-flat-lit.png");
    check(std::abs(channel(*lit_wrap,160,160,0)-channel(*lit_flat,160,160,0))<=2,"Wrap changed fully lit response");
  }
  surface.cloud_opacity=0;surface.properties=RgbaImage::create(1,1,{50,255,0,128});
  const auto ocean=capture({response},"planet-ocean.png");
  check(channel(*ocean,160,160,0)>channel(*matte,160,160,0)+30,"Ocean roughness/specular mask is not evaluated");
  response.material.surface_response.reset();response.material.light_color={1,.3f,.1f};response.material.light_intensity=.5f;
  const auto spectral=capture({response},"planet-star-color.png");
  check(channel(*spectral,160,160,0)>channel(*spectral,160,160,2)+35&&channel(*spectral,160,160,0)<100,"Stellar spectrum/irradiance did not affect material lighting");
  response.mesh=Mesh3D::uv_sphere(64,32);response.scale=.85f;response.material.ambient=1;response.material.diffuse=0;response.material.transparent=true;response.material.rim_power=2;
  const auto atmosphere=capture({response},"planet-atmosphere.png");
  check(channel(*atmosphere,285,160,0)>channel(*atmosphere,160,160,0)+20,"Atmosphere rim does not follow the 3D viewing angle");
  const auto rejects=[&](auto callback){bool rejected=false;try{callback();}catch(const std::invalid_argument&){rejected=true;}catch(const std::length_error&){rejected=true;}check(rejected,"Invalid dielectric accepted");};
  const auto optical_image=RgbaImage::create(1,1,{255,255,255,255});
  auto dielectric=lit;dielectric.material.dielectric=Dielectric3D{};
  rejects([&]{(void)Scene3D::create(camera,{dielectric});});
  dielectric.material.dielectric->environment=optical_image;
  (void)Scene3D::create(camera,{dielectric});
  for(int parameter=0;parameter<6;++parameter){auto invalid=dielectric;auto& d=*invalid.material.dielectric;
    if(parameter==0)d.index_of_refraction=.9f;if(parameter==1)d.roughness=0;
    if(parameter==2)d.transmission=1.01f;if(parameter==3)d.thickness=-1;
    if(parameter==4)d.absorption.x=-.01f;if(parameter==5)d.environment_strength=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{(void)Scene3D::create(camera,{invalid});});}
  {std::vector<MeshInstance3D> optics;
    for(std::size_t n=0;n<maximum_scene3d_resource_entries;++n){auto i=dielectric;i.material.dielectric->surface=RgbaImage::create(1,1,{128,128,128,255});optics.push_back(i);}
    rejects([&]{(void)Scene3D::create(camera,optics);});}
  // Separate forward reflection and backward transmission using known radiance.
  std::vector<std::uint8_t> environment(1024*64*4);
  for(int y=0;y<64;++y)for(int x=0;x<1024;++x){const auto at=(y*1024+x)*4;
    const bool front=x>256&&x<768;environment[at]=front?255:0;
    environment[at+2]=front?0:255;environment[at+3]=255;}
  auto ice=lit;ice.mesh=Mesh3D::uv_sphere(96,48);ice.scale=.85f;
  ice.material.ambient=ice.material.diffuse=0;ice.material.dielectric=Dielectric3D{};
  auto& optic=*ice.material.dielectric;optic.environment=RgbaImage::create(1024,64,environment);
  optic.transmission=1;optic.roughness=.04f;optic.absorption={};optic.specular_strength=0;
  const auto clear=capture({ice},"optics-fresnel.png");
  check(channel(*clear,160,160,2)>240&&channel(*clear,160,160,0)<10,"Ice failed to transmit the refracted environment");
  const auto two_hemispheres=optic.environment;
  {
    std::vector<std::uint8_t> pixels(1024*64*4);
    for(int y=0;y<64;++y)for(int x=0;x<1024;++x){const auto at=(y*1024+x)*4;pixels[at+1]=(x<16||x>=1008)?255:0;pixels[at+3]=255;}
    optic.environment=RgbaImage::create(1024,64,std::move(pixels));
    const auto seam=capture({ice},"mip-environment-seam.png");
    for(int x=158;x<=161;++x)check(channel(*seam,x,160,1)>230,"Environment longitude seam blurred away refracted radiance");
  }
  optic.environment=RgbaImage::create(1,1,{255,255,255,255});optic.transmission=0;
  const auto fresnel_view=capture({ice},"optics-grazing.png");
  check(channel(*fresnel_view,290,160,0)>channel(*fresnel_view,160,160,0)+20,"Grazing-angle Fresnel reflection is missing");
  optic.environment=two_hemispheres;optic.transmission=1;
  optic.absorption={0,0,2};optic.thickness=2;
  const auto absorbed=capture({ice},"optics-absorption.png");
  check(channel(*absorbed,160,160,2)<20,"Optical thickness did not attenuate transmitted light");
  // A narrow green feature is reached only when Snell's law bends the ray.
  std::fill(environment.begin(),environment.end(),std::uint8_t{0});
  for(int y=0;y<64;++y)for(int x=0;x<1024;++x){const auto at=(y*1024+x)*4;environment[at+1]=(x>15&&x<47)?255:0;environment[at+3]=255;}
  optic.environment=RgbaImage::create(1024,64,environment);optic.absorption={};
  ice.scale=1;auto inclined=quad(0,0)->vertices();for(auto& v:inclined)v.normal={.6f,0,.8f};
  ice.mesh=Mesh3D::create(inclined,{0,1,2,0,2,3});
  optic.index_of_refraction=1;const auto straight=capture({ice},"optics-straight.png");
  optic.index_of_refraction=1.31f;const auto bent=capture({ice},"optics-refracted.png");
  check(channel(*straight,160,160,1)<10&&channel(*bent,160,160,1)>220,"IOR did not bend the sampled transmission direction");
  optic.surface=RgbaImage::create(1,1,{255,0,255,255});
  const auto frost=capture({ice},"optics-frost.png");
  check(channel(*frost,160,160,1)<10,"Opaque frost mask did not suppress transmission");
  const auto optical_uploads=window.scene3d_statistics().texture_uploads;
  (void)capture({ice},"optics-cached.png");
  check(window.scene3d_statistics().texture_uploads==optical_uploads,"Optical resources were reuploaded every frame");
  ice.mesh=quad(0,0);optic.surface.reset();optic.environment=RgbaImage::create(1,1,{0,0,0,255});
  optic.transmission=0;optic.specular_strength=.3f;optic.roughness=.2f;
  const auto polished=capture({ice},"optics-polished.png");optic.roughness=.8f;
  const auto rough=capture({ice},"optics-rough.png");
  check(channel(*polished,160,160,0)>60&&channel(*rough,160,160,0)<5,"GGX roughness did not broaden the sun highlight");
  std::cout<<"dielectric_gpu=fresnel_snell_absorption_frost_cache_passed\n";
  {
    // Metallic-workflow materials: emissive output independent of lighting,
    // metallic kills diffuse and tints specular, cutout discards fragments,
    // tiling repeats the surface, IBL feeds unlit surfaces.
    auto flat=a;flat.mesh=quad(0,0);flat.material.tint={255,255,255,255};
    flat.material.ambient=0;flat.material.diffuse=0;flat.material.light_intensity=0;
    auto glow=flat;glow.material.pbr=PbrSurface3D{};
    glow.material.pbr->emissive_strength=2;glow.material.pbr->emissive_tint={1,0,0};
    const auto emissive=capture({glow},"pbr-emissive.png");
    check(channel(*emissive,160,160,0)>200&&channel(*emissive,160,160,1)<10,
        "Emissive material did not glow without incident light");
    std::vector<std::uint8_t> glow_pixels(64*64*4,255);
    for(int y=0;y<64;++y)for(int x=0;x<64;++x){const auto at=(y*64+x)*4;glow_pixels[at]=0;glow_pixels[at+1]=x<32?255:0;glow_pixels[at+2]=0;}
    glow.material.pbr->emissive_tint={1,1,1};
    glow.material.pbr->emissive=RgbaImage::create(64,64,std::move(glow_pixels));
    const auto masked=capture({glow},"pbr-emissive-map.png");
    check(channel(*masked,80,160,1)>200&&channel(*masked,240,160,1)<10,
        "Emissive map did not mask the emitted radiance");
    auto metal=flat;metal.material.diffuse=1;metal.material.light_direction=Vec3{0,0,1};
    metal.material.light_intensity=1;metal.material.tint={200,60,40,255};
    metal.material.pbr=PbrSurface3D{};metal.material.pbr->metallic=1;metal.material.pbr->roughness=.4f;
    const auto metal_frame=capture({metal},"pbr-metal.png");
    check(channel(*metal_frame,160,160,0)>channel(*metal_frame,160,160,1)+40,
        "Metallic specular was not tinted by the surface albedo");
    // Off the specular lobe a conductor loses all diffuse response — an
    // off-axis normal keeps only the residual lobe energy.
    auto off_axis_verts=quad(0,0)->vertices();for(auto& v:off_axis_verts)v.normal={.6f,0,.8f};
    const auto off_axis_mesh=Mesh3D::create(off_axis_verts,{0,1,2,0,2,3});
    auto off_axis=metal;off_axis.mesh=off_axis_mesh;
    const auto metal_off=capture({off_axis},"pbr-metal-offaxis.png");
    auto dielectric_off=off_axis;dielectric_off.material.pbr->metallic=0;
    const auto dielectric_frame=capture({dielectric_off},"pbr-dielectric-offaxis.png");
    check(channel(*metal_off,160,160,0)+80<channel(*dielectric_frame,160,160,0),
        "Metallic surface kept dielectric-level diffuse response off the specular lobe");
    auto cutout=flat;cutout.material.tint={255,255,255,255};cutout.material.ambient=1;cutout.material.diffuse=0;
    std::vector<std::uint8_t> mask_pixels(64*4,255);
    for(int x=0;x<64;++x)mask_pixels[x*4+3]=x<32?0:255;
    cutout.material.texture=RgbaImage::create(64,1,std::move(mask_pixels));cutout.material.alpha_threshold=.5f;
    const auto cut=capture({cutout},"pbr-cutout.png");
    check(channel(*cut,80,160,0)==5&&channel(*cut,240,160,0)>200,
        "Alpha cutout failed to discard sub-threshold fragments");
    auto tiled=cutout;tiled.material.alpha_threshold=0;tiled.material.texture_tiling={2.f,1.f};
    const auto tile=capture({tiled},"pbr-tiling.png");
    check(channel(*tile,100,160,0)>200&&channel(*tile,168,160,0)==5&&channel(*tile,240,160,0)>200,
        "UV tiling did not repeat the surface texture");
    auto enviro=flat;enviro.material.ambient=0;enviro.material.diffuse=0;enviro.material.light_intensity=0;
    enviro.material.pbr=PbrSurface3D{};enviro.material.pbr->environment_strength=.8f;
    enviro.material.pbr->environment=RgbaImage::create(1,1,{0,255,0,255});
    const auto ibl=capture({enviro},"pbr-ibl.png");
    check(channel(*ibl,160,160,1)>40&&channel(*ibl,160,160,0)<30,
        "Environment irradiance did not light an unlit PBR surface");
    std::cout<<"pbr_gpu=emissive_metallic_cutout_tiling_ibl_passed\n";
  }
  {
    // Scene point lights: windowed inverse-square falloff in view space.
    auto plate=b;plate.material.tint={255,255,255,255};plate.material.ambient=0;plate.material.diffuse=1;
    plate.material.light_intensity=0;plate.mesh=quad(0,0);
    PointLight3D lamp;lamp.position={0,0,2.5};lamp.color={0,1,0};lamp.intensity=4;lamp.range=5;
    DrawList lamps;lamps.world.emplace_back(Scene3DView{Scene3D::create(camera,{plate},{0,0,1},{lamp}),{0,0,320,320}});
    window.draw(lamps,folder/"point-light.png");const auto lit_point=decode_rgba_image(folder/"point-light.png");
    check(channel(*lit_point,160,160,1)>100&&channel(*lit_point,160,160,0)<20,
        "Point light did not illuminate the receiver with its color");
    lamp.range=.2f;
    DrawList dim;dim.world.emplace_back(Scene3DView{Scene3D::create(camera,{plate},{0,0,1},{lamp}),{0,0,320,320}});
    window.draw(dim,folder/"point-light-range.png");const auto dimmed=decode_rgba_image(folder/"point-light-range.png");
    check(channel(*dimmed,160,160,1)<20,"Range window did not attenuate the point light");
    std::cout<<"point_lights_gpu=falloff_color_range_passed\n";
  }
  {
    // Atmosphere limb scattering: a tinted shell brightens the silhouette
    // edge above the bare surface and fades to the night floor.
    auto planet=a;planet.mesh=Mesh3D::uv_sphere(96,48);planet.scale=.85f;
    planet.material.tint={120,120,120,255};planet.material.ambient=.05f;planet.material.diffuse=.9f;
    planet.material.light_direction=Vec3{0,0,1};
    const auto bare=capture({planet},"atmo-bare.png");
    planet.material.atmosphere=Atmosphere3D{};planet.material.atmosphere->strength=3;planet.material.atmosphere->power=2.5f;
    const auto limb=capture({planet},"atmo-limb.png");
    check(channel(*limb,280,160,2)>channel(*bare,280,160,2)+12&&channel(*limb,280,160,2)>channel(*limb,280,160,0),
        "Atmosphere did not brighten the limb with its tint");
    std::cout<<"atmosphere_gpu=limb_tint_dayweight_passed\n";
  }
  if(window.scene3d_statistics().hdr){
    // Post stack: exposure brightens, bloom spreads super-threshold light,
    // Low tier skips bloom/sharpen, Ultra runs the MSAA resolve path.
    // Exposure uses a mid-tone emitter (HDR 1.0 already sits at the tonemap
    // knee), bloom and MSAA use a super-threshold one.
    auto bright=a;bright.mesh=quad(0,0);bright.material.tint={255,255,255,255};
    bright.material.ambient=0;bright.material.diffuse=0;bright.material.light_intensity=0;
    bright.material.pbr=PbrSurface3D{};bright.material.pbr->emissive_strength=.4f;
    auto hot=bright;hot.material.pbr->emissive_strength=3;
    const auto options_view=[&](const MeshInstance3D& i,RenderOptions3D o,const char* name){
      DrawList list;list.world.emplace_back(Scene3DView{Scene3D::create(camera,{i}),{0,0,320,320},o});
      window.draw(list,folder/name);return decode_rgba_image(folder/name);};
    const auto neutral=options_view(bright,{},"post-neutral.png");
    RenderOptions3D exposed;exposed.exposure=2;
    const auto exposed_frame=options_view(bright,exposed,"post-exposure.png");
    check(channel(*exposed_frame,160,160,0)>channel(*neutral,160,160,0)+15,
        "Exposure did not brighten the HDR image");
    RenderOptions3D bloomed;bloomed.bloom_strength=1;bloomed.bloom_threshold=.1f;
    const auto bloom_frame=options_view(hot,bloomed,"post-bloom.png");
    const auto hot_neutral=options_view(hot,{},"post-bloom-off.png");
    check(channel(*bloom_frame,14,160,0)>channel(*hot_neutral,14,160,0)+8,
        "Bloom did not spread super-threshold light beyond the surface");
    RenderOptions3D low=bloomed;low.quality=RenderQuality3D::Low;
    const auto low_frame=options_view(hot,low,"post-low.png");
    check(channel(*low_frame,14,160,0)<channel(*bloom_frame,14,160,0),
        "Low quality tier still paid for bloom taps");
    auto colored=bright;colored.material.pbr->emissive_tint={1,0,0};
    RenderOptions3D graded;graded.exposure=1;graded.contrast=1.5f;graded.saturation=0;
    const auto gray=options_view(colored,graded,"post-graded.png");
    check(std::abs(int(channel(*gray,160,160,0))-int(channel(*gray,160,160,1)))<=3&&
          std::abs(int(channel(*gray,160,160,1))-int(channel(*gray,160,160,2)))<=3,
        "Zero saturation did not neutralize the channel spread");
    RenderOptions3D ultra;ultra.quality=RenderQuality3D::Ultra;
    const auto msaa=options_view(hot,ultra,"post-ultra-msaa.png");
    check(channel(*msaa,160,160,0)>200,"Ultra tier MSAA resolve produced a blank frame");
    std::cout<<"post_gpu=exposure_bloom_quality_tiers_msaa_passed\n";
  }
  {
    // Debug shading views isolate single channels for material review —
    // assertions use channel ordering so they hold on HDR and UNORM paths.
    const auto debug_view=[&](const MeshInstance3D& i,DebugView3D mode,const char* name){
      DrawList list;RenderOptions3D o;o.debug_view=mode;
      list.world.emplace_back(Scene3DView{Scene3D::create(camera,{i}),{0,0,320,320},o});
      window.draw(list,folder/name);return decode_rgba_image(folder/name);};
    auto plate=b;plate.mesh=quad(0,0);plate.material.tint={255,255,255,255};
    plate.material.ambient=.15f;plate.material.diffuse=.85f;plate.material.light_intensity=.5f;
    const auto lit_frame=debug_view(plate,DebugView3D::Lit,"debug-lit.png");
    const auto unlit_frame=debug_view(plate,DebugView3D::Unlit,"debug-unlit.png");
    check(channel(*unlit_frame,160,160,1)>channel(*lit_frame,160,160,1),
        "Unlit debug view stayed under lighting");
    const auto normals_frame=debug_view(plate,DebugView3D::Normals,"debug-normals.png");
    check(channel(*normals_frame,160,160,2)>channel(*normals_frame,160,160,0),
        "Normals view did not encode the view-space normal");
    auto metal_plate=plate;metal_plate.material.pbr=PbrSurface3D{};
    metal_plate.material.pbr->metallic=1;metal_plate.material.pbr->roughness=.25f;
    const auto metal_view=debug_view(metal_plate,DebugView3D::Metallic,"debug-metallic.png");
    const auto rough_view=debug_view(metal_plate,DebugView3D::Roughness,"debug-roughness.png");
    check(channel(*metal_view,160,160,0)>channel(*rough_view,160,160,0)+60,
        "Metallic/Roughness views did not report the GGX factors");
    check(std::abs(int(channel(*rough_view,160,160,0))-int(channel(*rough_view,160,160,1)))<=4,
        "Roughness view is not grayscale");
    auto glow_plate=plate;glow_plate.material.pbr=PbrSurface3D{};
    glow_plate.material.pbr->emissive_strength=2;glow_plate.material.pbr->emissive_tint={0,1,0};
    const auto emissive_view=debug_view(glow_plate,DebugView3D::Emissive,"debug-emissive.png");
    check(channel(*emissive_view,160,160,1)>channel(*emissive_view,160,160,0)+40,
        "Emissive view did not isolate the emitted channel");
    const auto lighting_view=debug_view(plate,DebugView3D::LightingOnly,"debug-lighting.png");
    check(channel(*lighting_view,160,160,0)>40,
        "Lighting-only view lost the scene illumination");
    // Distance culling: beyond visible_range the instance submits nothing.
    auto far=plate;far.visible_range=1.f;
    const auto culled_before=window.scene3d_statistics().culled_instances;
    const auto culled_frame=debug_view(far,DebugView3D::Lit,"debug-culled.png");
    check(window.scene3d_statistics().culled_instances==culled_before+1&&channel(*culled_frame,160,160,0)==5,
        "visible_range did not cull the distant instance");
    std::cout<<"debug_views_gpu=channels_distance_cull_passed\n";
  }
  {
    // Directional shadow mapping: an authored ortho volume on the key light.
    // A 45-degree star displaces the occluder's footprint onto the receiver;
    // coverage, bias, tier and distance policies all assert pixel probes.
    camera.projection=Projection3D::Orthographic;camera.position={0,0,5};camera.orthographic_height=2;
    Material3D diffuse;diffuse.tint={255,255,255,255};diffuse.ambient=0;diffuse.diffuse=1;diffuse.light_direction=Vec3{.7f,0,.7f};
    MeshInstance3D receiver{quad(0,0),{},{},1,diffuse};
    auto occluder=receiver;occluder.scale=.3f;occluder.position={.5f,0,.4f};
    ShadowMap3D shadow;shadow.extent=4;shadow.distance=4;shadow.depth=8;shadow.resolution=512;
    const Vec3 light{.7f,0,.7f};
    const auto shadow_view=[&](std::vector<MeshInstance3D> objects,RenderOptions3D o,const char* name){
      DrawList list;list.world.emplace_back(Scene3DView{Scene3D::create(camera,std::move(objects),light,{},shadow),{0,0,320,320},o});
      window.draw(list,folder/name);return decode_rgba_image(folder/name);};
    const auto open=shadow_view({receiver},{},"shadow-open.png");
    const auto occluded=shadow_view({receiver,occluder},{},"shadow-blocked.png");
    check(window.scene3d_statistics().shadow_casters==2,"Shadow pass did not submit receiver and occluder casters");
    check(channel(*open,176,160,0)>100,"Lit receiver pixel was dark without any occluder");
    check(channel(*occluded,176,160,0)<channel(*open,176,160,0)/2,"Blocker did not shadow the receiver");
    check(channel(*occluded,60,160,0)>100,"Shadow volume darkened unoccluded receiver pixels");
    // Bias stability: a lone receiver under the map stays as bright as a
    // shadow-free render — no self-shadow acne.
    {DrawList list;list.world.emplace_back(Scene3DView{Scene3D::create(camera,{receiver},light),{0,0,320,320}});
     window.draw(list,folder/"shadow-reference.png");const auto reference=decode_rgba_image(folder/"shadow-reference.png");
     check(std::abs(int(channel(*open,176,160,0))-int(channel(*reference,176,160,0)))<8,"Shadow map biased the lit receiver into acne");}
    // Direction: moving the occluder left moves the shadow left with it.
    auto shifted=occluder;shifted.position={-.1f,0,.4f};
    const auto moved=shadow_view({receiver,shifted},{},"shadow-moved.png");
    check(channel(*moved,80,160,0)<channel(*open,80,160,0)/2&&channel(*moved,176,160,0)>100,"Shadow did not follow the occluder");
    // Quality tiers: Low pays no shadow pass at all; High keeps it via PCF.
    RenderOptions3D low;low.quality=RenderQuality3D::Low;
    const auto low_tier=shadow_view({receiver,occluder},low,"shadow-low.png");
    check(channel(*low_tier,176,160,0)>100,"Low quality tier still evaluated the shadow map");
    check(window.scene3d_statistics().shadow_casters==0,"Low tier still submitted shadow casters");
    RenderOptions3D high;high.quality=RenderQuality3D::High;
    const auto pcf=shadow_view({receiver,occluder},high,"shadow-pcf.png");
    check(channel(*pcf,176,160,0)<channel(*open,176,160,0)/2,"High tier lost the shadow entirely");
    // Distance policy: a culled caster stops occluding receivers.
    auto ranged=occluder;ranged.visible_range=1.f;
    const auto culled_caster=shadow_view({receiver,ranged},{},"shadow-culled.png");
    check(channel(*culled_caster,176,160,0)>100,"Distance-culled caster still wrote the shadow map");
    std::cout<<"shadow_map_gpu=casters_bias_direction_tiers_range_passed\n";
  }
  auto reversed=b;auto back_indices=b.mesh->indices();std::reverse(back_indices.begin(),back_indices.end());
  reversed.mesh=Mesh3D::create(b.mesh->vertices(),std::move(back_indices));
  const auto back=capture({reversed},"back-face.png");check(channel(*back,160,160,0)==5,"Back faces were not culled");
  reversed.material.double_sided=true;const auto two_sided=capture({reversed},"two-sided.png");check(channel(*two_sided,160,160,1)>200,"Double-sided geometry was culled");
  auto globe=a;globe.mesh=Mesh3D::uv_sphere();globe.material.texture=textured.material.texture;globe.material.tint={255,255,255,255};globe.material.ambient=.08f;globe.material.diffuse=.92f;
  camera.projection=Projection3D::Perspective;
  DrawList orbit;orbit.world.emplace_back(Scene3DView{Scene3D::create(camera,{globe}),{0,0,640,360}});window.draw(orbit,folder/"sphere.png");
  const auto sphere_pixels=decode_rgba_image(folder/"sphere.png");check(channel(*sphere_pixels,320,180,0)>15||channel(*sphere_pixels,320,180,1)>15||channel(*sphere_pixels,320,180,2)>25,"Perspective sphere is blank or back-facing");
  window.set_scene_quality(75,1);window.draw(orbit,folder/"scaled-world.png");window.set_scene_quality(100,1);
  const auto benchmark_start=std::chrono::steady_clock::now();double submission=0;
  for(int i=0;i<120;++i){FrameTiming timing;window.draw(orbit,std::nullopt,&timing);submission+=timing.submission_ms;}
  const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-benchmark_start).count();
  std::cout<<"scene3d frames=120 cpu_submit_mean_ms="<<submission/120<<" frame_wall_mean_ms="<<elapsed/120<<" mesh_uploads="<<window.scene3d_statistics().mesh_uploads<<" gpu_driver="<<window.gpu_driver()<<'\n';
  DrawList invalid;invalid.world.emplace_back(Scene3DView{Scene3D::create(camera,{a}),{0,0,8192,8192}});
  bool rejected=false;try{window.draw(invalid);}catch(const std::length_error&){rejected=true;}check(rejected,"Oversized 3D target was accepted");
  window.draw(stable);check(window.scene3d_statistics().target_bytes==320u*320u*(window.scene3d_statistics().hdr?16u:8u),"Target budget did not recover after rejection/resize");
  const auto rejects_before_upload=[&](const DrawList& list){
    const auto before=window.scene3d_statistics();bool refused=false;
    try{window.draw(list);}catch(const std::length_error&){refused=true;}
    const auto after=window.scene3d_statistics();
    check(refused&&before.mesh_uploads==after.mesh_uploads&&before.texture_uploads==after.texture_uploads&&before.target_bytes==after.target_bytes,"Over-budget frame allocated GPU resources before rejection");
  };
  DrawList many_views;
  for(std::size_t i=0;i<maximum_scene3d_views+1;++i)many_views.overlay.emplace_back(Scene3DView{Scene3D::create(camera,{a}),{0,0,64,64}});
  rejects_before_upload(many_views);
  {
    std::vector<MeshInstance3D> images;
    for(int n=0;n<3;++n){auto item=a;item.material.texture=RgbaImage::create(4096,2048,std::vector<std::uint8_t>(4096u*2048u*4u));images.push_back(item);}
    // Base-only accounting admitted all three at exactly 192 MiB.
    rejects([&]{(void)Scene3D::create(camera,images);});
    auto shared=images[0];shared.material.surface_response=SurfaceResponse3D{};
    shared.material.surface_response->normal=shared.material.surface_response->properties=shared.material.surface_response->cloud_shadow=shared.material.texture;
    shared.material.dielectric=Dielectric3D{};shared.material.dielectric->environment=shared.material.dielectric->surface=shared.material.texture;
    shared.material.shadow=AnalyticShadow3D{};shared.material.shadow->shape=AnalyticShadowShape3D::Annulus;shared.material.shadow->opacity_map=shared.material.texture;
    (void)Scene3D::create(camera,{shared,images[1]});
    DrawList combined;combined.world.emplace_back(Scene3DView{Scene3D::create(camera,{images[0],images[1]}),{0,0,64,64}});
    combined.world.emplace_back(Scene3DView{Scene3D::create(camera,{images[2]}),{64,0,64,64}});
    rejects_before_upload(combined);
  }
  // Shadow-only opacity maps also count against the combined frame budget.
  {
    std::vector<MeshInstance3D> maps;
    for(std::size_t n=0;n<maximum_scene3d_resource_entries-1;++n){auto i=lit;i.material.shadow=blocker;i.material.shadow->opacity_map=RgbaImage::create(1,1,{255,255,255,255});maps.push_back(i);}
    DrawList combined;combined.world.emplace_back(Scene3DView{Scene3D::create(camera,maps),{0,0,64,64}});
    maps.front().material.shadow->opacity_map=RgbaImage::create(1,1,{255,255,255,128});
    combined.world.emplace_back(Scene3DView{Scene3D::create(camera,{maps.front()}),{64,0,64,64}});
    rejects_before_upload(combined);
    auto extra=maps.front();extra.material.shadow->opacity_map=RgbaImage::create(1,1,{255,255,255,64});maps.push_back(extra);
    rejects([&]{(void)Scene3D::create(camera,maps);});
  }
  // Each scene is valid by itself; the combined frame must also fit the cache.
  for(bool images:{false,true}){
    std::vector<MeshInstance3D> resources;
    for(std::size_t i=0;i<maximum_scene3d_resource_entries;++i){auto item=a;if(images)item.material.texture=RgbaImage::create(1,1,{10,20,30,255});else item.mesh=quad(0,0);resources.push_back(item);}
    auto oversized=resources;oversized.push_back(a);bool refused=false;
    try{(void)Scene3D::create(camera,std::move(oversized));}catch(const std::length_error&){refused=true;}
    check(refused,"Individual scene exceeded its resource-count budget");
    DrawList combined;combined.world.emplace_back(Scene3DView{Scene3D::create(camera,std::move(resources)),{0,0,64,64}});
    combined.overlay.emplace_back(Scene3DView{Scene3D::create(camera,{a}),{64,0,64,64}});
    rejects_before_upload(combined);
  }
  window.draw(stable);check(window.scene3d_statistics().draw_calls==2,"Valid frame did not recover after combined budget rejection");
  for(int i=0;i<132;++i){auto item=a;item.mesh=quad(0,0);item.material.texture=RgbaImage::create(1,1,{static_cast<std::uint8_t>(i),20,80,255});DrawList pressure;pressure.world.emplace_back(Scene3DView{Scene3D::create(camera,{item}),{0,0,64,64}});window.draw(pressure);}
  const auto bounded=window.scene3d_statistics();check(bounded.mesh_cache_entries<=128&&bounded.texture_cache_entries<=128&&bounded.mesh_cache_bytes<=maximum_mesh3d_cache_bytes&&bounded.texture_cache_bytes<=maximum_scene3d_texture_cache_bytes,"3D caches exceeded their budgets");
  // A true emission volume remains visible from its side and has stable
  // integrated brightness across ray budgets; ordinary 2D UI stays outside it.
  {
    camera.projection=Projection3D::Orthographic;camera.position={0,0,5};camera.orthographic_height=2;
    MeshInstance3D plasma;plasma.mesh=surface_emission_volume(.3f);plasma.material.transparent=true;plasma.material.double_sided=true;
    plasma.material.texture=RgbaImage::create(1,1,{255,120,30,190});
    SurfaceEffect3D effect;effect.next_texture=plasma.material.texture;effect.volume_depth=.3f;effect.volume_density=5;effect.volume_steps=16;plasma.material.surface_effect=effect;
    const auto front=capture({plasma},"plasma-volume-front.png");
    plasma.material.surface_effect->volume_steps=48;const auto high=capture({plasma},"plasma-volume-high.png");
    const auto energy=[&](const RgbaImage& img){long long total=0;for(int y=60;y<300;++y)for(int x=60;x<300;++x)total+=std::max(0,static_cast<int>(channel(img,x,y,0))-5);return total;};
    check(energy(*front)>100000&&std::abs(static_cast<double>(energy(*front)-energy(*high)))/energy(*high)<.06,"Volume integration disappeared or depends on quality");
    plasma.rotation=rotation_axis_angle({0,1,0},1.5707963f);const auto edge=capture({plasma},"plasma-volume-edge.png");
    check(energy(*edge)>50000,"Volume collapsed to a flat edge-on sheet");
    plasma.material.surface_effect->view_sphere_center={0,0,-4};plasma.material.surface_effect->sphere_radius=1.8f;
    const auto hidden=capture({plasma},"plasma-volume-hidden.png");check(energy(*hidden)==0,"Volume shines through the foreground photosphere");
    check(channel(*edge,400,160,0)==5,"Volume escaped its viewport");
  }
  {
    DrawList text;Text label{{320,175},"ALIGNED ARROW",{240,240,240,255},20,0,UiRect{260,60,120,240},TextAlign::Center,FontFace::Interface,90};text.world.emplace_back(label);
    const auto path=folder/"rotated-label.png";window.draw(text,path);const auto pixels=decode_rgba_image(path);
    int min_x=640,max_x=0,min_y=360,max_y=0;
    for(int y=0;y<360;++y)for(int x=0;x<640;++x)if(channel(*pixels,x,y,0)>50){min_x=std::min(min_x,x);max_x=std::max(max_x,x);min_y=std::min(min_y,y);max_y=std::max(max_y,y);}
    check(max_y-min_y>3*(max_x-min_x)&&min_x>=260&&max_x<380&&min_y>=60&&max_y<300,"Text rotation/clip does not match measured geometry");
  }
  window.draw({});check(window.scene3d_statistics().target_bytes==0,"Unused 3D targets stayed allocated");
  std::cout<<"GPU depth, transparency, perspective, textures, 2D ordering, independent viewports, culling, resize and cache checks passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
