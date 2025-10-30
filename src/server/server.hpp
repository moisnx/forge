#pragma once

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

struct Request {
  std::string method;
  std::string path;
  std::string version;
  std::unordered_map<std::string, std::string> headers;
  std::string body;
};

struct Response {
  int status = 200;
  std::unordered_map<std::string, std::string> headers;
  std::string body;

  void set_content(const std::string &content, const std::string &type) {
    body = content;
    headers["Content-Type"] = type;
  }

  std::string to_http() const {
    std::ostringstream oss;
    std::string status_text = get_status_text(status);

    oss << "HTTP/1.1 " << status << " " << status_text << "\r\n";
    oss << "Content-Length: " << body.size() << "\r\n";

    for (const auto &[key, value] : headers) {
      oss << key << ": " << value << "\r\n";
    }

    oss << "\r\n" << body;
    return oss.str();
  }

private:
  std::string get_status_text(int code) const {
    switch (code) {
    case 200:
      return "OK";
    case 201:
      return "Created";
    case 204:
      return "No Content";
    case 301:
      return "Moved Permanently";
    case 302:
      return "Found";
    case 304:
      return "Not Modified";
    case 400:
      return "Bad Request";
    case 401:
      return "Unauthorized";
    case 403:
      return "Forbidden";
    case 404:
      return "Not Found";
    case 500:
      return "Internal Server Error";
    case 502:
      return "Bad Gateway";
    case 503:
      return "Service Unavailable";
    default:
      return "Unknown";
    }
  }
};

using Handler = std::function<void(const Request &, Response &)>;
using Logger = std::function<void(const Request &, const Response &)>;

class Server {
private:
  int server_fd = -1;
  bool running = false;
  std::string mount_point_url;
  std::string mount_point_path;
  std::vector<std::pair<std::string, Handler>> routes;
  Handler default_handler;
  Logger logger;
  bool verbose_logging = false;

  static Request parse_request(const std::string &raw) {
    Request req;
    std::istringstream iss(raw);
    std::string line;

    if (std::getline(iss, line)) {
      line.pop_back();
      std::istringstream line_stream(line);
      line_stream >> req.method >> req.path >> req.version;
    }

    while (std::getline(iss, line) && line != "\r") {
      line.pop_back();
      size_t colon = line.find(':');
      if (colon != std::string::npos) {
        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 2);
        req.headers[key] = value;
      }
    }

    std::string body_content;
    while (std::getline(iss, line)) {
      body_content += line + "\n";
    }
    req.body = body_content;

