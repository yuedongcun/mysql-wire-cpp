// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/**
 * @file constants.h
 * @brief Protocol constants shared by handshake, command, and response code.
 *
 * The numeric values in this file are part of the MySQL wire contract. Keep
 * them independent of any SQL engine so the frontend remains embeddable.
 */

#pragma once

#include <cstdint>

namespace mysql_wire {

/** MySQL client capability flags supported by this frontend. */
constexpr uint32_t CLIENT_LONG_PASSWORD = 1U;
constexpr uint32_t CLIENT_LONG_FLAG = 1U << 2U;
constexpr uint32_t CLIENT_CONNECT_WITH_DB = 1U << 3U;
constexpr uint32_t CLIENT_PROTOCOL_41 = 1U << 9U;
constexpr uint32_t CLIENT_SSL = 1U << 11U;
constexpr uint32_t CLIENT_TRANSACTIONS = 1U << 13U;
constexpr uint32_t CLIENT_SECURE_CONNECTION = 1U << 15U;
constexpr uint32_t CLIENT_PLUGIN_AUTH = 1U << 19U;

/** Server status flag used in OK and EOF packets. */
constexpr uint16_t SERVER_STATUS_AUTOCOMMIT = 1U << 1U;

/** Capability set advertised during the MySQL handshake. */
constexpr uint32_t SERVER_CAPABILITIES =
    CLIENT_LONG_PASSWORD | CLIENT_LONG_FLAG | CLIENT_CONNECT_WITH_DB |
    CLIENT_PROTOCOL_41 | CLIENT_TRANSACTIONS | CLIENT_SECURE_CONNECTION |
    CLIENT_PLUGIN_AUTH;

/** Protocol version number for the MySQL protocol v10 handshake. */
constexpr uint8_t MYSQL_PROTOCOL_VERSION = 10;
/** Server version string advertised to clients for compatibility probes. */
constexpr const char *MYSQL_SERVER_VERSION = "8.0.0-mysql-wire-cpp";
/** Human-readable version comment returned for @@version_comment. */
constexpr const char *MYSQL_VERSION_COMMENT =
    "mysql-wire-cpp protocol frontend";
/** Authentication plugin named in the protocol handshake. */
constexpr const char *MYSQL_AUTH_PLUGIN_NAME = "mysql_native_password";
/** Default collation id advertised to clients. */
constexpr uint8_t MYSQL_DEFAULT_CHARSET = 45; // utf8mb4_general_ci
/** Malformed or unsupported connection handshake. */
constexpr uint16_t MYSQL_ERR_HANDSHAKE = 1043;
/** Client selected a database that this frontend does not expose. */
constexpr uint16_t MYSQL_ERR_BAD_DB = 1049;
/** Generic MySQL error code used by this frontend. */
constexpr uint16_t MYSQL_ERR_UNKNOWN = 1105;

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
  /** COM_QUIT: client asks the server to close the connection. */
  QUIT = 0x01,
  /** COM_INIT_DB: client changes the default schema for the connection. */
  INIT_DB = 0x02,
  /** COM_QUERY: client sends a text-protocol SQL query. */
  QUERY = 0x03,
  /** COM_PING: client checks whether the connection is alive. */
  PING = 0x0e,
};

/**
 * MySQL enum_field_types values used in Protocol::ColumnDefinition41.
 *
 * ColumnDefinition41 type field:
 * https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_com_query_response_text_resultset_column_definition.html
 *
 * enum_field_types source definition:
 * https://dev.mysql.com/doc/dev/mysql-server/8.0.46/field__types_8h_source.html
 */
enum class ColumnType : uint8_t {
  /** MYSQL_TYPE_LONG: 4-byte integer. */
  LONG = 0x03,
  /** MYSQL_TYPE_LONGLONG: 8-byte integer. */
  LONGLONG = 0x08,
  /** MYSQL_TYPE_VAR_STRING: variable-length string. */
  VAR_STRING = 0xfd,
};

} // namespace mysql_wire
