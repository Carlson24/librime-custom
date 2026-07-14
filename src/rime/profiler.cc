//
// Copyright RIME Developers
// Distributed under the BSD License
//
#include <rime/profiler.h>

#ifdef RIME_ENABLE_PROFILING

#include <cstdio>
#include <cstdlib>

namespace rime {

static FILE* get_profile_output() {
  static FILE* f = nullptr;
  static bool initialized = false;
  if (!initialized) {
    initialized = true;
    const char* path = std::getenv("RIME_PROFILE_LOG");
    if (path && path[0]) {
      f = std::fopen(path, "a");
    }
  }
  return f ? f : stderr;
}

ProfileManager::ProfileManager() : callback_(nullptr) {}

ProfileManager& ProfileManager::instance() {
  static ProfileManager mgr;
  return mgr;
}

void ProfileManager::set_callback(ComponentCallback callback) {
  callback_ = std::move(callback);
}

void ProfileManager::OnComponentTiming(const std::string& type,
                                       const std::string& name,
                                       int64_t elapsed_ns) {
  if (callback_) {
    callback_(type, name, elapsed_ns);
  } else {
    std::fprintf(get_profile_output(),
                 "[PROFILE] %-15s %-30s %8ld ns (%7.3f µs)\n",
                 type.c_str(), name.c_str(), (long)elapsed_ns,
                 elapsed_ns / 1000.0);
  }
}

ScopedComponentProfiler::ScopedComponentProfiler(const std::string& type,
                                                 const std::string& name)
    : type_(type), name_(name), start_(std::chrono::steady_clock::now()) {}

ScopedComponentProfiler::~ScopedComponentProfiler() {
  auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                     std::chrono::steady_clock::now() - start_)
                     .count();
  ProfileManager::instance().OnComponentTiming(type_, name_, elapsed);
}

}  // namespace rime

#endif  // RIME_ENABLE_PROFILING
