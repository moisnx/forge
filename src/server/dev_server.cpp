#include "dev_server.hpp"
#include "core/site_builder.hpp"
#include "livereload.js.h"
#include "server.hpp"
#include "utils/build_info.hpp"
#include "utils/file_watcher_listener.hpp"
#include "vendor/termcolor.hpp"
#include "websocket_manager.hpp"
#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <string>
#include <thread>

namespace fs = std::filesystem;

std::atomic<WebSocketManager *> ws_manager{nullptr};

std::string read_file(const std::string &filename) {
  std::ifstream file(filename);

  if (!file.is_open()) {
    std::cerr << termcolor::bright_red << "✗ Error: " << termcolor::reset
              << "Could not open file " << termcolor::bright_white << filename
              << termcolor::reset << "\n";
    return "";
  }

  return std::string((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
}

void start_dev_server(SiteBuilder &builder, const fs::path &project_root) {
  auto total_start = std::chrono::high_resolution_clock::now();

  std::cout << "\n"
            << termcolor::bright_cyan
            << "╔═══════════════════════════════════════════╗\n"
            << "║        🚀 Starting Dev Server             ║\n"
            << "╚═══════════════════════════════════════════╝"
            << termcolor::reset << "\n\n";

  Server svr;
  WebSocketManager ws;
  ws_manager.store(&ws);

  std::cout << termcolor::bright_cyan << "🔌 Starting WebSocket server..."
            << termcolor::reset << "\n";

  if (!ws.start(8081)) {
    std::cerr << termcolor::bright_red << "✗ Failed to start WebSocket server"
              << termcolor::reset << "\n";
    return;
  }

  std::cout << termcolor::bright_green << "✓ " << termcolor::reset
            << "WebSocket server running on " << termcolor::bright_white
            << "ws://localhost:8081" << termcolor::reset << "\n";

  svr.set_mount_point("/static", "./static");
  std::cout << termcolor::bright_green << "✓ " << termcolor::reset
            << "Static files mounted at " << termcolor::bright_white
            << "/static" << termcolor::reset << "\n";

  svr.Get("/version", [](const Request &, Response &res) {
    res.set_content(std::format("{{\"version\": {}}}",
                                BuildInfo::getInstance().getVersion()),
                    "application/json");
  });

  svr.Get("/livereload.js", [](const Request &, Response &res) {
    std::string script(
        reinterpret_cast<const char *>(assets_livereload_script_min_js),
        assets_livereload_script_min_js_len);

    script = std::regex_replace(
        script, std::regex(R"(\{\{\s*livereload_ws_url\s*\}\})"),
        "ws://localhost:8081");
    res.set_content(script, "text/javascript");
  });

  svr.Get(
      ".*",
      [&builder](const Request &req, Response &res) {
        std::string url = req.path;
        const auto &pages = builder.get_pages();
        auto it = pages.find(url);

        if (it != pages.end()) {
          try {
            std::string html = builder.render_page(it->second);
            res.set_content(html, "text/html");
          } catch (const std::exception &e) {
            res.status = 500;
            res.set_content(std::format("Error rendering page: {}", e.what()),
                            "text/plain");
          }
        } else {
          auto errorPage = pages.find("/404");
          if (errorPage == pages.end()) {
            res.status = 404;
            res.set_content(
                std::format("<h1>404 - Page Not Found</h1><p>URL: {}</p>", url),
                "text/html");
          } else {
            res.status = 404;
            std::string html = builder.render_page(errorPage->second);
            res.set_content(html, "text/html");
          }
        }
      },
      true);

  svr.set_logger([](const Request &req, const Response &res) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);

    std::cout << termcolor::bright_blue << std::put_time(&tm, "%H:%M:%S")
              << termcolor::reset << " " << termcolor::bright_cyan << req.method
              << termcolor::reset << " " << termcolor::white << std::setw(30)
              << std::left << req.path << termcolor::reset << " ";

    if (res.status >= 200 && res.status < 300) {
      std::cout << termcolor::bright_green;
    } else if (res.status >= 300 && res.status < 400) {
      std::cout << termcolor::bright_blue;
    } else if (res.status >= 400 && res.status < 500) {
      std::cout << termcolor::bright_yellow;
    } else {
      std::cout << termcolor::bright_red;
    }

    std::cout << res.status << termcolor::reset << " " << termcolor::bright_blue
              << res.body.size() << "B" << termcolor::reset << "\n";
  });

  std::cout << "\n"
            << termcolor::bright_cyan << "👁️  Setting up file watchers"
            << termcolor::reset << "\n";

  efsw::FileWatcher fileWatcher;
  DevServerListener listener(project_root, &builder);

  std::vector<std::string> folders = {"content", "templates", "static"};
  int watch_count = 0;

  for (const auto &folder : folders) {
    fs::path folder_path = project_root / folder;
    if (fs::exists(folder_path)) {
      fileWatcher.addWatch(folder_path.string(), &listener, true);
      std::cout << termcolor::bright_green << "  ✓ " << termcolor::reset
                << "Watching " << termcolor::bright_white << folder
                << termcolor::reset << "\n";
      watch_count++;
    } else {
      std::cout << termcolor::bright_yellow << "  ⚠ " << termcolor::reset
                << "Skipping " << termcolor::bright_blue << folder
                << termcolor::reset << " (not found)\n";
    }
  }

  fileWatcher.watch();

  auto total_end = std::chrono::high_resolution_clock::now();
  auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      total_end - total_start);

  std::cout << "\n"
            << termcolor::bright_cyan << "📄 Available routes"
            << termcolor::reset << "\n";

  for (const auto &[url, page] : builder.get_pages()) {
    std::cout << termcolor::bright_blue << "  → " << termcolor::reset
              << termcolor::cyan << std::setw(30) << std::left << url
              << termcolor::reset << termcolor::bright_blue << "("
              << page.content_type << ")" << termcolor::reset << "\n";
  }

  std::cout << "\n"
            << termcolor::bright_green
            << "╔═══════════════════════════════════════════╗\n"
            << "║           ✨ Server Ready!                ║\n"
            << "╠═══════════════════════════════════════════╣"
            << termcolor::reset << "\n";
  std::cout << termcolor::bright_green << "║  " << termcolor::reset
            << "HTTP:       " << termcolor::bright_white << std::setw(28)
            << std::left << "http://localhost:8080" << termcolor::reset
            << termcolor::bright_green << "║" << termcolor::reset << "\n";
  std::cout << termcolor::bright_green << "║  " << termcolor::reset
            << "WebSocket:  " << termcolor::bright_white << std::setw(28)
            << std::left << "ws://localhost:8081" << termcolor::reset
            << termcolor::bright_green << "║" << termcolor::reset << "\n";
  std::cout << termcolor::bright_green << "║  " << termcolor::reset
            << "Pages:      " << termcolor::bright_white << std::setw(28)
            << std::left << std::to_string(builder.get_pages().size())
            << termcolor::reset << termcolor::bright_green << "║"
            << termcolor::reset << "\n";
  std::cout << termcolor::bright_green << "║  " << termcolor::reset
            << "Watching:   " << termcolor::bright_white << std::setw(28)
            << std::left << (std::to_string(watch_count) + " folders")
            << termcolor::reset << termcolor::bright_green << "║"
            << termcolor::reset << "\n";
  std::cout << termcolor::bright_green << "║  " << termcolor::reset
            << "Started in: " << termcolor::bright_white << std::setw(28)
            << std::left << (std::to_string(total_duration.count()) + "ms")
            << termcolor::reset << termcolor::bright_green << "║"
            << termcolor::reset << "\n";
  std::cout << termcolor::bright_green
            << "╚═══════════════════════════════════════════╝"
            << termcolor::reset << "\n\n";

  std::cout << termcolor::bright_blue << "Press ENTER to stop server..."
            << termcolor::reset << "\n\n";

  std::thread server_thread([&svr]() {
    if (!svr.listen("0.0.0.0", 8080)) {
      std::cerr << termcolor::bright_red << "✗ Failed to start HTTP server"
                << termcolor::reset << "\n";
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::cin.get();

  std::cout << "\n"
            << termcolor::bright_yellow << "⏳ Shutting down servers..."
            << termcolor::reset << "\n";

  ws_manager = nullptr;
  ws.stop();
  svr.stop();

  if (server_thread.joinable()) {
    server_thread.join();
  }

  std::cout << termcolor::bright_green << "✓ " << termcolor::reset
            << "Dev server stopped cleanly\n\n";
}