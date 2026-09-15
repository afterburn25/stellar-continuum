#include <stellar/engine/native_map_platform.hpp>
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

std::shared_ptr<const RgbaImage> RgbaImage::create(int width,int height,std::vector<std::uint8_t> rgba_pixels){
  if(width<=0||height<=0||width>maximum_rgba_image_dimension||height>maximum_rgba_image_dimension)throw std::length_error("RGBA image dimensions must be between 1 and 8192 pixels.");
  const auto wide=static_cast<std::size_t>(width),high=static_cast<std::size_t>(height);
  if(wide>maximum_rgba_image_bytes/4u/high)throw std::length_error("RGBA image exceeds the 64 MiB decoded-byte limit.");
  const auto expected=wide*high*4u;if(rgba_pixels.size()!=expected)throw std::invalid_argument("RGBA image pixel storage does not match its dimensions.");
  return std::shared_ptr<const RgbaImage>(new RgbaImage(width,height,std::move(rgba_pixels)));
}

std::shared_ptr<const RgbaImage> decode_rgba_image(const std::filesystem::path &path){
  if(path.empty())throw std::invalid_argument("An image path is required.");
  ComApartment apartment;ComOwner<IWICImagingFactory> factory;
  require_wic(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,__uuidof(IWICImagingFactory),reinterpret_cast<void**>(factory.put())),"Windows image decoder creation failed");
  ComOwner<IWICBitmapDecoder> decoder;require_wic(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,decoder.put()),"Windows could not open the image");
  UINT frame_count{};require_wic(decoder->GetFrameCount(&frame_count),"Windows could not inspect the image frames");if(frame_count==0)throw std::runtime_error("The image contains no decodable frame.");
  ComOwner<IWICBitmapFrameDecode> frame;require_wic(decoder->GetFrame(0,frame.put()),"Windows could not read the first image frame");UINT width{},height{};require_wic(frame->GetSize(&width,&height),"Windows could not inspect the image dimensions");
  if(width==0||height==0||width>static_cast<UINT>(maximum_rgba_image_dimension)||height>static_cast<UINT>(maximum_rgba_image_dimension))throw std::length_error("Decoded image dimensions must be between 1 and 8192 pixels.");
  const auto wide=static_cast<std::size_t>(width),high=static_cast<std::size_t>(height);if(wide>maximum_rgba_image_bytes/4u/high)throw std::length_error("Decoded image exceeds the 64 MiB RGBA limit.");const auto byte_count=wide*high*4u;
  ComOwner<IWICFormatConverter> converter;require_wic(factory->CreateFormatConverter(converter.put()),"Windows image pixel converter creation failed");require_wic(converter->Initialize(frame.get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0.,WICBitmapPaletteTypeCustom),"Windows could not convert the image to RGBA");
  std::vector<std::uint8_t> pixels(byte_count);const auto stride=static_cast<UINT>(wide*4u);require_wic(converter->CopyPixels(nullptr,stride,static_cast<UINT>(byte_count),pixels.data()),"Windows could not decode the image pixels");return RgbaImage::create(static_cast<int>(width),static_cast<int>(height),std::move(pixels));
}
} // namespace stellar::native_map
