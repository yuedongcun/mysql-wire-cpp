// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/**
 * @file server.cpp
 * @brief Implements the POSIX TCP listener and per-connection worker launch.
 *
 * ServeForever binds and listens once, then transfers every accepted socket
 * to a detached thread running MysqlSession.
 */

#include "mysql_wire/mysql_wire.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>

#include "internal/session.h"

namespace mysql_wire {

MysqlServer::MysqlServer(std::string host, int port,
                         std::shared_ptr<SqlExecutor> executor)
    : host_(std::move(host)), port_(port), executor_(std::move(executor)) {}

auto MysqlServer::ServeForever() -> int {
  std::clog << "Starting MySQL frontend on " << host_ << ':' << port_ << '\n';

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    std::cerr << "socket failed: " << std::strerror(errno) << std::endl;
    return 1;
  }

  int opt = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0) {
    std::cerr << "setsockopt failed: " << std::strerror(errno) << std::endl;
    close(server_fd);
    return 1;
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<uint16_t>(port_));
  if (inet_pton(AF_INET, host_.c_str(), &address.sin_addr) != 1) {
    std::cerr << "invalid host: " << host_ << std::endl;
    close(server_fd);
    return 1;
  }

  if (bind(server_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) <
      0) {
    std::cerr << "bind failed: " << std::strerror(errno) << std::endl;
    close(server_fd);
    return 1;
  }

  if (listen(server_fd, 16) < 0) {
    std::cerr << "listen failed: " << std::strerror(errno) << std::endl;
    close(server_fd);
    return 1;
  }

  std::cerr << "mysql-wire-cpp frontend listening on " << host_ << ":" << port_
            << std::endl;

  while (true) {
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(
        server_fd, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
    if (client_fd < 0) {
      if (errno == EINTR) {
        continue;
      }
      std::cerr << "accept failed: " << std::strerror(errno) << std::endl;
      close(server_fd);
      return 1;
    }

    auto connection_id = next_connection_id_++;
    char client_host[INET_ADDRSTRLEN] = {};
    auto *client_ip = inet_ntop(AF_INET, &client_addr.sin_addr, client_host,
                                sizeof(client_host));
    auto client_port = ntohs(client_addr.sin_port);
    std::clog << "Accepted MySQL client connection_id=" << connection_id
              << " from " << (client_ip == nullptr ? "<unknown>" : client_ip)
              << ':' << client_port << " fd=" << client_fd << '\n';

    auto executor = executor_;
    std::thread([client_fd, executor = std::move(executor), connection_id] {
      MysqlSession session(client_fd, executor, connection_id);
      session.Run();
    }).detach();
  }
}

} // namespace mysql_wire
