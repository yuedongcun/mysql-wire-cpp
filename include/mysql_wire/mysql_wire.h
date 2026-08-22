// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/**
 * @file mysql_wire.h
 * @brief Public API for embedding the MySQL Wire frontend into a SQL engine.
 *
 * An embedding application implements SqlExecutor, constructs MysqlServer,
 * and returns SqlQueryResult values from its SQL engine:
 *
 * @code{.text}
 * mysql client -> MysqlServer -> SqlExecutor -> SqlQueryResult -> client
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

/** Result shape produced by the SQL execution layer. */
enum class SqlResultKind { OK, ROWS, ERROR };

/** Column metadata used when encoding a MySQL text resultset. */
struct SqlColumn {
  /** Display name returned to the client. */
  std::string name_;
  /** MySQL column type code. */
  ColumnType type_{ColumnType::VAR_STRING};
  /** Whether the column may contain NULL values. */
  bool nullable_{true};
};

/** SQL-engine-neutral result model consumed by the MySQL frontend. */
struct SqlQueryResult {
  /** Whether this result should be encoded as OK, result rows, or ERR. */
  SqlResultKind kind_{SqlResultKind::OK};
  /** Column metadata for ROWS results. */
  std::vector<SqlColumn> columns_;
  /** ROWS values; std::nullopt is encoded as SQL NULL. */
  std::vector<std::vector<std::optional<std::string>>> rows_;
  /** Number of rows affected for OK results. */
  int64_t affected_rows_{0};
  /** OK or ERR message text. */
  std::string message_;

  /** @return an OK result with optional affected-row count and message */
  static auto Ok(int64_t affected_rows = 0, std::string message = {})
      -> SqlQueryResult;
  /** @return a ROWS result with the given columns and rows */
  static auto Rows(std::vector<SqlColumn> columns,
                   std::vector<std::vector<std::optional<std::string>>> rows)
      -> SqlQueryResult;
  /** @return an ERROR result with the given message */
  static auto Error(std::string message) -> SqlQueryResult;
};

/** SQL execution boundary implemented by the embedding engine. */
class SqlExecutor {
public:
  virtual ~SqlExecutor() = default;

  /** Execute SQL and return a result that can be encoded by the frontend. */
  virtual auto Execute(const std::string &sql, const MysqlQueryContext &context)
      -> SqlQueryResult = 0;

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
