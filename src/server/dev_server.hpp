#ifndef DEV_SERVER_HPP
#define DEV_SERVER_HPP

#include <filesystem>

// Forward declarations
class SiteBuilder;

namespace fs = std::filesystem;

// Start the development server with hot-reloading
void start_dev_server(SiteBuilder &builder, const fs::path &project_root);

#endif // DEV_SERVER_HPP