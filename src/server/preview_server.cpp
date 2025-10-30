#include "preview_server.hpp"
#include "server.hpp"
#include "vendor/termcolor.hpp"
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <print>
#include <thread>

static bool ends_with(const std::string &str, const std::string &suffix) {
  if (suffix.length() > str.length())
    return false;
  return str.compare(str.length() - suffix.length(), suffix.length(), suffix) ==
         0;
}

static std::optional<std::string> read_file(const std::string &filename) {
  std::ifstream file(filename);

  if (!file.is_open()) {
    return std::nullopt;
  }

  return std::string((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
}

static std::string get_timestamp() {
  auto now = std::chrono::system_clock::now();
  auto time = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) %
            1000;

  std::stringstream ss;
  ss << std::put_time(std::localtime(&time), "%H:%M:%S");
  ss << "." << std::setfill('0') << std::setw(3) << ms.count();
  return ss.str();
}

static void log_request(const std::string &method, const std::string &path,
                        int status) {

  std::cout << termcolor::bright_blue << "[" << get_timestamp() << "]"
            << termcolor::reset << " ";

  if (method == "GET") {
    std::cout << termcolor::bright_cyan;
  } else if (method == "POST") {
    std::cout << termcolor::bright_yellow;
  } else if (method == "PUT") {
    std::cout << termcolor::bright_magenta;
  } else if (method == "DELETE") {
    std::cout << termcolor::bright_red;
  }
  std::cout << method << termcolor::reset << " ";

  std::cout << termcolor::white << path << termcolor::reset << " ";

  if (status >= 200 && status < 300) {
    std::cout << termcolor::bright_green;
  } else if (status >= 300 && status < 400) {
    std::cout << termcolor::bright_yellow;
  } else if (status >= 400 && status < 500) {
    std::cout << termcolor::bright_red;
  } else if (status >= 500) {
    std::cout << termcolor::red << termcolor::bold;
  }
  std::cout << status << termcolor::reset;

  std::cout << std::endl;
}

static std::string format_size(size_t bytes) {
  const char *units[] = {"B", "KB", "MB", "GB"};
  int unit = 0;
  double size = bytes;

  while (size >= 1024 && unit < 3) {
    size /= 1024;
    unit++;
  }

  std::stringstream ss;
  ss << std::fixed << std::setprecision(1) << size << " " << units[unit];
  return ss.str();
}

void start_preview_server(const fs::path &project_root) {
  Server svr;

  std::filesystem::path dist_path{project_root / "dist"};
  std::filesystem::path static_path{project_root / "dist" / "static"};

  if (!std::filesystem::exists(dist_path)) {
    std::cout << "\n"
              << termcolor::bright_red << "✗ Error: " << termcolor::reset
              << "Build directory not found\n";
    std::cout << termcolor::bright_blue << "  Path: " << termcolor::reset
              << dist_path << "\n";
    std::cout << termcolor::yellow << "  → " << termcolor::reset
              << "Run the build command first\n\n";
    exit(1);
  }

  size_t total_size = 0;
  size_t file_count = 0;
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(dist_path)) {
    if (entry.is_regular_file()) {
      total_size += entry.file_size();
      file_count++;
    }
  }

  svr.set_mount_point("/static", "./dist/static");

  svr.set_logger([](const Request &req, const Response &res) {
    log_request(req.method, req.path, res.status);
  });

  svr.Get(
      ".*",
      [](const Request &req, Response &res) {
        std::string url = req.path;

        if (url == "/") {
          auto html = read_file("./dist/index.html");
          if (html) {
            res.set_content(html->c_str(), "text/html");
          } else {
            res.status = 500;
            res.set_content("<h1>500 - Error loading page</h1>", "text/html");
          }
          return;
        }

        if (url.length() > 1 && url.back() == '/') {
          url.pop_back();
        }

        std::string file_path;
        if (ends_with(url, ".html")) {
          file_path = "./dist" + url;
        } else {
          file_path = "./dist" + url + "/index.html";
        }

        auto html = read_file(file_path);
        if (html) {
          res.set_content(html->c_str(), "text/html");
        } else {

          auto error_page = read_file("./dist/404/index.html");
          res.status = 404;
          if (!error_page) {
            res.set_content("<h1>404 - Page Not Found</h1><p>The page you're "
                            "looking for doesn't exist.</p>",
                            "text/html");
          } else {
            res.set_content(error_page->c_str(), "text/html");
          }
        }
      },
      true);

  std::cout << "\n"
            << termcolor::bright_cyan
            << "╔════════════════════════════════════════╗\n"
            << "║          Preview Server                ║\n"
            << "╚════════════════════════════════════════╝" << termcolor::reset
            << "\n\n";

  std::cout << termcolor::bright_green << "  ✓ " << termcolor::reset
            << "Ready to serve\n";
  std::cout << termcolor::bright_blue << "    " << termcolor::reset
            << "Directory: " << termcolor::white << dist_path
            << termcolor::reset << "\n";
  std::cout << termcolor::bright_blue << "    " << termcolor::reset
            << "Files: " << termcolor::white << file_count << termcolor::reset
            << " (" << format_size(total_size) << ")\n\n";

  std::cout << termcolor::bright_green << "  ✓ " << termcolor::reset
            << "Server started\n";
  std::cout << termcolor::bright_blue << "    " << termcolor::reset
            << "Local:   " << termcolor::bright_cyan << "http://localhost:8080"
            << termcolor::reset << "\n";
  std::cout << termcolor::bright_blue << "    " << termcolor::reset
            << "Network: " << termcolor::bright_cyan << "http://0.0.0.0:8080"
            << termcolor::reset << "\n\n";

  std::cout << termcolor::bright_blue
            << "───────────────────────────────────────────\n"
            << termcolor::reset;

  std::thread server_thread([&svr]() { svr.listen("0.0.0.0", 8080); });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::cout << "\n"
            << termcolor::bright_blue << "Press ENTER to stop server..."
            << termcolor::reset << "\n\n";
  std::cin.get();

  std::cout << termcolor::yellow << "\n⏳ Shutting down..." << termcolor::reset
            << "\n";

  svr.stop();

  if (server_thread.joinable()) {
    server_thread.join();
  }

  std::cout << termcolor::bright_green << "✓ Server stopped cleanly"
            << termcolor::reset << "\n\n";
}