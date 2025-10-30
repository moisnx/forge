#include "core/site_builder.hpp"
#include "server/dev_server.hpp"
#include "server/preview_server.hpp"
#include "utils/build_info.hpp"
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

void print_usage() {
  std::cout << "Forge - A minimal static site generator\n\n";
  std::cout << "Commands:\n";
  std::cout << "  forge dev                 Start development server\n";
  std::cout << "  forge build               Build static site to ./dist\n";
  std::cout
      << "  forge serve               Serve build static files in /dist\n";
  std::cout << "  forge --help              Show this help\n";
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    print_usage();
    return 1;
  }

  std::string command = argv[1];
  fs::path project_root = fs::current_path();

  if (command == "--help" || command == "-h") {
    print_usage();
    return 0;
  }

  try {

    if (command == "dev") {
      SiteBuilder builder(project_root);

      builder.discover_content();
      BuildInfo::getInstance().generate_build_version();
      builder.set_dev_mode(true);
      start_dev_server(builder, project_root);
    } else if (command == "build") {
      SiteBuilder builder(project_root);

      builder.discover_content();
      builder.export_static_site();
    } else if (command == "serve") {
      start_preview_server(project_root);
    } else {
      std::cerr << "Unknown command: " << command << std::endl;
      print_usage();
      return 1;
    }

  } catch (const std::exception &e) {
    std::cerr << "Fatal error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}