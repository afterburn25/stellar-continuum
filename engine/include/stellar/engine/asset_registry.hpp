#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace stellar::engine {
enum class AssetState { AcceptedRuntime, SourceOnly, Rejected, QaOnly, EditorOnly, DebugOnly, Deprecated };
enum class AssetCodec : std::uint32_t { None=0, XpressHuff=1, XpressRgbaDelta=2, Lzms=3, LzmsRgbaDelta=4 };
struct AssetChunk {
  std::string package,hash;
  std::uint64_t offset{},stored_bytes{},raw_bytes{};
  AssetCodec codec{};
  int width{},height{};
};
struct AssetRecord {
  std::string id,type,subtype,format,source_hash;
  std::vector<std::string> aliases,dependencies;
  std::vector<AssetChunk> chunks;
  int width{},height{};
  std::uint64_t source_bytes{};
  // Original canvas and crop bounds; identity for non-cropped content.
  int canvas_width{},canvas_height{},crop_x{},crop_y{};
  double quality_rmse{},quality_max_error{};
};
struct AssetDiagnostics {std::uint64_t reads{},bytes_read{},decoded_bytes{},failures{};std::size_t assets{},packages{};std::vector<std::string> recent_failures;};
class AssetRegistry final {
 public:
  explicit AssetRegistry(const std::filesystem::path& manifest);
  ~AssetRegistry();
  [[nodiscard]] const AssetRecord* find(std::string_view id_or_alias) const;
  [[nodiscard]] const std::vector<AssetRecord>& records() const;
  [[nodiscard]] std::vector<std::uint8_t> read(const AssetRecord&,std::size_t chunk=0) const;
  [[nodiscard]] AssetDiagnostics diagnostics() const;
  void validate_all() const;
 private:
  struct Impl;std::unique_ptr<Impl> impl_;
};
// Mount once before worker creation. Shared immutable ownership permits safe
// concurrent reads and remounts in editor/tests without dangling registry data.
void mount_asset_registry(std::filesystem::path root,bool allow_source_fallback=false);
void unmount_asset_registry();
[[nodiscard]] std::shared_ptr<const AssetRegistry> mounted_asset_registry();
[[nodiscard]] std::string asset_alias(const std::filesystem::path&);
[[nodiscard]] bool resource_exists(const std::filesystem::path&);
[[nodiscard]] std::vector<std::uint8_t> read_resource(const std::filesystem::path&,std::size_t maximum_bytes=256u*1024u*1024u);
[[nodiscard]] std::istringstream resource_stream(const std::filesystem::path&);
[[nodiscard]] std::string asset_path_utf8(const std::filesystem::path&);
[[nodiscard]] std::vector<std::uint8_t> compress_asset_bytes(std::span<const std::uint8_t>,AssetCodec&,bool rgba_predictor=false);
[[nodiscard]] std::vector<std::uint8_t> decompress_asset_bytes(std::span<const std::uint8_t>,AssetCodec,std::size_t raw_bytes);
// Cooker publishes a checksummed, portable CBOR index. No native struct ABI is serialized.
void write_asset_manifest(const std::filesystem::path&,std::span<const AssetRecord>,std::string_view profile);
void initialize_asset_package(const std::filesystem::path&);
}
