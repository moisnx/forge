#ifndef PREVIEW_SERVER_HPP
#define PREVIEW_SERVER_HPP

#include <filesystem>

// Forward declarations
class SiteBuilder;

namespace fs = std::filesystem;

// Start the preview server
void start_preview_server(const fs::path &project_root);

#endif