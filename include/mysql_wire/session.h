// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/**
 * @file session.h
 * @brief Connection-local MySQL handshake and command-loop state machine.
 *
 * A session owns an accepted socket for its entire lifetime:
 *
 * @code{.text}
 * server                     client
 *   |---- HandshakeV10 -------->|
 *   |<--- HandshakeResponse41 --|
 *   |---- OK / ERR ------------>|
 *   |                           |
 *   |<--- COM_* ----------------|  command phase (repeats)
 *   |---- OK / ERR / resultset >|
 *   |                           |
 *   +-- close on EOF, error, or COM_QUIT
 * @endcode
 *
 * Capability negotiation and the selected database are stored per session;
 * SQL execution itself is delegated through the shared SqlExecutor.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "mysql_wire/packet.h"
#include "mysql_wire/query_result.h"
#include "mysql_wire/sql_executor.h"

namespace mysql_wire {

/**
 * MysqlSession owns one client connection and handles the MySQL handshake and
 * command loop for that connection.
 */
class MysqlSession {
public:
  /** Create a session around an accepted socket. */
  MysqlSession(int fd, std::shared_ptr<SqlExecutor> executor,
               uint32_t connection_id);

  /** Run the session until the client disconnects or sends COM_QUIT. */
  void Run();

private:
  /** Perform the initial MySQL protocol handshake. */
  auto DoHandshake() -> bool;
  /** @return the protocol v10 handshake packet payload */
  auto MakeHandshakePayload() -> std::vector<uint8_t>;
  /** Dispatch one MySQL command packet. */
  auto HandleCommand(const MysqlPacket &packet) -> bool;
  /** Send a MySQL ERR packet. */
  auto SendError(uint8_t sequence_id, uint16_t error_code,
                 const std::string &message) -> bool;

  /** Accepted client socket. */
  int fd_;
  /** Execution backend used for SQL commands. */
  std::shared_ptr<SqlExecutor> executor_;
  /** Connection id exposed in the MySQL handshake. */
  uint32_t connection_id_;
  /** Capabilities negotiated between the client and this frontend. */
  uint32_t client_capabilities_{0};
  /** Maximum packet size reported by the client. */
  uint32_t client_max_packet_size_{0};
  /** Character set reported by the client. */
  uint8_t client_character_set_{0};
  /** User name reported by the client. Authentication is not enforced. */
  std::string username_;
  /** Authentication plugin selected by the client. */
  std::string auth_plugin_name_;
  /** Connection-local values exposed to compatibility queries. */
  MysqlQueryContext query_context_;
  /** Packet reader bound to fd_. */
  PacketReader reader_;
  /** Packet writer bound to fd_. */
  PacketWriter writer_;
};

} // namespace mysql_wire
