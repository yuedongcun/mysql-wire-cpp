// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/**
 * @file session.cpp
 * @brief Implements handshake parsing, session state, and COM_* dispatch.
 *
 * The parser validates HandshakeResponse41 according to negotiated capability
 * flags. Once authenticated, HandleCommand routes control commands locally
 * and COM_QUERY through ExecuteQuery and the result encoder.
 */

#include "internal/session.h"

#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "internal/constants.h"
#include "internal/result_encoder.h"
#include "internal/sql_dispatch.h"

namespace mysql_wire {

namespace {

class PayloadCursor {
public:
  explicit PayloadCursor(const std::vector<uint8_t> &payload)
      : payload_(payload) {}

  auto ReadInt1(uint8_t *value) -> bool {
    uint64_t decoded = 0;
    if (!ReadLittleEndian(1, &decoded)) {
      return false;
    }
    *value = static_cast<uint8_t>(decoded);
    return true;
  }

  auto ReadInt4(uint32_t *value) -> bool {
    uint64_t decoded = 0;
    if (!ReadLittleEndian(4, &decoded)) {
      return false;
    }
    *value = static_cast<uint32_t>(decoded);
    return true;
  }

  auto ReadNullTerminatedString(std::string *value) -> bool {
    const auto begin = offset_;
    while (offset_ < payload_.size() && payload_[offset_] != 0) {
      offset_++;
    }
    if (offset_ == payload_.size()) {
      return false;
    }
    value->assign(payload_.begin() + static_cast<ptrdiff_t>(begin),
                  payload_.begin() + static_cast<ptrdiff_t>(offset_));
    offset_++;
    return true;
  }

  auto Skip(size_t length) -> bool {
    if (length > payload_.size() - offset_) {
      return false;
    }
    offset_ += length;
    return true;
  }

private:
  auto ReadLittleEndian(size_t length, uint64_t *value) -> bool {
    if (length > payload_.size() - offset_) {
      return false;
    }
    uint64_t decoded = 0;
    for (size_t i = 0; i < length; i++) {
      decoded |= static_cast<uint64_t>(payload_[offset_ + i]) << (i * 8U);
    }
    offset_ += length;
    *value = decoded;
    return true;
  }

  const std::vector<uint8_t> &payload_;
  size_t offset_{0};
};

struct HandshakeResponse41 {
  uint32_t capabilities_{0};
  uint32_t max_packet_size_{0};
  uint8_t character_set_{0};
  std::string username_;
  size_t auth_response_length_{0};
  std::string database_;
  std::string auth_plugin_name_;
};

/**
 * MySQL 8.0.46 Protocol::HandshakeResponse41:
 * https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_connection_phase_packets_protocol_handshake_response.html
 */
auto ParseHandshakeResponse41(const std::vector<uint8_t> &payload,
                              HandshakeResponse41 *response, std::string *error)
    -> bool {
  PayloadCursor cursor(payload);
  if (!cursor.ReadInt4(&response->capabilities_) ||
      !cursor.ReadInt4(&response->max_packet_size_) ||
      !cursor.ReadInt1(&response->character_set_) || !cursor.Skip(23)) {
    *error = "malformed HandshakeResponse41 fixed fields";
    return false;
  }
  if ((response->capabilities_ & CLIENT_PROTOCOL_41) == 0) {
    *error = "client does not support CLIENT_PROTOCOL_41";
    return false;
  }
  if ((response->capabilities_ & CLIENT_SSL) != 0) {
    *error = "SSL is not supported by this frontend";
    return false;
  }
  const auto negotiated_capabilities =
      response->capabilities_ & SERVER_CAPABILITIES;
  if (!cursor.ReadNullTerminatedString(&response->username_)) {
    *error = "malformed username in HandshakeResponse41";
    return false;
  }

  if ((negotiated_capabilities & CLIENT_SECURE_CONNECTION) != 0) {
    uint8_t auth_response_length = 0;
    if (!cursor.ReadInt1(&auth_response_length) ||
        !cursor.Skip(auth_response_length)) {
      *error = "malformed secure authentication response";
      return false;
    }
    response->auth_response_length_ = auth_response_length;
  } else {
    std::string auth_response;
    if (!cursor.ReadNullTerminatedString(&auth_response)) {
      *error = "malformed null-terminated authentication response";
      return false;
    }
    response->auth_response_length_ = auth_response.size();
  }

  if ((negotiated_capabilities & CLIENT_CONNECT_WITH_DB) != 0 &&
      !cursor.ReadNullTerminatedString(&response->database_)) {
    *error = "malformed database in HandshakeResponse41";
    return false;
  }
  if ((negotiated_capabilities & CLIENT_PLUGIN_AUTH) != 0 &&
      !cursor.ReadNullTerminatedString(&response->auth_plugin_name_)) {
    *error = "malformed authentication plugin in HandshakeResponse41";
    return false;
  }
  return true;
}

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
  if (!writer_.WritePacket(sequence_id++, MakeHandshakePayload())) {
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

/**
 * MySQL 8.0.46 Protocol::HandshakeV10:
 * https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_connection_phase_packets_protocol_handshake_v10.html
 */
auto MysqlSession::MakeHandshakePayload() -> std::vector<uint8_t> {
  std::vector<uint8_t> payload;
  // The frontend does not authenticate clients, but the protocol still requires
  // auth-plugin data and an advertised plugin name in the handshake packet.
  const std::string auth_plugin_data = "12345678abcdefghijkl";

  AppendInt1(&payload, MYSQL_PROTOCOL_VERSION); // protocol_version
  AppendNullTerminatedString(&payload, MYSQL_SERVER_VERSION); // server_version
  AppendInt4(&payload, connection_id_);                       // connection_id
  AppendBytes(&payload,
              auth_plugin_data.substr(0, 8)); // auth_plugin_data_part_1
  AppendInt1(&payload, 0);                    // filler
  AppendInt2(&payload, static_cast<uint16_t>(SERVER_CAPABILITIES &
                                             0xffffU)); // capability_flags_1
  AppendInt1(&payload, MYSQL_DEFAULT_CHARSET);          // character_set
  AppendInt2(&payload, SERVER_STATUS_AUTOCOMMIT);       // status_flags
  AppendInt2(&payload, static_cast<uint16_t>((SERVER_CAPABILITIES >> 16U) &
                                             0xffffU)); // capability_flags_2
  AppendInt1(&payload, static_cast<uint8_t>(auth_plugin_data.size() +
                                            1)); // auth_plugin_data_len
  for (int i = 0; i < 10; i++) {
    AppendInt1(&payload, 0); // reserved
  }
  AppendBytes(&payload, auth_plugin_data.substr(8)); // auth_plugin_data_part_2
  // mysql_native_password consumes a 20-byte challenge as a NUL-terminated
  // message. The trailing zero is framing, not part of the random challenge.
  AppendInt1(&payload, 0);
  AppendNullTerminatedString(&payload,
                             MYSQL_AUTH_PLUGIN_NAME); // auth_plugin_name
  return payload;
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
