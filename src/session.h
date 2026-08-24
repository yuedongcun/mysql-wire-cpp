// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/**
 * @file session.h
 * @brief Internal connection-local handshake and command-loop state machine.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "mysql_wire/mysql_wire.h"
#include "packet.h"

namespace mysql_wire {

class MysqlSession {
public:
  MysqlSession(int fd, std::shared_ptr<SqlExecutor> executor,
               uint32_t connection_id);

  void Run();

private:
  auto DoHandshake() -> bool;
  auto HandleCommand(const MysqlPacket &packet) -> bool;
  auto SendError(uint8_t sequence_id, uint16_t error_code,
                 const std::string &message) -> bool;

  int fd_;
  std::shared_ptr<SqlExecutor> executor_;
  uint32_t connection_id_;
  uint32_t client_capabilities_{0};
  uint32_t client_max_packet_size_{0};
  uint8_t client_character_set_{0};
  std::string username_;
  std::string auth_plugin_name_;
  MysqlQueryContext query_context_;
  PacketReader reader_;
  PacketWriter writer_;
};

} // namespace mysql_wire
