// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "mysql_wire/sql_executor.h"

namespace mysql_wire {

/**
 * MysqlServer accepts TCP clients and starts one MysqlSession per connection.
 */
class MysqlServer {
public:
  /** Create a MySQL frontend bound to the given host and port. */
  MysqlServer(std::string host, int port,
              std::shared_ptr<SqlExecutor> executor);

  /** Listen for clients forever. */
  auto ServeForever() -> int;

private:
  /** Host address to bind. */
  std::string host_;
  /** TCP port to bind. */
  int port_;
  /** Shared execution backend used by all sessions. */
  std::shared_ptr<SqlExecutor> executor_;
  /** Monotonic id assigned to new client connections. */
  uint32_t next_connection_id_{1};
};

} // namespace mysql_wire
