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
 */

#include "session.h"

#include <unistd.h>

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <utility>

#include "handshake.h"
#include "protocol_constants.h"
#include "query_dispatch.h"
#include "result_encoder.h"

namespace mysql_wire {

namespace {

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
      query_context_{connection_id, ""}, reader_(fd), writer_(fd) {}

void MysqlSession::Run() {
  std::clog << "MySQL session connection_id=" << connection_id_ << " started\n";
  if (!DoHandshake()) {
    std::clog << "MySQL session connection_id=" << connection_id_
              << " handshake failed\n";
    close(fd_);
    return;
  }

  while (true) {
    auto packet = reader_.ReadPacket();
    if (!packet.has_value()) {
      std::clog << "MySQL session connection_id=" << connection_id_
                << " client disconnected\n";
      break;
    }
    if (!HandleCommand(packet.value())) {
      break;
    }
  }

  std::clog << "MySQL session connection_id=" << connection_id_ << " closed\n";
  close(fd_);
}

auto MysqlSession::DoHandshake() -> bool {
  uint8_t sequence_id = 0;
  std::clog << "MySQL session connection_id=" << connection_id_
            << " sending handshake seq=" << static_cast<uint32_t>(sequence_id)
            << '\n';
  if (!writer_.WritePacket(sequence_id++,
                           MakeHandshakeV10Payload(connection_id_))) {
    return false;
  }

  auto response = reader_.ReadPacket();
  if (!response.has_value()) {
    return false;
  }
  std::clog << "MySQL session connection_id=" << connection_id_
            << " received handshake response seq="
            << static_cast<uint32_t>(response->sequence_id_)
            << " payload_len=" << response->payload_.size() << '\n';

  sequence_id = static_cast<uint8_t>(response->sequence_id_ + 1);
  if (response->sequence_id_ != 1) {
    SendError(sequence_id, MYSQL_ERR_HANDSHAKE,
              "unexpected handshake response sequence id");
    return false;
  }

  HandshakeResponse41 handshake_response;
  std::string parse_error;
  if (!ParseHandshakeResponse41(response->payload_, &handshake_response,
                                &parse_error)) {
    SendError(sequence_id, MYSQL_ERR_HANDSHAKE, parse_error);
    return false;
  }

  if (!handshake_response.database_.empty() &&
      !SelectDatabase(*executor_, &query_context_,
                      handshake_response.database_)) {
    SendError(sequence_id, MYSQL_ERR_BAD_DB,
              "unknown database: " + handshake_response.database_);
    return false;
  }

  client_capabilities_ = handshake_response.capabilities_ & SERVER_CAPABILITIES;
  client_max_packet_size_ = handshake_response.max_packet_size_;
  client_character_set_ = handshake_response.character_set_;
  username_ = std::move(handshake_response.username_);
  auth_plugin_name_ = handshake_response.auth_plugin_name_.empty()
                          ? MYSQL_AUTH_PLUGIN_NAME
                          : std::move(handshake_response.auth_plugin_name_);

  std::clog << "MySQL session connection_id=" << connection_id_
            << " user=" << username_ << " database="
            << (query_context_.current_database_.empty()
                    ? "<none>"
                    : query_context_.current_database_)
            << " capabilities=0x" << std::hex << client_capabilities_
            << std::dec
            << " charset=" << static_cast<uint32_t>(client_character_set_)
            << " max_packet=" << client_max_packet_size_ << '\n';

  std::clog << "MySQL session connection_id=" << connection_id_
            << " sending auth OK seq=" << static_cast<uint32_t>(sequence_id)
            << '\n';

  return writer_.WritePacket(sequence_id, MakeOkPayload(0, ""));
}

auto MysqlSession::HandleCommand(const MysqlPacket &packet) -> bool {
  if (packet.payload_.empty()) {
    return SendError(1, MYSQL_ERR_UNKNOWN, "empty command packet");
  }

  auto command = static_cast<Command>(packet.payload_[0]);
  uint8_t sequence_id = 1;
  std::clog << "MySQL session connection_id=" << connection_id_ << " received "
            << CommandName(command)
            << " seq=" << static_cast<uint32_t>(packet.sequence_id_)
            << " payload_len=" << packet.payload_.size() << '\n';

  switch (command) {
  case Command::QUIT:
    std::clog << "MySQL session connection_id=" << connection_id_
              << " received COM_QUIT\n";
    return false;

  case Command::PING:
    std::clog << "MySQL session connection_id=" << connection_id_
              << " replying OK to " << CommandName(command) << '\n';
    return writer_.WritePacket(sequence_id, MakeOkPayload(0, ""));

  case Command::INIT_DB: {
    const std::string database(packet.payload_.begin() + 1,
                               packet.payload_.end());
    if (!SelectDatabase(*executor_, &query_context_, database)) {
      return SendError(sequence_id, MYSQL_ERR_BAD_DB,
                       "unknown database: " + database);
    }
    std::clog << "MySQL session connection_id=" << connection_id_
              << " replying OK to " << CommandName(command) << '\n';
    return writer_.WritePacket(sequence_id, MakeOkPayload(0, ""));
  }

  case Command::QUERY: {
    std::string sql(packet.payload_.begin() + 1, packet.payload_.end());
    std::clog << "MySQL session connection_id=" << connection_id_
              << " executing SQL: " << sql << '\n';
    MysqlResultSink sink(&writer_, &sequence_id);
    try {
      const bool written = ExecuteQuery(*executor_, sql, &query_context_, sink);
      std::clog << "MySQL session connection_id=" << connection_id_
                << " finished SQL response\n";
      return written;
    } catch (const std::exception &ex) {
      std::clog << "MySQL session connection_id=" << connection_id_
                << " SQL execution failed: " << ex.what() << '\n';
      if (sink.ResponseStarted()) {
        return false;
      }
      return SendError(sequence_id, MYSQL_ERR_UNKNOWN, ex.what());
    }
  }

  default:
    std::clog << "MySQL session connection_id=" << connection_id_
              << " unsupported command=0x" << std::hex
              << static_cast<uint32_t>(packet.payload_[0]) << std::dec << '\n';
    return SendError(sequence_id, MYSQL_ERR_UNKNOWN,
                     "unsupported MySQL command");
  }
}

auto MysqlSession::SendError(uint8_t sequence_id, uint16_t error_code,
                             const std::string &message) -> bool {
  std::clog << "MySQL session connection_id=" << connection_id_
            << " sending ERR seq=" << static_cast<uint32_t>(sequence_id)
            << " code=" << error_code << " message=" << message << '\n';
  return writer_.WritePacket(sequence_id, MakeErrPayload(error_code, message));
}

} // namespace mysql_wire
