// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/**
 * @file handshake_test.cpp
 * @brief Unit tests for MySQL connection-phase handshake packets.
 */

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "handshake.h"
#include "packet.h"
#include "protocol_constants.h"

namespace {

void Require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

auto MakeHandshakeResponse(uint32_t capabilities) -> std::vector<uint8_t> {
  std::vector<uint8_t> payload;
  mysql_wire::AppendInt4(payload, capabilities);
  mysql_wire::AppendInt4(payload, 1U << 24U);
  mysql_wire::AppendInt1(payload, mysql_wire::MYSQL_DEFAULT_CHARSET);
  for (int i = 0; i < 23; i++) {
    mysql_wire::AppendInt1(payload, 0);
  }
  mysql_wire::AppendNullTerminatedString(payload, "test-user");
  mysql_wire::AppendInt1(payload, 0);
  mysql_wire::AppendNullTerminatedString(payload, "testdb");
  mysql_wire::AppendNullTerminatedString(payload,
                                         mysql_wire::MYSQL_AUTH_PLUGIN_NAME);
  return payload;
}

void TestHandshakeV10Payload() {
  constexpr uint32_t connection_id = 42;
  const auto payload = mysql_wire::MakeHandshakeV10Payload(connection_id);

  Require(!payload.empty(), "empty HandshakeV10 payload");
  Require(payload[0] == mysql_wire::MYSQL_PROTOCOL_VERSION,
          "protocol version mismatch");

  const size_t connection_id_offset =
      1 + std::string(mysql_wire::MYSQL_SERVER_VERSION).size() + 1;
  Require(payload.size() >= connection_id_offset + 4,
          "truncated connection id");
  const uint32_t encoded_connection_id =
      static_cast<uint32_t>(payload[connection_id_offset]) |
      (static_cast<uint32_t>(payload[connection_id_offset + 1]) << 8U) |
      (static_cast<uint32_t>(payload[connection_id_offset + 2]) << 16U) |
      (static_cast<uint32_t>(payload[connection_id_offset + 3]) << 24U);
  Require(encoded_connection_id == connection_id, "connection id mismatch");
}

void TestHandshakeResponse41() {
  constexpr uint32_t capabilities =
      mysql_wire::CLIENT_CONNECT_WITH_DB | mysql_wire::CLIENT_PROTOCOL_41 |
      mysql_wire::CLIENT_SECURE_CONNECTION | mysql_wire::CLIENT_PLUGIN_AUTH;
  const auto payload = MakeHandshakeResponse(capabilities);

  mysql_wire::HandshakeResponse41 response;
  std::string error;
  Require(mysql_wire::ParseHandshakeResponse41(payload, response, error),
          "valid HandshakeResponse41 rejected: " + error);
  Require(response.capabilities_ == capabilities, "capabilities mismatch");
  Require(response.max_packet_size_ == 1U << 24U, "max packet size mismatch");
  Require(response.username_ == "test-user", "username mismatch");
  Require(response.database_ == "testdb", "database mismatch");
}

void TestSslRequestIsRejected() {
  constexpr uint32_t capabilities = mysql_wire::CLIENT_PROTOCOL_41 |
                                    mysql_wire::CLIENT_SSL |
                                    mysql_wire::CLIENT_SECURE_CONNECTION;
  const auto payload = MakeHandshakeResponse(capabilities);

  mysql_wire::HandshakeResponse41 response;
  std::string error;
  Require(!mysql_wire::ParseHandshakeResponse41(payload, response, error),
          "SSL request should be rejected");
  Require(error == "SSL is not supported by this frontend",
          "unexpected SSL rejection error");
}

void TestTruncatedHandshakeResponseIsRejected() {
  const std::vector<uint8_t> payload(10, 0);
  mysql_wire::HandshakeResponse41 response;
  std::string error;
  Require(!mysql_wire::ParseHandshakeResponse41(payload, response, error),
          "truncated HandshakeResponse41 should be rejected");
  Require(error == "malformed HandshakeResponse41 fixed fields",
          "unexpected truncated packet error");
}

} // namespace

auto main() -> int {
  try {
    TestHandshakeV10Payload();
    TestHandshakeResponse41();
    TestSslRequestIsRejected();
    TestTruncatedHandshakeResponseIsRejected();
  } catch (const std::exception &error) {
    std::cerr << "handshake_test failed: " << error.what() << '\n';
    return 1;
  }
  std::cout << "handshake_test passed\n";
  return 0;
}
