// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/**
 * @file session.cpp
 * @brief Implements the lifecycle of one MySQL client connection.
 *
 * Session state transitions:
 *
 * @code{.text}
 * TCP accepted
 *      |
 *      v
 * Handshake
 *      | failure
 *      +------------------------------> Closed
 *      |
 *      v
 * Command phase <----------------------+
 *      |                               |
 *      +-- QUERY / INIT_DB / PING -----+
 *      +-- unsupported command / ERR --+
 *      |
 *      +-- QUIT / disconnect / write failure --> Closed
 * @endcode
 *
 * The implementation is blocking. Run() completes the handshake first, then
 * reads and handles one command packet at a time.
 * Each command starts a new packet exchange, so its first response packet uses
 * sequence id 1.
 */

#include "session.h"

#include <unistd.h>

#include <cstdint>
#include <exception>
#include <string>
#include <utility>

#include "handshake.h"
#include "log.h"
#include "protocol_constants.h"
#include "query_dispatch.h"
#include "result_encoder.h"

namespace mysql_wire {

namespace {

constexpr uint8_t HANDSHAKE_SEQUENCE_ID = 0;
constexpr uint8_t HANDSHAKE_RESPONSE_SEQUENCE_ID = 1;
constexpr uint8_t HANDSHAKE_RESULT_SEQUENCE_ID = 2;
constexpr uint8_t COMMAND_SEQUENCE_ID = 0;
constexpr uint8_t COMMAND_RESPONSE_SEQUENCE_ID = 1;

auto CommandName(Command command) -> const char * {
  switch (command) {
  case Command::QUIT:
    return "COM_QUIT";
  case Command::INIT_DB:
    return "COM_INIT_DB";
  case Command::QUERY:
    return "COM_QUERY";
  case Command::PING:
    return "COM_PING";
  default:
    return "UNKNOWN";
  }
}

} // namespace

MysqlSession::MysqlSession(int fd, std::shared_ptr<SqlExecutor> executor,
                           uint32_t connection_id)
    : fd_(fd), executor_(std::move(executor)), connection_id_(connection_id),
      reader_(fd), writer_(fd) {}

void MysqlSession::Run() {
  LogInfo("session id=", connection_id_, " started");
  if (!DoHandshake()) {
    LogWarning("session id=", connection_id_, " handshake failed");
    close(fd_);
    return;
  }

  while (true) {
    auto packet = reader_.ReadPacket();
    if (!packet.has_value()) {
      LogInfo("session id=", connection_id_, " client disconnected");
      break;
    }
    if (!HandleCommand(packet.value())) {
      break;
    }
  }

  LogInfo("session id=", connection_id_, " closed");
  close(fd_);
}

auto MysqlSession::DoHandshake() -> bool {
  LogInfo("session id=", connection_id_,
          " handshake send seq=", static_cast<uint32_t>(HANDSHAKE_SEQUENCE_ID));
  if (!writer_.WritePacket(HANDSHAKE_SEQUENCE_ID,
                           MakeHandshakeV10Payload(connection_id_))) {
    return false;
  }

  auto response = reader_.ReadPacket();
  if (!response.has_value()) {
    return false;
  }
  LogInfo("session id=", connection_id_, " handshake response received seq=",
          static_cast<uint32_t>(response->sequence_id_),
          " bytes=", response->payload_.size());

  if (response->sequence_id_ != HANDSHAKE_RESPONSE_SEQUENCE_ID) {
    SendError(HANDSHAKE_RESULT_SEQUENCE_ID, MYSQL_ERROR_HANDSHAKE,
              "unexpected handshake response sequence id");
    return false;
  }

  HandshakeResponse41 handshake_response;
  std::string parse_error;
  if (!ParseHandshakeResponse41(response->payload_, handshake_response,
                                parse_error)) {
    SendError(HANDSHAKE_RESULT_SEQUENCE_ID, MYSQL_ERROR_HANDSHAKE, parse_error);
    return false;
  }

  if (!handshake_response.database_.empty() &&
      !SelectDatabase(*executor_, handshake_response.database_,
                      current_database_)) {
    SendError(HANDSHAKE_RESULT_SEQUENCE_ID, MYSQL_ERROR_BAD_DATABASE,
              "unknown database: " + handshake_response.database_);
    return false;
  }

  const uint32_t client_capabilities =
      handshake_response.capabilities_ & SERVER_CAPABILITIES;

  LogInfo(
      "session id=", connection_id_,
      " handshake accepted user=", handshake_response.username_,
      " database=", (current_database_.empty() ? "<none>" : current_database_),
      " capabilities=0x", std::hex, client_capabilities, std::dec,
      " charset=", static_cast<uint32_t>(handshake_response.character_set_),
      " max_packet=", handshake_response.max_packet_size_);

  // A structurally valid handshake is accepted because this frontend has no
  // account or credential verification.
  LogInfo("session id=", connection_id_, " handshake OK send seq=",
          static_cast<uint32_t>(HANDSHAKE_RESULT_SEQUENCE_ID));

  return writer_.WritePacket(HANDSHAKE_RESULT_SEQUENCE_ID,
                             MakeOkPayload(0, ""));
}

/**
 * Command packet payloads handled by this session:
 *
 * @code{.text}
 * +-------------+--------+----------------------------------+
 * | command     | id     | bytes after the command id       |
 * +-------------+--------+----------------------------------+
 * | COM_QUIT    | 0x01   | empty                            |
 * | COM_INIT_DB | 0x02   | database name                    |
 * | COM_QUERY   | 0x03   | SQL text                         |
 * | COM_PING    | 0x0E   | empty                            |
 * +-------------+--------+----------------------------------+
 * @endcode
 */
auto MysqlSession::HandleCommand(const MysqlPacket &packet) -> bool {
  if (packet.sequence_id_ != COMMAND_SEQUENCE_ID) {
    SendError(COMMAND_RESPONSE_SEQUENCE_ID, MYSQL_ERROR_PACKETS_OUT_OF_ORDER,
              "unexpected command sequence id");
    return false;
  }

  if (packet.payload_.empty()) {
    return SendError(COMMAND_RESPONSE_SEQUENCE_ID, MYSQL_ERROR_UNKNOWN_COMMAND,
                     "empty command packet");
  }

  auto command = static_cast<Command>(packet.payload_[0]);
  LogInfo("session id=", connection_id_,
          " command received type=", CommandName(command),
          " seq=", static_cast<uint32_t>(packet.sequence_id_),
          " bytes=", packet.payload_.size());

  switch (command) {
  case Command::QUIT:
    LogInfo("session id=", connection_id_, " command quit");
    return false;

  case Command::PING:
    LogInfo("session id=", connection_id_,
            " command reply=OK type=", CommandName(command));
    return writer_.WritePacket(COMMAND_RESPONSE_SEQUENCE_ID,
                               MakeOkPayload(0, ""));

  case Command::INIT_DB: {
    const std::string database(packet.payload_.begin() + 1,
                               packet.payload_.end());
    if (!SelectDatabase(*executor_, database, current_database_)) {
      return SendError(COMMAND_RESPONSE_SEQUENCE_ID, MYSQL_ERROR_BAD_DATABASE,
                       "unknown database: " + database);
    }
    LogInfo("session id=", connection_id_,
            " command reply=OK type=", CommandName(command));
    return writer_.WritePacket(COMMAND_RESPONSE_SEQUENCE_ID,
                               MakeOkPayload(0, ""));
  }

  case Command::QUERY: {
    std::string sql(packet.payload_.begin() + 1, packet.payload_.end());
    LogInfo("session id=", connection_id_, " query execute sql=", sql);
    MysqlResultSink sink(&writer_, COMMAND_RESPONSE_SEQUENCE_ID);
    try {
      const bool written = ExecuteQuery(*executor_, sql, connection_id_,
                                        current_database_, sink);
      LogInfo("session id=", connection_id_, " query response finished");
      return written;
    } catch (const std::exception &ex) {
      LogError("session id=", connection_id_,
               " query execution failed error=", ex.what());
      return sink.WriteError(ex.what());
    }
  }

  default:
    LogWarning("session id=", connection_id_, " command unsupported type=0x",
               std::hex, static_cast<uint32_t>(packet.payload_[0]), std::dec);
    return SendError(COMMAND_RESPONSE_SEQUENCE_ID, MYSQL_ERROR_UNKNOWN_COMMAND,
                     "unsupported MySQL command");
  }
}

auto MysqlSession::SendError(uint8_t sequence_id, const MysqlError &error,
                             const std::string &message) -> bool {
  LogWarning("session id=", connection_id_,
             " response send type=ERR seq=", static_cast<uint32_t>(sequence_id),
             " code=", error.code_, " message=", message);
  return writer_.WritePacket(sequence_id, MakeErrPayload(error, message));
}

} // namespace mysql_wire
