// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

#include "mysql_wire/sql_executor.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "mysql_wire/constants.h"

namespace mysql_wire {

namespace {

auto HasPrefix(const std::string &value, const std::string &prefix) -> bool {
  return value.size() >= prefix.size() &&
         std::equal(prefix.begin(), prefix.end(), value.begin());
}

auto TrimSql(std::string sql) -> std::string {
  const auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
  sql.erase(sql.begin(), std::find_if_not(sql.begin(), sql.end(), is_space));
  sql.erase(std::find_if_not(sql.rbegin(), sql.rend(), is_space).base(),
            sql.end());
  while (!sql.empty() && sql.back() == ';') {
    sql.pop_back();
    sql.erase(std::find_if_not(sql.rbegin(), sql.rend(), is_space).base(),
              sql.end());
  }
  return sql;
}

auto Lower(std::string value) -> std::string {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

auto NormalizeSql(const std::string &sql) -> std::string {
  auto normalized = Lower(TrimSql(sql));
  std::string collapsed;
  bool last_space = false;
  for (char ch : normalized) {
    const bool is_space = std::isspace(static_cast<unsigned char>(ch)) != 0;
    if (is_space) {
      if (!last_space) {
        collapsed.push_back(' ');
      }
    } else {
      collapsed.push_back(ch);
    }
    last_space = is_space;
  }
  return collapsed;
}

auto OneStringRow(std::string column, std::optional<std::string> value)
    -> SqlQueryResult {
  std::vector<SqlColumn> columns{
      SqlColumn{std::move(column), ColumnType::VAR_STRING, true}};
  std::vector<std::vector<std::optional<std::string>>> rows{{std::move(value)}};
  return SqlQueryResult::Rows(std::move(columns), std::move(rows));
}

auto HandleFrontendQuery(SqlExecutor &executor, const std::string &sql,
                         MysqlQueryContext *context)
    -> std::optional<SqlQueryResult> {
  const auto normalized = NormalizeSql(sql);
  if (normalized.empty()) {
    return SqlQueryResult::Ok();
  }
  // The mysql CLI sends setup and metadata probes before user queries. Handle
  // the small compatibility subset here instead of routing it to the backend.
  if (HasPrefix(normalized, "set ")) {
    return SqlQueryResult::Ok();
  }
  if (HasPrefix(normalized, "use ")) {
    const auto database = normalized.substr(4);
    if (!SelectDatabase(executor, context, database)) {
      return SqlQueryResult::Error("unknown database: " + database);
    }
    return SqlQueryResult::Ok();
  }
  if (normalized == "select database()" || normalized == "select schema()") {
    if (context->current_database_.empty()) {
      return OneStringRow("database()", std::nullopt);
    }
    return OneStringRow("database()", context->current_database_);
  }
  if (HasPrefix(normalized, "select @@version_comment")) {
    return OneStringRow("@@version_comment", MYSQL_VERSION_COMMENT);
  }
  if (HasPrefix(normalized, "select @@version")) {
    return OneStringRow("@@version", MYSQL_SERVER_VERSION);
  }
  if (HasPrefix(normalized, "select connection_id()")) {
    return OneStringRow("connection_id()",
                        std::to_string(context->connection_id_));
  }
  if (normalized == "show databases") {
    std::vector<SqlColumn> columns{
        SqlColumn{"Database", ColumnType::VAR_STRING, false}};
    std::vector<std::vector<std::optional<std::string>>> rows{
        {std::string(executor.DatabaseName())}};
    return SqlQueryResult::Rows(std::move(columns), std::move(rows));
  }
  return std::nullopt;
}

} // namespace

auto SelectDatabase(const SqlExecutor &executor, MysqlQueryContext *context,
                    const std::string &database) -> bool {
  auto normalized = Lower(TrimSql(database));
  if (normalized.size() >= 2 && normalized.front() == '`' &&
      normalized.back() == '`') {
    normalized = normalized.substr(1, normalized.size() - 2);
  }
  if (normalized != Lower(std::string(executor.DatabaseName()))) {
    return false;
  }
  context->current_database_ = std::string(executor.DatabaseName());
  return true;
}

auto ExecuteQuery(SqlExecutor &executor, const std::string &sql,
                  MysqlQueryContext *context) -> SqlQueryResult {
  if (auto frontend_result = HandleFrontendQuery(executor, sql, context);
      frontend_result.has_value()) {
    return frontend_result.value();
  }
  return executor.Execute(TrimSql(sql), *context);
}

} // namespace mysql_wire
