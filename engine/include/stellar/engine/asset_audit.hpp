#pragma once
#include <filesystem>
namespace stellar::engine {
// Inventories every regular file without following directory links. Hashes source
// asset formats with bounded workers, including identical assets in build copies.
void audit_asset_repository(const std::filesystem::path& root,
                            const std::filesystem::path& output, unsigned threads);
}
