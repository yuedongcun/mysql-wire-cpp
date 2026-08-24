// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/**
 * @file handshake.cpp
 * @brief Encodes and parses MySQL connection-phase handshake packets.
 */

#include "handshake.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "packet.h"
#include "protocol_constants.h"

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

} // namespace

/**
 * MySQL 8.0.46 Protocol::HandshakeV10:
 * https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_connection_phase_packets_protocol_handshake_v10.html
 */
auto MakeHandshakeV10Payload(uint32_t connection_id) -> std::vector<uint8_t> {
  std::vector<uint8_t> payload;
  // The frontend does not authenticate clients, but the protocol still requires
  // auth-plugin data and an advertised plugin name in the handshake packet.
  const std::string auth_plugin_data = "12345678abcdefghijkl";

  AppendInt1(&payload, MYSQL_PROTOCOL_VERSION); // protocol_version
  AppendNullTerminatedString(&payload, MYSQL_SERVER_VERSION); // server_version
  AppendInt4(&payload, connection_id);                        // connection_id
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

} // namespace mysql_wire
