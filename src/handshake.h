// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/** @file handshake.h @brief Internal MySQL connection-phase packet codec. */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mysql_wire {

/** Fields consumed from Protocol::HandshakeResponse41. */
struct HandshakeResponse41 {
  uint32_t capabilities_{0};
  uint32_t max_packet_size_{0};
  uint8_t character_set_{0};
  std::string username_;
  size_t auth_response_length_{0};
  std::string database_;
  std::string auth_plugin_name_;
};

/** Build the server's initial Protocol::HandshakeV10 payload. */
auto MakeHandshakeV10Payload(uint32_t connection_id) -> std::vector<uint8_t>;

/** Parse and validate the client's Protocol::HandshakeResponse41 payload. */
auto ParseHandshakeResponse41(const std::vector<uint8_t> &payload,
                              HandshakeResponse41 *response, std::string *error)
    -> bool;

} // namespace mysql_wire
