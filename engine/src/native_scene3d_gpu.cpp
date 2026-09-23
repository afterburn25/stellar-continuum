#include <stellar/engine/texture_cook.hpp>
#include <stellar/engine/draw_batcher.hpp>
#include <stellar/engine/render_graph.hpp>
#include "native_scene3d_gpu.hpp"
#include "generated/scene3d_shaders.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace stellar::native_map {
namespace {
std::runtime_error gpu_error(const char* action){return std::runtime_error(std::string(action)+": "+SDL_GetError());}
void checked(bool value,const char* action){if(!value)throw gpu_error(action);}
struct Command {
  SDL_GPUCommandBuffer* value;
  explicit Command(SDL_GPUDevice* device):value(SDL_AcquireGPUCommandBuffer(device)){if(!value)throw gpu_error("3D command buffer allocation failed");}
  ~Command(){if(value)SDL_CancelGPUCommandBuffer(value);}
  void submit(){auto* current=value;value=nullptr;checked(SDL_SubmitGPUCommandBuffer(current),"3D GPU submission failed");}
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
struct Texture {
  SDL_GPUDevice* device;std::shared_ptr<const RgbaImage> owner;SDL_GPUTexture* texture{};std::uint64_t use{};std::size_t gpu_bytes{};
  explicit Texture(SDL_GPUDevice* d):device(d){}
  ~Texture(){if(texture)SDL_ReleaseGPUTexture(device,texture);}
  std::size_t bytes()const{return owner->byte_size()+gpu_bytes;}
};
struct Target {
  SDL_GPUDevice* device;SDL_GPUTexture* color{};SDL_GPUTexture* depth{};SDL_GPUTexture* hdr{};SDL_Texture* composite{};int width{},height{};
  explicit Target(SDL_GPUDevice* d):device(d){}
  ~Target(){if(composite)SDL_DestroyTexture(composite);if(hdr)SDL_ReleaseGPUTexture(device,hdr);if(depth)SDL_ReleaseGPUTexture(device,depth);if(color)SDL_ReleaseGPUTexture(device,color);}
  std::size_t bytes()const{return static_cast<std::size_t>(width)*height*(hdr?16u:8u);}
};
SDL_GPUTexture* make_texture(SDL_GPUDevice* d,int w,int h,SDL_GPUTextureFormat format,SDL_GPUTextureUsageFlags usage,Uint32 levels=1){
  SDL_GPUTextureCreateInfo info{};info.type=SDL_GPU_TEXTURETYPE_2D;info.format=format;info.usage=usage;
  info.width=w;info.height=h;info.layer_count_or_depth=1;info.num_levels=levels;info.sample_count=SDL_GPU_SAMPLECOUNT_1;
  auto* result=SDL_CreateGPUTexture(d,&info);if(!result)throw gpu_error("3D texture allocation failed");return result;
}
template<class Map> void evict(Map& cache,std::size_t& bytes,std::size_t incoming,std::size_t budget){
  while(!cache.empty()&&(bytes+incoming>budget||cache.size()>=maximum_scene3d_resource_entries)){
    const auto oldest=std::min_element(cache.begin(),cache.end(),[](const auto& a,const auto& b){return a.second->use<b.second->use;});
    bytes-=oldest->second->bytes();cache.erase(oldest);
  }
}
struct VertexUniform {Matrix4 mvp,model_view,shadow_from_model;};
struct FragmentUniform {std::array<float,4> tint,light,parameters,optics,absorption,view_options,camera_orientation,illumination,surface_response,surface_options,shadow_light,shadow_radii,shadow_options,effect_options,effect_sphere,volume_options;Matrix4 effect_from_view;std::array<std::array<float,4>,2> additional_direction,additional_illumination,additional_shadow;std::array<float,4> texture_options;};
static_assert(sizeof(Vertex3D)==32&&sizeof(VertexUniform)==192&&sizeof(FragmentUniform)==432);
}
struct Scene3DRenderer::Storage {
  SDL_GPUDevice* device;SDL_Renderer* renderer;std::thread::id owner=std::this_thread::get_id();
  SDL_GPUSampler* sampler{};SDL_GPUSampler* environment_sampler{};SDL_GPUSampler* detail_sampler{};std::array<SDL_GPUGraphicsPipeline*,4> pipelines{};SDL_GPUGraphicsPipeline* tonemap_pipeline{};
  std::unordered_map<const Mesh3D*,std::shared_ptr<Geometry>> meshes;
  std::unordered_map<const RgbaImage*,std::shared_ptr<Texture>> textures;
  std::vector<std::unique_ptr<Target>> targets;std::vector<const Scene3DView*> views;
  std::shared_ptr<const RgbaImage> white=RgbaImage::create(1,1,{255,255,255,255});
  Scene3DStatistics stats;std::uint64_t serial{};std::size_t next_view{};bool hdr{};SDL_GPUTextureFormat scene_format{};
  SDL_GPUBuffer* vertex_buffer{};SDL_GPUBuffer* fragment_buffer{};std::size_t vertex_capacity{64},fragment_capacity{64};
  Storage(SDL_GPUDevice* d,SDL_Renderer* r):device(d),renderer(r){}
  ~Storage(){targets.clear();textures.clear();meshes.clear();for(auto* p:pipelines)if(p)SDL_ReleaseGPUGraphicsPipeline(device,p);if(tonemap_pipeline)SDL_ReleaseGPUGraphicsPipeline(device,tonemap_pipeline);if(vertex_buffer)SDL_ReleaseGPUBuffer(device,vertex_buffer);if(fragment_buffer)SDL_ReleaseGPUBuffer(device,fragment_buffer);if(sampler)SDL_ReleaseGPUSampler(device,sampler);if(environment_sampler)SDL_ReleaseGPUSampler(device,environment_sampler);if(detail_sampler)SDL_ReleaseGPUSampler(device,detail_sampler);}
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
    hdr=SDL_GPUTextureSupportsFormat(device,SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER);
    scene_format=hdr?SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT:SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    const auto shader=[&](const auto& code,SDL_GPUShaderStage stage,Uint32 uniforms,Uint32 samplers,Uint32 storage=0){
      SDL_GPUShaderCreateInfo info{};info.code=reinterpret_cast<const Uint8*>(code);info.code_size=sizeof(code);info.entrypoint="main";info.format=SDL_GPU_SHADERFORMAT_SPIRV;info.stage=stage;info.num_uniform_buffers=uniforms;info.num_samplers=samplers;info.num_storage_buffers=storage;
      auto* result=SDL_CreateGPUShader(device,&info);if(!result)throw gpu_error("3D shader creation failed");return result;
    };
    const auto release=[&](SDL_GPUShader* value){SDL_ReleaseGPUShader(device,value);};
    std::unique_ptr<SDL_GPUShader,decltype(release)> vertex(shader(shaders::scene3d_vert,SDL_GPU_SHADERSTAGE_VERTEX,0,0,1),release),fragment(shader(shaders::scene3d_frag,SDL_GPU_SHADERSTAGE_FRAGMENT,0,8,1),release);
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
    }
    if(hdr){
      std::unique_ptr<SDL_GPUShader,decltype(release)> tonemap_vertex(shader(shaders::tonemap_vert,SDL_GPU_SHADERSTAGE_VERTEX,0,0),release),tonemap_fragment(shader(shaders::tonemap_frag,SDL_GPU_SHADERSTAGE_FRAGMENT,0,1),release);
      SDL_GPUColorTargetDescription color{};color.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
      SDL_GPUGraphicsPipelineCreateInfo info{};info.vertex_shader=tonemap_vertex.get();info.fragment_shader=tonemap_fragment.get();
      info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
      info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;info.rasterizer_state.front_face=SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;info.rasterizer_state.enable_depth_clip=true;
      info.multisample_state.sample_count=SDL_GPU_SAMPLECOUNT_1;
      info.target_info.color_target_descriptions=&color;info.target_info.num_color_targets=1;
      tonemap_pipeline=SDL_CreateGPUGraphicsPipeline(device,&info);if(!tonemap_pipeline)throw gpu_error("3D tonemap pipeline creation failed");
    }
  }
  std::shared_ptr<Geometry> geometry(std::shared_ptr<const Mesh3D> resource){
    if(auto it=meshes.find(resource.get());it!=meshes.end()){it->second->use=++serial;return it->second;}
    auto result=std::make_shared<Geometry>(device);result->owner=std::move(resource);result->use=++serial;
    const auto vb=static_cast<Uint32>(result->owner->vertices().size()*sizeof(Vertex3D)),ib=static_cast<Uint32>(result->owner->indices().size()*sizeof(std::uint32_t));
    evict(meshes,stats.mesh_cache_bytes,result->bytes(),maximum_mesh3d_cache_bytes);
    SDL_GPUBufferCreateInfo info{};info.usage=SDL_GPU_BUFFERUSAGE_VERTEX;info.size=vb;result->vertices=SDL_CreateGPUBuffer(device,&info);if(!result->vertices)throw gpu_error("3D vertex buffer creation failed");
    info.usage=SDL_GPU_BUFFERUSAGE_INDEX;info.size=ib;result->indices=SDL_CreateGPUBuffer(device,&info);if(!result->indices)throw gpu_error("3D index buffer creation failed");
    Transfer transfer(device,vb+ib);auto* memory=static_cast<std::byte*>(transfer.map());std::memcpy(memory,result->owner->vertices().data(),vb);std::memcpy(memory+vb,result->owner->indices().data(),ib);SDL_UnmapGPUTransferBuffer(device,transfer.value);
    Command command(device);auto* copy=SDL_BeginGPUCopyPass(command.value);if(!copy)throw gpu_error("3D mesh copy pass failed");
    SDL_GPUTransferBufferLocation from{transfer.value,0};SDL_GPUBufferRegion to{result->vertices,0,vb};SDL_UploadToGPUBuffer(copy,&from,&to,false);
    from.offset=vb;to={result->indices,0,ib};SDL_UploadToGPUBuffer(copy,&from,&to,false);SDL_EndGPUCopyPass(copy);command.submit();
    meshes.emplace(result->owner.get(),result);stats.mesh_cache_bytes+=result->bytes();++stats.mesh_uploads;return result;
  }
  std::shared_ptr<Texture> texture(std::shared_ptr<const RgbaImage> resource){
    if(!resource)resource=white;
    if(auto it=textures.find(resource.get());it!=textures.end()){it->second->use=++serial;return it->second;}
    auto result=std::make_shared<Texture>(device);result->owner=std::move(resource);result->use=++serial;
    const auto& image=*result->owner;const auto layout=texture_mip_layout3d(&image);
    result->gpu_bytes=layout.gpu_bytes;
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
      const bool block=format!=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
      result->gpu_bytes=0;for(const auto&m:levels)result->gpu_bytes+=m.blocks.size();
      evict(textures,stats.texture_cache_bytes,result->bytes(),maximum_scene3d_texture_cache_bytes);
      result->texture=make_texture(device,image.width(),image.height(),format,SDL_GPU_TEXTUREUSAGE_SAMPLER,static_cast<Uint32>(levels.size()));
      // Every transfer offset is aligned, including the tiny 1x1 RGBA tail.
      std::size_t transfer_bytes=0;for(const auto&m:levels)transfer_bytes+=((m.blocks.size()+15)/16)*16;
      Transfer transfer(device,static_cast<Uint32>(transfer_bytes));auto* memory=static_cast<std::uint8_t*>(transfer.map());std::size_t offset=0;
      for(const auto&m:levels){std::memcpy(memory+offset,m.blocks.data(),m.blocks.size());offset+=((m.blocks.size()+15)/16)*16;}SDL_UnmapGPUTransferBuffer(device,transfer.value);
      Command command(device);auto* copy=SDL_BeginGPUCopyPass(command.value);if(!copy)throw gpu_error("Cooked texture upload failed");offset=0;
      for(Uint32 level=0;level<levels.size();++level){const auto&m=levels[level];SDL_GPUTextureTransferInfo from{};from.transfer_buffer=transfer.value;from.offset=static_cast<Uint32>(offset);from.pixels_per_row=block?(m.width+3)/4*4:m.width;from.rows_per_layer=block?(m.height+3)/4*4:m.height;
        SDL_GPUTextureRegion to{};to.texture=result->texture;to.mip_level=level;to.w=m.width;to.h=m.height;to.d=1;SDL_UploadToGPUTexture(copy,&from,&to,false);offset+=((m.blocks.size()+15)/16)*16;
      }SDL_EndGPUCopyPass(copy);command.submit();textures.emplace(result->owner.get(),result);stats.texture_cache_bytes+=result->bytes();++stats.texture_uploads;return result;
    }
    const bool compressed=!image.bc1_mips().empty()&&SDL_GPUTextureSupportsFormat(device,SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM,SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_SAMPLER);
    if(compressed){result->gpu_bytes=0;for(const auto& m:image.bc1_mips())result->gpu_bytes+=m.blocks.size();}
    evict(textures,stats.texture_cache_bytes,result->bytes(),maximum_scene3d_texture_cache_bytes);
    if(compressed){
      result->texture=make_texture(device,image.width(),image.height(),SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM,SDL_GPU_TEXTUREUSAGE_SAMPLER,layout.levels);
      Transfer transfer(device,static_cast<Uint32>(result->gpu_bytes));auto* data=static_cast<std::uint8_t*>(transfer.map());std::size_t offset=0;
      for(const auto& m:image.bc1_mips()){std::memcpy(data+offset,m.blocks.data(),m.blocks.size());offset+=m.blocks.size();}SDL_UnmapGPUTransferBuffer(device,transfer.value);
      Command command(device);auto* copy=SDL_BeginGPUCopyPass(command.value);if(!copy)throw gpu_error("Compressed texture copy pass failed");offset=0;
      for(Uint32 level=0;level<image.bc1_mips().size();++level){const auto& m=image.bc1_mips()[level];
        SDL_GPUTextureTransferInfo from{};from.transfer_buffer=transfer.value;from.offset=static_cast<Uint32>(offset);from.pixels_per_row=(m.width+3)/4*4;from.rows_per_layer=(m.height+3)/4*4;
        SDL_GPUTextureRegion to{};to.texture=result->texture;to.mip_level=level;to.w=m.width;to.h=m.height;to.d=1;SDL_UploadToGPUTexture(copy,&from,&to,false);offset+=m.blocks.size();
      }SDL_EndGPUCopyPass(copy);command.submit();
      textures.emplace(result->owner.get(),result);stats.texture_cache_bytes+=result->bytes();++stats.texture_uploads;return result;
    }
    result->texture=make_texture(device,image.width(),image.height(),SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,SDL_GPU_TEXTUREUSAGE_SAMPLER|SDL_GPU_TEXTUREUSAGE_COLOR_TARGET,layout.levels);
    Transfer transfer(device,static_cast<Uint32>(image.pixels().size()));std::memcpy(transfer.map(),image.pixels().data(),image.pixels().size());SDL_UnmapGPUTransferBuffer(device,transfer.value);
    Command command(device);auto* copy=SDL_BeginGPUCopyPass(command.value);if(!copy)throw gpu_error("3D texture copy pass failed");
    SDL_GPUTextureTransferInfo from{};from.transfer_buffer=transfer.value;from.pixels_per_row=image.width();from.rows_per_layer=image.height();
    SDL_GPUTextureRegion to{};to.texture=result->texture;to.w=image.width();to.h=image.height();to.d=1;
    SDL_UploadToGPUTexture(copy,&from,&to,false);SDL_EndGPUCopyPass(copy);
    // Generate once, outside the copy pass and before any consumer samples it.
    // All four channels are data: alpha may encode opacity OR surface height.
    // SDL 3.4.16's Vulkan GenerateMipmaps shifts dimensions without clamping
    // to one, yielding empty blits for rectangular tails and every 1D mip.
    // Explicit blit regions keep both axes valid through the final 1x1 level.
    Uint32 width=image.width(),height=image.height();
    for(Uint32 level=1;level<layout.levels;++level){
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
    if(hdr)result->hdr=make_texture(device,w,h,SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER);
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
    struct Draw {const MeshInstance3D* instance;PreparedInstance3D transform;std::shared_ptr<Geometry> mesh;std::shared_ptr<Texture> image,optical,environment,normal,properties,cloud,shadow,next;};
    std::vector<Draw> draws;draws.reserve(view.scene->instances().size());
    for(const auto& instance:view.scene->instances()){
      auto prepared=prepare_instance3d(view.scene->camera(),instance,view.destination.width/view.destination.height);
      if(!prepared.visible){++stats.culled_instances;continue;}
      const auto& optical=instance.material.dielectric;
      // Disabled optics reuse the existing texture binding, without an upload.
      auto surface=texture(instance.material.texture);
      const auto& response=instance.material.surface_response;
      draws.push_back({&instance,prepared,geometry(instance.mesh),surface,
        optical?texture(optical->surface):surface,optical?texture(optical->environment):surface,
        response?texture(response->normal):surface,response?texture(response->properties):surface,response?texture(response->cloud_shadow):surface,
        instance.material.shadow&&instance.material.shadow->opacity_map?texture(instance.material.shadow->opacity_map):surface,
        instance.material.surface_effect?texture(instance.material.surface_effect->next_texture):surface});
    }
    // Engine DrawBatcher owns submission ordering/batching: opaque groups by
    // (material,mesh), transparent stays back-to-front. A material_id interns
    // everything the binding must share — all eight textures plus the flags
    // that change sampler/pipeline selection — so equal ids mean one
    // instanced call can serve the whole run.
    using MaterialKey=std::tuple<bool,bool,const Texture*,const Texture*,const Texture*,const Texture*,const Texture*,const Texture*,const Texture*,const Texture*>;
    std::map<MaterialKey,std::uint32_t> material_ids;std::map<const Mesh3D*,std::uint32_t> mesh_ids;
    engine::DrawBatcher batcher;batcher.begin_frame();
    for(std::size_t submission=0;submission<draws.size();++submission){
      const auto& d=draws[submission];
      const MaterialKey key{d.instance->material.double_sided,d.instance->material.anisotropic_texture,d.image.get(),d.optical.get(),d.environment.get(),d.normal.get(),d.properties.get(),d.cloud.get(),d.shadow.get(),d.next.get()};
      engine::DrawItem item{};item.material_id=material_ids.try_emplace(key,static_cast<std::uint32_t>(material_ids.size())).first->second;
      item.mesh_id=mesh_ids.try_emplace(d.mesh->owner.get(),static_cast<std::uint32_t>(mesh_ids.size())).first->second;
      item.instance_index=static_cast<std::uint32_t>(submission);item.depth=d.transform.camera_depth;item.transparent=d.instance->material.transparent;
      batcher.submit(item);
    }
    batcher.build();const auto& sorted=batcher.sorted_items();
    std::vector<VertexUniform> vertex_data(sorted.size());std::vector<FragmentUniform> fragment_data(sorted.size());
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
      fragment.texture_options[0]=material.cubic_magnification?1.f:0.f;
      if(material.shadow){const auto& s=*material.shadow;
        fragment.shadow_light={shadow.light.x,shadow.light.y,shadow.light.z,s.shape==AnalyticShadowShape3D::Ellipsoid?1.f:2.f};
        fragment.shadow_radii={s.radii.x,s.radii.y,s.radii.z,0};
        fragment.shadow_options={s.inner_radius,s.outer_radius,s.opacity,s.opacity_map?1.f:0.f};}
      if(material.surface_effect){const auto& e=*material.surface_effect;
        fragment.effect_options={1,e.blend,e.flow_phase,e.distortion};
        fragment.effect_sphere={e.view_sphere_center.x,e.view_sphere_center.y,e.view_sphere_center.z,e.sphere_radius};
        fragment.volume_options={e.volume_depth,static_cast<float>(e.volume_steps),e.volume_density,e.volume_seed};
        if(e.volume_depth>0){
          // Model transforms use uniform scale and an orthonormal rotation.
          // Invert their camera-relative matrix once per draw, not per fragment.
          const auto& from=draw.transform.model_view.values;auto& to=fragment.effect_from_view.values;
          const float inverse_square=1.f/(draw.instance->scale*draw.instance->scale);
          for(int column=0;column<3;++column)for(int row=0;row<3;++row)to[column*4+row]=from[row*4+column]*inverse_square;
          for(int row=0;row<3;++row)to[12+row]=-(to[row]*from[12]+to[4+row]*from[13]+to[8+row]*from[14]);
          to[15]=1;
        }}
      if(material.surface_response){const auto& s=*material.surface_response;
        fragment.surface_response={1,s.normal_strength,s.relief*draw.instance->scale,s.cloud_shadow?s.cloud_opacity:0};
        fragment.surface_options[0]=s.cloud_offset.x;fragment.surface_options[1]=s.cloud_offset.y;}
      if(material.dielectric){const auto& d=*material.dielectric;
        fragment.optics={d.index_of_refraction,d.roughness,d.transmission,d.thickness};
        fragment.absorption={d.absorption.x,d.absorption.y,d.absorption.z,d.environment_strength};fragment.view_options[1]=d.specular_strength;
        fragment.view_options[2]=d.surface_relief*draw.instance->scale;}
    }
    // Pass scheduling goes through the engine RenderGraph: resources and
    // dependencies are declared per frame, compile() validates the DAG and
    // emits the execution order, and the backend binds routines by tag.
    engine::RenderGraph graph;
    const auto hdr_target=graph.add_resource({engine::RenderResourceDesc::Kind::Texture2D,"hdr-scene",view.destination.width,view.destination.height,"rgba16f",true});
    const auto color_target=graph.add_resource({engine::RenderResourceDesc::Kind::Texture2D,"color",view.destination.width,view.destination.height,"rgba8",true});
    const auto depth_target=graph.add_resource({engine::RenderResourceDesc::Kind::Texture2D,"depth",view.destination.width,view.destination.height,"d32",true});
    graph.add_pass({"scene3d",{},{target.hdr?hdr_target:color_target,depth_target},{},"scene3d"});
    graph.add_pass({"tonemap",{hdr_target},{color_target},{},"tonemap",target.hdr!=nullptr});
    std::vector<std::string> order;std::vector<engine::RenderGraphDiagnostic> diagnostics;
    if(!graph.compile(&order,&diagnostics))throw gpu_error("3D render graph compile failed: "+(diagnostics.empty()?"unknown":diagnostics.front().message));
    Command command(device);
    if(!sorted.empty()){
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
      Transfer transfer(device,vertex_bytes+fragment_bytes);auto* memory=static_cast<std::byte*>(transfer.map());
      std::memcpy(memory,vertex_data.data(),vertex_bytes);std::memcpy(memory+vertex_bytes,fragment_data.data(),fragment_bytes);SDL_UnmapGPUTransferBuffer(device,transfer.value);
      auto* copy=SDL_BeginGPUCopyPass(command.value);if(!copy)throw gpu_error("3D instance upload copy pass failed");
      SDL_GPUTransferBufferLocation from{transfer.value,0};SDL_GPUBufferRegion to{vertex_buffer,0,vertex_bytes};SDL_UploadToGPUBuffer(copy,&from,&to,false);
      from.offset=vertex_bytes;to={fragment_buffer,0,fragment_bytes};SDL_UploadToGPUBuffer(copy,&from,&to,false);
      SDL_EndGPUCopyPass(copy);
    }
    for(const auto& name:order){
      if(name=="scene3d"){
        SDL_GPUColorTargetInfo color{};color.texture=target.hdr?target.hdr:target.color;color.load_op=SDL_GPU_LOADOP_CLEAR;color.store_op=SDL_GPU_STOREOP_STORE;
        SDL_GPUDepthStencilTargetInfo depth{};depth.texture=target.depth;depth.clear_depth=1;depth.load_op=SDL_GPU_LOADOP_CLEAR;depth.store_op=SDL_GPU_STOREOP_DONT_CARE;depth.stencil_load_op=SDL_GPU_LOADOP_DONT_CARE;depth.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE;
        auto* pass=SDL_BeginGPURenderPass(command.value,&color,1,&depth);if(!pass)throw gpu_error("3D render pass failed");
        if(!sorted.empty()){
          SDL_BindGPUVertexStorageBuffers(pass,0,&vertex_buffer,1);SDL_BindGPUFragmentStorageBuffers(pass,0,&fragment_buffer,1);
          for(const auto& batch:batcher.batches()){
            const auto& draw=draws[sorted[batch.first_item].instance_index];const auto& material=draw.instance->material;
            SDL_BindGPUGraphicsPipeline(pass,pipelines[(material.transparent?1:0)+(material.double_sided?2:0)]);
            const SDL_GPUBufferBinding vertices{draw.mesh->vertices,0},indices{draw.mesh->indices,0};
            SDL_BindGPUVertexBuffers(pass,0,&vertices,1);SDL_BindGPUIndexBuffer(pass,&indices,SDL_GPU_INDEXELEMENTSIZE_32BIT);
            const SDL_GPUTextureSamplerBinding sampled[]{{draw.image->texture,material.anisotropic_texture?detail_sampler:sampler},{draw.optical->texture,sampler},{draw.environment->texture,environment_sampler},{draw.normal->texture,environment_sampler},{draw.properties->texture,environment_sampler},{draw.cloud->texture,environment_sampler},{draw.shadow->texture,sampler},{draw.next->texture,sampler}};
            SDL_BindGPUFragmentSamplers(pass,0,sampled,8);
            SDL_DrawGPUIndexedPrimitives(pass,static_cast<Uint32>(draw.mesh->owner->indices().size()),batch.count,0,0,batch.first_item);++stats.draw_calls;
          }
        }
        SDL_EndGPURenderPass(pass);
      }else if(name=="tonemap"){
        SDL_GPUColorTargetInfo resolve{};resolve.texture=target.color;resolve.load_op=SDL_GPU_LOADOP_DONT_CARE;resolve.store_op=SDL_GPU_STOREOP_STORE;
        auto* resolve_pass=SDL_BeginGPURenderPass(command.value,&resolve,1,nullptr);if(!resolve_pass)throw gpu_error("3D tonemap pass failed");
        SDL_BindGPUGraphicsPipeline(resolve_pass,tonemap_pipeline);
        const SDL_GPUTextureSamplerBinding sampled[]{{target.hdr,sampler}};
        SDL_BindGPUFragmentSamplers(resolve_pass,0,sampled,1);
        SDL_DrawGPUPrimitives(resolve_pass,3,1,0,0);
        SDL_EndGPURenderPass(resolve_pass);
      }
    }
    command.submit();
  }
};
Scene3DRenderer::Scene3DRenderer(SDL_GPUDevice* device,SDL_Renderer* renderer):storage_(std::make_unique<Storage>(device,renderer)){storage_->initialize();}
Scene3DRenderer::~Scene3DRenderer()=default;
void Scene3DRenderer::prepare(const DrawList& list){
  auto& s=*storage_;s.require_owner();s.views.clear();s.next_view=0;s.stats.draw_calls=s.stats.culled_instances=0;
  for(const auto& c:list.world)if(const auto* view=std::get_if<Scene3DView>(&c))s.views.push_back(view);
  for(const auto& c:list.overlay)if(const auto* view=std::get_if<Scene3DView>(&c))s.views.push_back(view);
  if(s.views.size()>maximum_scene3d_views)throw std::length_error("3D frame exceeds its viewport budget.");
  std::size_t total=0;
  std::unordered_set<const Mesh3D*> meshes;std::unordered_set<const RgbaImage*> textures;
  std::size_t geometry_bytes=0,texture_bytes=0;
  for(const auto* view:s.views){const auto r=view->destination;
    if(!view->scene||!std::isfinite(r.x)||!std::isfinite(r.y)||std::abs(r.x)>65536||std::abs(r.y)>65536||
       !std::isfinite(r.width)||!std::isfinite(r.height)||r.width<1||r.height<1||r.width>8192||r.height>8192)
      throw std::invalid_argument("3D viewport requires a scene and finite bounded dimensions.");
    total+=static_cast<std::size_t>(std::ceil(r.width))*static_cast<std::size_t>(std::ceil(r.height))*(s.hdr?16u:8u);
    for(const auto& instance:view->scene->instances()){
      if(meshes.insert(instance.mesh.get()).second)geometry_bytes+=instance.mesh->byte_size()*2;
      const auto& image=instance.material.texture;
      if(textures.insert(image.get()).second)texture_bytes+=texture_mip_layout3d(image.get()).resident_bytes;
      if(instance.material.dielectric)for(const auto& optical:{instance.material.dielectric->environment,instance.material.dielectric->surface})
        if(textures.insert(optical.get()).second)texture_bytes+=texture_mip_layout3d(optical.get()).resident_bytes;
      if(instance.material.surface_response)for(const auto& response_image:{instance.material.surface_response->normal,instance.material.surface_response->properties,instance.material.surface_response->cloud_shadow})
        if(textures.insert(response_image.get()).second)texture_bytes+=texture_mip_layout3d(response_image.get()).resident_bytes;
      if(instance.material.surface_effect){const auto& image_next=instance.material.surface_effect->next_texture;
        if(textures.insert(image_next.get()).second)texture_bytes+=texture_mip_layout3d(image_next.get()).resident_bytes;}
      if(instance.material.shadow&&instance.material.shadow->opacity_map){const auto& shadow_image=instance.material.shadow->opacity_map;
        if(textures.insert(shadow_image.get()).second)texture_bytes+=texture_mip_layout3d(shadow_image.get()).resident_bytes;}
    }
  }
  if(total>maximum_scene3d_target_bytes)throw std::length_error("3D viewports exceed their 128 MiB target budget.");
  if(meshes.size()>maximum_scene3d_resource_entries||textures.size()>maximum_scene3d_resource_entries||geometry_bytes>maximum_mesh3d_cache_bytes||texture_bytes>maximum_scene3d_texture_cache_bytes)
    throw std::length_error("Combined 3D views exceed their resident resource budget.");
  // SDL's GPU renderer keeps its command buffer until present. Flush alone
  // does not submit it: render all independent 3D targets before any 2D work.
  // One target per view prevents an earlier viewport sampling a later view.
  s.targets.resize(s.views.size());
  for(std::size_t i=0;i<s.views.size();++i){const auto r=s.views[i]->destination;const int w=static_cast<int>(std::ceil(r.width)),h=static_cast<int>(std::ceil(r.height));
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
Scene3DStatistics Scene3DRenderer::statistics()const noexcept{auto result=storage_->stats;result.mesh_cache_entries=storage_->meshes.size();result.texture_cache_entries=storage_->textures.size();result.hdr=storage_->hdr;return result;}
} // namespace stellar::native_map

