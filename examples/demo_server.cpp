// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

#include <csignal>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mysql_wire/query_result.h"
#include "mysql_wire/server.h"
#include "mysql_wire/sql_executor.h"

namespace {

class DemoExecutor : public mysql_wire::SqlExecutor {
public:
  auto Execute(const std::string &sql, const mysql_wire::MysqlQueryContext &)
      -> mysql_wire::SqlQueryResult override {
    if (sql == "SELECT 1" || sql == "select 1") {
      std::vector<mysql_wire::SqlColumn> columns{
          {"1", mysql_wire::ColumnType::LONGLONG, false}};
      std::vector<std::vector<std::optional<std::string>>> rows{{"1"}};
      return mysql_wire::SqlQueryResult::Rows(std::move(columns),
                                              std::move(rows));
    }
    if (sql == "SELECT 'hello'" || sql == "select 'hello'") {
      std::vector<mysql_wire::SqlColumn> columns{
          {"hello", mysql_wire::ColumnType::VAR_STRING, false}};
      std::vector<std::vector<std::optional<std::string>>> rows{{"hello"}};
      return mysql_wire::SqlQueryResult::Rows(std::move(columns),
                                              std::move(rows));
    }
    return mysql_wire::SqlQueryResult::Error(
        "demo executor supports only SELECT 1 and SELECT 'hello'");
  }

  auto DatabaseName() const -> std::string_view override { return "demo"; }
};

auto ParsePort(const char *value) -> int {
  const int port = std::stoi(value);
  if (port <= 0 || port > UINT16_MAX) {
    throw std::out_of_range("port must be between 1 and 65535");
  }
  return port;
}

} // namespace

auto main(int argc, char **argv) -> int {
  std::string host = "127.0.0.1";
  int port = 3307;
  for (int i = 1; i < argc; i++) {
    const std::string argument = argv[i];
    if (argument == "--host" && i + 1 < argc) {
      host = argv[++i];
      continue;
    }
    if (argument == "--port" && i + 1 < argc) {
      port = ParsePort(argv[++i]);
      continue;
    }
    if (argument == "--help") {
      std::cout << "usage: mysql-wire-demo [--host ADDRESS] [--port PORT]\n";
      return 0;
    }
    std::cerr << "unknown or incomplete argument: " << argument << '\n';
    return 1;
  }

  std::signal(SIGPIPE, SIG_IGN);
  auto executor = std::make_shared<DemoExecutor>();
  mysql_wire::MysqlServer server(std::move(host), port, std::move(executor));
  return server.ServeForever();
}
