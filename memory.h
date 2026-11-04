#pragma once
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

namespace mem {
inline int g_fd = -1;
inline uintptr_t g_base = 0;   // libil2cpp.so load address (file offset 0 mapping)

inline int FindPid(const char* name) {
    DIR* dir = opendir("/proc");
    if (!dir) return 0;
    const size_t n = std::strlen(name) > 15 ? 15 : std::strlen(name);  // /proc comm is 15 chars
    int found = 0;
    while (dirent* e = readdir(dir)) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        std::ifstream f(std::string("/proc/") + e->d_name + "/comm");
        std::string comm;
        std::getline(f, comm);
        if (comm == std::string(name, n)) { found = std::atoi(e->d_name); break; }
    }
    closedir(dir);
    return found;
}

inline bool Attach(int pid) {
    std::ifstream maps("/proc/" + std::to_string(pid) + "/maps");
    std::string line;
    while (std::getline(maps, line)) {
        if (line.find("libil2cpp.so") == std::string::npos) continue;
        if (line.find(" 00000000 ") == std::string::npos) continue;  // first mapping = file offset 0
        g_base = std::stoull(line, nullptr, 16);
        break;
    }
    g_fd = open(("/proc/" + std::to_string(pid) + "/mem").c_str(), O_RDONLY);
    return g_fd >= 0 && g_base != 0;
}

inline bool ReadBytes(uintptr_t addr, void* dst, size_t n) {
    if (g_fd < 0 || !addr) return false;
    return pread(g_fd, dst, n, static_cast<off_t>(addr)) == static_cast<ssize_t>(n);
}

template <typename T>
inline T Read(uintptr_t addr) {
    T v{};
    if (!ReadBytes(addr, &v, sizeof(T))) return T{};
    return v;
}
}  // namespace mem
