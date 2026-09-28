#include <stellar/engine/runtime_paths.hpp>
#include <stdexcept>
#include <system_error>
#include <vector>
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
namespace stellar::engine {
std::filesystem::path executable_path() {
#if defined(_WIN32)
    std::vector<wchar_t> path(32768);
    const auto size=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));
    if(size==0) throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),"Cannot locate native executable");
    if(size>=path.size()) throw std::runtime_error("Native executable path exceeds supported Windows length");
    return std::filesystem::path(std::wstring(path.data(),size));
#elif defined(__APPLE__)
    std::uint32_t size=0; _NSGetExecutablePath(nullptr,&size); std::vector<char> path(size);
    if(_NSGetExecutablePath(path.data(),&size)!=0) throw std::runtime_error("Cannot locate native executable");
    return std::filesystem::canonical(path.data());
#elif defined(__linux__)
    return std::filesystem::canonical("/proc/self/exe");
#else
    throw std::runtime_error("Executable location is not implemented for this platform");
#endif
}
std::filesystem::path executable_directory(){return executable_path().parent_path();}
}
