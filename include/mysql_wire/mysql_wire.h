// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/**
 * @file mysql_wire.h
 * @brief Public API for connecting a SQL engine to the MySQL Wire frontend.
 *
 * The application implements SqlExecutor and passes it to MysqlServer. For
 * each query, the frontend calls SqlExecutor::Execute and supplies a
 * SqlResultSink for writing the response:
 *
 * @code{.text}
 * mysql client -> MysqlServer -> SqlExecutor -> SqlResultSink -> client
 * @endcode
 */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mysql_wire {

/**
 * MySQL column types supported by the text-resultset encoder.
 *
 * Protocol::ColumnDefinition41 type field:
 * https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_com_query_response_text_resultset_column_definition.html
 *
 * MySQL column type code definitions:
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

/** Column metadata used when encoding a MySQL text resultset. */
struct SqlColumn {
  /** Display name returned to the client. */
  std::string name_;
  /** MySQL column type code. */
  ColumnType type_{ColumnType::VAR_STRING};
  /** Whether the column may contain NULL values. */
  bool nullable_{true};
};

/** One text-protocol cell; std::nullopt represents SQL NULL. */
using SqlCell = std::optional<std::string>;

/** One text-protocol row in the same order as its column metadata. */
using SqlRow = std::vector<SqlCell>;

/**
 * Streams one SQL response to the connected MySQL client.
 *
 * An executor produces exactly one of these response shapes:
 *
 * @code{.text}
 * WriteOk(...)
 * WriteError(...)
 * BeginRows(columns) -> WriteRow(row) ... -> EndRows()
 * @endcode
 *
 * Methods process their arguments synchronously and do not retain references
 * after returning. Once a method returns false, the producer must stop writing
 * because the response cannot continue, usually after a client disconnects.
 */
class SqlResultSink {
public:
  virtual ~SqlResultSink() = default;

  /** Write an OK response for a statement that does not return rows. */
  virtual auto WriteOk(uint64_t affected_rows = 0,
                       const std::string &message = {}) -> bool = 0;

  /** Write an ERR response. */
  virtual auto WriteError(const std::string &message) -> bool = 0;

  /** Start a row response and write its column metadata. */
  virtual auto BeginRows(const std::vector<SqlColumn> &columns) -> bool = 0;

  /** Write one row whose cell count and order match the column metadata. */
  virtual auto WriteRow(const SqlRow &row) -> bool = 0;

  /** Finish the row response started by BeginRows. */
  virtual auto EndRows() -> bool = 0;
};

/** Implemented by the program that provides SQL query execution. */
class SqlExecutor {
public:
  virtual ~SqlExecutor() = default;

  /**
   * Execute SQL and write exactly one response through sink.
   *
   * Calls for different client connections may run concurrently.
   *
   * @return true if the response was written; false if output cannot continue
   */
  virtual auto Execute(const std::string &sql, SqlResultSink &sink) -> bool = 0;

  /** @return the single logical database exposed by this executor */
  virtual auto DatabaseName() const -> std::string = 0;
};

/**
 * TCP server that creates one MySQL protocol session per client.
 *
 * All sessions share the same SqlExecutor and may call it concurrently.
 */
class MysqlServer {
public:
  /**
   * Create a MySQL frontend bound to the given host and port.
   *
   * executor must be non-null and safe for concurrent calls.
   */
  MysqlServer(std::string host, int port,
              std::shared_ptr<SqlExecutor> executor);

  /** Block while accepting clients; return nonzero after a listener error. */
  auto ServeForever() -> int;

private:
  std::string host_;
  int port_;
  std::shared_ptr<SqlExecutor> executor_;
  uint32_t next_connection_id_{1};
};

} // namespace mysql_wire
