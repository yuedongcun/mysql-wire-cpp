// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/**
 * @file query_result.cpp
 * @brief Implements factories for OK, row-producing, and error SQL results.
 */

#include "mysql_wire/query_result.h"

#include <utility>

namespace mysql_wire {

auto SqlQueryResult::Ok(int64_t affected_rows, std::string message)
    -> SqlQueryResult {
  SqlQueryResult result;
  result.kind_ = SqlResultKind::OK;
  result.affected_rows_ = affected_rows;
  result.message_ = std::move(message);
  return result;
}

auto SqlQueryResult::Rows(
    std::vector<SqlColumn> columns,
    std::vector<std::vector<std::optional<std::string>>> rows)
    -> SqlQueryResult {
  SqlQueryResult result;
  result.kind_ = SqlResultKind::ROWS;
  result.columns_ = std::move(columns);
  result.rows_ = std::move(rows);
  return result;
}

auto SqlQueryResult::Error(std::string message) -> SqlQueryResult {
  SqlQueryResult result;
  result.kind_ = SqlResultKind::ERROR;
  result.message_ = std::move(message);
  return result;
}

} // namespace mysql_wire