    return req;
  }

  bool ends_with(const std::string &str, const std::string &suffix) {
    if (suffix.size() > str.size())
      return false;
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
  }

  std::string get_mime_type(const std::string &path) {
    if (ends_with(path, ".html"))
      return "text/html";
    if (ends_with(path, ".css"))
      return "text/css";
    if (ends_with(path, ".js"))
      return "application/javascript";
    if (ends_with(path, ".json"))
      return "application/json";
    if (ends_with(path, ".png"))
      return "image/png";
    if (ends_with(path, ".jpg") || ends_with(path, ".jpeg"))
      return "image/jpeg";
    if (ends_with(path, ".gif"))
      return "image/gif";
    if (ends_with(path, ".webp"))
      return "image/webp";
    if (ends_with(path, ".svg"))
      return "image/svg+xml";
    if (ends_with(path, ".ico"))
      return "image/x-icon";
    if (ends_with(path, ".woff"))
      return "font/woff";
    if (ends_with(path, ".woff2"))
      return "font/woff2";
    if (ends_with(path, ".ttf"))
      return "font/ttf";
    if (ends_with(path, ".pdf"))
      return "application/pdf";
    if (ends_with(path, ".xml"))
      return "application/xml";
    if (ends_with(path, ".txt"))
      return "text/plain";
    return "application/octet-stream";
  }

  bool starts_with(const std::string &str, const std::string &prefix) {
    if (prefix.size() > str.size())
      return false;
    return str.compare(0, prefix.size(), prefix) == 0;
  }

  bool serve_static_file(const std::string &url_path, Response &res) {
    if (!mount_point_url.empty() && starts_with(url_path, mount_point_url)) {

      std::string clean_path = url_path;
      size_t query_pos = clean_path.find('?');
      if (query_pos != std::string::npos) {
        clean_path = clean_path.substr(0, query_pos);
      }

      std::string relative = clean_path.substr(mount_point_url.length());

      if (!relative.empty() && relative[0] == '/') {
        relative = relative.substr(1);
      }

      std::filesystem::path file_path =
          std::filesystem::path(mount_point_path) / relative;

      if (verbose_logging) {
        std::cout << "[STATIC] " << url_path << " → " << file_path.string()
                  << std::endl;
      }

      if (std::filesystem::exists(file_path) &&
          std::filesystem::is_regular_file(file_path)) {
        std::ifstream file(file_path, std::ios::binary);
        if (file) {
          std::string content((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
          res.set_content(content, get_mime_type(file_path.string()));

          if (verbose_logging) {
            std::cout << "[STATIC] ✓ " << content.size() << " bytes"
                      << std::endl;
          }
          return true;
        }
      }

      if (verbose_logging) {
        std::cout << "[STATIC] ✗ Not found" << std::endl;
      }
    }
    return false;
  }

  void handle_client(int client_fd) {
    char buffer[8192];
    ssize_t bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

    if (bytes <= 0) {
      close(client_fd);
      return;
    }

    buffer[bytes] = '\0';
    Request req = parse_request(std::string(buffer));
    Response res;

    if (serve_static_file(req.path, res)) {
      if (logger) {
        logger(req, res);
      }
      std::string response = res.to_http();
      send(client_fd, response.c_str(), response.size(), 0);
      close(client_fd);
      return;
    }

    bool handled = false;
    for (const auto &[pattern, handler] : routes) {
      if (pattern == req.path) {
        handler(req, res);
        handled = true;
        break;
      }
    }

    if (!handled && default_handler) {
      default_handler(req, res);
    }

    if (logger) {
      logger(req, res);
    }

    std::string response = res.to_http();
    send(client_fd, response.c_str(), response.size(), 0);
    close(client_fd);
  }

public:
  ~Server() { stop(); }

  void set_mount_point(const std::string &url, const std::string &path) {
    mount_point_url = url;
    mount_point_path = path;
  }

  void set_logger(Logger log_handler) { logger = log_handler; }

  void set_verbose(bool verbose) { verbose_logging = verbose; }

  void Get(const std::string &pattern, Handler handler) {
    routes.push_back({pattern, handler});
  }

  void Get(const std::string &pattern, Handler handler, bool is_catch_all) {
    if (is_catch_all) {
      default_handler = handler;
    } else {
      routes.push_back({pattern, handler});
    }
  }

  bool listen(const std::string &host, int port) {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
      std::cerr << "Failed to create socket: " << strerror(errno) << "\n";
      return false;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) <
        0) {
      std::cerr << "Failed to set SO_REUSEADDR: " << strerror(errno) << "\n";
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) <
        0) {
      std::cerr << "Failed to set SO_REUSEPORT: " << strerror(errno) << "\n";
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (sockaddr *)&addr, sizeof(addr)) < 0) {
      std::cerr << "Bind failed on port " << port << ": " << strerror(errno)
                << "\n";
      std::cerr << "Port may already be in use. Try: lsof -i :" << port << "\n";
      close(server_fd);
      return false;
    }

    if (::listen(server_fd, 10) < 0) {
      std::cerr << "Listen failed: " << strerror(errno) << "\n";
      close(server_fd);
      return false;
    }

    running = true;

    while (running) {
      sockaddr_in client_addr{};
      socklen_t client_len = sizeof(client_addr);
      int client_fd = accept(server_fd, (sockaddr *)&client_addr, &client_len);

      if (client_fd < 0) {
        if (running) {
          std::cerr << "Accept failed\n";
        }
        continue;
      }

      handle_client(client_fd);
    }

    return true;
  }

  void stop() {
    running = false;
    if (server_fd != -1) {
      close(server_fd);
      server_fd = -1;
    }
  }
};