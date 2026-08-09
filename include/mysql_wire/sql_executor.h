// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/**
 * @file sql_executor.h
 * @brief Embedding interface between the protocol frontend and a SQL engine.
 *
 * ExecuteQuery first handles the small compatibility subset required by the
 * mysql CLI (for example DATABASE(), CONNECTION_ID(), and USE). All remaining
 * statements cross the SqlExecutor boundary and are owned by the host engine.
 */

#pragma once

#include <string>
#include <string_view>

#include "mysql_wire/query_result.h"

namespace mysql_wire {

/** SQL execution boundary consumed by the MySQL protocol frontend. */
class SqlExecutor {
public:
  virtual ~SqlExecutor() = default;

  /** Execute SQL and return a result that can be encoded by the frontend. */
  virtual auto Execute(const std::string &sql, const MysqlQueryContext &context)
      -> SqlQueryResult = 0;

  /** @return the single logical database exposed by this executor */
  virtual auto DatabaseName() const -> std::string_view = 0;
};

/**
 * Select the executor's logical database for this connection.
 *
 * @return true if the database exists and was selected
 */
auto SelectDatabase(const SqlExecutor &executor, MysqlQueryContext *context,
                    const std::string &database) -> bool;

/** Handle frontend compatibility queries or delegate SQL to the configured
 * executor. */
auto ExecuteQuery(SqlExecutor &executor, const std::string &sql,
                  MysqlQueryContext *context) -> SqlQueryResult;

} // namespace mysql_wire
