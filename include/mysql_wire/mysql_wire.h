// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/**
 * @file mysql_wire.h
 * @brief Public API for embedding the MySQL Wire frontend into a SQL engine.
 *
 * An embedding application implements SqlExecutor, constructs MysqlServer,
 * and writes query results to the supplied SqlResultSink:
 *
 * @code{.text}
 * mysql client -> MysqlServer -> SqlExecutor -> SqlResultSink -> client
 * @endcode
 *
 * Packet framing, handshake processing, session state, and response encoding
 * are implementation details and are intentionally absent from this header.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mysql_wire {

/** MySQL column types supported by the text-resultset encoder. */
enum class ColumnType : uint8_t {
  /** MYSQL_TYPE_LONG: 4-byte integer. */
  LONG = 0x03,
  /** MYSQL_TYPE_LONGLONG: 8-byte integer. */
  LONGLONG = 0x08,
  /** MYSQL_TYPE_VAR_STRING: variable-length string. */
  VAR_STRING = 0xfd,
};

/** Connection-local values supplied to SQL execution. */
struct MysqlQueryContext {
  /** Connection id advertised during the handshake. */
  uint32_t connection_id_{0};
  /** Currently selected logical database, or empty if none is selected. */
  std::string current_database_;
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
 * Receives one SQL response and writes it to the connected MySQL client.
 *
 * An executor produces exactly one of these response shapes:
 *
 * @code{.text}
 * WriteOk(...)
 * WriteError(...)
 * BeginRows(columns) -> WriteRow(row) ... -> EndRows()
 * @endcode
 *
 * Methods consume their arguments before returning. A false return value means
 * the response could not be written, usually because the client disconnected.
 */
class SqlResultSink {
public:
  virtual ~SqlResultSink() = default;

  /** Write an OK response for a statement that does not return rows. */
  virtual auto WriteOk(uint64_t affected_rows = 0,
                       const std::string &message = {}) -> bool = 0;

  /** Write an ERR response. */
  virtual auto WriteError(const std::string &message) -> bool = 0;

  /** Start a text resultset and write its column metadata. */
  virtual auto BeginRows(const std::vector<SqlColumn> &columns) -> bool = 0;

  /** Write one text resultset row. */
  virtual auto WriteRow(const SqlRow &row) -> bool = 0;

  /** Finish the current text resultset. */
  virtual auto EndRows() -> bool = 0;
};

/** SQL execution boundary implemented by the embedding engine. */
class SqlExecutor {
public:
  virtual ~SqlExecutor() = default;

  /** Execute SQL and write exactly one response through sink. */
  virtual auto Execute(const std::string &sql, const MysqlQueryContext &context,
                       SqlResultSink &sink) -> bool = 0;

  /** @return the single logical database exposed by this executor */
  virtual auto DatabaseName() const -> std::string_view = 0;
};

/** TCP server that creates one MySQL protocol session per client. */
class MysqlServer {
public:
  /** Create a MySQL frontend bound to the given host and port. */
  MysqlServer(std::string host, int port,
              std::shared_ptr<SqlExecutor> executor);

  /** Listen for clients forever. */
  auto ServeForever() -> int;

private:
  std::string host_;
  int port_;
  std::shared_ptr<SqlExecutor> executor_;
  uint32_t next_connection_id_{1};
};

} // namespace mysql_wire
