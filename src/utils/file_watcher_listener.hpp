#pragma once

#include <efsw/efsw.hpp>
#include <filesystem>
#include <string>
#include <unordered_set>

class SiteBuilder;

class DevServerListener : public efsw::FileWatchListener {
private:
  std::filesystem::path project_root;
  SiteBuilder *builder;
  std::unordered_set<std::string> watched_extensions;

public:
  DevServerListener(const std::filesystem::path &root, SiteBuilder *b);

  void handleFileAction(efsw::WatchID watchid, const std::string &dir,
                        const std::string &filename, efsw::Action action,
                        std::string oldFilename = "") override;
};