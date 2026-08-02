// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mysql_wire/packet.h"
#include "mysql_wire/query_result.h"

namespace mysql_wire {

/** @return a MySQL OK packet payload */
auto MakeOkPayload(uint64_t affected_rows, const std::string &message)
    -> std::vector<uint8_t>;
/** @return a MySQL ERR packet payload */
auto MakeErrPayload(uint16_t error_code, const std::string &message)
    -> std::vector<uint8_t>;
/** @return a MySQL EOF packet payload used by text resultsets */
auto MakeEofPayload() -> std::vector<uint8_t>;
/** Write a complete MySQL response for the given SQL result. ROWS use full
 * metadata and EOF terminators. */
auto WriteQueryResult(PacketWriter *writer, const SqlQueryResult &result,
                      uint8_t *sequence_id) -> bool;

} // namespace mysql_wire
