// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/**
 * @file query_result.h
 * @brief SQL-engine-neutral request context and result data model.
 *
 * This is the seam between an embedded SQL engine and the wire frontend:
 *
 * @code{.text}
 * MysqlSession -> SqlExecutor -> SqlQueryResult -> result encoder -> client
 *                    ^                |
 *                    +-- MysqlQueryContext
 * @endcode
 *
 * Cells are represented as strings because MySQL's text protocol sends each
 * non-NULL value as a length-encoded byte string.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mysql_wire/constants.h"

namespace mysql_wire {

/** Connection-local values needed by MySQL compatibility queries. */
struct MysqlQueryContext {
  /** Connection id advertised during the handshake. */
  uint32_t connection_id_{0};
  /** Currently selected logical database, or empty if none is selected. */
  std::string current_database_;
};

/** Result shape produced by the SQL execution layer for the MySQL frontend. */
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

/**
 * SqlQueryResult is a frontend-neutral result model between SQL execution and
 * MySQL packet encoding.
 */
struct SqlQueryResult {
  /** Whether this result should be encoded as OK, result rows, or ERR. */
  SqlResultKind kind_{SqlResultKind::OK};
  /** Column metadata for ROWS results. */
  std::vector<SqlColumn> columns_;
  /** Cell values for ROWS results. A std::nullopt cell is encoded as SQL NULL.
   */
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

} // namespace mysql_wire
