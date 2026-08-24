// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/** @file protocol_constants.h @brief Internal MySQL wire protocol constants. */

#pragma once

#include <cstdint>
#include <string_view>

namespace mysql_wire {

constexpr uint32_t CLIENT_LONG_PASSWORD = 1U;
constexpr uint32_t CLIENT_LONG_FLAG = 1U << 2U;
constexpr uint32_t CLIENT_CONNECT_WITH_DB = 1U << 3U;
constexpr uint32_t CLIENT_PROTOCOL_41 = 1U << 9U;
constexpr uint32_t CLIENT_SSL = 1U << 11U;
constexpr uint32_t CLIENT_TRANSACTIONS = 1U << 13U;
constexpr uint32_t CLIENT_SECURE_CONNECTION = 1U << 15U;
constexpr uint32_t CLIENT_PLUGIN_AUTH = 1U << 19U;

constexpr uint16_t SERVER_STATUS_AUTOCOMMIT = 1U << 1U;

constexpr uint32_t SERVER_CAPABILITIES =
    CLIENT_LONG_PASSWORD | CLIENT_LONG_FLAG | CLIENT_CONNECT_WITH_DB |
    CLIENT_PROTOCOL_41 | CLIENT_TRANSACTIONS | CLIENT_SECURE_CONNECTION |
    CLIENT_PLUGIN_AUTH;

constexpr uint8_t MYSQL_PROTOCOL_VERSION = 10;
constexpr const char *MYSQL_SERVER_VERSION = "8.0.0-mysql-wire-cpp";
constexpr const char *MYSQL_VERSION_COMMENT =
    "mysql-wire-cpp protocol frontend";
constexpr const char *MYSQL_AUTH_PLUGIN_NAME = "mysql_native_password";
constexpr uint8_t MYSQL_DEFAULT_CHARSET = 45; // utf8mb4_general_ci

/**
 * Error code and SQLSTATE encoded together in an ERR_Packet.
 *
 * MySQL 8.0 server error message reference:
 * https://dev.mysql.com/doc/mysql-errors/8.0/en/server-error-reference.html
 */
struct MysqlError {
  uint16_t code_;
  std::string_view sql_state_;
};

constexpr MysqlError MYSQL_ERROR_HANDSHAKE{1043, "08S01"};
constexpr MysqlError MYSQL_ERROR_BAD_DATABASE{1049, "42000"};
constexpr MysqlError MYSQL_ERROR_UNKNOWN_COMMAND{1047, "08S01"};
constexpr MysqlError MYSQL_ERROR_UNKNOWN{1105, "HY000"};
constexpr MysqlError MYSQL_ERROR_PACKETS_OUT_OF_ORDER{1156, "08S01"};

/**
 * MySQL command ids handled by the session command loop.
 *
 * Command Phase:
 * https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_command_phase.html
 *
 * COM_QUIT:
 * https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_com_quit.html
 *
 * COM_INIT_DB:
 * https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_com_init_db.html
 *
 * COM_QUERY:
 * https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_com_query.html
 *
 * COM_PING:
 * https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_com_ping.html
 */
enum class Command : uint8_t {
  QUIT = 0x01,
  INIT_DB = 0x02,
  QUERY = 0x03,
  PING = 0x0e,
};

} // namespace mysql_wire
