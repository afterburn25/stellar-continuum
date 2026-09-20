#include "stellar/engine/runtime_directory_lease.hpp"
#include "stellar/engine/sha256.hpp"
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
namespace stellar::engine {
RuntimeDirectoryLease::RuntimeDirectoryLease(const std::filesystem::path& directory) {
#ifdef _WIN32
  auto path=std::filesystem::weakly_canonical(directory).wstring();
  CharLowerBuffW(path.data(),static_cast<DWORD>(path.size()));
  Sha256 digest; digest.update({reinterpret_cast<const std::uint8_t*>(path.data()),path.size()*sizeof(wchar_t)});
  auto hash=digest_hex(digest.finish());
  std::wstring name=L"Global\\StellarContinuum.Runtime."+std::wstring(hash.begin(),hash.end());
  handle_=CreateMutexW(nullptr,FALSE,name.c_str());
  if(!handle_) throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),"Cannot create the game maintenance lock");
  auto wait=WaitForSingleObject(handle_,0);
  if(wait!=WAIT_OBJECT_0 && wait!=WAIT_ABANDONED) {CloseHandle(handle_);handle_=nullptr;throw std::runtime_error("Stellar Continuum or its installer is running. Close it, then retry.");}
#else
  (void)directory;
#endif
}
RuntimeDirectoryLease::~RuntimeDirectoryLease() {
#ifdef _WIN32
  if(handle_) {ReleaseMutex(handle_);CloseHandle(handle_);}
#endif
}
}
