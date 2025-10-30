#pragma once

#include "vendor/termcolor.hpp"
#include <atomic>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class WebSocketManager;

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
public:
  explicit WebSocketSession(tcp::socket socket, WebSocketManager *manager)
      : ws_(std::move(socket)), manager_(manager) {}

  void run() {
    ws_.set_option(
        websocket::stream_base::timeout::suggested(beast::role_type::server));
    ws_.set_option(
        websocket::stream_base::decorator([](websocket::response_type &res) {
          res.set(beast::http::field::server, "Forge-DevServer");
        }));

    ws_.async_accept(beast::bind_front_handler(&WebSocketSession::on_accept,
                                               shared_from_this()));
  }

  void send(const std::string &message) {
    net::post(ws_.get_executor(), [self = shared_from_this(), message]() {
      self->queue_.push_back(message);
      if (self->queue_.size() == 1) {
        self->do_write();
      }
    });
  }

  bool is_open() const { return ws_.is_open(); }

  std::string get_remote_address() const {
    try {
      return ws_.next_layer().remote_endpoint().address().to_string();
    } catch (...) {
      return "unknown";
    }
  }

private:
  void on_accept(beast::error_code ec) {
    if (ec) {
      std::cerr << termcolor::bright_red
                << "✗ WebSocket accept error: " << termcolor::reset
                << termcolor::bright_white << ec.message() << termcolor::reset
                << "\n";
      return;
    }

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);

    std::cout << termcolor::bright_blue << std::put_time(&tm, "%H:%M:%S")
              << termcolor::reset << " " << termcolor::bright_green
              << "🔌 WebSocket" << termcolor::reset << " Client connected from "
              << termcolor::bright_white << get_remote_address()
              << termcolor::reset << "\n";

    do_read();
  }

  void do_read() {
    ws_.async_read(buffer_,
                   beast::bind_front_handler(&WebSocketSession::on_read,
                                             shared_from_this()));
  }

  void on_read(beast::error_code ec, std::size_t bytes) {
    if (ec == websocket::error::closed) {
      auto now = std::chrono::system_clock::now();
      auto time = std::chrono::system_clock::to_time_t(now);
      std::tm tm = *std::localtime(&time);

      std::cout << termcolor::bright_blue << std::put_time(&tm, "%H:%M:%S")
                << termcolor::reset << " " << termcolor::bright_blue
                << "🔌 WebSocket" << termcolor::reset
                << " Connection closed by " << termcolor::bright_white
                << get_remote_address() << termcolor::reset << "\n";
      return;
    }

    if (ec) {
      std::cerr << termcolor::bright_red
                << "✗ WebSocket read error: " << termcolor::reset
                << termcolor::bright_white << ec.message() << termcolor::reset
                << "\n";
      return;
    }

    std::string msg = beast::buffers_to_string(buffer_.data());

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);

    std::cout << termcolor::bright_blue << std::put_time(&tm, "%H:%M:%S")
              << termcolor::reset << " " << termcolor::bright_cyan
              << "📩 WebSocket" << termcolor::reset << " Received "
              << termcolor::bright_white << bytes << "B" << termcolor::reset
              << " from " << termcolor::bright_white << get_remote_address()
              << termcolor::reset << "\n";

    buffer_.clear();
    do_read();
  }

  void do_write() {
    ws_.async_write(net::buffer(queue_.front()),
                    beast::bind_front_handler(&WebSocketSession::on_write,
                                              shared_from_this()));
  }

  void on_write(beast::error_code ec, std::size_t bytes) {
    if (ec) {
      std::cerr << termcolor::bright_red
                << "✗ WebSocket write error: " << termcolor::reset
                << termcolor::bright_white << ec.message() << termcolor::reset
                << "\n";
      return;
    }

    queue_.erase(queue_.begin());
    if (!queue_.empty()) {
      do_write();
    }
  }

  websocket::stream<tcp::socket> ws_;
  beast::flat_buffer buffer_;
  std::vector<std::string> queue_;
  WebSocketManager *manager_;
};

class WebSocketManager {
public:
  WebSocketManager() = default;

  bool start(int port = 8081) {
    try {
      running_ = true;

      ws_thread_ = std::thread([this, port]() {
        try {
          ioc_ = std::make_unique<net::io_context>();
          tcp::acceptor acceptor{*ioc_, tcp::endpoint(tcp::v4(), port)};
          acceptor.set_option(net::socket_base::reuse_address(true));

          std::cout << termcolor::bright_green << "✓ " << termcolor::reset
                    << "WebSocket server listening on "
                    << termcolor::bright_white << "ws://localhost:" << port
                    << termcolor::reset << "\n";

          do_accept(acceptor);

          ioc_->run();

        } catch (const std::exception &e) {
          if (running_.load()) {
            std::cerr << termcolor::bright_red
                      << "✗ WebSocket server error: " << termcolor::reset
                      << termcolor::bright_white << e.what() << termcolor::reset
                      << "\n";
          }
        }
      });

      return true;
    } catch (const std::exception &e) {
      std::cerr << termcolor::bright_red
                << "✗ Failed to start WebSocket server: " << termcolor::reset
                << termcolor::bright_white << e.what() << termcolor::reset
                << "\n";
      return false;
    }
  }

