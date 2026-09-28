#include <stellar/engine/native_map_platform.hpp>
#include <stellar/engine/texture_cook.hpp>
#include <stellar/engine/asset_registry.hpp>
#include <stellar/engine/spherical_material_preparation.hpp>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <objbase.h>
#include <wincodec.h>
#else
#error The native image decoder requires the Windows Imaging Component.
#endif

namespace stellar::native_map {
namespace {
template<class Interface> class ComOwner final {
 public:
  ~ComOwner(){if(value_)value_->Release();}
  ComOwner()=default;ComOwner(const ComOwner&)=delete;ComOwner&operator=(const ComOwner&)=delete;
  [[nodiscard]] Interface *get()const noexcept{return value_;}
  [[nodiscard]] Interface **put()noexcept{return &value_;}
  [[nodiscard]] Interface *operator->()const noexcept{return value_;}
 private:Interface *value_{};
};
class ComApartment final {
 public:
  ComApartment(){const auto result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(SUCCEEDED(result))uninitialize_=true;else if(result!=RPC_E_CHANGED_MODE)throw std::runtime_error("Windows image decoder COM initialization failed.");}
  ~ComApartment(){if(uninitialize_)CoUninitialize();}
  ComApartment(const ComApartment&)=delete;ComApartment&operator=(const ComApartment&)=delete;
 private:bool uninitialize_{};
};
void require_wic(HRESULT result,const char *operation){if(FAILED(result))throw std::runtime_error(std::string(operation)+" (HRESULT "+std::to_string(static_cast<unsigned long>(result))+").");}
}

std::shared_ptr<const RgbaImage> RgbaImage::create(int width,int height,std::vector<std::uint8_t> rgba_pixels,std::vector<Bc1MipLevel> bc1_mips){
  if(width<=0||height<=0||width>maximum_rgba_image_dimension||height>maximum_rgba_image_dimension)throw std::length_error("RGBA image dimensions must be between 1 and 8192 pixels.");
  const auto wide=static_cast<std::size_t>(width),high=static_cast<std::size_t>(height);
  if(wide>maximum_rgba_image_bytes/4u/high)throw std::length_error("RGBA image exceeds the 64 MiB decoded-byte limit.");
  const auto expected=wide*high*4u;if(rgba_pixels.size()!=expected)throw std::invalid_argument("RGBA image pixel storage does not match its dimensions.");
  if(!bc1_mips.empty()){
    if(width%4||height%4)throw std::invalid_argument("BC1 base dimensions must be block aligned");
    int w=width,h=height;std::size_t n=0,total=expected;
    for(;;){if(n>=bc1_mips.size())throw std::invalid_argument("Incomplete BC1 mip chain");const auto& m=bc1_mips[n++];
      if(m.width!=w||m.height!=h||m.blocks.size()!=static_cast<std::size_t>((w+3)/4)*((h+3)/4)*8)throw std::invalid_argument("Invalid BC1 mip storage");
      total+=m.blocks.size();if(total>maximum_rgba_image_bytes)throw std::length_error("Image and compressed mips exceed the 64 MiB resource limit");
      if(w==1&&h==1)break;w=std::max(1,w/2);h=std::max(1,h/2);
    }if(n!=bc1_mips.size())throw std::invalid_argument("Excess BC1 mip levels");
  }
  auto result=std::shared_ptr<RgbaImage>(new RgbaImage(width,height,std::move(rgba_pixels)));result->bc1_mips_=std::move(bc1_mips);return result;
}

std::shared_ptr<const RgbaImage> RgbaImage::create_cooked(TextureFormat format,std::vector<Bc1MipLevel> mips){
  if(mips.empty()||mips.size()>14)throw std::runtime_error("Invalid cooked texture mip count");
  int w=mips.front().width,h=mips.front().height;std::size_t total=0;
  if(w<1||h<1||w>maximum_rgba_image_dimension||h>maximum_rgba_image_dimension||
     static_cast<std::uint64_t>(w)*h*4>maximum_rgba_image_bytes||
     (format!=TextureFormat::Rgba8&&format!=TextureFormat::Bc7&&format!=TextureFormat::Bc5&&format!=TextureFormat::Bc4))
    throw std::runtime_error("Invalid cooked texture dimensions/format");
  for(std::size_t i=0;i<mips.size();++i){const auto& m=mips[i];
    if(m.width!=w||m.height!=h)throw std::runtime_error("Cooked mip dimensions mismatch");
    const auto expected=format==TextureFormat::Rgba8?static_cast<std::size_t>(w)*h*4:static_cast<std::size_t>((w+3)/4)*((h+3)/4)*(format==TextureFormat::Bc4?8:16);
    if(m.blocks.size()!=expected)throw std::runtime_error("Cooked mip byte count mismatch");
    total+=expected;if(total>128u*1024u*1024u)throw std::runtime_error("Cooked mip chain exceeds budget");
    if(w==1&&h==1&&i+1!=mips.size())throw std::runtime_error("Excess cooked mip levels");w=std::max(1,w/2);h=std::max(1,h/2);
  }
  if(mips.back().width!=1||mips.back().height!=1)throw std::runtime_error("Incomplete cooked mip chain");
  auto pixels=format==TextureFormat::Rgba8?std::vector<std::uint8_t>{}:decode_texture_level(format,mips.front());
  auto result=std::shared_ptr<RgbaImage>(new RgbaImage(mips.front().width,mips.front().height,std::move(pixels)));
  result->cooked_format_=format;result->cooked_mips_=std::move(mips);return result;
}
std::size_t image_decode_output_bytes(const std::filesystem::path& path,std::size_t loose_output_bytes,int maximum_width,ImageDecodeUsage usage){
  if(auto registry=stellar::engine::mounted_asset_registry())if(const auto* record=registry->find(stellar::engine::asset_alias(path));record&&record->type=="texture"){
    const auto alias=stellar::engine::asset_alias(path);
    for(const auto limit:{256,512})if(alias.find("stellar-eruptions/"+std::to_string(limit)+"/")!=std::string::npos)maximum_width=maximum_width?std::min(maximum_width,limit):limit;
    std::size_t first=0;while(maximum_width>0&&first+1<record->chunks.size()&&record->chunks[first].width>maximum_width)++first;
    const auto& base=record->chunks.at(first);const auto pixels=static_cast<std::size_t>(base.width)*base.height*4;
    if(usage==ImageDecodeUsage::PixelsOnly)return pixels;
    std::size_t total=texture_format_from_name(record->format)==TextureFormat::Rgba8?0:pixels;
    for(std::size_t i=first;i<record->chunks.size();++i)total+=static_cast<std::size_t>(record->chunks[i].raw_bytes);
    return total;
  }
  return loose_output_bytes;
}
std::shared_ptr<const RgbaImage> decode_rgba_image(const std::filesystem::path &path,int maximum_width,ImageDecodeUsage usage){
  if(auto registry=stellar::engine::mounted_asset_registry())if(const auto* record=registry->find(stellar::engine::asset_alias(path));record&&record->type=="texture"){
    const auto alias=stellar::engine::asset_alias(path);
    for(const auto limit:{256,512})if(alias.find("stellar-eruptions/"+std::to_string(limit)+"/")!=std::string::npos)maximum_width=maximum_width?std::min(maximum_width,limit):limit;
    std::size_t first=0;while(maximum_width>0&&first+1<record->chunks.size()&&record->chunks[first].width>maximum_width)++first;
    if(usage==ImageDecodeUsage::PixelsOnly){
      const auto& chunk=record->chunks.at(first);
      Bc1MipLevel mip{chunk.width,chunk.height,registry->read(*record,first)};
      auto pixels=texture_format_from_name(record->format)==TextureFormat::Rgba8?std::move(mip.blocks):decode_texture_level(texture_format_from_name(record->format),mip);
      return RgbaImage::create(mip.width,mip.height,std::move(pixels));
    }
    std::vector<Bc1MipLevel> mips;for(std::size_t i=first;i<record->chunks.size();++i)mips.push_back({record->chunks[i].width,record->chunks[i].height,registry->read(*record,i)});
    return RgbaImage::create_cooked(texture_format_from_name(record->format),std::move(mips));
  }
  if(path.empty())throw std::invalid_argument("An image path is required.");
  ComApartment apartment;ComOwner<IWICImagingFactory> factory;
  require_wic(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,__uuidof(IWICImagingFactory),reinterpret_cast<void**>(factory.put())),"Windows image decoder creation failed");
    auto encoded=stellar::engine::read_resource(path,maximum_rgba_image_bytes);
  ComOwner<IWICStream> source;require_wic(factory->CreateStream(source.put()),"Image stream creation failed");
  require_wic(source->InitializeFromMemory(encoded.data(),static_cast<DWORD>(encoded.size())),"Image stream initialization failed");
  ComOwner<IWICBitmapDecoder> decoder;require_wic(factory->CreateDecoderFromStream(source.get(),nullptr,WICDecodeMetadataCacheOnDemand,decoder.put()),"Windows could not open the image");
  UINT frame_count{};require_wic(decoder->GetFrameCount(&frame_count),"Windows could not inspect the image frames");if(frame_count==0)throw std::runtime_error("The image contains no decodable frame.");
  ComOwner<IWICBitmapFrameDecode> frame;require_wic(decoder->GetFrame(0,frame.put()),"Windows could not read the first image frame");UINT width{},height{};require_wic(frame->GetSize(&width,&height),"Windows could not inspect the image dimensions");
  if(width==0||height==0||width>static_cast<UINT>(maximum_rgba_image_dimension)||height>static_cast<UINT>(maximum_rgba_image_dimension))throw std::length_error("Decoded image dimensions must be between 1 and 8192 pixels.");
  const auto wide=static_cast<std::size_t>(width),high=static_cast<std::size_t>(height);if(wide>maximum_rgba_image_bytes/4u/high)throw std::length_error("Decoded image exceeds the 64 MiB RGBA limit.");const auto byte_count=wide*high*4u;
  ComOwner<IWICFormatConverter> converter;require_wic(factory->CreateFormatConverter(converter.put()),"Windows image pixel converter creation failed");require_wic(converter->Initialize(frame.get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0.,WICBitmapPaletteTypeCustom),"Windows could not convert the image to RGBA");
  std::vector<std::uint8_t> pixels(byte_count);const auto stride=static_cast<UINT>(wide*4u);require_wic(converter->CopyPixels(nullptr,stride,static_cast<UINT>(byte_count),pixels.data()),"Windows could not decode the image pixels");return RgbaImage::create(static_cast<int>(width),static_cast<int>(height),std::move(pixels));
}
void encode_rgba_png(const RgbaImage& image,const std::filesystem::path& path){
  if(path.empty())throw std::invalid_argument("A PNG output path is required.");
  ComApartment apartment;ComOwner<IWICImagingFactory> factory;
  require_wic(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,__uuidof(IWICImagingFactory),reinterpret_cast<void**>(factory.put())),"PNG encoder creation failed");
  ComOwner<IWICStream> stream;require_wic(factory->CreateStream(stream.put()),"PNG stream creation failed");
  require_wic(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE),"PNG output could not be opened");
  ComOwner<IWICBitmapEncoder> encoder;require_wic(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,encoder.put()),"PNG encoder unavailable");
  require_wic(encoder->Initialize(stream.get(),WICBitmapEncoderNoCache),"PNG initialization failed");
  ComOwner<IWICBitmapFrameEncode> frame;require_wic(encoder->CreateNewFrame(frame.put(),nullptr),"PNG frame creation failed");
  require_wic(frame->Initialize(nullptr),"PNG frame initialization failed");
  require_wic(frame->SetSize(static_cast<UINT>(image.width()),static_cast<UINT>(image.height())),"PNG size failed");
  WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;require_wic(frame->SetPixelFormat(&format),"PNG format failed");
  if(format!=GUID_WICPixelFormat32bppBGRA)throw std::runtime_error("PNG encoder cannot preserve four-channel pixels.");
  auto pixels=image.pixels();for(std::size_t i=0;i<pixels.size();i+=4)std::swap(pixels[i],pixels[i+2]);
  require_wic(frame->WritePixels(static_cast<UINT>(image.height()),static_cast<UINT>(image.width()*4),static_cast<UINT>(pixels.size()),pixels.data()),"PNG pixels failed");
  require_wic(frame->Commit(),"PNG frame commit failed");require_wic(encoder->Commit(),"PNG commit failed");
}
} // namespace stellar::native_map

