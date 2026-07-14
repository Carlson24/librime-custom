//
// Copyright RIME Developers
// Distributed under the BSD License
//
#ifndef RIME_PROFILER_H_
#define RIME_PROFILER_H_

#include <rime/build_config.h>

#ifdef RIME_ENABLE_PROFILING

#include <chrono>
#include <functional>
#include <string>

namespace rime {

class ProfileManager {
 public:
  using ComponentCallback = std::function<void(
      const std::string& type,
      const std::string& name,
      int64_t elapsed_ns)>;

  void OnComponentTiming(const std::string& type,
                         const std::string& name,
                         int64_t elapsed_ns);

  void set_callback(ComponentCallback callback);

  static ProfileManager& instance();

 private:
  ProfileManager();
  ComponentCallback callback_;
};

class ScopedComponentProfiler {
 public:
  ScopedComponentProfiler(const std::string& type, const std::string& name);
  ~ScopedComponentProfiler();

 private:
  std::string type_;
  std::string name_;
  std::chrono::steady_clock::time_point start_;
};

}  // namespace rime

#define RIME_PROFILE_SCOPE(type, name) \
  rime::ScopedComponentProfiler _rime_profil0r_(type, name)

#else  // RIME_ENABLE_PROFILING

#define RIME_PROFILE_SCOPE(type, name) ((void)0)

#endif  // RIME_ENABLE_PROFILING

#endif  // RIME_PROFILER_H_
