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
#include "protocol_constants.h"

namespace mysql_wire {

/**
 * Owns the protocol state for one connected MySQL client.
 *
 * Run() performs the handshake, then processes commands sequentially until the
 * client quits, disconnects, or a read/write failure occurs.
 */
class MysqlSession {
public:
  /**
   * Create a session for fd. Run() closes fd before returning.
   *
   * executor is shared with other sessions and may be called concurrently.
   */
  MysqlSession(int fd, std::shared_ptr<SqlExecutor> executor,
               uint32_t connection_id);

  /** Run this connection synchronously until it closes. */
  void Run();

private:
  auto DoHandshake() -> bool;
  /** @return true to continue the command loop; false to close the session */
  auto HandleCommand(const MysqlPacket &packet) -> bool;
  auto SendError(uint8_t sequence_id, const MysqlError &error,
                 const std::string &message) -> bool;

  int fd_;
  std::shared_ptr<SqlExecutor> executor_;
  const uint32_t connection_id_;
  std::string current_database_;
  PacketReader reader_;
  PacketWriter writer_;
};

} // namespace mysql_wire
