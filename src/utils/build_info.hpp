#include <chrono>
#include <cstdint>

class BuildInfo {
private:
  uint64_t BUILD_VERSION_;

  BuildInfo() {}

public:
  BuildInfo(const BuildInfo &) = delete;
  BuildInfo &operator=(const BuildInfo &) = delete;

  static BuildInfo &getInstance() {
    static BuildInfo instance;
    return instance;
  }

  const uint64_t &getVersion() const { return BUILD_VERSION_; }

  void setVersion(const uint64_t &new_version) { BUILD_VERSION_ = new_version; }

  void generate_build_version() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    setVersion(std::chrono::duration_cast<std::chrono::milliseconds>(duration)
                   .count());
  }
};
