// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/**
 * @file query_dispatch.cpp
 * @brief Routes mysql CLI compatibility queries into the result sink.
 *
 * Queries needed by common mysql CLI startup and metadata probes are answered
 * here. Statements outside that deliberately small subset are delegated to
 * the configured SqlExecutor.
 */

#include "query_dispatch.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "protocol_constants.h"
#include "result_encoder.h"

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

/**
 * The frontend has no session-variable system. SET statements are accepted as
 * no-ops for mysql CLI startup compatibility.
 */
auto IsIgnoredSetStatement(const std::string &sql) -> bool {
  return HasPrefix(sql, "set ");
}

auto WriteOneStringRow(SqlResultSink &sink, SqlColumn column, SqlCell value)
    -> bool {
  const std::vector<SqlColumn> columns{std::move(column)};
  const SqlRow row{std::move(value)};
  return sink.BeginRows(columns) && sink.WriteRow(row) && sink.EndRows();
}

auto HandleFrontendQuery(SqlExecutor &executor, const std::string &sql,
                         uint32_t connection_id, std::string &current_database,
                         MysqlResultSink &sink) -> std::optional<bool> {
  const auto normalized = NormalizeSql(sql);
  if (normalized.empty()) {
    return sink.WriteOk();
  }

  // The mysql CLI sends setup and metadata probes before user queries. Handle
  // the small compatibility subset here instead of routing it to the backend.
  if (IsIgnoredSetStatement(normalized)) {
    return sink.WriteOk();
  }

  if (HasPrefix(normalized, "use ")) {
    const auto database = normalized.substr(4);
    if (!SelectDatabase(executor, database, current_database)) {
      return sink.WriteError(MYSQL_ERROR_BAD_DATABASE,
                             "unknown database: " + database);
    }
    return sink.WriteOk();
  }

  if (normalized == "select database()" || normalized == "select schema()") {
    const SqlColumn column{"database()", ColumnType::VAR_STRING, true};
    if (current_database.empty()) {
      return WriteOneStringRow(sink, column, std::nullopt);
    }
    return WriteOneStringRow(sink, column, current_database);
  }

  if (HasPrefix(normalized, "select @@version_comment")) {
    return WriteOneStringRow(
        sink, SqlColumn{"@@version_comment", ColumnType::VAR_STRING, false},
        MYSQL_VERSION_COMMENT);
  }

  if (HasPrefix(normalized, "select @@version")) {
    return WriteOneStringRow(
        sink, SqlColumn{"@@version", ColumnType::VAR_STRING, false},
        MYSQL_SERVER_VERSION);
  }

  if (HasPrefix(normalized, "select connection_id()")) {
    return WriteOneStringRow(
        sink, SqlColumn{"connection_id()", ColumnType::LONGLONG, false},
        std::to_string(connection_id));
  }

  if (normalized == "show databases") {
    return WriteOneStringRow(
        sink, SqlColumn{"Database", ColumnType::VAR_STRING, false},
        executor.DatabaseName());
  }

  return std::nullopt;
}

} // namespace

auto SelectDatabase(const SqlExecutor &executor, const std::string &database,
                    std::string &current_database) -> bool {
  auto normalized = Lower(TrimSql(database));
  if (normalized.size() >= 2 && normalized.front() == '`' &&
      normalized.back() == '`') {
    normalized = normalized.substr(1, normalized.size() - 2);
  }
  auto database_name = executor.DatabaseName();
  if (normalized != Lower(database_name)) {
    return false;
  }
  current_database = std::move(database_name);
  return true;
}

auto ExecuteQuery(SqlExecutor &executor, const std::string &sql,
                  uint32_t connection_id, std::string &current_database,
                  MysqlResultSink &sink) -> bool {
  if (auto frontend_result = HandleFrontendQuery(executor, sql, connection_id,
                                                 current_database, sink);
      frontend_result.has_value()) {
    return frontend_result.value();
  }
  return executor.Execute(TrimSql(sql), sink);
}

} // namespace mysql_wire