  void add_session(std::shared_ptr<WebSocketSession> session) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.insert(session);

    std::cout << termcolor::bright_green << "✓ " << termcolor::reset
              << "WebSocket client connected " << termcolor::bright_blue
              << "(total: " << sessions_.size() << ")" << termcolor::reset
              << "\n";
  }

  void remove_session(std::shared_ptr<WebSocketSession> session) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(session);

    std::cout << termcolor::bright_blue << "→ " << termcolor::reset
              << "WebSocket client disconnected " << termcolor::bright_blue
              << "(total: " << sessions_.size() << ")" << termcolor::reset
              << "\n";
  }

  void broadcast_reload(const std::string &change_type, uint64_t version) {
    if (shutting_down_.load() || !running_.load()) {
      return;
    }

    std::string message =
        std::format("{{\"type\":\"{}\",\"version\":{}}}", change_type, version);

    std::vector<std::shared_ptr<WebSocketSession>> sessions_copy;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      sessions_copy.reserve(sessions_.size());
      for (const auto &session : sessions_) {
        sessions_copy.push_back(session);
      }
    }

    size_t sent_count = 0;
    size_t failed_count = 0;

    for (const auto &session : sessions_copy) {
      if (session->is_open()) {
        try {
          session->send(message);
          sent_count++;
        } catch (const std::exception &e) {
          failed_count++;
          std::cerr << termcolor::bright_red
                    << "✗ Failed to send: " << termcolor::reset
                    << termcolor::bright_white << e.what() << termcolor::reset
                    << "\n";
        }
      }
    }

    if (sent_count > 0) {
      auto now = std::chrono::system_clock::now();
      auto time = std::chrono::system_clock::to_time_t(now);
      std::tm tm = *std::localtime(&time);

      std::cout << termcolor::bright_blue << std::put_time(&tm, "%H:%M:%S")
                << termcolor::reset << " " << termcolor::bright_magenta
                << "📡 Broadcast" << termcolor::reset << " "
                << termcolor::bright_cyan << change_type << termcolor::reset
                << " to " << termcolor::bright_white << sent_count
                << termcolor::reset;

      if (sent_count == 1) {
        std::cout << " client";
      } else {
        std::cout << " clients";
      }

      if (failed_count > 0) {
        std::cout << termcolor::bright_red << " (" << failed_count << " failed)"
                  << termcolor::reset;
      }

      std::cout << "\n";
    }
  }

  void stop() {
    if (!running_.exchange(false)) {
      return;
    }

    std::cout << termcolor::bright_yellow << "⏳ Stopping WebSocket server..."
              << termcolor::reset << "\n";

    shutting_down_ = true;

    if (ioc_) {
      ioc_->stop();
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!sessions_.empty()) {
        std::cout << termcolor::bright_blue << "→ " << termcolor::reset
                  << "Closing " << termcolor::bright_white << sessions_.size()
                  << termcolor::reset << " active WebSocket connections\n";
      }
      sessions_.clear();
    }

    if (ws_thread_.joinable()) {
      ws_thread_.join();
    }

    shutting_down_ = false;

    std::cout << termcolor::bright_green << "✓ " << termcolor::reset
              << "WebSocket server stopped\n";
  }

  size_t client_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.size();
  }

  ~WebSocketManager() { stop(); }

private:
  void do_accept(tcp::acceptor &acceptor) {
    acceptor.async_accept(
        [this, &acceptor](beast::error_code ec, tcp::socket socket) {
          if (!ec) {
            auto session =
                std::make_shared<WebSocketSession>(std::move(socket), this);
            add_session(session);
            session->run();
          } else if (ec != net::error::operation_aborted) {
            std::cerr << termcolor::bright_red
                      << "✗ WebSocket accept error: " << termcolor::reset
                      << termcolor::bright_white << ec.message()
                      << termcolor::reset << "\n";
          }

          if (running_.load()) {
            do_accept(acceptor);
          }
        });
  }

  std::set<std::shared_ptr<WebSocketSession>> sessions_;
  mutable std::mutex mutex_;
  std::thread ws_thread_;
  std::unique_ptr<net::io_context> ioc_;
  std::atomic<bool> running_{false};
  std::atomic<bool> shutting_down_{false};
};