#include <stellar/engine/texture_cook.hpp>
#include <stellar/engine/draw_batcher.hpp>
#include <stellar/engine/render_graph.hpp>
#include <stellar/engine/texture_streaming.hpp>
#include "native_scene3d_gpu.hpp"
#include "generated/scene3d_shaders.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <numbers>
#include <set>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace stellar::native_map {
namespace {
std::runtime_error gpu_error(const char* action){return std::runtime_error(std::string(action)+": "+SDL_GetError());}
void checked(bool value,const char* action){if(!value)throw gpu_error(action);}
struct PendingSubmit {
  SDL_GPUFence* fence{};
  std::chrono::steady_clock::time_point submitted{};
};
struct Command {
  SDL_GPUDevice* device;SDL_GPUCommandBuffer* value;
  std::deque<PendingSubmit>* pending{};
  explicit Command(SDL_GPUDevice* device):device(device),value(SDL_AcquireGPUCommandBuffer(device)){if(!value)throw gpu_error("3D command buffer allocation failed");}
  Command(SDL_GPUDevice* device,std::deque<PendingSubmit>& pending_)
      :device(device),value(SDL_AcquireGPUCommandBuffer(device)),pending(&pending_){if(!value)throw gpu_error("3D command buffer allocation failed");}
  ~Command(){if(value)SDL_CancelGPUCommandBuffer(value);}
  void submit(){auto* current=value;value=nullptr;
    if(pending){
      auto* fence=SDL_SubmitGPUCommandBufferAndAcquireFence(current);
      if(!fence)throw gpu_error("3D GPU submission failed");
      pending->push_back({fence,std::chrono::steady_clock::now()});
      return;
    }
    checked(SDL_SubmitGPUCommandBuffer(current),"3D GPU submission failed");}
};
struct Transfer {
  SDL_GPUDevice* device;SDL_GPUTransferBuffer* value;
  Transfer(SDL_GPUDevice* d,Uint32 size):device(d){SDL_GPUTransferBufferCreateInfo info{};info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;info.size=size;value=SDL_CreateGPUTransferBuffer(d,&info);if(!value)throw gpu_error("3D upload staging allocation failed");}
  ~Transfer(){SDL_ReleaseGPUTransferBuffer(device,value);}
  void* map(){auto* p=SDL_MapGPUTransferBuffer(device,value,false);if(!p)throw gpu_error("3D upload staging map failed");return p;}
};
struct Geometry {
  SDL_GPUDevice* device;std::shared_ptr<const Mesh3D> owner;SDL_GPUBuffer* vertices{};SDL_GPUBuffer* indices{};std::uint64_t use{};
  explicit Geometry(SDL_GPUDevice* d):device(d){}
  ~Geometry(){if(vertices)SDL_ReleaseGPUBuffer(device,vertices);if(indices)SDL_ReleaseGPUBuffer(device,indices);}
  std::size_t bytes()const{return owner->byte_size()*2;}
};
// One 2x box-filter step matching the halving rule the GPU blit chain uses
// (clamped edges make 1-wide/1-high tails safe). Partial residency uploads a
// downsampled level 0 because RgbaImage keeps only full-resolution pixels.
std::vector<std::uint8_t> downsample_rgba2x(const std::vector<std::uint8_t>& src,int w,int h,int& out_w,int& out_h){
  out_w=std::max(1,w/2);out_h=std::max(1,h/2);
  std::vector<std::uint8_t> out(static_cast<std::size_t>(out_w)*out_h*4);
  for(int y=0;y<out_h;++y)for(int x=0;x<out_w;++x){
    unsigned sum[4]{};
    const int xs[2]{std::min(2*x,w-1),std::min(2*x+1,w-1)},ys[2]{std::min(2*y,h-1),std::min(2*y+1,h-1)};
    for(const int sy:ys)for(const int sx:xs)for(int c=0;c<4;++c)sum[c]+=src[(static_cast<std::size_t>(sy)*w+sx)*4+c];
    for(int c=0;c<4;++c)out[(static_cast<std::size_t>(y)*out_w+x)*4+c]=static_cast<std::uint8_t>((sum[c]+2)/4);
  }
  return out;
}
struct Texture {
  SDL_GPUDevice* device;std::shared_ptr<const RgbaImage> owner;SDL_GPUTexture* texture{};std::uint64_t use{};std::size_t gpu_bytes{};
  // Finest mip level actually uploaded; the texture's level 0 holds source
  // mip base_mip when the streamer admitted a partial tail under budget.
  std::uint32_t base_mip{};
  explicit Texture(SDL_GPUDevice* d):device(d){}
  ~Texture(){if(texture)SDL_ReleaseGPUTexture(device,texture);}
  std::size_t bytes()const{return owner->byte_size()+gpu_bytes;}
};
struct Target {
  SDL_GPUDevice* device;SDL_GPUTexture* color{};SDL_GPUTexture* depth{};SDL_GPUTexture* hdr{};SDL_GPUTexture* msaa_color{};SDL_GPUTexture* msaa_depth{};SDL_GPUTexture* shadow{};SDL_GPUTexture* spot_shadow{};SDL_GPUTexture* point_shadow{};SDL_GPUTexture* shadow_cascade{};SDL_Texture* composite{};int width{},height{};Uint32 hdr_levels{1};int shadow_size{},spot_shadow_size{},point_shadow_w{},point_shadow_h{},shadow_cascade_size{},shadow_cascade_layers{};
  explicit Target(SDL_GPUDevice* d):device(d){}
  ~Target(){if(composite)SDL_DestroyTexture(composite);if(shadow_cascade)SDL_ReleaseGPUTexture(device,shadow_cascade);if(point_shadow)SDL_ReleaseGPUTexture(device,point_shadow);if(spot_shadow)SDL_ReleaseGPUTexture(device,spot_shadow);if(shadow)SDL_ReleaseGPUTexture(device,shadow);if(msaa_depth)SDL_ReleaseGPUTexture(device,msaa_depth);if(msaa_color)SDL_ReleaseGPUTexture(device,msaa_color);if(hdr)SDL_ReleaseGPUTexture(device,hdr);if(depth)SDL_ReleaseGPUTexture(device,depth);if(color)SDL_ReleaseGPUTexture(device,color);}
  std::size_t bytes()const{auto base=static_cast<std::size_t>(width)*height*(hdr?16u:8u);return base+(msaa_color?base*4+static_cast<std::size_t>(width)*height*16u:0)+static_cast<std::size_t>(shadow_size)*shadow_size*4+static_cast<std::size_t>(spot_shadow_size)*spot_shadow_size*4+static_cast<std::size_t>(point_shadow_w)*point_shadow_h*4+static_cast<std::size_t>(shadow_cascade_size)*shadow_cascade_size*shadow_cascade_layers*4;}
};
SDL_GPUTexture* make_texture(SDL_GPUDevice* d,int w,int h,SDL_GPUTextureFormat format,SDL_GPUTextureUsageFlags usage,Uint32 levels=1,SDL_GPUSampleCount samples=SDL_GPU_SAMPLECOUNT_1){
  SDL_GPUTextureCreateInfo info{};info.type=SDL_GPU_TEXTURETYPE_2D;info.format=format;info.usage=usage;
  info.width=w;info.height=h;info.layer_count_or_depth=1;info.num_levels=levels;info.sample_count=samples;
  auto* result=SDL_CreateGPUTexture(d,&info);if(!result)throw gpu_error("3D texture allocation failed");return result;
}
// Far cascades share one 2D-array depth texture so the fragment
// shader loops tiers through a single sampler binding; each tier's
// pass writes its own layer via the depth-target layer field.
SDL_GPUTexture* make_texture_array(SDL_GPUDevice* d,int w,int h,int layers,SDL_GPUTextureFormat format,SDL_GPUTextureUsageFlags usage){
  SDL_GPUTextureCreateInfo info{};info.type=SDL_GPU_TEXTURETYPE_2D_ARRAY;info.format=format;info.usage=usage;
  info.width=w;info.height=h;info.layer_count_or_depth=static_cast<Uint32>(layers);info.num_levels=1;info.sample_count=SDL_GPU_SAMPLECOUNT_1;
  auto* result=SDL_CreateGPUTexture(d,&info);if(!result)throw gpu_error("3D texture allocation failed");return result;
}
template<class Map> void evict(Map& cache,std::size_t& bytes,std::size_t incoming,std::size_t budget){
  while(!cache.empty()&&(bytes+incoming>budget||cache.size()>=maximum_scene3d_resource_entries)){
    const auto oldest=std::min_element(cache.begin(),cache.end(),[](const auto& a,const auto& b){return a.second->use<b.second->use;});
    bytes-=oldest->second->bytes();cache.erase(oldest);
  }
}
struct VertexUniform {Matrix4 mvp,model_view,shadow_from_model;};
struct FragmentUniform {std::array<float,4> tint,light,parameters,optics,absorption,view_options,camera_orientation,illumination,surface_response,surface_options,shadow_light,shadow_radii,shadow_options,effect_options,effect_sphere,volume_options;Matrix4 effect_from_view;std::array<std::array<float,4>,2> additional_direction,additional_illumination,additional_shadow;std::array<float,4> texture_options,pbr_options,pbr_values,emissive_tint,uv_options,atmo_options,atmo_shape;std::array<float,4> response_options;std::array<std::array<float,4>,4> point_position,point_energy,point_cone;std::array<float,4> point_outer;std::array<float,4> anim_options;std::array<float,4> atmo_sunset;std::array<float,4> env_flags;std::array<float,4> drift_options;std::array<float,4> scatter_options;std::array<float,4> wave_options;};
struct PostUniform {std::array<float,4> a,b,c,d,e;};
// View-wide fragment uniform: debug selector, then the key light's
// view→shadow-clip transform, {texel size (>0 enables), PCF radius in
// texels, strength, bias}, and the world-units normal-offset lift for
// the directional shadow map. The spot pair covers the shadowed cone
// lights (one depth-atlas quadrant each — the bounds lane clamps
// sampling so out-of-cone fragments can't sample a neighbour quadrant),
// the cascade arrays the optional wider tiers sharing the directional
// box's centre/depth (one depth-array layer each, options .z carrying
// each tier's own texel-scaled lift), the omni pair the cube-atlas rows.
struct ViewUniform {std::array<float,4> debug_mode;Matrix4 shadow_from_view;std::array<float,4> shadow_options;std::array<float,4> shadow_advanced;std::array<Matrix4,4> spot_from_view;std::array<std::array<float,4>,4> spot_options;std::array<std::array<float,4>,4> spot_bounds;std::array<Matrix4,maximum_scene3d_shadow_cascades> cascade_from_view;std::array<std::array<float,4>,maximum_scene3d_shadow_cascades> cascade_options;std::array<std::array<float,4>,4> omni_options,omni_atlas;std::array<std::array<float,4>,4> spot_advanced,omni_advanced;};
static_assert(sizeof(Vertex3D)==32&&sizeof(VertexUniform)==192&&sizeof(FragmentUniform)==848&&sizeof(PostUniform)==80&&sizeof(ViewUniform)==1072);
// IEEE-754 binary16 -> float for the RGBA16F probe downloads.
float half_to_float(std::uint16_t h){
  const int e=(h>>10)&0x1f,m=h&0x3ff;
  const float v=e==0?std::ldexp(static_cast<float>(m),-24)
    :e==31?std::numeric_limits<float>::infinity()
    :std::ldexp(1.f+static_cast<float>(m)/1024.f,e-15);
  return h&0x8000?-v:v;
}
// Column-major rotation for a unit quaternion — same convention as
// rotation_matrix in native_scene3d.cpp, kept local to avoid exporting it.
Matrix4 rotation_from(Quaternion q){
  const auto [x,y,z,w]=q;
  return {{{1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w),0,
            2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w),0,
            2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y),0,0,0,0,1}}};
}
}
struct Scene3DRenderer::Storage {
  SDL_GPUDevice* device;SDL_Renderer* renderer;std::thread::id owner=std::this_thread::get_id();
  SDL_GPUSampler* sampler{};SDL_GPUSampler* environment_sampler{};SDL_GPUSampler* detail_sampler{};SDL_GPUSampler* repeat_sampler{};SDL_GPUSampler* repeat_aniso_sampler{};std::array<SDL_GPUGraphicsPipeline*,4> pipelines{},pipelines_ms4{};SDL_GPUGraphicsPipeline* tonemap_pipeline{};SDL_GPUGraphicsPipeline* shadow_pipeline{};bool shadow_supported{};
  std::unordered_map<const Mesh3D*,std::shared_ptr<Geometry>> meshes;
  std::unordered_map<const RgbaImage*,std::shared_ptr<Texture>> textures;
  std::vector<std::unique_ptr<Target>> targets;std::vector<const Scene3DView*> views;
  std::shared_ptr<const RgbaImage> white=RgbaImage::create(1,1,{255,255,255,255});
  // Captured environment probes. A view's probe_epoch selects the key:
  // zero keys the bake to the scene instance (weak — a freed scene can
  // never lend its probe to a different scene reusing the heap slot);
  // nonzero keys to the caller's declared generation so hosts that
  // rebuild Scene3D per frame bake one logical scene once. Anchor and
  // resolution join the key so re-authoring capture params rebakes.
  struct ProbeKey {
    std::weak_ptr<const Scene3D> scene;std::uint64_t epoch{};Position3 anchor{};int res{};
  };
  struct ProbeKeyLess {
    bool operator()(const ProbeKey&a,const ProbeKey&b)const{
      if(a.epoch!=b.epoch)return a.epoch<b.epoch;
      if(a.res!=b.res)return a.res<b.res;
      if(a.anchor.x!=b.anchor.x)return a.anchor.x<b.anchor.x;
      if(a.anchor.y!=b.anchor.y)return a.anchor.y<b.anchor.y;
      if(a.anchor.z!=b.anchor.z)return a.anchor.z<b.anchor.z;
      return std::owner_less<>()(a.scene,b.scene);
    }
  };
  static ProbeKey probe_key(const Scene3DView&view){
    const auto&cap=view.scene->environment_capture();
    return {view.probe_epoch?std::weak_ptr<const Scene3D>{}:std::weak_ptr<const Scene3D>(view.scene),
        view.probe_epoch,cap.anchor,cap.face_resolution};
  }
  std::map<ProbeKey,std::shared_ptr<const RgbaImage>,ProbeKeyLess> probe_cache;
  // Engine TextureStreamer owns the byte-budget residency decision; this
  // backend registers real per-mip sizes, declares each frame's demand with
  // a camera-distance priority, and executes the streamer's load/evict list.
  engine::TextureStreamer streamer{maximum_scene3d_texture_cache_bytes};
  std::unordered_map<const RgbaImage*,engine::TextureId> stream_ids;
  // Owners are weak so an expired image does not lend its registration to a
  // new RgbaImage that reuses its heap address: a stale hit would otherwise
  // inherit the dead image's mip-byte desc and residency, corrupting both the
  // streamer's accounting and the resident-tail granularity forever after.
  std::unordered_map<engine::TextureId,std::weak_ptr<const RgbaImage>> stream_owners;
  std::uint64_t stream_frame{},stream_registrations{};
  // Fences acquired at each GPU submit; polled at the next prepare() so
  // queue depth and submit→signal latency are visible without backend
  // timestamp queries (SDL_GPU exposes fences only).
  std::deque<PendingSubmit> gpu_pending;
  Scene3DStatistics stats;std::uint64_t serial{};std::size_t next_view{};bool hdr{},msaa_supported{};SDL_GPUTextureFormat scene_format{};
  SDL_GPUBuffer* vertex_buffer{};SDL_GPUBuffer* fragment_buffer{};SDL_GPUBuffer* shadow_buffer{};SDL_GPUBuffer* spot_shadow_buffer[4]{};SDL_GPUBuffer* omni_shadow_buffer[4]{};SDL_GPUBuffer* cascade_shadow_buffer[maximum_scene3d_shadow_cascades]{};std::size_t vertex_capacity{64},fragment_capacity{64},shadow_capacity{64},spot_shadow_capacity[4]{},omni_shadow_capacity[4]{},cascade_shadow_capacity[maximum_scene3d_shadow_cascades]{};
  Storage(SDL_GPUDevice* d,SDL_Renderer* r):device(d),renderer(r){}
  ~Storage(){for(const auto& p:gpu_pending)SDL_ReleaseGPUFence(device,p.fence);gpu_pending.clear();targets.clear();textures.clear();meshes.clear();for(auto* p:pipelines)if(p)SDL_ReleaseGPUGraphicsPipeline(device,p);for(auto* p:pipelines_ms4)if(p)SDL_ReleaseGPUGraphicsPipeline(device,p);if(tonemap_pipeline)SDL_ReleaseGPUGraphicsPipeline(device,tonemap_pipeline);if(shadow_pipeline)SDL_ReleaseGPUGraphicsPipeline(device,shadow_pipeline);if(vertex_buffer)SDL_ReleaseGPUBuffer(device,vertex_buffer);if(fragment_buffer)SDL_ReleaseGPUBuffer(device,fragment_buffer);for(auto* b:spot_shadow_buffer)if(b)SDL_ReleaseGPUBuffer(device,b);for(auto* b:omni_shadow_buffer)if(b)SDL_ReleaseGPUBuffer(device,b);for(auto* b:cascade_shadow_buffer)if(b)SDL_ReleaseGPUBuffer(device,b);if(shadow_buffer)SDL_ReleaseGPUBuffer(device,shadow_buffer);if(sampler)SDL_ReleaseGPUSampler(device,sampler);if(environment_sampler)SDL_ReleaseGPUSampler(device,environment_sampler);if(detail_sampler)SDL_ReleaseGPUSampler(device,detail_sampler);if(repeat_sampler)SDL_ReleaseGPUSampler(device,repeat_sampler);if(repeat_aniso_sampler)SDL_ReleaseGPUSampler(device,repeat_aniso_sampler);}
  void require_owner()const{if(std::this_thread::get_id()!=owner)throw std::logic_error("3D rendering must run on the window thread.");}
  void initialize(){
    SDL_GPUSamplerCreateInfo sampling{};sampling.min_filter=sampling.mag_filter=SDL_GPU_FILTER_LINEAR;
    sampling.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
    sampling.max_lod=std::log2(static_cast<float>(maximum_rgba_image_dimension));
    sampling.address_mode_u=sampling.address_mode_v=sampling.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    sampler=SDL_CreateGPUSampler(device,&sampling);if(!sampler)throw gpu_error("3D sampler creation failed");
    sampling.enable_anisotropy=true;sampling.max_anisotropy=8.f;
    detail_sampler=SDL_CreateGPUSampler(device,&sampling);if(!detail_sampler)throw gpu_error("3D detail sampler creation failed");
    sampling.enable_anisotropy=false;sampling.max_anisotropy=1.f;
    sampling.address_mode_u=SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    environment_sampler=SDL_CreateGPUSampler(device,&sampling);if(!environment_sampler)throw gpu_error("3D environment sampler creation failed");
    // UV tiling wraps on both axes; surface/normal/emissive maps bound with
    // these samplers see fract-style repeats without edge smearing.
    sampling.address_mode_v=SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    repeat_sampler=SDL_CreateGPUSampler(device,&sampling);if(!repeat_sampler)throw gpu_error("3D tiling sampler creation failed");
    sampling.enable_anisotropy=true;sampling.max_anisotropy=8.f;
    repeat_aniso_sampler=SDL_CreateGPUSampler(device,&sampling);if(!repeat_aniso_sampler)throw gpu_error("3D anisotropic tiling sampler creation failed");
    hdr=SDL_GPUTextureSupportsFormat(device,SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER);
    scene_format=hdr?SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT:SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    const auto shader=[&](const auto& code,SDL_GPUShaderStage stage,Uint32 uniforms,Uint32 samplers,Uint32 storage=0){
      SDL_GPUShaderCreateInfo info{};info.code=reinterpret_cast<const Uint8*>(code);info.code_size=sizeof(code);info.entrypoint="main";info.format=SDL_GPU_SHADERFORMAT_SPIRV;info.stage=stage;info.num_uniform_buffers=uniforms;info.num_samplers=samplers;info.num_storage_buffers=storage;
      auto* result=SDL_CreateGPUShader(device,&info);if(!result)throw gpu_error("3D shader creation failed");return result;
    };
    const auto release=[&](SDL_GPUShader* value){SDL_ReleaseGPUShader(device,value);};
    msaa_supported=SDL_GPUTextureSupportsSampleCount(device,scene_format,SDL_GPU_SAMPLECOUNT_4)&&
      SDL_GPUTextureSupportsSampleCount(device,SDL_GPU_TEXTUREFORMAT_D32_FLOAT,SDL_GPU_SAMPLECOUNT_4);
    std::unique_ptr<SDL_GPUShader,decltype(release)> vertex(shader(shaders::scene3d_vert,SDL_GPU_SHADERSTAGE_VERTEX,0,0,1),release),fragment(shader(shaders::scene3d_frag,SDL_GPU_SHADERSTAGE_FRAGMENT,1,14,1),release);
    SDL_GPUVertexBufferDescription buffer{0,sizeof(Vertex3D),SDL_GPU_VERTEXINPUTRATE_VERTEX,0};
    const SDL_GPUVertexAttribute attributes[]{{0,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,offsetof(Vertex3D,position)},{1,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,offsetof(Vertex3D,normal)},{2,0,SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,offsetof(Vertex3D,uv)}};
    for(int index=0;index<4;++index){
      SDL_GPUColorTargetDescription color{};color.format=scene_format;
      auto& blend=color.blend_state;blend.enable_blend=(index&1)!=0;
      blend.src_color_blendfactor=blend.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
      blend.dst_color_blendfactor=blend.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
      blend.color_blend_op=blend.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
      SDL_GPUGraphicsPipelineCreateInfo info{};info.vertex_shader=vertex.get();info.fragment_shader=fragment.get();
      info.vertex_input_state={&buffer,1,attributes,3};info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
      info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;info.rasterizer_state.cull_mode=(index&2)?SDL_GPU_CULLMODE_NONE:SDL_GPU_CULLMODE_BACK;
      info.rasterizer_state.front_face=SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;info.rasterizer_state.enable_depth_clip=true;
      info.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;
      info.depth_stencil_state.compare_op=SDL_GPU_COMPAREOP_LESS_OR_EQUAL;info.depth_stencil_state.enable_depth_test=true;info.depth_stencil_state.enable_depth_write=(index&1)==0;
      info.target_info.color_target_descriptions=&color;info.target_info.num_color_targets=1;info.target_info.depth_stencil_format=SDL_GPU_TEXTUREFORMAT_D32_FLOAT;info.target_info.has_depth_stencil_target=true;
      pipelines[index]=SDL_CreateGPUGraphicsPipeline(device,&info);if(!pipelines[index])throw gpu_error("3D depth pipeline creation failed");
      if(msaa_supported){
        info.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_4;
        pipelines_ms4[index]=SDL_CreateGPUGraphicsPipeline(device,&info);if(!pipelines_ms4[index])throw gpu_error("3D MSAA pipeline creation failed");
      }
    }
    // Depth-only directional shadow pipeline: same vertex layout and SSBO
    // transform convention as the scene pass, no colour target, and a
    // slope-scaled rasterizer bias that complements the shader's constant
    // receiver bias. Unculled rasterization keeps thin hulls casting.
    shadow_supported=SDL_GPUTextureSupportsFormat(device,SDL_GPU_TEXTUREFORMAT_D32_FLOAT,SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER);
    if(shadow_supported){
      std::unique_ptr<SDL_GPUShader,decltype(release)> shadow_vertex(shader(shaders::scene3d_shadow_vert,SDL_GPU_SHADERSTAGE_VERTEX,0,0,1),release),shadow_fragment(shader(shaders::scene3d_shadow_frag,SDL_GPU_SHADERSTAGE_FRAGMENT,0,1),release);
      SDL_GPUGraphicsPipelineCreateInfo info{};info.vertex_shader=shadow_vertex.get();info.fragment_shader=shadow_fragment.get();
      info.vertex_input_state={&buffer,1,attributes,3};info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
      info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
      info.rasterizer_state.front_face=SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;info.rasterizer_state.enable_depth_clip=true;
      info.rasterizer_state.enable_depth_bias=true;info.rasterizer_state.depth_bias_slope_factor=1.5f;
      info.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;
      info.depth_stencil_state.compare_op=SDL_GPU_COMPAREOP_LESS_OR_EQUAL;info.depth_stencil_state.enable_depth_test=true;info.depth_stencil_state.enable_depth_write=true;
      info.target_info.num_color_targets=0;info.target_info.depth_stencil_format=SDL_GPU_TEXTUREFORMAT_D32_FLOAT;info.target_info.has_depth_stencil_target=true;
      shadow_pipeline=SDL_CreateGPUGraphicsPipeline(device,&info);if(!shadow_pipeline)throw gpu_error("3D shadow pipeline creation failed");
    }
    if(hdr){
      std::unique_ptr<SDL_GPUShader,decltype(release)> tonemap_vertex(shader(shaders::tonemap_vert,SDL_GPU_SHADERSTAGE_VERTEX,0,0),release),tonemap_fragment(shader(shaders::tonemap_frag,SDL_GPU_SHADERSTAGE_FRAGMENT,1,1),release);
      SDL_GPUColorTargetDescription color{};color.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
      SDL_GPUGraphicsPipelineCreateInfo info{};info.vertex_shader=tonemap_vertex.get();info.fragment_shader=tonemap_fragment.get();
      info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
      info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;info.rasterizer_state.front_face=SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;info.rasterizer_state.enable_depth_clip=true;
      info.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;
      info.target_info.color_target_descriptions=&color;info.target_info.num_color_targets=1;
      tonemap_pipeline=SDL_CreateGPUGraphicsPipeline(device,&info);if(!tonemap_pipeline)throw gpu_error("3D tonemap pipeline creation failed");
    }
    streamer.set_pinned(stream_id_for(white),true);
  }
  std::shared_ptr<Geometry> geometry(std::shared_ptr<const Mesh3D> resource){
    if(auto it=meshes.find(resource.get());it!=meshes.end()){it->second->use=++serial;return it->second;}
    auto result=std::make_shared<Geometry>(device);result->owner=std::move(resource);result->use=++serial;
    const auto vb=static_cast<Uint32>(result->owner->vertices().size()*sizeof(Vertex3D)),ib=static_cast<Uint32>(result->owner->indices().size()*sizeof(std::uint32_t));
    evict(meshes,stats.mesh_cache_bytes,result->bytes(),maximum_mesh3d_cache_bytes);
    SDL_GPUBufferCreateInfo info{};info.usage=SDL_GPU_BUFFERUSAGE_VERTEX;info.size=vb;result->vertices=SDL_CreateGPUBuffer(device,&info);if(!result->vertices)throw gpu_error("3D vertex buffer creation failed");
    info.usage=SDL_GPU_BUFFERUSAGE_INDEX;info.size=ib;result->indices=SDL_CreateGPUBuffer(device,&info);if(!result->indices)throw gpu_error("3D index buffer creation failed");
    Transfer transfer(device,vb+ib);auto* memory=static_cast<std::byte*>(transfer.map());std::memcpy(memory,result->owner->vertices().data(),vb);std::memcpy(memory+vb,result->owner->indices().data(),ib);SDL_UnmapGPUTransferBuffer(device,transfer.value);
    Command command(device,gpu_pending);auto* copy=SDL_BeginGPUCopyPass(command.value);if(!copy)throw gpu_error("3D mesh copy pass failed");
    SDL_GPUTransferBufferLocation from{transfer.value,0};SDL_GPUBufferRegion to{result->vertices,0,vb};SDL_UploadToGPUBuffer(copy,&from,&to,false);
    from.offset=vb;to={result->indices,0,ib};SDL_UploadToGPUBuffer(copy,&from,&to,false);SDL_EndGPUCopyPass(copy);command.submit();
    meshes.emplace(result->owner.get(),result);stats.mesh_cache_bytes+=result->bytes();++stats.mesh_uploads;return result;
  }
  engine::TextureId stream_id_for(const std::shared_ptr<const RgbaImage>& image){
    if(const auto it=stream_ids.find(image.get());it!=stream_ids.end()){
      const auto owner_it=stream_owners.find(it->second);
      if(owner_it!=stream_owners.end()&&owner_it->second.lock())return it->second;
    }
    // The name must stay unique per registration: the streamer dedups by name,
    // and reusing a dead image's name would silently return its retired id.
    engine::TextureDesc desc{};desc.name="tex:"+std::to_string(reinterpret_cast<std::uintptr_t>(image.get()))+"#"+std::to_string(++stream_registrations);
    if(!image->cooked_mips().empty()){
      SDL_GPUTextureFormat format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
      switch(image->cooked_format()){
        case TextureFormat::Bc7:format=SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM;break;
        case TextureFormat::Bc5:format=SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM;break;
        case TextureFormat::Bc4:format=SDL_GPU_TEXTUREFORMAT_BC4_R_UNORM;break;
        default:break;
      }
      if(SDL_GPUTextureSupportsFormat(device,format,SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_SAMPLER))
        for(const auto& m:image->cooked_mips())desc.mip_bytes.push_back(m.blocks.size());
      else
        for(const auto& m:image->cooked_mips())desc.mip_bytes.push_back(static_cast<std::uint64_t>(m.width)*m.height*4);
    }else if(!image->bc1_mips().empty()&&SDL_GPUTextureSupportsFormat(device,SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM,SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_SAMPLER))
      for(const auto& m:image->bc1_mips())desc.mip_bytes.push_back(m.blocks.size());
    else{
      const auto levels=texture_mip_layout3d(image.get()).levels;
      for(std::uint32_t i=0;i<levels;++i)desc.mip_bytes.push_back(static_cast<std::uint64_t>(std::max(1,image->width()>>i))*std::max(1,image->height()>>i)*4);
    }
    const auto id=streamer.register_texture(std::move(desc));stream_ids[image.get()]=id;stream_owners[id]=image;return id;
  }
  // advance_frame() yields the frame's residency changes: evictions apply
  // whole-texture (backend granularity); loads stay lazy — the first bind
  // uploads. Unadmitted requests bind the pinned fallback, keeping GPU
  // residency under the streamer's byte budget.
  void apply_streaming(){
    std::unordered_set<engine::TextureId> touched;
    for(const auto& change:streamer.advance_frame(++stream_frame))touched.insert(change.id);
    for(const auto id:touched){
      const auto owner_it=stream_owners.find(id);
      if(owner_it==stream_owners.end())continue;
      // An expired owner means the image is gone; its cache entry died with
      // it (the Texture holds the owning shared_ptr), so nothing needs erasing.
      const auto live=owner_it->second.lock();
      if(!live)continue;
      const auto it=textures.find(live.get());if(it==textures.end())continue;
      const auto finest=streamer.finest_resident_mip(id);
      // Any residency change invalidates the cached granularity — demotions
      // and promotions alike re-upload the new resident tail on next bind.
      if(!finest||*finest!=it->second->base_mip){stats.texture_cache_bytes-=it->second->bytes();stats.streamed_evicted_bytes+=it->second->bytes();textures.erase(it);}
    }
  }
  std::shared_ptr<Texture> texture(std::shared_ptr<const RgbaImage> resource){
    if(!resource)resource=white;
    if(auto it=textures.find(resource.get());it!=textures.end()){it->second->use=++serial;return it->second;}
    std::uint32_t base_mip=0;
    if(resource.get()!=white.get())if(const auto sid=stream_ids.find(resource.get());sid!=stream_ids.end()){
      const auto finest=streamer.finest_resident_mip(sid->second);
      if(!finest){++stats.streamed_fallbacks;return texture(white);}
      base_mip=*finest;if(base_mip)++stats.streamed_partial_binds;
    }
    auto result=std::make_shared<Texture>(device);result->owner=std::move(resource);result->use=++serial;
    const auto& image=*result->owner;const auto layout=texture_mip_layout3d(&image);
    if(!image.cooked_mips().empty()){
      SDL_GPUTextureFormat format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
      switch(image.cooked_format()){
        case TextureFormat::Bc7:format=SDL_GPU_TEXTUREFORMAT_BC7_RGBA_UNORM;break;
        case TextureFormat::Bc5:format=SDL_GPU_TEXTUREFORMAT_BC5_RG_UNORM;break;
        case TextureFormat::Bc4:format=SDL_GPU_TEXTUREFORMAT_BC4_R_UNORM;break;
        default:break;
      }
      const bool supported=SDL_GPUTextureSupportsFormat(device,format,SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_SAMPLER);
      std::vector<Bc1MipLevel> fallback_mips;
      if(!supported){format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;for(const auto& m:image.cooked_mips())fallback_mips.push_back({m.width,m.height,decode_texture_level(image.cooked_format(),m)});}
      const auto& levels=supported?image.cooked_mips():fallback_mips;
      const auto base=std::min(base_mip,static_cast<std::uint32_t>(levels.size()-1));result->base_mip=base;
      const bool block=format!=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
      result->gpu_bytes=0;for(std::size_t i=base;i<levels.size();++i)result->gpu_bytes+=levels[i].blocks.size();
      if(textures.size()>=maximum_scene3d_resource_entries)evict(textures,stats.texture_cache_bytes,0,std::numeric_limits<std::size_t>::max());
      result->texture=make_texture(device,levels[base].width,levels[base].height,format,SDL_GPU_TEXTUREUSAGE_SAMPLER,static_cast<Uint32>(levels.size()-base));
      // Every transfer offset is aligned, including the tiny 1x1 RGBA tail.
      std::size_t transfer_bytes=0;for(std::size_t i=base;i<levels.size();++i)transfer_bytes+=((levels[i].blocks.size()+15)/16)*16;
      Transfer transfer(device,static_cast<Uint32>(transfer_bytes));auto* memory=static_cast<std::uint8_t*>(transfer.map());std::size_t offset=0;
      for(std::size_t i=base;i<levels.size();++i){std::memcpy(memory+offset,levels[i].blocks.data(),levels[i].blocks.size());offset+=((levels[i].blocks.size()+15)/16)*16;}SDL_UnmapGPUTransferBuffer(device,transfer.value);
      Command command(device,gpu_pending);auto* copy=SDL_BeginGPUCopyPass(command.value);if(!copy)throw gpu_error("Cooked texture upload failed");offset=0;
      for(Uint32 level=base;level<levels.size();++level){const auto&m=levels[level];SDL_GPUTextureTransferInfo from{};from.transfer_buffer=transfer.value;from.offset=static_cast<Uint32>(offset);from.pixels_per_row=block?(m.width+3)/4*4:m.width;from.rows_per_layer=block?(m.height+3)/4*4:m.height;
        SDL_GPUTextureRegion to{};to.texture=result->texture;to.mip_level=level-base;to.w=m.width;to.h=m.height;to.d=1;SDL_UploadToGPUTexture(copy,&from,&to,false);offset+=((m.blocks.size()+15)/16)*16;
      }SDL_EndGPUCopyPass(copy);command.submit();textures.emplace(result->owner.get(),result);stats.texture_cache_bytes+=result->bytes();++stats.texture_uploads;return result;
    }
    const bool compressed=!image.bc1_mips().empty()&&SDL_GPUTextureSupportsFormat(device,SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM,SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_SAMPLER);
    const auto bc1_base=image.bc1_mips().empty()?0u:std::min(base_mip,static_cast<std::uint32_t>(image.bc1_mips().size()-1));result->base_mip=bc1_base;
    if(compressed){result->gpu_bytes=0;for(std::size_t i=bc1_base;i<image.bc1_mips().size();++i)result->gpu_bytes+=image.bc1_mips()[i].blocks.size();}
    if(textures.size()>=maximum_scene3d_resource_entries)evict(textures,stats.texture_cache_bytes,0,std::numeric_limits<std::size_t>::max());
    if(compressed){
      const auto& tail=image.bc1_mips()[bc1_base];
      result->texture=make_texture(device,tail.width,tail.height,SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM,SDL_GPU_TEXTUREUSAGE_SAMPLER,static_cast<Uint32>(image.bc1_mips().size()-bc1_base));
      Transfer transfer(device,static_cast<Uint32>(result->gpu_bytes));auto* data=static_cast<std::uint8_t*>(transfer.map());std::size_t offset=0;
      for(std::size_t i=bc1_base;i<image.bc1_mips().size();++i){std::memcpy(data+offset,image.bc1_mips()[i].blocks.data(),image.bc1_mips()[i].blocks.size());offset+=image.bc1_mips()[i].blocks.size();}SDL_UnmapGPUTransferBuffer(device,transfer.value);
      Command command(device,gpu_pending);auto* copy=SDL_BeginGPUCopyPass(command.value);if(!copy)throw gpu_error("Compressed texture copy pass failed");offset=0;
      for(Uint32 level=bc1_base;level<image.bc1_mips().size();++level){const auto& m=image.bc1_mips()[level];
        SDL_GPUTextureTransferInfo from{};from.transfer_buffer=transfer.value;from.offset=static_cast<Uint32>(offset);from.pixels_per_row=(m.width+3)/4*4;from.rows_per_layer=(m.height+3)/4*4;
        SDL_GPUTextureRegion to{};to.texture=result->texture;to.mip_level=level-bc1_base;to.w=m.width;to.h=m.height;to.d=1;SDL_UploadToGPUTexture(copy,&from,&to,false);offset+=m.blocks.size();
      }SDL_EndGPUCopyPass(copy);command.submit();
      textures.emplace(result->owner.get(),result);stats.texture_cache_bytes+=result->bytes();++stats.texture_uploads;return result;
    }
    const auto tail_levels=layout.levels-base_mip;result->base_mip=base_mip;
    std::vector<std::uint8_t> degraded;int base_width=image.width(),base_height=image.height();
    if(base_mip){degraded=image.pixels();for(std::uint32_t i=0;i<base_mip;++i)degraded=downsample_rgba2x(degraded,base_width,base_height,base_width,base_height);}
    const auto& level0=base_mip?degraded:image.pixels();
    result->gpu_bytes=0;{int w=base_width,h=base_height;for(std::uint32_t i=0;i<tail_levels;++i){result->gpu_bytes+=static_cast<std::size_t>(w)*h*4;if(w==1&&h==1)break;w=std::max(1,w/2);h=std::max(1,h/2);}}
    result->texture=make_texture(device,base_width,base_height,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,SDL_GPU_TEXTUREUSAGE_SAMPLER|SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,tail_levels);
    Transfer transfer(device,static_cast<Uint32>(level0.size()));std::memcpy(transfer.map(),level0.data(),level0.size());SDL_UnmapGPUTransferBuffer(device,transfer.value);
    Command command(device,gpu_pending);auto* copy=SDL_BeginGPUCopyPass(command.value);if(!copy)throw gpu_error("3D texture copy pass failed");
    SDL_GPUTextureTransferInfo from{};from.transfer_buffer=transfer.value;from.pixels_per_row=base_width;from.rows_per_layer=base_height;
    SDL_GPUTextureRegion to{};to.texture=result->texture;to.w=base_width;to.h=base_height;to.d=1;
    SDL_UploadToGPUTexture(copy,&from,&to,false);SDL_EndGPUCopyPass(copy);
    // Generate once, outside the copy pass and before any consumer samples it.
    // All four channels are data: alpha may encode opacity OR surface height.
    // SDL 3.4.16's Vulkan GenerateMipmaps shifts dimensions without clamping
    // to one, yielding empty blits for rectangular tails and every 1D mip.
    // Explicit blit regions keep both axes valid through the final 1x1 level.
    Uint32 width=base_width,height=base_height;
    for(Uint32 level=1;level<tail_levels;++level){
      const auto next_width=std::max(1u,width/2),next_height=std::max(1u,height/2);
      SDL_GPUBlitInfo blit{};blit.source.texture=blit.destination.texture=result->texture;
      blit.source.mip_level=level-1;blit.source.w=width;blit.source.h=height;
      blit.destination.mip_level=level;blit.destination.w=next_width;blit.destination.h=next_height;
      blit.load_op=SDL_GPU_LOADOP_DONT_CARE;blit.filter=SDL_GPU_FILTER_LINEAR;
      SDL_BlitGPUTexture(command.value,&blit);width=next_width;height=next_height;
    }
    command.submit();
    textures.emplace(result->owner.get(),result);stats.texture_cache_bytes+=result->bytes();++stats.texture_uploads;return result;
  }
  std::unique_ptr<Target> target(int w,int h){
    auto result=std::make_unique<Target>(device);result->width=w;result->height=h;
    result->color=make_texture(device,w,h,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER);
    result->depth=make_texture(device,w,h,SDL_GPU_TEXTUREFORMAT_D32_FLOAT,SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
    if(hdr){
      // Coarse levels feed the tonemap's bloom taps; COLOR_TARGET lets the
      // backend blit-generate the chain between the scene and resolve passes.
      const Uint32 levels=static_cast<Uint32>(std::min(5.0,std::floor(std::log2(static_cast<float>(std::max(w,h))))+1.0));
      result->hdr_levels=levels;
      result->hdr=make_texture(device,w,h,SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER,levels);}
    const auto props=SDL_CreateProperties();if(!props)throw gpu_error("3D composite properties allocation failed");
    const auto destroy=[](SDL_PropertiesID p){SDL_DestroyProperties(p);};
    try{
      checked(SDL_SetPointerProperty(props,SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER,result->color)&&
        SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,SDL_PIXELFORMAT_RGBA32)&&
        SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER,SDL_TEXTUREACCESS_STATIC)&&
        SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER,w)&&SDL_SetNumberProperty(props,SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER,h),"3D composite properties failed");
      result->composite=SDL_CreateTextureWithProperties(renderer,props);destroy(props);
    }catch(...){destroy(props);throw;}
    if(!result->composite)throw gpu_error("3D compositor texture creation failed");
    checked(SDL_SetTextureBlendMode(result->composite,SDL_BLENDMODE_BLEND_PREMULTIPLIED)&&SDL_SetTextureScaleMode(result->composite,SDL_SCALEMODE_LINEAR),"3D compositor setup failed");return result;
  }
  void render(const Scene3DView& view,Target& target){
    // Per-view policy is resolved once: quality gates expensive sampling
    // (aniso, cubic magnification, emission-volume steps) before uniform
    // fill, and the post/debug uniforms feed the pass below.
    // Render resolution is the target's — render_scale shrinks it below
    // the destination extent and composite() upscales linearly.
    const auto res_w=static_cast<std::uint32_t>(target.width),res_h=static_cast<std::uint32_t>(target.height);
    const float res_aspect=static_cast<float>(res_w)/static_cast<float>(res_h);
    const auto& opt=view.options;
    const bool low_tier=opt.quality==RenderQuality3D::Low;
    struct Draw {const MeshInstance3D* instance;PreparedInstance3D transform;std::shared_ptr<Geometry> mesh;std::shared_ptr<Texture> image,optical,environment,normal,properties,cloud,shadow,next,emissive,mr;
      // Screen-door LOD keep-probability (1 = draw every fragment).
      float lod_keep{1.f};
      // Camera inside an emission volume's proxy: rasterize both faces
      // and lift the shader's front-face gate so the interior marches.
      bool inside_volume{false};
      // DebugView3D::Lod tint class on texture_options.w: 0 full mesh,
      // 1..8 chain level, 10 group proxy.
      float lod_class{0.f};};
    std::vector<Draw> draws;draws.reserve(view.scene->instances().size());
    // Screen-space LOD uses the same px-per-world-unit convention as the
    // streamer footprint so both agree on which level is submitted.
    const auto& lod_camera=view.scene->camera();
    const float lod_focal=lod_camera.projection==Projection3D::Orthographic?static_cast<float>(res_h)/std::max(lod_camera.orthographic_height,1e-6f)
      :static_cast<float>(res_h)/std::max(2.f*std::tan(lod_camera.vertical_fov_radians*.5f),1e-6f);
    // Group proxy collapse: each named group accumulates the merged
    // view-space bounding sphere of its contributing members (frustum-
    // and range-culled members neither contribute nor collapse). When
    // the merged sphere's projected diameter drops below the authored
    // lod_group_pixels the whole group renders as one view-aligned
    // proxy draw — a fleet/cluster impostor for extreme zoom-out.
    // Volume proxies are excluded: a marched volume cannot collapse
    // into a surface proxy.
    struct GroupBounds{double x,y,z,r;const MeshInstance3D*rep{};std::size_t count{};bool collapse{};float share{};};
    std::map<std::string,GroupBounds> groups;
    std::unordered_map<const MeshInstance3D*,GroupBounds*> group_of;
    for(const auto& instance:view.scene->instances()){
      if(instance.lod_group.empty()||!instance.lod_group_proxy||instance.lod_group_pixels<=0.f)continue;
      if(instance.material.surface_effect&&instance.material.surface_effect->volume_depth>0.f)continue;
      const auto prepared=prepare_instance3d(lod_camera,instance,res_aspect);
      if(!prepared.visible)continue;
      const double gx=prepared.model_view.values[12],gy=prepared.model_view.values[13],gz=prepared.model_view.values[14];
      const double radius=static_cast<double>(instance.scale)*instance.mesh->bounding_radius();
      if(instance.visible_range>0.f&&std::sqrt(gx*gx+gy*gy+gz*gz)>static_cast<double>(instance.visible_range)+radius)continue;
      auto&g=groups[instance.lod_group];
      if(g.count++==0){g.x=gx;g.y=gy;g.z=gz;g.r=radius;g.rep=&instance;}
      else{
        const double dx=gx-g.x,dy=gy-g.y,dz=gz-g.z;const double d=std::sqrt(dx*dx+dy*dy+dz*dz);
        if(d+g.r<=radius+1e-9){g.x=gx;g.y=gy;g.z=gz;g.r=radius;}
        else if(d+radius>g.r+1e-9){const double nr=(d+g.r+radius)*.5;const double k=(nr-g.r)/d;
          g.x+=dx*k;g.y+=dy*k;g.z+=dz*k;g.r=nr;}
      }
      group_of[&instance]=&g;
    }
    for(auto&[group_name,g]:groups){
      const double dist=std::sqrt(g.x*g.x+g.y*g.y+g.z*g.z);
      const float diameter=2.f*static_cast<float>(g.r)*lod_focal/
        (lod_camera.projection==Projection3D::Orthographic?1.f:static_cast<float>(std::max(dist,1e-4)));
      const float threshold=g.rep->lod_group_pixels;
      g.collapse=diameter<threshold;
      // The representative's lod_fade widens the collapse into a
      // screen-door band: inside it members thin out by 1-p while the
      // proxy keeps p through the complementary mask — the swap is
      // continuous, like a chain-level crossfade.
      if(!g.collapse&&!low_tier&&g.rep->lod_fade>0.f){
        const float top=threshold*(1.f+g.rep->lod_fade);
        if(diameter<top)g.share=std::clamp((top-diameter)/(threshold*g.rep->lod_fade),0.f,1.f);
      }
    }
    // Scene environment probe: a baked capture (environmentCapture)
    // preferred, else the authored view-level equirect fills the IBL
    // slot for PBR materials which opted in (environment_strength > 0)
    // but authored no map of their own — one shared starfield per scene.
    const std::shared_ptr<Texture> scene_env=[&]{
      if(view.scene->environment_capture().enabled)
        if(const auto it=probe_cache.find(probe_key(view));it!=probe_cache.end())return texture(it->second);
      return view.scene->environment()?texture(view.scene->environment()):nullptr;}();
    for(const auto& instance:view.scene->instances()){
      auto prepared=prepare_instance3d(view.scene->camera(),instance,res_aspect);
      if(!prepared.visible){++stats.culled_instances;continue;}
      // A card mesh is camera-facing: its view-space rotation collapses
      // to uniform scale (position and depth stay) so the impostor always
      // presents its face regardless of instance or camera orientation.
      // Per drawn mesh — a fading pair can mix a card with solid geometry.
      const auto facing=[&](const std::shared_ptr<const Mesh3D>& mesh){
        PreparedInstance3D out=prepared;
        if(mesh->billboard()){
          for(int c=0;c<3;++c)for(int r=0;r<3;++r)
            out.model_view.values[c*4+r]=c==r?instance.scale:0.f;
          out.model_view_projection=multiply(projection3d_matrix(lod_camera,res_aspect),out.model_view);
        }
        return out;};
      std::shared_ptr<const Mesh3D> drawn_mesh=instance.mesh;
      std::size_t lod_level=0;float lod_share=0.f;
      // The view-space translation length is the camera distance —
      // identical to the streamer footprint's world-space delta.
      const float vx=prepared.model_view.values[12],vy=prepared.model_view.values[13],vz=prepared.model_view.values[14];
      const float dist=std::sqrt(vx*vx+vy*vy+vz*vz);
      if(!instance.lod_meshes.empty()){
        const float diameter=2.f*instance.scale*static_cast<float>(instance.mesh->bounding_radius())*lod_focal/
          (lod_camera.projection==Projection3D::Orthographic?1.f:std::max(dist,1e-4f));
        lod_level=select_lod3d_level(instance,diameter);
        if(lod_level>0){drawn_mesh=instance.lod_meshes[lod_level-1];++stats.lod_instances;}
        if(!low_tier)lod_share=lod3d_fade_share(instance,diameter);
      }
      // Visible-range fade-out: inside the authored band the single draw
      // keeps a shrinking share of its pixels through the same screen-door
      // mask the LOD crossfade uses — the fade completes exactly at the
      // existing range+radius cull edge, so the disappearance distance is
      // unchanged. The band overlaps the cull boundary only, so a fully
      // faded instance is already culled upstream; keep<=0 is a guard.
      float range_keep=1.f;
      if(!low_tier&&instance.visible_range>0.f&&instance.visible_fade>0.f){
        const double edge=static_cast<double>(instance.visible_range)+instance.scale*instance.mesh->bounding_radius();
        range_keep=std::clamp(static_cast<float>((edge-dist)/(instance.visible_fade*instance.visible_range)),0.f,1.f);
      }
      if(range_keep<=0.f){++stats.culled_instances;continue;}
      if(range_keep<1.f)++stats.visible_fades;
      const auto& optical=instance.material.dielectric;
      const auto& pbr=instance.material.pbr;
      // Disabled optics reuse the existing texture binding, without an upload.
      auto surface=texture(instance.material.texture);
      // Environment slot order: authored map, then the scene probe for
      // PBR materials that opted in with environment_strength, else the
      // surface placeholder the shader's zero-strength gate skips.
      // Dielectrics always carry an authored map — validation requires it.
      const auto env_map=[&]{
        if(optical)return texture(optical->environment);
        return pbr&&pbr->environment?texture(pbr->environment)
          :(pbr&&pbr->environment_strength>0.f&&scene_env?scene_env:surface);};
      const auto& response=instance.material.surface_response;
      // Inside a screen-door transition band the view submits the same
      // material twice: the selected level keeps 1-p of its pixels, the
      // next-coarser level keeps p — complementary discards partition
      // the silhouette so opaque geometry crossfades without blending.
      // Low tier keeps the hard switch (one draw, zero cost).
      // Inside the range fade band the whole object is thinning out —
      // which level shows stops mattering, so the LOD pair degrades to the
      // selected level's single thinned draw.
      const bool fading=lod_share>0.f&&lod_level<instance.lod_meshes.size()&&range_keep>=1.f;
      if(fading)++stats.lod_fades;
      // Camera inside the proxy sphere: the outward-facing walls all
      // turn away, so the draw needs both faces rasterized and the
      // fragment gate lifted (packed into volume_options.y bit 7).
      const bool inside_volume=instance.material.surface_effect&&
        instance.material.surface_effect->volume_depth>0.f&&
        dist<instance.scale*instance.mesh->bounding_radius();
      // Group proxy: inside the collapse zone the representative member
      // carries the whole group as one view-aligned draw centred on the
      // merged sphere, scaled to cover it — the identity rotation is the
      // same collapse `billboard` applies, so a `card:` proxy always
      // presents its face. Inside the transition band the members thin
      // out by 1-p while the proxy keeps the complementary p.
      float group_keep=1.f;
      if(const auto it=group_of.find(&instance);it!=group_of.end()){
        const GroupBounds&g=*it->second;
        const auto emit_proxy=[&](float keep){
          const auto& proxy_mesh=instance.lod_group_proxy;
          const double ps=g.r/std::max(static_cast<double>(proxy_mesh->bounding_radius()),1e-9);
          PreparedInstance3D proxy_t{};
          for(int c=0;c<3;++c)proxy_t.model_view.values[c*4+c]=static_cast<float>(ps);
          proxy_t.model_view.values[12]=static_cast<float>(g.x);
          proxy_t.model_view.values[13]=static_cast<float>(g.y);
          proxy_t.model_view.values[14]=static_cast<float>(g.z);
          proxy_t.model_view.values[15]=1.f;
          proxy_t.model_view_projection=multiply(projection3d_matrix(lod_camera,res_aspect),proxy_t.model_view);
          proxy_t.camera_depth=static_cast<float>(-g.z);proxy_t.visible=true;
          draws.push_back({&instance,proxy_t,geometry(proxy_mesh),surface,
            optical?texture(optical->surface):surface,
            env_map(),
            response?texture(response->normal):surface,response?texture(response->properties):surface,response?texture(response->cloud_shadow):surface,
            instance.material.shadow&&instance.material.shadow->opacity_map?texture(instance.material.shadow->opacity_map):surface,
            instance.material.surface_effect?texture(instance.material.surface_effect->next_texture):surface,
            pbr&&pbr->emissive?texture(pbr->emissive):texture(white),
            pbr&&pbr->metallic_roughness?texture(pbr->metallic_roughness):texture(white),keep});
          draws.back().lod_class=10.f;};
        if(g.collapse){++stats.lod_groups;if(g.rep==&instance)emit_proxy(1.f);continue;}
        if(g.share>0.f){group_keep=1.f-g.share;
          if(g.rep==&instance){++stats.lod_fades;emit_proxy(-g.share);}}
      }
      draws.push_back({&instance,facing(drawn_mesh),geometry(drawn_mesh),surface,
        optical?texture(optical->surface):surface,
        env_map(),
        response?texture(response->normal):surface,response?texture(response->properties):surface,response?texture(response->cloud_shadow):surface,
        instance.material.shadow&&instance.material.shadow->opacity_map?texture(instance.material.shadow->opacity_map):surface,
        instance.material.surface_effect?texture(instance.material.surface_effect->next_texture):surface,
        pbr&&pbr->emissive?texture(pbr->emissive):texture(white),
        pbr&&pbr->metallic_roughness?texture(pbr->metallic_roughness):texture(white),
        (fading?1.f-lod_share:range_keep)*group_keep});
      draws.back().inside_volume=inside_volume;
      draws.back().lod_class=static_cast<float>(lod_level);
      if(fading)draws.push_back({&instance,facing(instance.lod_meshes[lod_level]),geometry(instance.lod_meshes[lod_level]),surface,
        optical?texture(optical->surface):surface,
        env_map(),
        response?texture(response->normal):surface,response?texture(response->properties):surface,response?texture(response->cloud_shadow):surface,
        instance.material.shadow&&instance.material.shadow->opacity_map?texture(instance.material.shadow->opacity_map):surface,
        instance.material.surface_effect?texture(instance.material.surface_effect->next_texture):surface,
        pbr&&pbr->emissive?texture(pbr->emissive):texture(white),
        pbr&&pbr->metallic_roughness?texture(pbr->metallic_roughness):texture(white),
        -lod_share*group_keep}); // negative = keep the high mask (complement of 1-p)
      if(fading){draws.back().inside_volume=inside_volume;draws.back().lod_class=static_cast<float>(lod_level+1);}
    }
    // Engine DrawBatcher owns submission ordering/batching: opaque groups by
    // (material,mesh), transparent stays back-to-front. A material_id interns
    // everything the binding must share — all eight textures plus the flags
    // that change sampler/pipeline selection — so equal ids mean one
    // instanced call can serve the whole run.
    using MaterialKey=std::tuple<bool,bool,bool,const Texture*,const Texture*,const Texture*,const Texture*,const Texture*,const Texture*,const Texture*,const Texture*,const Texture*,const Texture*>;
    std::map<MaterialKey,std::uint32_t> material_ids;std::map<const Mesh3D*,std::uint32_t> mesh_ids;
    engine::DrawBatcher batcher;batcher.begin_frame();
    for(std::size_t submission=0;submission<draws.size();++submission){
      const auto& d=draws[submission];
      const auto& tm=d.instance->material.texture_tiling;
      const MaterialKey key{d.instance->material.double_sided||d.inside_volume,d.instance->material.anisotropic_texture&&!low_tier,tm.x!=1.f||tm.y!=1.f,
        d.image.get(),d.optical.get(),d.environment.get(),d.normal.get(),d.properties.get(),d.cloud.get(),d.shadow.get(),d.next.get(),d.emissive.get(),d.mr.get()};
      engine::DrawItem item{};item.material_id=material_ids.try_emplace(key,static_cast<std::uint32_t>(material_ids.size())).first->second;
      item.mesh_id=mesh_ids.try_emplace(d.mesh->owner.get(),static_cast<std::uint32_t>(mesh_ids.size())).first->second;
      item.instance_index=static_cast<std::uint32_t>(submission);item.depth=d.transform.camera_depth;item.transparent=d.instance->material.transparent;
      batcher.submit(item);
    }
    batcher.build();const auto& sorted=batcher.sorted_items();
    std::vector<VertexUniform> vertex_data(sorted.size());std::vector<FragmentUniform> fragment_data(sorted.size());
    // World-space point lights become view-space once per view; every
    // material record carries the same four slots so instanced draws share.
    std::array<std::array<float,4>,4> pl_position{},pl_energy{},pl_cone{};
    std::array<float,4> pl_outer{};
    {
      const auto& cam=view.scene->camera();
      const Quaternion inv{-cam.orientation.x,-cam.orientation.y,-cam.orientation.z,cam.orientation.w};
      for(std::size_t i=0;i<view.scene->point_lights().size()&&i<4;++i){
        const auto& l=view.scene->point_lights()[i];
        const Vec3 v=rotate_vec(inv,{static_cast<float>(l.position.x-cam.position.x),static_cast<float>(l.position.y-cam.position.y),static_cast<float>(l.position.z-cam.position.z)});
        pl_position[i]={v.x,v.y,v.z,l.range};pl_energy[i]={l.color.x,l.color.y,l.color.z,l.intensity};
        // Spot cones rotate into view space with no translation; a zero
        // direction leaves the slot omni.
        const Vec3 d=rotate_vec(inv,l.spot_direction);
        pl_cone[i]={d.x,d.y,d.z,l.spot_inner};pl_outer[i]=l.spot_outer;}
    }
    for(std::size_t slot=0;slot<sorted.size();++slot){
      const auto& draw=draws[sorted[slot].instance_index];
      const auto& material=draw.instance->material;const auto light=material.light_direction.value_or(view.scene->light_direction());
      const auto shadow=material.shadow?prepare_shadow3d(view.scene->camera(),*draw.instance,light):PreparedShadow3D{};
      VertexUniform& vertex=vertex_data[slot];vertex={draw.transform.model_view_projection,draw.transform.model_view,shadow.from_model};
      FragmentUniform& fragment=fragment_data[slot];fragment.tint={material.tint.r/255.f,material.tint.g/255.f,material.tint.b/255.f,material.tint.a/255.f};
      fragment.light={light.x,light.y,light.z,0};fragment.parameters={material.ambient,material.diffuse,material.opacity,material.dark_side_strength};
      const auto q=view.scene->camera().orientation;fragment.camera_orientation={q.x,q.y,q.z,q.w};
      fragment.view_options[3]=material.linear_light?1.f:0.f;
      fragment.view_options[0]=view.scene->camera().projection==Projection3D::Orthographic?1.f:0.f;
      fragment.illumination={material.light_color.x,material.light_color.y,material.light_color.z,material.light_intensity};
      for(std::size_t i=0;i<2;++i){const auto& l=material.additional_lights[i];fragment.additional_direction[i]={l.direction.x,l.direction.y,l.direction.z,0};fragment.additional_illumination[i]={l.color.x,l.color.y,l.color.z,l.intensity};
        if(material.shadow&&l.intensity>0){const auto s=prepare_shadow3d(view.scene->camera(),*draw.instance,l.direction);fragment.additional_shadow[i]={s.light.x,s.light.y,s.light.z,0};}}
      fragment.surface_options[2]=material.rim_power;
      fragment.surface_options[3]=material.two_sided_diffuse?1.f:0.f;
      fragment.texture_options[0]=material.cubic_magnification&&!low_tier?1.f:0.f;
      // texture_options.w is the debug-class lane: Lod view reads the
      // submitted LOD class, Residency reads the surface texture's
      // resident base mip — 9 when an authored image fell back to the
      // pinned white texture (owner mismatch) rather than its own tail.
      if(opt.debug_view==DebugView3D::Residency){
        const auto& want=draw.instance->material.texture;
        fragment.texture_options[3]=(want&&draw.image->owner.get()!=want.get())?9.f
          :static_cast<float>(draw.image->base_mip);
      }else fragment.texture_options[3]=draw.lod_class;
      // Animated terms: band drift scrolls equirect longitude, volume
      // flow rate advances the filament phase — both scaled by the
      // view's scene time on debug_mode.y.
      fragment.anim_options={material.band_drift,material.surface_effect?material.surface_effect->flow_rate:0.f,material.band_turbulence,material.limb_darkening_q};
      fragment.drift_options={material.band_diff,
        // Azimuthal shear as turns/s at the inner edge (rad/s ÷ 2π);
        // the shader scales it by rho^-3/2 and folds it into V.
        material.shear_rate*(.5f/std::numbers::pi_v<float>),
        material.shear_ratio,0.f};
      if(material.shadow){const auto& s=*material.shadow;
        fragment.shadow_light={shadow.light.x,shadow.light.y,shadow.light.z,s.shape==AnalyticShadowShape3D::Ellipsoid?1.f:2.f};
        fragment.shadow_radii={s.radii.x,s.radii.y,s.radii.z,0};
        fragment.shadow_options={s.inner_radius,s.outer_radius,s.opacity,s.opacity_map?1.f:0.f};}
      // shadow_radii.w carries the per-instance receive opt-out (0 = the
      // default receives-maps path; 1 keeps depth-map terms lit).
      fragment.shadow_radii[3]=draw.instance->receives_shadow?0.f:1.f;
      if(material.surface_effect){const auto& e=*material.surface_effect;
        fragment.effect_options={1,e.blend,e.flow_phase,e.distortion};
        fragment.effect_sphere={e.view_sphere_center.x,e.view_sphere_center.y,e.view_sphere_center.z,e.sphere_radius};
        // Authored occlusion sphere: centred on the instance origin in
        // view space (the star its corona wraps). An explicit view-space
        // sphere takes precedence.
        if(e.sphere_radius==0.f&&e.occlude>0.f)
          fragment.effect_sphere={draw.transform.model_view.values[12],
                                  draw.transform.model_view.values[13],
                                  draw.transform.model_view.values[14],
                                  e.occlude*draw.instance->scale};
        // Emission-volume ray marching scales with the quality tier: Low
        // caps at 16 steps, Medium at 32; authored budgets apply above.
        const int volume_steps=low_tier?std::min(e.volume_steps,16)
            :opt.quality==RenderQuality3D::Medium?std::min(e.volume_steps,32):e.volume_steps;
        fragment.volume_options={e.volume_depth,static_cast<float>(volume_steps+(draw.inside_volume?128:0)),e.volume_density,e.volume_seed};
        // Scatter rides atmo_shape.z — atmospheres never render inside
        // the volume branch, so the lane is free for volume materials.
        fragment.atmo_shape[2]=e.volume_scatter;}
      fragment.atmo_shape[3]=material.forward_scatter;
      // Secondary terms ride one lane: x = HG secondary-lobe
      // asymmetry, y = secondary-lobe weight (0 keeps the single-lobe
      // phase), z = three-term limb-darkening mid-curve coefficient,
      // w = doppler beaming tint strength.
      fragment.scatter_options={material.forward_scatter_back,
                                material.forward_scatter_back_mix,
                                material.limb_darkening_mid,
                                material.orbital_beaming_tint};
      // x = Rayleigh wavelength weight applied to the HG phase lobes.
      fragment.wave_options={material.forward_scatter_hue,0.f,0.f,0.f};
      if((material.surface_effect&&material.surface_effect->volume_depth>0.f)||material.orbital_beaming!=0.f){
        // Model transforms use uniform scale and an orthonormal rotation.
        // Invert their camera-relative matrix once per draw, not per
        // fragment — emission volumes and orbital beaming both consume it.
        const auto& from=draw.transform.model_view.values;auto& to=fragment.effect_from_view.values;
        const float inverse_square=1.f/(draw.instance->scale*draw.instance->scale);
        for(int column=0;column<3;++column)for(int row=0;row<3;++row)to[column*4+row]=from[row*4+column]*inverse_square;
        for(int row=0;row<3;++row)to[12+row]=-(to[row]*from[12]+to[4+row]*from[13]+to[8+row]*from[14]);
        to[15]=1;
      }
      if(material.surface_response){const auto& s=*material.surface_response;
        fragment.surface_response={1,s.normal_strength,s.relief*draw.instance->scale,s.cloud_shadow?s.cloud_opacity:0};
        fragment.surface_options[0]=s.cloud_offset.x;fragment.surface_options[1]=s.cloud_offset.y;
        // Deck altitude shares the texture_options block (x = cubic
        // sampling, y = zonal waves): world-units height like relief.
        if(s.cloud_height>0.f)fragment.texture_options[2]=s.cloud_height*draw.instance->scale;
        // Map presence flags gate the shader's per-map sampling so a
        // cloud-only or normal-only material needs no placeholder art.
        const float map_flags=(s.normal?1.f:0.f)+(s.properties?2.f:0.f)+(s.cloud_shadow?4.f:0.f);
        fragment.response_options={material.terminator_wrap,s.cloud_shadow?s.cloud_albedo:0.f,map_flags,material.limb_darkening};}
      else fragment.response_options={material.terminator_wrap,0.f,0.f,material.limb_darkening};
      if(material.dielectric){const auto& d=*material.dielectric;
        fragment.optics={d.index_of_refraction,d.roughness,d.transmission,d.thickness};
        fragment.absorption={d.absorption.x,d.absorption.y,d.absorption.z,d.environment_strength};fragment.view_options[1]=d.specular_strength;
        fragment.view_options[2]=d.surface_relief*draw.instance->scale;}
      // uv_options.w is this draw's screen-door keep probability —
      // a fading LOD pair shares the material but keeps complementary
      // pixel masks (1-p on the selected level, p on the coarser).
      fragment.uv_options={material.texture_tiling.x,material.texture_tiling.y,material.orbital_beaming,draw.lod_keep};
      fragment.pbr_options[3]=material.alpha_threshold;
      if(material.pbr){const auto& p=*material.pbr;
        fragment.pbr_options={1.f,p.metallic_roughness?1.f:0.f,p.night_emissive,material.alpha_threshold};
        // The environment binding falls back to the scene probe, then to
        // the surface texture when neither map exists; zeroing the
        // strength keeps the shader off that path.
        fragment.pbr_values={p.metallic,p.roughness,p.emissive_strength,(p.environment||scene_env)?p.environment_strength:0.f};
        fragment.emissive_tint={p.emissive_tint.x,p.emissive_tint.y,p.emissive_tint.z,0.f};}
      // HDR bakes ride the same RGBA8 image path RGBM-encoded — the flag
      // selects the decode so authored maps keep their literal pixels.
      if(draw.environment->owner&&draw.environment->owner->hdr_rgbm())fragment.env_flags[0]=1.f;
      // Band shear rides the spare emissive_tint.w lane — written after
      // the PBR block since that branch clears the channel. The zonal
      // harmonic strength shares texture_options (x = cubic sampling).
      if(material.band_shear!=0.f)fragment.emissive_tint[3]=material.band_shear;
      if(material.band_waves!=0.f)fragment.texture_options[1]=material.band_waves;
      if(material.atmosphere){const auto& a=*material.atmosphere;
        fragment.atmo_options={a.tint.x,a.tint.y,a.tint.z,a.strength};
        fragment.atmo_shape={a.power,a.night_floor,fragment.atmo_shape[2],fragment.atmo_shape[3]};
        fragment.atmo_sunset={a.sunset.x,a.sunset.y,a.sunset.z,a.sunset_strength};}
      fragment.point_position=pl_position;fragment.point_energy=pl_energy;
      fragment.point_cone=pl_cone;fragment.point_outer=pl_outer;
    }
    // Directional shadow map: an authored ortho volume centres `distance`
    // along camera forward, so strategy scenes pick shadowed coverage
    // explicitly rather than fitting the camera frustum. Low tier skips the
    // pass; resolution and PCF radius default per tier. All light-space math
    // is double-precision so far-field system coordinates stay stable.
    const auto& shadow_settings=view.scene->shadow_map();
    const bool use_shadow=shadow_supported&&!low_tier&&shadow_settings.has_value()&&shadow_settings->strength>0.f;
    Uint32 shadow_res=0;ViewUniform view_uniform{};view_uniform.debug_mode[0]=static_cast<float>(opt.debug_view);view_uniform.debug_mode[1]=opt.time;
    // Shadow casters carry a signed keep probability matching the lit
    // pass's screen-door mask so LOD/range/group transitions thin the
    // silhouette instead of popping it; std430 stride is 80 bytes.
    // params: keep, alpha_threshold, tile_u, tile_v — the last three let the
    // depth pass discard below the caster's own cutout threshold so holed
    // textures don't cast solid silhouettes.
    struct alignas(16) ShadowCast{Matrix4 light_mvp;float keep;float alpha_threshold;float tile_u;float tile_v;};
    static_assert(sizeof(ShadowCast)==80,"std430 shadow caster stride");
    std::vector<std::shared_ptr<Geometry>> caster_geometry;std::vector<ShadowCast> shadow_transforms;std::vector<std::shared_ptr<Texture>> caster_textures;
    // Shadowed spots keep per-light caster lists (indexed by light
    // slot): each shadowed cone renders into its own atlas quadrant.
    std::array<std::vector<std::shared_ptr<Geometry>>,4> spot_geometry;std::array<std::vector<ShadowCast>,4> spot_transforms;std::array<std::vector<std::shared_ptr<Texture>>,4> spot_textures;
    std::array<engine::DrawBatcher,4> spot_batcher;
    // Shadowed omni lights keep per-light caster lists too: each light's
    // six cube faces append into the same flat list (the face's atlas
    // cell offset is baked into its transform), so one buffer + batcher
    // serves all faces like the spot atlas serves quadrants.
    std::array<std::vector<std::shared_ptr<Geometry>>,4> omni_geometry;std::array<std::vector<ShadowCast>,4> omni_transforms;std::array<std::vector<std::shared_ptr<Texture>>,4> omni_textures;
    std::array<std::vector<const MeshInstance3D*>,4> omni_insts;
    std::array<engine::DrawBatcher,4> omni_batcher;
    // Far cascades: per-tier caster lists — tier index == depth-array
    // layer; the fragment reads the tightest covering tier.
    std::array<std::vector<std::shared_ptr<Geometry>>,maximum_scene3d_shadow_cascades> cascade_geometry;
    std::array<std::vector<ShadowCast>,maximum_scene3d_shadow_cascades> cascade_transforms;
    std::array<std::vector<std::shared_ptr<Texture>>,maximum_scene3d_shadow_cascades> cascade_textures;
    std::array<engine::DrawBatcher,maximum_scene3d_shadow_cascades> cascade_batcher;
    engine::DrawBatcher shadow_batcher;
    const auto& cam=view.scene->camera();
    // Shared caster collection: emits every non-transparent instance's
    // light-facing submission (LOD pick, group proxy, range/LOD/group
    // keep terms identical to the lit pass) into one shadow pass's
    // transform+geometry lists, then batches and sorts them so
    // gl_InstanceIndex maps. `in_volume` decides the light-space frustum
    // cut (ortho box for the key light, cone for a shadowed spot).
    // Per-instance caster evaluation is camera-side only: every depth
    // pass (near, both far tiers, each spot quadrant) recomputes the
    // same range cull, keep masks, group collapse and LOD pick, so they
    // are evaluated once per frame and each pass only transforms, tests
    // its own volume and emits.
    struct CasterEval{
      const MeshInstance3D* inst;double radius;
      // group!=nullptr ⇒ the rep emits the merged proxy at proxy_keep;
      // mesh==nullptr ⇒ proxy-only (collapsed rep). partner emits on the
      // complementary fade share when non-null.
      const GroupBounds* group{};float proxy_keep{};
      float keep{},partner_keep{};
      std::shared_ptr<const Mesh3D> mesh,partner;
    };
    std::vector<CasterEval> caster_evals;bool caster_evals_built=false;
    const auto build_caster_evals=[&]{
      if(caster_evals_built)return;caster_evals_built=true;
      for(const auto& instance:view.scene->instances()){
        if(instance.material.transparent||!instance.casts_shadow)continue;
        const double radius=static_cast<double>(instance.mesh->bounding_radius())*instance.scale;
        const double px=instance.position.x-cam.position.x,py=instance.position.y-cam.position.y,pz=instance.position.z-cam.position.z;
        const double cam_dist=std::sqrt(px*px+py*py+pz*pz);
        if(instance.visible_range>0.f&&cam_dist>static_cast<double>(instance.visible_range)+radius)continue;
        // Same keep terms the lit pass computes: the shadow silhouette
        // crossfades in lockstep with the draw that casts it. The sign
        // convention partitions texels — negative keeps the complement.
        float range_keep=1.f;
        if(instance.visible_range>0.f&&instance.visible_fade>0.f){
          const double edge=static_cast<double>(instance.visible_range)+radius;
          range_keep=std::clamp(static_cast<float>((edge-cam_dist)/(instance.visible_fade*instance.visible_range)),0.f,1.f);
        }
        if(range_keep<=0.f)continue;
        // Collapsed groups share one caster: the representative submits
        // its proxy scaled to the merged sphere, identity-rotated so a
        // card faces the light like it faces the camera in the lit pass.
        if(const auto git=group_of.find(&instance);git!=group_of.end()&&git->second->collapse){
          if(git->second->rep!=&instance)continue;
          caster_evals.push_back({&instance,radius,git->second,1.f});
          continue;
        }
        // Inside a collapse band members thin by 1-p while the rep
        // additionally submits the proxy on the complementary share —
        // the same partition the lit pass screen-doors.
        float group_keep=1.f;const GroupBounds* proxy_group=nullptr;float proxy_keep=0.f;
        if(const auto git=group_of.find(&instance);git!=group_of.end()&&git->second->share>0.f){
          const GroupBounds&g=*git->second;
          group_keep=1.f-g.share;
          if(g.rep==&instance){proxy_group=&g;proxy_keep=-g.share;}
        }
        // Screen-space LOD: casters submit the level the lit pass picks
        // instead of always paying the full mesh's vertex cost, plus the
        // next-coarser partner on its complementary share inside a band.
        std::shared_ptr<const Mesh3D> caster_mesh=instance.mesh;
        std::size_t lvl=0;float lod_share=0.f;
        if(!instance.lod_meshes.empty()){
          const float diameter=2.f*instance.scale*static_cast<float>(instance.mesh->bounding_radius())*lod_focal/
            (lod_camera.projection==Projection3D::Orthographic?1.f:static_cast<float>(std::max(cam_dist,1e-4)));
          lvl=select_lod3d_level(instance,diameter);
          if(lvl>0)caster_mesh=instance.lod_meshes[lvl-1];
          lod_share=lod3d_fade_share(instance,diameter);
        }
        const bool fading=lod_share>0.f&&lvl<instance.lod_meshes.size()&&range_keep>=1.f;
        caster_evals.push_back({&instance,radius,proxy_group,proxy_keep,
            (fading?1.f-lod_share:range_keep)*group_keep,
            fading?-lod_share*group_keep:0.f,
            std::move(caster_mesh),fading?instance.lod_meshes[lvl]:nullptr});
      }
    };
    const auto emit_casters=[&](double ex,double ey,double ez,
        double xx,double xy,double xz,double yx,double yy,double yz,double zx,double zy,double zz,
        const Matrix4& light_rotation,const Matrix4& light_projection,const Matrix4& view_to_light,const auto& in_volume,
        std::vector<std::shared_ptr<Geometry>>& geo,std::vector<ShadowCast>& xf,
        std::vector<const MeshInstance3D*>& insts){
      build_caster_evals();
      for(const auto& eval:caster_evals){
        const auto& instance=*eval.inst;
        const auto emit_proxy=[&](const GroupBounds&g,float keep){
          // g is view-space (the merged sphere accumulates model_view
          // translations) — map it through the pass's view→light transform.
          const double lx=view_to_light.values[0]*g.x+view_to_light.values[4]*g.y+view_to_light.values[8]*g.z+view_to_light.values[12];
          const double ly=view_to_light.values[1]*g.x+view_to_light.values[5]*g.y+view_to_light.values[9]*g.z+view_to_light.values[13];
          const double lz=view_to_light.values[2]*g.x+view_to_light.values[6]*g.y+view_to_light.values[10]*g.z+view_to_light.values[14];
          if(!in_volume(lx,ly,lz,g.r))return;
          const double ps=g.r/std::max(static_cast<double>(instance.lod_group_proxy->bounding_radius()),1e-9);
          Matrix4 light_model{};
          for(int c=0;c<3;++c)light_model.values[c*4+c]=static_cast<float>(ps);
          light_model.values[12]=static_cast<float>(lx);light_model.values[13]=static_cast<float>(ly);light_model.values[14]=static_cast<float>(lz);light_model.values[15]=1.f;
          geo.push_back(geometry(instance.lod_group_proxy));
          // The lit pass's cutout tests texel.a*opacity<threshold, so the
          // depth mask pre-divides opacity out — a low-opacity cutout
          // casts the same smaller silhouette it renders.
          xf.push_back({multiply(light_projection,light_model),keep,
              instance.material.alpha_threshold/std::max(instance.material.opacity,1e-6f),
              instance.material.texture_tiling.x,instance.material.texture_tiling.y});
          insts.push_back(&instance);
        };
        if(eval.group)emit_proxy(*eval.group,eval.proxy_keep);
        if(!eval.mesh)continue;
        const double dx=instance.position.x-ex,dy=instance.position.y-ey,dz=instance.position.z-ez;
        const double lx=xx*dx+xy*dy+xz*dz,ly=yx*dx+yy*dy+yz*dz,lz=zx*dx+zy*dy+zz*dz;
        if(!in_volume(lx,ly,lz,eval.radius))continue;
        // A billboard caster faces the light the way it faces the camera
        // in the lit pass — the identity-rotation collapse the group
        // proxy uses; authored rotation would make a card edge-on.
        const auto emit_caster=[&](const std::shared_ptr<const Mesh3D>& mesh,float keep){
          Matrix4 light_model{};
          if(mesh->billboard()){
            for(int c=0;c<3;++c)light_model.values[c*4+c]=instance.scale;
          }else{
            Matrix4 model=rotation_from(instance.rotation);
            for(int c=0;c<3;++c)for(int r=0;r<3;++r)model.values[c*4+r]*=instance.scale;
            light_model=multiply(light_rotation,model);
          }
          light_model.values[12]=static_cast<float>(lx);light_model.values[13]=static_cast<float>(ly);light_model.values[14]=static_cast<float>(lz);light_model.values[15]=1.f;
          geo.push_back(geometry(mesh));
          xf.push_back({multiply(light_projection,light_model),keep,
              instance.material.alpha_threshold/std::max(instance.material.opacity,1e-6f),
              instance.material.texture_tiling.x,instance.material.texture_tiling.y});
          insts.push_back(&instance);
        };
        emit_caster(eval.mesh,eval.keep);
        if(eval.partner)emit_caster(eval.partner,eval.partner_keep);
      }
    };
    // Same instancing convention as the scene pass: batch by mesh, then
    // reorder transforms/geometry to sorted order so gl_InstanceIndex maps.
    // material_id interns the caster's base texture — the depth pass binds
    // it per batch so alpha_threshold cuts the silhouette's own texels.
    const auto finish_casters=[&](
        std::vector<std::shared_ptr<Geometry>>& geo,std::vector<ShadowCast>& xf,
        const std::vector<const MeshInstance3D*>& insts,
        std::vector<std::shared_ptr<Texture>>& tex_table,engine::DrawBatcher& batcher,
        std::uint64_t& audit){
      batcher.begin_frame();
      std::map<const Mesh3D*,std::uint32_t> mesh_ids;
      std::map<const RgbaImage*,std::uint32_t> tex_ids;
      tex_table.clear();
      for(std::size_t i=0;i<geo.size();++i){
        const auto mesh_id=mesh_ids.try_emplace(geo[i]->owner.get(),static_cast<std::uint32_t>(mesh_ids.size())).first->second;
        const auto* image=insts[i]->material.texture.get();
        const auto tex_id=tex_ids.try_emplace(image,static_cast<std::uint32_t>(tex_ids.size())).first->second;
        if(tex_table.size()<=tex_id)tex_table.push_back(texture(insts[i]->material.texture));
        engine::DrawItem item{};item.mesh_id=mesh_id;item.material_id=tex_id;item.instance_index=static_cast<std::uint32_t>(i);
        batcher.submit(item);
      }
      batcher.build();
      {
        const auto& sorted_items=batcher.sorted_items();
        std::vector<ShadowCast> ordered(sorted_items.size());
        std::vector<std::shared_ptr<Geometry>> ordered_geometry(sorted_items.size());
        for(std::size_t slot=0;slot<sorted_items.size();++slot){
          ordered[slot]=xf[sorted_items[slot].instance_index];
          ordered_geometry[slot]=std::move(geo[sorted_items[slot].instance_index]);
        }
        xf=std::move(ordered);geo=std::move(ordered_geometry);
        audit+=xf.size();
      }
    };
    const auto collect_casters=[&](double ex,double ey,double ez,
        double xx,double xy,double xz,double yx,double yy,double yz,double zx,double zy,double zz,
        const Matrix4& light_rotation,const Matrix4& light_projection,const Matrix4& view_to_light,const auto& in_volume,
        std::vector<std::shared_ptr<Geometry>>& geo,std::vector<ShadowCast>& xf,
        std::vector<std::shared_ptr<Texture>>& tex_table,engine::DrawBatcher& batcher,
        std::uint64_t& audit){
      std::vector<const MeshInstance3D*> insts;
      emit_casters(ex,ey,ez,xx,xy,xz,yx,yy,yz,zx,zy,zz,light_rotation,light_projection,view_to_light,in_volume,geo,xf,insts);
      finish_casters(geo,xf,insts,tex_table,batcher,audit);
    };
    if(use_shadow){
      const auto& s=*shadow_settings;
      shadow_res=s.resolution?std::min<Uint32>(s.resolution,8192)
          :opt.quality==RenderQuality3D::Ultra?4096u:opt.quality==RenderQuality3D::High?2048u:1024u;
      const Quaternion& cq=cam.orientation; // unit by Scene3D::create
      const Vec3 lw=rotate_vec(cq,view.scene->light_direction());
      const Vec3 fw=rotate_vec(cq,{0,0,-1});
      const Vec3 uw=rotate_vec(cq,{0,1,0});
      const double zx=lw.x,zy=lw.y,zz=lw.z; // light-space +Z toward the star
      double xx=uw.y*zz-uw.z*zy,xy=uw.z*zx-uw.x*zz,xz=uw.x*zy-uw.y*zx;
      const double xl=std::sqrt(xx*xx+xy*xy+xz*xz);
      if(xl>1e-12){xx/=xl;xy/=xl;xz/=xl;}else{xx=1;xy=0;xz=0;}
      const double yx=zy*xz-zz*xy,yy=zz*xx-zx*xz,yz=zx*xy-zy*xx; // z × x
      const double fx=cam.position.x+static_cast<double>(fw.x)*s.distance,fy=cam.position.y+static_cast<double>(fw.y)*s.distance,fz=cam.position.z+static_cast<double>(fw.z)*s.distance;
      const double ex=fx+zx*s.depth*.5,ey=fy+zy*s.depth*.5,ez=fz+zz*s.depth*.5;
      // Camera-fitted coverage (ShadowMap3D::camera_fit): authored
      // extents become multipliers of the camera's visible half-height
      // at the box centre — orthographic uses orthographic_height/2,
      // perspective the frustum half-height at the authored distance
      // slice. The whole tier chain tracks zoom while authored ratios
      // (and the strictly-increasing validation) stay meaningful.
      const double footprint_half=
          cam.projection==Projection3D::Orthographic
              ?static_cast<double>(cam.orthographic_height)*.5
              :static_cast<double>(s.distance)*std::tan(static_cast<double>(cam.vertical_fov_radians)*.5);
      const double fit_scale=s.camera_fit?std::max(footprint_half,1e-3):1.;
      const double extent=s.extent*fit_scale,depth=s.depth;
      Matrix4 light_rotation{};light_rotation.values={{static_cast<float>(xx),static_cast<float>(yx),static_cast<float>(zx),0,
        static_cast<float>(xy),static_cast<float>(yy),static_cast<float>(zy),0,
        static_cast<float>(xz),static_cast<float>(yz),static_cast<float>(zz),0,0,0,0,1}};
      Matrix4 light_projection{};light_projection.values={{static_cast<float>(1./extent),0,0,0,0,static_cast<float>(1./extent),0,0,0,0,-1.f/s.depth,0,0,0,0,1}};
      // world = R_cam·view + cam, so LV·world = R_l·R_cam·view + R_l·(cam-eye).
      Matrix4 from_view=multiply(light_rotation,rotation_from(cq));
      const double cx=cam.position.x-ex,cy=cam.position.y-ey,cz=cam.position.z-ez;
      from_view.values[12]=static_cast<float>(xx*cx+xy*cy+xz*cz);
      from_view.values[13]=static_cast<float>(yx*cx+yy*cy+yz*cz);
      from_view.values[14]=static_cast<float>(zx*cx+zy*cy+zz*cz);
      view_uniform.shadow_from_view=multiply(light_projection,from_view);
      const float radius_texels=(opt.quality==RenderQuality3D::Ultra?1.5f:opt.quality==RenderQuality3D::High?1.f:0.f)*s.softness;
      view_uniform.shadow_options={1.f/static_cast<float>(shadow_res),radius_texels,s.strength,s.bias};
      // Authored normal offset in near-map texels → world units: the
      // receiver lifts along its shading normal by this much before
      // depth projection so slope-scaled acne clears without raising
      // the constant bias. Each cascade tier scales by its own
      // world-per-texel (2*extent/res).
      view_uniform.shadow_advanced={s.normal_offset*2.f*static_cast<float>(extent)/static_cast<float>(shadow_res),0.f,0.f,0.f};
      // Casters: transparent blends never occlude; visible_range culls cast
      // shadows identically to the camera draw; the light-space box test is
      // the only frustum cut — off-camera casters still write the map.
      const auto box_volume=[&](double lx,double ly,double lz,double r){
        return std::abs(lx)<=extent+r&&std::abs(ly)<=extent+r&&lz<=r&&lz>=-depth-r;};
      collect_casters(ex,ey,ez,xx,xy,xz,yx,yy,yz,zx,zy,zz,light_rotation,light_projection,from_view,box_volume,
          caster_geometry,shadow_transforms,caster_textures,shadow_batcher,stats.shadow_casters);
      // Optional far cascades: the same ortho centre/depth at each
      // authored cascade extent — progressively coarser tiers so
      // receivers past the near window keep a shadow instead of
      // snapping lit at extreme zoom. Tier index == depth-array layer.
      const auto cascade_tiers=std::min(s.cascade_extents.size(),maximum_scene3d_shadow_cascades);
      for(std::size_t t=0;t<cascade_tiers;++t){
        const double tier_extent=s.cascade_extents[t]*fit_scale;
        Matrix4 tier_projection{};tier_projection.values={{1.f/static_cast<float>(tier_extent),0,0,0,0,1.f/static_cast<float>(tier_extent),0,0,0,0,-1.f/s.depth,0,0,0,0,1}};
        view_uniform.cascade_from_view[t]=multiply(tier_projection,from_view);
        view_uniform.cascade_options[t]={1.f/static_cast<float>(shadow_res),radius_texels,
            s.normal_offset*2.f*static_cast<float>(tier_extent)/static_cast<float>(shadow_res),s.bias};
        const auto tier_volume=[&](double lx,double ly,double lz,double r){
          return std::abs(lx)<=tier_extent+r&&std::abs(ly)<=tier_extent+r&&lz<=r&&lz>=-depth-r;};
        std::uint64_t tier_casters=0;
        collect_casters(ex,ey,ez,xx,xy,xz,yx,yy,yz,zx,zy,zz,light_rotation,tier_projection,from_view,tier_volume,
            cascade_geometry[t],cascade_transforms[t],cascade_textures[t],cascade_batcher[t],tier_casters);
        stats.shadow_casters+=tier_casters;stats.shadow_cascade_casters[t]+=tier_casters;
      }
    }
    const Uint32 cascade_layers=use_shadow?static_cast<Uint32>(std::min(shadow_settings->cascade_extents.size(),maximum_scene3d_shadow_cascades)):0;
    const Uint32 cascade_res=cascade_layers>0?shadow_res:0;
    // Shadowed spot lights: every coned point light flagged casts_shadow
    // gets a quadrant of the shared depth atlas — 2×2 when more than one
    // casts, a full-size map for a single light. Each caster pass renders
    // through the same depth pipeline with a perspective cone frustum
    // centred on the spot direction — receivers outside the outer cone
    // get zero radiance anyway, so the map's fov clamps at ~150 degrees
    // without losing coverage.
    std::array<int,4> spot_slot{-1,-1,-1,-1};int spot_count=0;
    for(std::size_t i=0;i<view.scene->point_lights().size();++i){
      const auto& l=view.scene->point_lights()[i];
      // Coned lights only — a zero-direction caster is an omni light and
      // renders through the cube atlas below.
      if(l.casts_shadow&&l.intensity>0.f&&
         (l.spot_direction.x!=0.f||l.spot_direction.y!=0.f||l.spot_direction.z!=0.f))
        spot_slot[i]=spot_count++;
    }
    const Uint32 spot_res=(spot_count>0&&shadow_supported&&!low_tier)?(opt.quality==RenderQuality3D::Ultra?2048u:opt.quality==RenderQuality3D::High?1024u:512u):0;
    const Uint32 spot_map=spot_count>1?spot_res*2u:spot_res;
    if(spot_res>0){
      for(std::size_t i=0;i<view.scene->point_lights().size();++i){
        if(spot_slot[i]<0)continue;
        const auto& l=view.scene->point_lights()[i];
        const double dl=std::sqrt(l.spot_direction.x*l.spot_direction.x+l.spot_direction.y*l.spot_direction.y+l.spot_direction.z*l.spot_direction.z);
        const double zx=l.spot_direction.x/dl,zy=l.spot_direction.y/dl,zz=l.spot_direction.z/dl;
        // Cone rolls are arbitrary — pick any stable orthonormal frame.
        double ax=0.0,ay=1.0,az=0.0;
        if(std::abs(zy)>.9){ax=1.0;ay=0.0;}
        double xx=ay*zz-az*zy,xy=az*zx-ax*zz,xz=ax*zy-ay*zx;
        const double xl=std::sqrt(xx*xx+xy*xy+xz*xz);xx/=xl;xy/=xl;xz/=xl;
        const double yx=zy*xz-zz*xy,yy=zz*xx-zx*xz,yz=zx*xy-zy*xx; // z × x
        const double ex=l.position.x,ey=l.position.y,ez=l.position.z;
        const double far=l.range>0.f?static_cast<double>(l.range):4096.0;
        const double near=std::max(0.01,far*0.0005);
        // Cull volumes cover the clamped map fov, not the authored cone —
        // the outer band is dark to receivers anyway (window==0 there).
        // Spot space looks down +Z (the cone direction), so casters sit
        // at positive lz between near and far.
        const double cos_o=std::max(std::min(static_cast<double>(l.spot_outer),1.0),std::cos(150.0*0.017453292519943295));
        const double tan_o=std::sqrt(1.0-cos_o*cos_o)/cos_o;
        const auto cone_volume=[&](double lx,double ly,double lz,double r){
          if(lz<-r||lz>far+r)return false;
          return std::sqrt(lx*lx+ly*ly)<=lz*tan_o+r/std::max(cos_o,.05);};
        Matrix4 light_rotation{};light_rotation.values={{static_cast<float>(xx),static_cast<float>(yx),static_cast<float>(zx),0,
          static_cast<float>(xy),static_cast<float>(yy),static_cast<float>(zy),0,
          static_cast<float>(xz),static_cast<float>(yz),static_cast<float>(zz),0,0,0,0,1}};
        // Perspective projection looking down +Z, NDC depth [0,1]:
        // f = cot(fov/2) with the half-angle capped at 75 degrees.
        const double focal=1.0/tan_o;
        const double pn=far/(far-near),pq=-near*far/(far-near);
        Matrix4 light_projection{};light_projection.values={{static_cast<float>(focal),0,0,0,
          0,static_cast<float>(focal),0,0,
          0,0,static_cast<float>(pn),1.f,
          0,0,static_cast<float>(pq),0}};
        // view→spot-clip for receivers: same R_l·R_cam + R_l·(cam-eye)
        // pattern the directional map uses.
        const Quaternion& cq=cam.orientation;
        Matrix4 from_view=multiply(light_rotation,rotation_from(cq));
        const double cx=cam.position.x-ex,cy=cam.position.y-ey,cz=cam.position.z-ez;
        from_view.values[12]=static_cast<float>(xx*cx+xy*cy+xz*cz);
        from_view.values[13]=static_cast<float>(yx*cx+yy*cy+yz*cz);
        from_view.values[14]=static_cast<float>(zx*cx+zy*cy+zz*cz);
        // The quadrant assignment bakes into the receiver transform
        // (atlas NDC offset + 0.5 scale) so out-of-cone fragments land
        // outside spot_bounds and stay lit; casters write the quadrant
        // through the baked light_mvp the same way.
        if(spot_count>1){
          const float qx=static_cast<float>(spot_slot[i]&1),qy=static_cast<float>(spot_slot[i]>>1);
          Matrix4 quadrant{};quadrant.values={{.5f,0,0,0,0,.5f,0,0,0,0,1.f,0,qx-.5f,qy-.5f,0,1.f}};
          view_uniform.spot_from_view[i]=multiply(quadrant,multiply(light_projection,from_view));
          // The viewport's y flip lands the qy=0 cell at v [.5,1] — bounds
          // must match the sampler convention, not the clip offset.
          view_uniform.spot_bounds[i]={qx*.5f,.5f-qy*.5f,qx*.5f+.5f,1.f-qy*.5f};
          light_projection=multiply(quadrant,light_projection);
        }else{
          view_uniform.spot_from_view[i]=multiply(light_projection,from_view);
          view_uniform.spot_bounds[i]={0.f,0.f,1.f,1.f};
        }
        const float radius_texels=(opt.quality==RenderQuality3D::Ultra?1.5f:opt.quality==RenderQuality3D::High?1.f:0.f)*l.shadow_softness;
        // Perspective depth compresses distant differences, so the bias
        // stays texel-scaled — the rasterizer's slope bias covers the
        // geometric term already. z carries the umbra strength.
        view_uniform.spot_options[i]={1.f/static_cast<float>(spot_map),radius_texels,
            l.shadow_strength,1.5f/static_cast<float>(spot_map)};
        // Receiver-side normal offset in texels → a per-unit-distance
        // world lift: one cell texel spans 2·tan_o·d/cell_px at light
        // distance d (cell_px = spot_map/2 under the 2x2 quadrant
        // atlas, spot_map for a single shadowed spot).
        view_uniform.spot_advanced[i]={l.shadow_normal_offset*
            static_cast<float>(2.0*tan_o*(spot_count>1?2.0:1.0)/spot_map),0.f,0.f,0.f};
        collect_casters(ex,ey,ez,xx,xy,xz,yx,yy,yz,zx,zy,zz,light_rotation,light_projection,from_view,cone_volume,
            spot_geometry[i],spot_transforms[i],spot_textures[i],spot_batcher[i],stats.spot_shadow_casters);
      }
    }
    // Shadowed omni lights: a casts_shadow point light with no cone
    // renders six 90-degree cube faces into its row of a shared cube
    // atlas (one row per light, six square columns). Faces are
    // view-space-locked — the receiver shader selects the face and
    // rebuilds its projective depth analytically from the view-space
    // offset — so the face bases rotate into world space with the
    // camera for the caster transforms only.
    std::array<int,4> omni_slot{-1,-1,-1,-1};int omni_count=0;
    for(std::size_t i=0;i<view.scene->point_lights().size();++i){
      const auto& l=view.scene->point_lights()[i];
      if(l.casts_shadow&&l.intensity>0.f&&l.spot_direction.x==0.f&&l.spot_direction.y==0.f&&l.spot_direction.z==0.f)
        omni_slot[i]=omni_count++;
    }
    const Uint32 omni_cell=(omni_count>0&&shadow_supported&&!low_tier)?(opt.quality==RenderQuality3D::Ultra?1024u:opt.quality==RenderQuality3D::High?512u:256u):0;
    const Uint32 omni_map_w=omni_cell*6u,omni_map_h=omni_cell*static_cast<Uint32>(omni_count);
    if(omni_cell>0){
      // Cube-face bases in view space (right, up, forward rows of the
      // view→face rotation), matching the shader's dominant-axis order:
      // +X,-X,+Y,-Y,+Z,-Z.
      static const Vec3 face_right[6]={{0,0,-1},{0,0,1},{-1,0,0},{-1,0,0},{1,0,0},{-1,0,0}};
      static const Vec3 face_up[6]={{0,1,0},{0,1,0},{0,0,1},{0,0,-1},{0,1,0},{0,1,0}};
      static const Vec3 face_fwd[6]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
      const Quaternion& cq=cam.orientation;
      const double rows=static_cast<double>(omni_count);
      for(std::size_t i=0;i<view.scene->point_lights().size();++i){
        if(omni_slot[i]<0)continue;
        const auto& l=view.scene->point_lights()[i];
        const double ex=l.position.x,ey=l.position.y,ez=l.position.z;
        const double far=l.range>0.f?static_cast<double>(l.range):4096.0;
        const double near=std::max(0.01,far*0.0005);
        const double pn=far/(far-near),pq=-near*far/(far-near);
        const double row=static_cast<double>(omni_slot[i]);
        const float radius_texels=(opt.quality==RenderQuality3D::Ultra?1.5f:opt.quality==RenderQuality3D::High?1.f:0.f)*l.shadow_softness;
        // omni_options = atlas texel width (>0 enabled), PCF radius in
        // cell texels, umbra strength, depth bias. omni_atlas = the
        // light row's uv span plus the face projection's pn/pq terms so
        // the shader rebuilds projective depth without a matrix.
        view_uniform.omni_options[i]={1.f/static_cast<float>(omni_map_w),radius_texels,
            l.shadow_strength,1.5f/static_cast<float>(omni_cell)};
        // One cell texel spans 2·t/omni_cell at face distance t (a
        // 90-degree face is 2t wide), so the authored lift in texels is
        // a per-t world scale; the shader multiplies by the fragment's
        // dominant-axis distance.
        view_uniform.omni_advanced[i]={l.shadow_normal_offset*
            (12.f/static_cast<float>(omni_map_w)),0.f,0.f,0.f};
        view_uniform.omni_atlas[i]={static_cast<float>(row/rows),static_cast<float>((row+1.0)/rows),
            static_cast<float>(pn),static_cast<float>(pq)};
        const double cx=cam.position.x-ex,cy=cam.position.y-ey,cz=cam.position.z-ez;
        for(int f=0;f<6;++f){
          const Vec3 rx=rotate_vec(cq,face_right[f]),uy=rotate_vec(cq,face_up[f]),fz=rotate_vec(cq,face_fwd[f]);
          const double xx=rx.x,xy=rx.y,xz=rx.z,yx=uy.x,yy=uy.y,yz=uy.z,zx=fz.x,zy=fz.y,zz=fz.z;
          Matrix4 light_rotation{};light_rotation.values={{static_cast<float>(xx),static_cast<float>(yx),static_cast<float>(zx),0,
            static_cast<float>(xy),static_cast<float>(yy),static_cast<float>(zy),0,
            static_cast<float>(xz),static_cast<float>(yz),static_cast<float>(zz),0,0,0,0,1}};
          // 90-degree face frustum (focal 1, tan(45)=1) with the atlas
          // cell's NDC offset+scale baked in like the spot quadrant.
          Matrix4 light_projection{};light_projection.values={{1.f,0,0,0,0,1.f,0,0,
            0,0,static_cast<float>(pn),1.f,0,0,static_cast<float>(pq),0}};
          Matrix4 cell{};cell.values={{1.f/6.f,0,0,0,0,static_cast<float>(1.0/rows),0,0,0,0,1.f,0,
            static_cast<float>((2.0*f-5.0)/6.0),static_cast<float>(1.0-(2.0*row+1.0)/rows),0,1.f}};
          light_projection=multiply(cell,light_projection);
          Matrix4 from_view=multiply(light_rotation,rotation_from(cq));
          from_view.values[12]=static_cast<float>(xx*cx+xy*cy+xz*cz);
          from_view.values[13]=static_cast<float>(yx*cx+yy*cy+yz*cz);
          from_view.values[14]=static_cast<float>(zx*cx+zy*cy+zz*cz);
          const auto face_volume=[&](double lx,double ly,double lz,double r){
            return lz>-r&&lz<far+r&&std::abs(lx)<=lz+r&&std::abs(ly)<=lz+r;};
          // All six faces append into the light's one caster list; the
          // shared finish batches the merged set so an instance that
          // crosses face seams still draws with each face's transform.
          emit_casters(ex,ey,ez,xx,xy,xz,yx,yy,yz,zx,zy,zz,light_rotation,light_projection,from_view,face_volume,
              omni_geometry[i],omni_transforms[i],omni_insts[i]);
        }
        finish_casters(omni_geometry[i],omni_transforms[i],omni_insts[i],omni_textures[i],omni_batcher[i],stats.omni_shadow_casters);
      }
    }
    // Pass scheduling goes through the engine RenderGraph: resources and
    // dependencies are declared per frame, compile() validates the DAG and
    // emits the execution order, and the backend binds routines by tag.
    engine::RenderGraph graph;
    const auto hdr_target=graph.add_resource({engine::RenderResourceDesc::Kind::Texture2D,"hdr-scene",res_w,res_h,"rgba16f",true});
    const auto color_target=graph.add_resource({engine::RenderResourceDesc::Kind::Texture2D,"color",res_w,res_h,"rgba8",true});
    const auto depth_target=graph.add_resource({engine::RenderResourceDesc::Kind::Texture2D,"depth",res_w,res_h,"d32",true});
    const auto shadow_target=graph.add_resource({engine::RenderResourceDesc::Kind::Texture2D,"shadow-depth",shadow_res,shadow_res,"d32",true});
    const auto spot_target=graph.add_resource({engine::RenderResourceDesc::Kind::Texture2D,"spot-shadow-depth",spot_map,spot_map,"d32",true});
    const auto omni_target=graph.add_resource({engine::RenderResourceDesc::Kind::Texture2D,"omni-shadow-depth",omni_map_w,omni_map_h,"d32",true});
    const auto cascade_target=graph.add_resource({engine::RenderResourceDesc::Kind::Texture2D,"shadow-cascade-depth",cascade_res,cascade_res,"d32",true});
    graph.add_pass({"shadow",{},{shadow_target},{},"shadow",use_shadow});
    graph.add_pass({"spot-shadow",{},{spot_target},{},"spot-shadow",spot_res>0});
    graph.add_pass({"omni-shadow",{},{omni_target},{},"omni-shadow",omni_cell>0});
    graph.add_pass({"shadow-cascade",{},{cascade_target},{},"shadow-cascade",cascade_res>0});
    std::vector<engine::ResourceId> scene_inputs;
    if(use_shadow)scene_inputs.push_back(shadow_target);
    if(spot_res>0)scene_inputs.push_back(spot_target);
    if(omni_cell>0)scene_inputs.push_back(omni_target);
    if(cascade_res>0)scene_inputs.push_back(cascade_target);
    graph.add_pass({"scene3d",scene_inputs,{target.hdr?hdr_target:color_target,depth_target},{},"scene3d"});
    graph.add_pass({"tonemap",{hdr_target},{color_target},{},"tonemap",target.hdr!=nullptr});
    std::vector<std::string> order;std::vector<engine::RenderGraphDiagnostic> diagnostics;
    if(!graph.compile(&order,&diagnostics))throw std::runtime_error("3D render graph compile failed: "+(diagnostics.empty()?std::string("unknown"):diagnostics.front().message));
    Command command(device,gpu_pending);
    const auto shadow_bytes=static_cast<Uint32>(shadow_transforms.size()*sizeof(ShadowCast));
    std::array<Uint32,4> spot_bytes{};
    Uint32 spot_bytes_total=0;
    for(std::size_t i=0;i<4;++i){spot_bytes[i]=static_cast<Uint32>(spot_transforms[i].size()*sizeof(ShadowCast));spot_bytes_total+=spot_bytes[i];}
    std::array<Uint32,4> omni_bytes{};
    Uint32 omni_bytes_total=0;
    for(std::size_t i=0;i<4;++i){omni_bytes[i]=static_cast<Uint32>(omni_transforms[i].size()*sizeof(ShadowCast));omni_bytes_total+=omni_bytes[i];}
    std::array<Uint32,maximum_scene3d_shadow_cascades> cascade_bytes{};
    Uint32 cascade_bytes_total=0;
    for(std::size_t i=0;i<cascade_layers;++i){cascade_bytes[i]=static_cast<Uint32>(cascade_transforms[i].size()*sizeof(ShadowCast));cascade_bytes_total+=cascade_bytes[i];}
    if(!sorted.empty()||shadow_bytes||spot_bytes_total||omni_bytes_total||cascade_bytes_total){
      const auto vertex_bytes=static_cast<Uint32>(vertex_data.size()*sizeof(VertexUniform)),fragment_bytes=static_cast<Uint32>(fragment_data.size()*sizeof(FragmentUniform));
      if(!vertex_buffer||!fragment_buffer||vertex_capacity<vertex_data.size()||fragment_capacity<fragment_data.size()){
        if(vertex_buffer)SDL_ReleaseGPUBuffer(device,vertex_buffer);
        if(fragment_buffer)SDL_ReleaseGPUBuffer(device,fragment_buffer);
        vertex_capacity=std::max<std::size_t>(vertex_data.size(),vertex_capacity*2);fragment_capacity=std::max<std::size_t>(fragment_data.size(),fragment_capacity*2);
        SDL_GPUBufferCreateInfo info{};info.usage=SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;info.size=static_cast<Uint32>(vertex_capacity*sizeof(VertexUniform));
        vertex_buffer=SDL_CreateGPUBuffer(device,&info);if(!vertex_buffer)throw gpu_error("3D instance transform buffer creation failed");
        info.size=static_cast<Uint32>(fragment_capacity*sizeof(FragmentUniform));
        fragment_buffer=SDL_CreateGPUBuffer(device,&info);if(!fragment_buffer)throw gpu_error("3D instance material buffer creation failed");
      }
      if(shadow_bytes&&(!shadow_buffer||shadow_capacity<shadow_transforms.size())){
        if(shadow_buffer)SDL_ReleaseGPUBuffer(device,shadow_buffer);
        shadow_capacity=std::max<std::size_t>(shadow_transforms.size(),shadow_capacity*2);
        SDL_GPUBufferCreateInfo info{};info.usage=SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;info.size=static_cast<Uint32>(shadow_capacity*sizeof(ShadowCast));
        shadow_buffer=SDL_CreateGPUBuffer(device,&info);if(!shadow_buffer)throw gpu_error("3D shadow transform buffer creation failed");
      }
      for(std::size_t i=0;i<4;++i)
        if(spot_bytes[i]&&(!spot_shadow_buffer[i]||spot_shadow_capacity[i]<spot_transforms[i].size())){
          if(spot_shadow_buffer[i])SDL_ReleaseGPUBuffer(device,spot_shadow_buffer[i]);
          spot_shadow_capacity[i]=std::max<std::size_t>(spot_transforms[i].size(),spot_shadow_capacity[i]*2);
          SDL_GPUBufferCreateInfo info{};info.usage=SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;info.size=static_cast<Uint32>(spot_shadow_capacity[i]*sizeof(ShadowCast));
          spot_shadow_buffer[i]=SDL_CreateGPUBuffer(device,&info);if(!spot_shadow_buffer[i])throw gpu_error("3D spot shadow transform buffer creation failed");
        }
      for(std::size_t i=0;i<4;++i)
        if(omni_bytes[i]&&(!omni_shadow_buffer[i]||omni_shadow_capacity[i]<omni_transforms[i].size())){
          if(omni_shadow_buffer[i])SDL_ReleaseGPUBuffer(device,omni_shadow_buffer[i]);
          omni_shadow_capacity[i]=std::max<std::size_t>(omni_transforms[i].size(),omni_shadow_capacity[i]*2);
          SDL_GPUBufferCreateInfo info{};info.usage=SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;info.size=static_cast<Uint32>(omni_shadow_capacity[i]*sizeof(ShadowCast));
          omni_shadow_buffer[i]=SDL_CreateGPUBuffer(device,&info);if(!omni_shadow_buffer[i])throw gpu_error("3D omni shadow transform buffer creation failed");
        }
      for(std::size_t i=0;i<cascade_layers;++i)
        if(cascade_bytes[i]&&(!cascade_shadow_buffer[i]||cascade_shadow_capacity[i]<cascade_transforms[i].size())){
          if(cascade_shadow_buffer[i])SDL_ReleaseGPUBuffer(device,cascade_shadow_buffer[i]);
          cascade_shadow_capacity[i]=std::max<std::size_t>(cascade_transforms[i].size(),cascade_shadow_capacity[i]*2);
          SDL_GPUBufferCreateInfo info{};info.usage=SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;info.size=static_cast<Uint32>(cascade_shadow_capacity[i]*sizeof(ShadowCast));
          cascade_shadow_buffer[i]=SDL_CreateGPUBuffer(device,&info);if(!cascade_shadow_buffer[i])throw gpu_error("3D cascade shadow transform buffer creation failed");
        }
      Transfer transfer(device,vertex_bytes+fragment_bytes+shadow_bytes+spot_bytes_total+omni_bytes_total+cascade_bytes_total);auto* memory=static_cast<std::byte*>(transfer.map());
      if(vertex_bytes)std::memcpy(memory,vertex_data.data(),vertex_bytes);
      if(fragment_bytes)std::memcpy(memory+vertex_bytes,fragment_data.data(),fragment_bytes);
      if(shadow_bytes)std::memcpy(memory+vertex_bytes+fragment_bytes,shadow_transforms.data(),shadow_bytes);
      std::size_t spot_at=vertex_bytes+fragment_bytes+shadow_bytes;
      for(std::size_t i=0;i<4;++i){
        if(spot_bytes[i]){std::memcpy(memory+spot_at,spot_transforms[i].data(),spot_bytes[i]);spot_at+=spot_bytes[i];}
      }
      std::size_t omni_at=spot_at;
      for(std::size_t i=0;i<4;++i){
        if(omni_bytes[i]){std::memcpy(memory+omni_at,omni_transforms[i].data(),omni_bytes[i]);omni_at+=omni_bytes[i];}
      }
      std::size_t cascade_at=omni_at;
      for(std::size_t i=0;i<cascade_layers;++i){
        if(cascade_bytes[i]){std::memcpy(memory+cascade_at,cascade_transforms[i].data(),cascade_bytes[i]);cascade_at+=cascade_bytes[i];}
      }
      SDL_UnmapGPUTransferBuffer(device,transfer.value);
      auto* copy=SDL_BeginGPUCopyPass(command.value);if(!copy)throw gpu_error("3D instance upload copy pass failed");
      SDL_GPUTransferBufferLocation from{transfer.value,0};
      if(vertex_bytes){SDL_GPUBufferRegion to{vertex_buffer,0,vertex_bytes};SDL_UploadToGPUBuffer(copy,&from,&to,false);}
      if(fragment_bytes){from.offset=vertex_bytes;SDL_GPUBufferRegion to{fragment_buffer,0,fragment_bytes};SDL_UploadToGPUBuffer(copy,&from,&to,false);}
      if(shadow_bytes){from.offset=vertex_bytes+fragment_bytes;SDL_GPUBufferRegion to{shadow_buffer,0,shadow_bytes};SDL_UploadToGPUBuffer(copy,&from,&to,false);}
      std::size_t spot_from=vertex_bytes+fragment_bytes+shadow_bytes;
      for(std::size_t i=0;i<4;++i){
        if(spot_bytes[i]){from.offset=static_cast<Uint32>(spot_from);SDL_GPUBufferRegion to{spot_shadow_buffer[i],0,spot_bytes[i]};SDL_UploadToGPUBuffer(copy,&from,&to,false);spot_from+=spot_bytes[i];}
      }
      std::size_t omni_from=spot_from;
      for(std::size_t i=0;i<4;++i){
        if(omni_bytes[i]){from.offset=static_cast<Uint32>(omni_from);SDL_GPUBufferRegion to{omni_shadow_buffer[i],0,omni_bytes[i]};SDL_UploadToGPUBuffer(copy,&from,&to,false);omni_from+=omni_bytes[i];}
      }
      std::size_t cascade_from=omni_from;
      for(std::size_t i=0;i<cascade_layers;++i){
        if(cascade_bytes[i]){from.offset=static_cast<Uint32>(cascade_from);SDL_GPUBufferRegion to{cascade_shadow_buffer[i],0,cascade_bytes[i]};SDL_UploadToGPUBuffer(copy,&from,&to,false);cascade_from+=cascade_bytes[i];}
      }
      SDL_EndGPUCopyPass(copy);
    }
    // Resolve per-view quality policy once: Low keeps the tonemap-only path,
    // Medium adds mip bloom, High adds sharpen, Ultra renders MSAA when the
    // device supports it. Options are clamped, never trusted raw.
    const bool use_msaa=msaa_supported&&opt.quality==RenderQuality3D::Ultra;
    if(use_msaa&&!target.msaa_color){
      target.msaa_color=make_texture(device,target.width,target.height,scene_format,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,1,SDL_GPU_SAMPLECOUNT_4);
      target.msaa_depth=make_texture(device,target.width,target.height,SDL_GPU_TEXTUREFORMAT_D32_FLOAT,SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,1,SDL_GPU_SAMPLECOUNT_4);
    }
    PostUniform post{};
    post.a={std::clamp(std::isfinite(opt.exposure)?opt.exposure:1.f,0.01f,64.f),
      target.hdr&&opt.quality>=RenderQuality3D::Medium?std::clamp(opt.bloom_strength,0.f,8.f):0.f,
      std::clamp(std::isfinite(opt.bloom_threshold)?opt.bloom_threshold:1.f,0.f,8.f),
      std::clamp(std::isfinite(opt.contrast)?opt.contrast:1.f,0.f,2.f)};
    // Post-tonemap channel remap: active only when every element is
    // finite and the matrix differs from identity, so the default path
    // stays bit-identical and never pays the multiply.
    bool use_matrix=std::ranges::all_of(opt.color_matrix,[](float v){return std::isfinite(v);});
    if(use_matrix){static constexpr float identity[]{1,0,0,0,1,0,0,0,1};
      use_matrix=!std::equal(opt.color_matrix.begin(),opt.color_matrix.end(),std::begin(identity));}
    if(use_matrix){
      post.c={opt.color_matrix[0],opt.color_matrix[1],opt.color_matrix[2],0.f};
      post.d={opt.color_matrix[3],opt.color_matrix[4],opt.color_matrix[5],0.f};
      post.e={opt.color_matrix[6],opt.color_matrix[7],opt.color_matrix[8],0.f};}
    post.b={std::clamp(std::isfinite(opt.saturation)?opt.saturation:1.f,0.f,2.f),
      opt.quality>=RenderQuality3D::High?std::clamp(std::isfinite(opt.sharpen)?opt.sharpen:0.f,0.f,1.f):0.f,
      std::clamp(std::isfinite(opt.vignette)?opt.vignette:0.f,0.f,1.f),
      use_matrix?1.f:0.f};
    // Disabled shadow passes never reach their resize branch — release a
    // stale map so target memory and bytes() stop charging it.
    if(shadow_res==0&&target.shadow){SDL_ReleaseGPUTexture(device,target.shadow);target.shadow=nullptr;target.shadow_size=0;}
    if(spot_res==0&&target.spot_shadow){SDL_ReleaseGPUTexture(device,target.spot_shadow);target.spot_shadow=nullptr;target.spot_shadow_size=0;}
    if(omni_cell==0&&target.point_shadow){SDL_ReleaseGPUTexture(device,target.point_shadow);target.point_shadow=nullptr;target.point_shadow_w=target.point_shadow_h=0;}
    if(cascade_res==0&&target.shadow_cascade){SDL_ReleaseGPUTexture(device,target.shadow_cascade);target.shadow_cascade=nullptr;target.shadow_cascade_size=target.shadow_cascade_layers=0;}
    for(const auto& name:order){
      if(name=="shadow"){
        if(target.shadow_size!=static_cast<int>(shadow_res)){
          if(target.shadow)SDL_ReleaseGPUTexture(device,target.shadow);
          target.shadow=make_texture(device,static_cast<int>(shadow_res),static_cast<int>(shadow_res),SDL_GPU_TEXTUREFORMAT_D32_FLOAT,SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER);
          target.shadow_size=static_cast<int>(shadow_res);
        }
        SDL_GPUDepthStencilTargetInfo map{};map.texture=target.shadow;map.clear_depth=1;map.load_op=SDL_GPU_LOADOP_CLEAR;map.store_op=SDL_GPU_STOREOP_STORE;map.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;map.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
        auto* pass=SDL_BeginGPURenderPass(command.value,nullptr,0,&map);if(!pass)throw gpu_error("3D shadow pass failed");
        if(!shadow_transforms.empty()){
          SDL_BindGPUGraphicsPipeline(pass,shadow_pipeline);
          SDL_BindGPUVertexStorageBuffers(pass,0,&shadow_buffer,1);
          for(const auto& batch:shadow_batcher.batches()){
            // Geometry is already in sorted order, so first_item addresses it.
            const auto& geo=*caster_geometry[batch.first_item];
            const auto& cast=shadow_transforms[batch.first_item];
            const SDL_GPUTextureSamplerBinding sampled[]{{caster_textures[batch.material_id]->texture,(cast.tile_u!=1.f||cast.tile_v!=1.f)?repeat_sampler:sampler}};
            SDL_BindGPUFragmentSamplers(pass,0,sampled,1);
            const SDL_GPUBufferBinding vertices{geo.vertices,0},indices{geo.indices,0};
            SDL_BindGPUVertexBuffers(pass,0,&vertices,1);SDL_BindGPUIndexBuffer(pass,&indices,SDL_GPU_INDEXELEMENTSIZE_32BIT);
            SDL_DrawGPUIndexedPrimitives(pass,static_cast<Uint32>(geo.owner->indices().size()),batch.count,0,0,batch.first_item);++stats.draw_calls;
          }
        }
        SDL_EndGPURenderPass(pass);
      }else if(name=="spot-shadow"){
        if(target.spot_shadow_size!=static_cast<int>(spot_map)){
          if(target.spot_shadow)SDL_ReleaseGPUTexture(device,target.spot_shadow);
          target.spot_shadow=make_texture(device,static_cast<int>(spot_map),static_cast<int>(spot_map),SDL_GPU_TEXTUREFORMAT_D32_FLOAT,SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER);
          target.spot_shadow_size=static_cast<int>(spot_map);
        }
        SDL_GPUDepthStencilTargetInfo map{};map.texture=target.spot_shadow;map.clear_depth=1;map.load_op=SDL_GPU_LOADOP_CLEAR;map.store_op=SDL_GPU_STOREOP_STORE;map.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;map.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
        auto* pass=SDL_BeginGPURenderPass(command.value,nullptr,0,&map);if(!pass)throw gpu_error("3D spot shadow pass failed");
        // One atlas pass: each shadowed light's caster list binds its own
        // transform buffer; the quadrant offset is baked into light_mvp,
        // so all lights share the full-viewport depth target.
        for(std::size_t i=0;i<4;++i){
          if(spot_transforms[i].empty())continue;
          SDL_BindGPUGraphicsPipeline(pass,shadow_pipeline);
          SDL_BindGPUVertexStorageBuffers(pass,0,&spot_shadow_buffer[i],1);
          for(const auto& batch:spot_batcher[i].batches()){
            const auto& geo=*spot_geometry[i][batch.first_item];
            const auto& cast=spot_transforms[i][batch.first_item];
            const SDL_GPUTextureSamplerBinding sampled[]{{spot_textures[i][batch.material_id]->texture,(cast.tile_u!=1.f||cast.tile_v!=1.f)?repeat_sampler:sampler}};
            SDL_BindGPUFragmentSamplers(pass,0,sampled,1);
            const SDL_GPUBufferBinding vertices{geo.vertices,0},indices{geo.indices,0};
            SDL_BindGPUVertexBuffers(pass,0,&vertices,1);SDL_BindGPUIndexBuffer(pass,&indices,SDL_GPU_INDEXELEMENTSIZE_32BIT);
            SDL_DrawGPUIndexedPrimitives(pass,static_cast<Uint32>(geo.owner->indices().size()),batch.count,0,0,batch.first_item);++stats.draw_calls;
          }
        }
        SDL_EndGPURenderPass(pass);
      }else if(name=="omni-shadow"){
        if(target.point_shadow_w!=static_cast<int>(omni_map_w)||target.point_shadow_h!=static_cast<int>(omni_map_h)){
          if(target.point_shadow)SDL_ReleaseGPUTexture(device,target.point_shadow);
          target.point_shadow=make_texture(device,static_cast<int>(omni_map_w),static_cast<int>(omni_map_h),SDL_GPU_TEXTUREFORMAT_D32_FLOAT,SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER);
          target.point_shadow_w=static_cast<int>(omni_map_w);target.point_shadow_h=static_cast<int>(omni_map_h);
        }
        SDL_GPUDepthStencilTargetInfo map{};map.texture=target.point_shadow;map.clear_depth=1;map.load_op=SDL_GPU_LOADOP_CLEAR;map.store_op=SDL_GPU_STOREOP_STORE;map.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;map.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
        auto* pass=SDL_BeginGPURenderPass(command.value,nullptr,0,&map);if(!pass)throw gpu_error("3D omni shadow pass failed");
        // One atlas pass per shared cube map: every light's caster list
        // binds its own transform buffer holding all six faces back to
        // back — the cell offset is baked into each face's light_mvp, so
        // faces share the full-viewport depth target like spot cells.
        for(std::size_t i=0;i<4;++i){
          if(omni_transforms[i].empty())continue;
          SDL_BindGPUGraphicsPipeline(pass,shadow_pipeline);
          SDL_BindGPUVertexStorageBuffers(pass,0,&omni_shadow_buffer[i],1);
          for(const auto& batch:omni_batcher[i].batches()){
            const auto& geo=*omni_geometry[i][batch.first_item];
            const auto& cast=omni_transforms[i][batch.first_item];
            const SDL_GPUTextureSamplerBinding sampled[]{{omni_textures[i][batch.material_id]->texture,(cast.tile_u!=1.f||cast.tile_v!=1.f)?repeat_sampler:sampler}};
            SDL_BindGPUFragmentSamplers(pass,0,sampled,1);
            const SDL_GPUBufferBinding vertices{geo.vertices,0},indices{geo.indices,0};
            SDL_BindGPUVertexBuffers(pass,0,&vertices,1);SDL_BindGPUIndexBuffer(pass,&indices,SDL_GPU_INDEXELEMENTSIZE_32BIT);
            SDL_DrawGPUIndexedPrimitives(pass,static_cast<Uint32>(geo.owner->indices().size()),batch.count,0,0,batch.first_item);++stats.draw_calls;
          }
        }
        SDL_EndGPURenderPass(pass);
      }else if(name=="shadow-cascade"){
        // One depth-array target, one layer per authored tier — any
        // tier-count or resolution change reallocates the whole array.
        if(target.shadow_cascade_size!=static_cast<int>(cascade_res)||target.shadow_cascade_layers!=static_cast<int>(cascade_layers)){
          if(target.shadow_cascade)SDL_ReleaseGPUTexture(device,target.shadow_cascade);
          target.shadow_cascade=make_texture_array(device,static_cast<int>(cascade_res),static_cast<int>(cascade_res),static_cast<int>(cascade_layers),SDL_GPU_TEXTUREFORMAT_D32_FLOAT,SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER);
          target.shadow_cascade_size=static_cast<int>(cascade_res);target.shadow_cascade_layers=static_cast<int>(cascade_layers);
        }
        for(std::size_t tier=0;tier<cascade_layers;++tier){
          SDL_GPUDepthStencilTargetInfo map{};map.texture=target.shadow_cascade;map.layer=static_cast<Uint8>(tier);map.clear_depth=1;map.load_op=SDL_GPU_LOADOP_CLEAR;map.store_op=SDL_GPU_STOREOP_STORE;map.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;map.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
          auto* pass=SDL_BeginGPURenderPass(command.value,nullptr,0,&map);if(!pass)throw gpu_error("3D cascade shadow pass failed");
          if(!cascade_transforms[tier].empty()){
            SDL_BindGPUGraphicsPipeline(pass,shadow_pipeline);
            SDL_BindGPUVertexStorageBuffers(pass,0,&cascade_shadow_buffer[tier],1);
            for(const auto& batch:cascade_batcher[tier].batches()){
              const auto& geo=*cascade_geometry[tier][batch.first_item];
              const auto& cast=cascade_transforms[tier][batch.first_item];
              const SDL_GPUTextureSamplerBinding sampled[]{{cascade_textures[tier][batch.material_id]->texture,(cast.tile_u!=1.f||cast.tile_v!=1.f)?repeat_sampler:sampler}};
              SDL_BindGPUFragmentSamplers(pass,0,sampled,1);
              const SDL_GPUBufferBinding vertices{geo.vertices,0},indices{geo.indices,0};
              SDL_BindGPUVertexBuffers(pass,0,&vertices,1);SDL_BindGPUIndexBuffer(pass,&indices,SDL_GPU_INDEXELEMENTSIZE_32BIT);
              SDL_DrawGPUIndexedPrimitives(pass,static_cast<Uint32>(geo.owner->indices().size()),batch.count,0,0,batch.first_item);++stats.draw_calls;
            }
          }
          SDL_EndGPURenderPass(pass);
        }
      }else if(name=="scene3d"){
        SDL_GPUColorTargetInfo color{};
        color.texture=use_msaa?target.msaa_color:(target.hdr?target.hdr:target.color);
        color.load_op=SDL_GPU_LOADOP_CLEAR;
        if(use_msaa){color.store_op=SDL_GPU_STOREOP_RESOLVE;color.resolve_texture=target.hdr?target.hdr:target.color;}
        else color.store_op=SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo depth{};depth.texture=use_msaa?target.msaa_depth:target.depth;depth.clear_depth=1;depth.load_op=SDL_GPU_LOADOP_CLEAR;depth.store_op=SDL_GPU_STOREOP_DONT_CARE;depth.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;depth.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
        auto* pass=SDL_BeginGPURenderPass(command.value,&color,1,&depth);if(!pass)throw gpu_error("3D render pass failed");
        // View-level fragment uniform: debug selector plus the key light's
        // shadow-map transform/options — identical for every draw in the pass.
        SDL_PushGPUFragmentUniformData(command.value,0,&view_uniform,static_cast<Uint32>(sizeof(view_uniform)));
        if(!sorted.empty()){
          SDL_BindGPUVertexStorageBuffers(pass,0,&vertex_buffer,1);SDL_BindGPUFragmentStorageBuffers(pass,0,&fragment_buffer,1);
          for(const auto& batch:batcher.batches()){
            const auto& draw=draws[sorted[batch.first_item].instance_index];const auto& material=draw.instance->material;
            const auto* pipe_set=use_msaa?&pipelines_ms4:&pipelines;
            SDL_BindGPUGraphicsPipeline(pass,(*pipe_set)[(material.transparent?1:0)+((material.double_sided||draw.inside_volume)?2:0)]);
            const SDL_GPUBufferBinding vertices{draw.mesh->vertices,0},indices{draw.mesh->indices,0};
            SDL_BindGPUVertexBuffers(pass,0,&vertices,1);SDL_BindGPUIndexBuffer(pass,&indices,SDL_GPU_INDEXELEMENTSIZE_32BIT);
            const bool tiled=material.texture_tiling.x!=1.f||material.texture_tiling.y!=1.f;
            const bool aniso=material.anisotropic_texture&&!low_tier;
            const SDL_GPUTextureSamplerBinding sampled[]{
              {draw.image->texture,tiled?(aniso?repeat_aniso_sampler:repeat_sampler):(aniso?detail_sampler:sampler)},
              {draw.optical->texture,sampler},{draw.environment->texture,environment_sampler},
              {draw.normal->texture,environment_sampler},{draw.properties->texture,environment_sampler},
              {draw.cloud->texture,environment_sampler},{draw.shadow->texture,sampler},{draw.next->texture,sampler},
              {draw.emissive->texture,tiled?repeat_sampler:sampler},{draw.mr->texture,tiled?repeat_sampler:sampler},
              {use_shadow?target.shadow:texture(white)->texture,sampler},
              {spot_res>0?target.spot_shadow:texture(white)->texture,sampler},
              {cascade_res>0?target.shadow_cascade:texture(white)->texture,sampler},
              {omni_cell>0?target.point_shadow:texture(white)->texture,sampler}};
            SDL_BindGPUFragmentSamplers(pass,0,sampled,14);
            SDL_DrawGPUIndexedPrimitives(pass,static_cast<Uint32>(draw.mesh->owner->indices().size()),batch.count,0,0,batch.first_item);++stats.draw_calls;
          }
        }
        SDL_EndGPURenderPass(pass);
      }else if(name=="tonemap"){
        // Bloom reads coarse HDR mips; generate them between passes when the
        // view requests bloom. Blit chain cost is proportional to pixel count.
        if(post.a[1]>0.f){
          Uint32 bw=static_cast<Uint32>(target.width),bh=static_cast<Uint32>(target.height);
          for(Uint32 level=1;level<target.hdr_levels;++level){
            const auto nw=std::max(1u,bw/2),nh=std::max(1u,bh/2);
            SDL_GPUBlitInfo blit{};blit.source.texture=blit.destination.texture=target.hdr;
            blit.source.mip_level=level-1;blit.source.w=bw;blit.source.h=bh;
            blit.destination.mip_level=level;blit.destination.w=nw;blit.destination.h=nh;
            blit.load_op=SDL_GPU_LOADOP_DONT_CARE;blit.filter=SDL_GPU_FILTER_LINEAR;
            SDL_BlitGPUTexture(command.value,&blit);bw=nw;bh=nh;
          }
        }
        SDL_GPUColorTargetInfo resolve{};resolve.texture=target.color;resolve.load_op=SDL_GPU_LOADOP_DONT_CARE;resolve.store_op=SDL_GPU_STOREOP_STORE;
        auto* resolve_pass=SDL_BeginGPURenderPass(command.value,&resolve,1,nullptr);if(!resolve_pass)throw gpu_error("3D tonemap pass failed");
        SDL_BindGPUGraphicsPipeline(resolve_pass,tonemap_pipeline);
        SDL_PushGPUFragmentUniformData(command.value,0,&post,sizeof(post));
        const SDL_GPUTextureSamplerBinding sampled[]{{target.hdr,sampler}};
        SDL_BindGPUFragmentSamplers(resolve_pass,0,sampled,1);
        SDL_DrawGPUPrimitives(resolve_pass,3,1,0,0);
        SDL_EndGPURenderPass(resolve_pass);
      }
    }
    command.submit();
    stats.draw_batches+=batcher.batches().size();stats.submitted_instances+=sorted.size();
  }
  // Bakes a captured environment probe for one scene: six face views
  // through the full pipeline at the authored resolution, downloaded
  // and CPU-resampled into the equirect layout radiance() samples.
  // Static by policy — the bake captures the layout the first prepared
  // frame sees; cache-lifetime upload accounting is preserved while
  // per-frame counters are restored so a bake never inflates them.
  void bake_probe(const std::shared_ptr<const Scene3D>& scene,const RenderOptions3D& options,const ProbeKey& key){
    const auto& cap=scene->environment_capture();
    const int res=cap.face_resolution;
    // Face orientations — identity looks -Z with +Y up; each quaternion
    // aims the camera down one world axis. The resampler derives each
    // face's basis by rotating the same quaternions, so bake and
    // resample can never disagree about orientation.
    static const std::array<Quaternion,6> faces{{
        {0.f,-0.70710678f,0.f,0.70710678f},  // +X
        {0.f, 0.70710678f,0.f,0.70710678f},  // -X
        {0.70710678f,0.f,0.f,0.70710678f},   // +Y
        {-0.70710678f,0.f,0.f,0.70710678f},  // -Y
        {0.f,1.f,0.f,0.f},                   // +Z
        {0.f,0.f,0.f,1.f}}};                 // -Z
    const auto& cam=scene->camera();
    // The authored key light is camera-space — convert through world so
    // every face view lights the scene identically.
    const auto world=rotate_vec(cam.orientation,scene->light_direction());
    const Scene3DStatistics pre=stats;
    // Linear radiance per face, three channels. HDR-capable targets
    // download the RGBA16F scene target so the bake keeps pre-tonemap
    // headroom; the UNORM fallback keeps the tonemapped composite.
    std::array<std::vector<float>,6> face_pixels;
    bool hdr_bake=false;
    // A debug override must not bake into the probe.
    RenderOptions3D capture_options=options;capture_options.debug_view=DebugView3D::Lit;
    for(int f=0;f<6;++f){
      Camera3D fc=cam;
      fc.position=cap.anchor;
      fc.orientation=faces[f];
      fc.projection=Projection3D::Perspective;
      fc.vertical_fov_radians=1.5707963267948966f;
      const auto& fq=faces[f];
      const auto fl=rotate_vec({-fq.x,-fq.y,-fq.z,fq.w},world);
      const auto face_scene=Scene3D::create(fc,scene->instances(),fl,scene->point_lights(),scene->shadow_map(),scene->environment());
      Scene3DView view{face_scene,{0.f,0.f,static_cast<float>(res),static_cast<float>(res)},capture_options};
      auto face_target=target(res,res);
      render(view,*face_target);
      const bool hdr_face=face_target->hdr!=nullptr;if(f==0)hdr_bake=hdr_face;
      face_pixels[f].resize(static_cast<std::size_t>(res)*res*3);
      SDL_GPUTransferBufferCreateInfo info{};
      info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
      info.size=static_cast<Uint32>(res*res*(hdr_face?8u:4u));
      auto* dl=SDL_CreateGPUTransferBuffer(device,&info);
      if(!dl)throw gpu_error("3D probe download buffer creation failed");
      {
        Command command(device,gpu_pending);
        auto* pass=SDL_BeginGPUCopyPass(command.value);
        if(!pass)throw gpu_error("3D probe download copy pass failed");
        SDL_GPUTextureRegion src{};src.texture=hdr_face?face_target->hdr:face_target->color;src.w=static_cast<Uint32>(res);src.h=static_cast<Uint32>(res);src.d=1;
        SDL_GPUTextureTransferInfo dst{};dst.transfer_buffer=dl;dst.pixels_per_row=static_cast<Uint32>(res);dst.rows_per_layer=static_cast<Uint32>(res);
        SDL_DownloadFromGPUTexture(pass,&src,&dst);
        SDL_EndGPUCopyPass(pass);
        command.submit();
      }
      checked(SDL_WaitForGPUIdle(device),"3D probe bake wait failed");
      auto* mapped=SDL_MapGPUTransferBuffer(device,dl,false);
      if(mapped){
        if(hdr_face){const auto* half=static_cast<const std::uint16_t*>(mapped);
          for(std::size_t i=0;i<face_pixels[f].size();++i)face_pixels[f][i]=half_to_float(half[i/3*4+(i%3)]);
        }else{const auto* bytes=static_cast<const std::uint8_t*>(mapped);
          for(std::size_t i=0;i<face_pixels[f].size();++i)face_pixels[f][i]=bytes[i/3*4+(i%3)]/255.f;
        }
      }
      SDL_UnmapGPUTransferBuffer(device,dl);
      SDL_ReleaseGPUTransferBuffer(device,dl);
      if(!mapped)throw gpu_error("3D probe download map failed");
    }
    // Resample the six faces into the equirect layout radiance()
    // samples: u = atan(d.x,d.z)/2π+0.5 longitude, v = acos(d.y)/π
    // latitude. Each output texel's direction projects onto the face
    // whose camera-space forward dominates.
    const int ew=2*res,eh=res;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(ew)*eh*4);
    std::array<Matrix4,6> basis;
    for(int f=0;f<6;++f)basis[f]=rotation_from(faces[f]);
    const auto texel=[&](int f,double u,double v,int c){
      const double uu=std::clamp(u,0.,1.)*res-0.5,vv=std::clamp(v,0.,1.)*res-0.5;
      const int x0=std::clamp(static_cast<int>(std::floor(uu)),0,res-1),y0=std::clamp(static_cast<int>(std::floor(vv)),0,res-1);
      const int x1=std::min(x0+1,res-1),y1=std::min(y0+1,res-1);
      const double tx=uu-x0,ty=vv-y0;
      const auto& img=face_pixels[f];
      const auto at=[&](int x,int y){return img[(static_cast<std::size_t>(y)*res+x)*3+c];};
      return at(x0,y0)*(1-tx)*(1-ty)+at(x1,y0)*tx*(1-ty)+at(x0,y1)*(1-tx)*ty+at(x1,y1)*tx*ty;
    };
    for(int y=0;y<eh;++y)for(int x=0;x<ew;++x){
      const double lon=6.2831853071795865*((x+0.5)/ew-0.5),lat=3.1415926535897932*(y+0.5)/eh;
      const double s=std::sin(lat),dy=std::cos(lat),dx=s*std::sin(lon),dz=s*std::cos(lon);
      int best=-1;double fwd=0.,cx=0.,cy=0.;
      for(int f=0;f<6;++f){
        const auto& m=basis[f].values;
        const double px=dx*m[0]+dy*m[1]+dz*m[2],py=dx*m[4]+dy*m[5]+dz*m[6],cf=-(dx*m[8]+dy*m[9]+dz*m[10]);
        if(cf>0&&cf>=std::abs(px)&&cf>=std::abs(py)&&cf>fwd){best=f;fwd=cf;cx=px;cy=py;}
      }
      auto* out=&pixels[(static_cast<std::size_t>(y)*ew+x)*4];
      if(best<0){out[0]=out[1]=out[2]=0;out[3]=255;continue;}
      const double u=cx/fwd*0.5+0.5,v=0.5-cy/fwd*0.5;
      const float r=static_cast<float>(texel(best,u,v,0)),g=static_cast<float>(texel(best,u,v,1)),b=static_cast<float>(texel(best,u,v,2));
      if(hdr_bake){
        // RGBM: alpha stores the shared magnitude so rgb*a*8 decodes up
        // to 8x white — a captured sun survives the clamp that would
        // flatten it to a plain white blob in the LDR path.
        const float mx=std::max(r,std::max(g,b));
        const float a=mx>0.f?std::ceil(std::min(mx/8.f,1.f)*255.f)/255.f:0.f;
        const float inv=a>0.f?1.f/(a*8.f):0.f;
        out[0]=static_cast<std::uint8_t>(std::clamp(r*inv,0.f,1.f)*255.f+0.5f);
        out[1]=static_cast<std::uint8_t>(std::clamp(g*inv,0.f,1.f)*255.f+0.5f);
        out[2]=static_cast<std::uint8_t>(std::clamp(b*inv,0.f,1.f)*255.f+0.5f);
        out[3]=static_cast<std::uint8_t>(a*255.f);
      }else{
        out[0]=static_cast<std::uint8_t>(std::clamp(r,0.f,1.f)*255.f+0.5f);
        out[1]=static_cast<std::uint8_t>(std::clamp(g,0.f,1.f)*255.f+0.5f);
        out[2]=static_cast<std::uint8_t>(std::clamp(b,0.f,1.f)*255.f+0.5f);
        out[3]=255;
      }
    }
    // Preserve cache-lifetime accounting (the bake's uploads are real
    // cache entries) while restoring the per-frame counters the six
    // face renders accrued — a bake must not inflate frame stats.
    const auto tex_bytes=stats.texture_cache_bytes-pre.texture_cache_bytes,
        mesh_bytes=stats.mesh_cache_bytes-pre.mesh_cache_bytes,
        tex_up=stats.texture_uploads-pre.texture_uploads,
        mesh_up=stats.mesh_uploads-pre.mesh_uploads;
    stats=pre;
    stats.texture_cache_bytes+=tex_bytes;stats.mesh_cache_bytes+=mesh_bytes;
    stats.texture_uploads+=tex_up;stats.mesh_uploads+=mesh_up;
    ++stats.probe_bakes;if(hdr_bake)++stats.probe_bakes_hdr;
    probe_cache[key]=RgbaImage::create(ew,eh,std::move(pixels),{},hdr_bake);
  }
};
Scene3DRenderer::Scene3DRenderer(SDL_GPUDevice* device,SDL_Renderer* renderer):storage_(std::make_unique<Storage>(device,renderer)){storage_->initialize();}
Scene3DRenderer::~Scene3DRenderer()=default;
void Scene3DRenderer::prepare(const DrawList& list){
  auto& s=*storage_;s.require_owner();s.views.clear();s.next_view=0;s.stats.draw_calls=s.stats.culled_instances=s.stats.shadow_casters=s.stats.spot_shadow_casters=s.stats.omni_shadow_casters=s.stats.draw_batches=s.stats.submitted_instances=s.stats.lod_instances=s.stats.lod_fades=s.stats.visible_fades=s.stats.lod_groups=0;s.stats.shadow_cascade_casters.fill(0);
  // Poll GPU submit fences — queue depth plus submit→signal latency is the
  // GPU-side lag signal SDL_GPU's fence-only API affords (no timestamp
  // queries exist). Latency is an upper bound: a fence may have signaled
  // earlier within the frame interval.
  {
    const auto now=std::chrono::steady_clock::now();
    float max_latency=0.f;
    while(!s.gpu_pending.empty()&&SDL_QueryGPUFence(s.device,s.gpu_pending.front().fence)){
      const auto& front=s.gpu_pending.front();
      max_latency=std::max(max_latency,
          std::chrono::duration<float,std::milli>(now-front.submitted).count());
      SDL_ReleaseGPUFence(s.device,front.fence);
      s.gpu_pending.pop_front();
      ++s.stats.gpu_signaled;
    }
    s.stats.gpu_signal_latency_ms=max_latency;
    s.stats.gpu_pending_submits=s.gpu_pending.size();
    s.stats.gpu_oldest_pending_ms=s.gpu_pending.empty()?0.f:
        std::chrono::duration<float,std::milli>(now-s.gpu_pending.front().submitted).count();
  }
  for(const auto& c:list.world)if(const auto* view=std::get_if<Scene3DView>(&c))s.views.push_back(view);
  for(const auto& c:list.overlay)if(const auto* view=std::get_if<Scene3DView>(&c))s.views.push_back(view);
  if(s.views.size()>maximum_scene3d_views)throw std::length_error("3D frame exceeds its viewport budget.");
  // Captured environment probes bake once per scene — ahead of the
  // demand pass so the baked equirect joins streamer residency, and
  // ahead of the lit pass so opt-in materials bind it this frame. A
  // nonzero probe_epoch shares one bake across rebuilt scenes.
  std::set<Storage::ProbeKey,Storage::ProbeKeyLess> demanded;
  for(const auto* view:s.views)
    if(view->scene&&view->scene->environment_capture().enabled){
      const auto key=Storage::probe_key(*view);
      demanded.insert(key);
      if(!s.probe_cache.count(key))s.bake_probe(view->scene,view->options,key);
    }
  for(auto it=s.probe_cache.begin();it!=s.probe_cache.end();){
    // Scene-keyed bakes die with their owner; epoch-keyed bakes die the
    // first frame no view declares their generation — a replaced
    // document's serial stops arriving, releasing the entry instead of
    // accumulating one baked image per document load.
    const bool stale=it->first.epoch?demanded.count(it->first)==0:it->first.scene.expired();
    if(stale)it=s.probe_cache.erase(it);else++it;
  }
  std::size_t total=0;
  std::unordered_set<const Mesh3D*> meshes;std::unordered_set<const RgbaImage*> textures;
  std::unordered_map<engine::TextureId,std::pair<float,std::uint32_t>> stream_demand;
  const auto stream_request=[&](const std::shared_ptr<const RgbaImage>& image,float priority,std::uint32_t desired_mip){
    const auto id=s.stream_id_for(image?image:s.white);
    const auto [it,inserted]=stream_demand.try_emplace(id,priority,desired_mip);
    if(!inserted){it->second.first=std::max(it->second.first,priority);it->second.second=std::min(it->second.second,desired_mip);}};
  std::size_t geometry_bytes=0,texture_bytes=0;
  for(const auto* view:s.views){const auto r=view->destination;
    if(!view->scene||!std::isfinite(r.x)||!std::isfinite(r.y)||std::abs(r.x)>65536||std::abs(r.y)>65536||
       !std::isfinite(r.width)||!std::isfinite(r.height)||r.width<1||r.height<1||r.width>8192||r.height>8192||
       !std::isfinite(view->render_scale)||view->render_scale<.25f||view->render_scale>1.f)
      throw std::invalid_argument("3D viewport requires a scene, finite bounded dimensions and render_scale in [0.25,1].");
    const auto extent=scene3d_view_target_extent(*view);
    total+=extent[0]*extent[1]*(s.hdr?16u:8u);
    const auto& cam=view->scene->camera();const auto& camera=cam.position;
    // Projected px per world unit: the sampler only reaches the mip whose
    // texel density matches the on-screen footprint, so the resident tail
    // can start there. Bounding-sphere diameter overestimates surface texel
    // density — the choice errs finer, never blurrier than the full chain.
    // render_scale shrinks the actual target: demand follows the rendered
    // height, not the destination's.
    const float res_h=static_cast<float>(extent[1]);
    const float focal=cam.projection==Projection3D::Orthographic?res_h/std::max(cam.orthographic_height,1e-6f)
      :res_h/std::max(2.f*std::tan(cam.vertical_fov_radians*.5f),1e-6f);
    for(const auto& instance:view->scene->instances()){
      // Distance-culled instances never submit — no residency demand either.
      const auto dx=instance.position.x-camera.x,dy=instance.position.y-camera.y,dz=instance.position.z-camera.z;
      const float dist=static_cast<float>(std::sqrt(dx*dx+dy*dy+dz*dz));
      if(instance.visible_range>0.f&&dist>instance.visible_range+instance.scale*instance.mesh->bounding_radius())continue;
      // Nearer instances win texture-budget contention.
      const float priority=1.f/(1.f+dist);
      const float footprint=2.f*instance.scale*instance.mesh->bounding_radius()*focal/(cam.projection==Projection3D::Orthographic?1.f:std::max(dist,1e-4f));
      // Geometry demand follows the same screen-space pick the draw loop
      // makes — only the level this view submits is charged.
      const auto* lod_mesh=instance.mesh.get();
      const auto lod_level=select_lod3d_level(instance,footprint);
      if(lod_level>0)lod_mesh=instance.lod_meshes[lod_level-1].get();
      if(meshes.insert(lod_mesh).second)geometry_bytes+=lod_mesh->byte_size()*2;
      // A fading instance draws the next-coarser level too — charge its
      // residency so the paired draw never binds stale geometry.
      if(view->options.quality!=RenderQuality3D::Low&&lod3d_fade_share(instance,footprint)>0.f&&lod_level<instance.lod_meshes.size())
        {const auto* fade_mesh=instance.lod_meshes[lod_level].get();if(meshes.insert(fade_mesh).second)geometry_bytes+=fade_mesh->byte_size()*2;}
      const auto mip_for=[&](const std::shared_ptr<const RgbaImage>& image){
        const auto& img=image?*image:*s.white;const int tex=std::max(img.width(),img.height());
        return footprint>=tex||tex<=1?0u:static_cast<std::uint32_t>(std::floor(std::log2(static_cast<float>(tex)/footprint)));};
      const auto& image=instance.material.texture;
      if(textures.insert(image.get()).second)texture_bytes+=texture_mip_layout3d(image.get()).resident_bytes;
      // anisotropic_texture declares high-frequency content (polar/ring
      // maps): isotropic LOD erases it, so it keeps full-chain residency.
      // Low tier drops the promotion — the aniso sampler is gated off too.
      const bool aniso=instance.material.anisotropic_texture&&view->options.quality!=RenderQuality3D::Low;
      stream_request(image,priority,aniso?0u:mip_for(image));
      if(instance.material.dielectric){
        // The environment map is a view-independent equirect sampled at
        // reflected/refracted directions — surface footprint does not bound
        // its texel demand, so it keeps full-chain residency like aniso
        // content. The roughness/relief surface map IS surface content.
        const auto& env=instance.material.dielectric->environment;
        if(textures.insert(env.get()).second)texture_bytes+=texture_mip_layout3d(env.get()).resident_bytes;
        stream_request(env,priority,0u);
        const auto& optical=instance.material.dielectric->surface;
        if(textures.insert(optical.get()).second)texture_bytes+=texture_mip_layout3d(optical.get()).resident_bytes;
        stream_request(optical,priority,mip_for(optical));
      }
      if(instance.material.surface_response)for(const auto& response_image:{instance.material.surface_response->normal,instance.material.surface_response->properties,instance.material.surface_response->cloud_shadow})
        {if(textures.insert(response_image.get()).second)texture_bytes+=texture_mip_layout3d(response_image.get()).resident_bytes;stream_request(response_image,priority,mip_for(response_image));}
      if(instance.material.surface_effect){const auto& image_next=instance.material.surface_effect->next_texture;
        if(textures.insert(image_next.get()).second)texture_bytes+=texture_mip_layout3d(image_next.get()).resident_bytes;stream_request(image_next,priority,mip_for(image_next));}
      if(instance.material.shadow&&instance.material.shadow->opacity_map){const auto& shadow_image=instance.material.shadow->opacity_map;
        if(textures.insert(shadow_image.get()).second)texture_bytes+=texture_mip_layout3d(shadow_image.get()).resident_bytes;stream_request(shadow_image,priority,mip_for(shadow_image));}
      if(instance.material.pbr){const auto& p=*instance.material.pbr;
        for(const auto& pbr_image:{p.metallic_roughness,p.emissive})
          {if(textures.insert(pbr_image.get()).second)texture_bytes+=texture_mip_layout3d(pbr_image.get()).resident_bytes;stream_request(pbr_image,priority,mip_for(pbr_image));}
        // Environment maps are direction-space lookups: like dielectric
        // environments they keep full-chain residency. An unmapped PBR
        // material that opted in (strength > 0) uses the scene probe —
        // the baked capture when this scene has one, else its authored
        // equirect.
        auto env=p.environment;
        if(!env&&p.environment_strength>0.f){
          if(view->scene->environment_capture().enabled)
            if(const auto it=s.probe_cache.find(Storage::probe_key(*view));it!=s.probe_cache.end())env=it->second;
          if(!env)env=view->scene->environment();
        }
        if(env){if(textures.insert(env.get()).second)texture_bytes+=texture_mip_layout3d(env.get()).resident_bytes;stream_request(env,priority,0u);}
      }
    }
  }
  for(const auto& [id,demand]:stream_demand)s.streamer.request(id,demand.second,demand.first);
  s.apply_streaming();
  if(total>maximum_scene3d_target_bytes){
    std::string detail="3D viewports exceed their 128 MiB target budget ("+std::to_string(total)+" B across "+std::to_string(s.views.size())+" views:";
    for(const auto* view:s.views){detail+=" "+std::to_string(static_cast<int>(view->destination.width))+"x"+std::to_string(static_cast<int>(view->destination.height))+"@"+std::to_string(view->render_scale);}
    throw std::length_error(detail+")");}
  if(meshes.size()>maximum_scene3d_resource_entries||textures.size()>maximum_scene3d_resource_entries||geometry_bytes>maximum_mesh3d_cache_bytes||texture_bytes>maximum_scene3d_texture_cache_bytes)
    throw std::length_error("Combined 3D views exceed their resident resource budget.");
  // SDL's GPU renderer keeps its command buffer until present. Flush alone
  // does not submit it: render all independent 3D targets before any 2D work.
  // One target per view prevents an earlier viewport sampling a later view.
  s.targets.resize(s.views.size());
  for(std::size_t i=0;i<s.views.size();++i){const auto e=scene3d_view_target_extent(*s.views[i]);const int w=static_cast<int>(e[0]),h=static_cast<int>(e[1]);
    if(!s.targets[i]||s.targets[i]->width!=w||s.targets[i]->height!=h)s.targets[i]=s.target(w,h);
    s.render(*s.views[i],*s.targets[i]);
  }
  s.stats.target_bytes=total;
}
void Scene3DRenderer::composite(const Scene3DView& view){
  auto& s=*storage_;s.require_owner();if(s.next_view>=s.views.size()||s.views[s.next_view]!=&view)throw std::logic_error("3D viewport composition order changed after preparation.");
  const auto r=view.destination;const SDL_FRect destination{r.x,r.y,r.width,r.height};
  checked(SDL_RenderTexture(s.renderer,s.targets[s.next_view++]->composite,nullptr,&destination),"3D viewport composition failed");
}
void Scene3DRenderer::set_texture_budget(std::uint64_t bytes){auto& s=*storage_;s.require_owner();s.streamer.set_budget(bytes);}
Scene3DStatistics Scene3DRenderer::statistics()const noexcept{auto result=storage_->stats;result.renderer_active=true;result.mesh_cache_entries=storage_->meshes.size();result.texture_cache_entries=storage_->textures.size();result.hdr=storage_->hdr;result.texture_budget_bytes=storage_->streamer.budget();return result;}
std::size_t Scene3DRenderer::bytes_per_pixel()const noexcept{return storage_->hdr?16u:8u;}
} // namespace stellar::native_map

