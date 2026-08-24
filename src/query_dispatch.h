// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/** @file query_dispatch.h @brief Internal frontend compatibility query routing.
 */

#pragma once

#include <string>

#include "mysql_wire/mysql_wire.h"

namespace mysql_wire {

auto SelectDatabase(const SqlExecutor &executor, MysqlQueryContext *context,
                    const std::string &database) -> bool;
auto ExecuteQuery(SqlExecutor &executor, const std::string &sql,
                  MysqlQueryContext *context, SqlResultSink &sink) -> bool;

} // namespace mysql_wire
