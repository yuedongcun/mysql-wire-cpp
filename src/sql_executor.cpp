// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/**
 * @file sql_executor.cpp
 * @brief Routes mysql CLI compatibility queries into the result sink.
 *
 * Queries needed by common mysql CLI startup and metadata probes are answered
 * here. Statements outside that deliberately small subset are delegated to
 * the configured SqlExecutor.
 */

#include "mysql_wire/mysql_wire.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "internal/constants.h"
#include "internal/sql_dispatch.h"

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

auto WriteOneStringRow(SqlResultSink &sink, const std::string &column,
                       std::optional<std::string> value) -> bool {
  const std::vector<SqlColumn> columns{
      SqlColumn{column, ColumnType::VAR_STRING, true}};
  const SqlRow row{std::move(value)};
  return sink.BeginRows(columns) && sink.WriteRow(row) && sink.EndRows();
}

auto HandleFrontendQuery(SqlExecutor &executor, const std::string &sql,
                         MysqlQueryContext *context, SqlResultSink &sink)
    -> std::optional<bool> {
  const auto normalized = NormalizeSql(sql);
  if (normalized.empty()) {
    return sink.WriteOk();
  }

  // The mysql CLI sends setup and metadata probes before user queries. Handle
  // the small compatibility subset here instead of routing it to the backend.
  if (HasPrefix(normalized, "set ")) {
    return sink.WriteOk();
  }

  if (HasPrefix(normalized, "use ")) {
    const auto database = normalized.substr(4);
    if (!SelectDatabase(executor, context, database)) {
      return sink.WriteError("unknown database: " + database);
    }
    return sink.WriteOk();
  }

  if (normalized == "select database()" || normalized == "select schema()") {
    if (context->current_database_.empty()) {
      return WriteOneStringRow(sink, "database()", std::nullopt);
    }
    return WriteOneStringRow(sink, "database()", context->current_database_);
  }

  if (HasPrefix(normalized, "select @@version_comment")) {
    return WriteOneStringRow(sink, "@@version_comment", MYSQL_VERSION_COMMENT);
  }

  if (HasPrefix(normalized, "select @@version")) {
    return WriteOneStringRow(sink, "@@version", MYSQL_SERVER_VERSION);
  }

  if (HasPrefix(normalized, "select connection_id()")) {
    return WriteOneStringRow(sink, "connection_id()",
                             std::to_string(context->connection_id_));
  }

  if (normalized == "show databases") {
    const std::vector<SqlColumn> columns{
        SqlColumn{"Database", ColumnType::VAR_STRING, false}};
    const SqlRow row{std::string(executor.DatabaseName())};
    return sink.BeginRows(columns) && sink.WriteRow(row) && sink.EndRows();
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
                  MysqlQueryContext *context, SqlResultSink &sink) -> bool {
  if (auto frontend_result = HandleFrontendQuery(executor, sql, context, sink);
      frontend_result.has_value()) {
    return frontend_result.value();
  }
  return executor.Execute(TrimSql(sql), *context, sink);
}

} // namespace mysql_wire
