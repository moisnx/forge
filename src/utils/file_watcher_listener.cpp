#include "file_watcher_listener.hpp"
#include "build_info.hpp"
#include "core/site_builder.hpp"
#include "server/websocket_manager.hpp"
#include "vendor/termcolor.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>

namespace fs = std::filesystem;

extern std::atomic<WebSocketManager *> ws_manager;

DevServerListener::DevServerListener(const fs::path &root, SiteBuilder *b)
    : project_root(root), builder(b),
      watched_extensions({".md", ".yaml", ".yml", ".html", ".css", ".js"}) {}

void DevServerListener::handleFileAction(efsw::WatchID watchid,
                                         const std::string &dir,
                                         const std::string &filename,
                                         efsw::Action action,
                                         std::string oldFilename) {
  (void)watchid;
  (void)oldFilename;

  if (filename.empty() || filename[0] == '.' || filename[0] == '~') {
    return;
  }

  fs::path modified = fs::path(dir) / filename;
  std::string ext = modified.extension().string();

  if (watched_extensions.find(ext) == watched_extensions.end()) {
    return;
  }

  if (action != efsw::Actions::Modified && action != efsw::Actions::Add) {
    return;
  }

  auto rebuild_start = std::chrono::high_resolution_clock::now();

  fs::path relative = fs::relative(modified, project_root);
  std::string change_type = "reload";

  auto now = std::chrono::system_clock::now();
  auto time = std::chrono::system_clock::to_time_t(now);
  std::tm tm = *std::localtime(&time);

  std::cout << "\n"
            << termcolor::bright_blue << std::put_time(&tm, "%H:%M:%S")
            << termcolor::reset << " ";

  if (action == efsw::Actions::Add) {
    std::cout << termcolor::bright_green << "➕ Added" << termcolor::reset;
  } else {
    std::cout << termcolor::bright_cyan << "📝 Modified" << termcolor::reset;
  }

  std::cout << " " << termcolor::bright_white << relative.string()
            << termcolor::reset;

  if (ext == ".md") {
    std::cout << termcolor::bright_blue << " [markdown]" << termcolor::reset;
  } else if (ext == ".html") {
    std::cout << termcolor::bright_magenta << " [html]" << termcolor::reset;
  } else if (ext == ".css") {
    std::cout << termcolor::bright_yellow << " [css]" << termcolor::reset;
  } else if (ext == ".js") {
    std::cout << termcolor::bright_green << " [javascript]" << termcolor::reset;
  } else if (ext == ".yaml" || ext == ".yml") {
    std::cout << termcolor::bright_cyan << " [config]" << termcolor::reset;
  }

  std::cout << "\n";

  try {

    if (relative.string().find("templates") == 0) {
      change_type = "template";
      std::cout << termcolor::bright_blue << "  🔄 Reloading templates..."
                << termcolor::reset << "\n";

      fs::path base_path = project_root / "templates" / "base.html";
      if (fs::exists(base_path)) {
        builder->reload_base_template(base_path);
      }
    } else if (ext == ".css") {
      change_type = "css";
      std::cout << termcolor::bright_yellow << "  🎨 CSS update detected"
                << termcolor::reset << "\n";
    } else if (ext == ".js") {
      change_type = "js";
      std::cout << termcolor::bright_green << "  ⚡ JavaScript update detected"
                << termcolor::reset << "\n";
    } else if (ext == ".yaml" || ext == ".yml") {
      change_type = "config";
      std::cout << termcolor::bright_cyan << "  ⚙️  Configuration changed"
                << termcolor::reset << "\n";
    } else if (relative.string().find("content") == 0) {
      change_type = "content";
      std::cout << termcolor::bright_magenta << "  📄 Content updated"
                << termcolor::reset << "\n";
    }

    std::cout << termcolor::bright_cyan << "  🔨 Rebuilding site..."
              << termcolor::reset << "\n";

    builder->discover_content();
    BuildInfo::getInstance().generate_build_version();

    auto rebuild_end = std::chrono::high_resolution_clock::now();
    auto rebuild_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(rebuild_end -
                                                              rebuild_start);

    std::cout << termcolor::bright_green << "  ✓ " << termcolor::reset
              << "Rebuild complete in " << termcolor::bright_white
              << rebuild_duration.count() << "ms" << termcolor::reset
              << termcolor::bright_blue << " (v"
              << BuildInfo::getInstance().getVersion() << ")"
              << termcolor::reset << "\n";

    WebSocketManager *ws = ws_manager.load();
    if (ws) {
      size_t client_count = ws->client_count();
      if (client_count > 0) {
        ws->broadcast_reload(change_type,
                             BuildInfo::getInstance().getVersion());
        std::cout << termcolor::bright_magenta << "  📡 Notified "
                  << termcolor::bright_white << client_count
                  << termcolor::reset;
        if (client_count == 1) {
          std::cout << " client";
        } else {
          std::cout << " clients";
        }
        std::cout << "\n";
      } else {
        std::cout << termcolor::bright_blue << "  ℹ  No clients connected"
                  << termcolor::reset << "\n";
      }
    }

    std::cout << "\n";

  } catch (const std::exception &e) {
    std::cerr << termcolor::bright_red
              << "  ✗ Rebuild failed: " << termcolor::reset
              << termcolor::bright_white << e.what() << termcolor::reset
              << "\n\n";
  }
}