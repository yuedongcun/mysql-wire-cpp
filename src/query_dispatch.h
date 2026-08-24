// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/** @file query_dispatch.h @brief Internal frontend compatibility query routing.
 */

#pragma once

#include <cstdint>
#include <string>

#include "mysql_wire/mysql_wire.h"

namespace mysql_wire {

class MysqlResultSink;

auto SelectDatabase(const SqlExecutor &executor, const std::string &database,
                    std::string &current_database) -> bool;
auto ExecuteQuery(SqlExecutor &executor, const std::string &sql,
                  uint32_t connection_id, std::string &current_database,
                  MysqlResultSink &sink) -> bool;

} // namespace mysql_wire
