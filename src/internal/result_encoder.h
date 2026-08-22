// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/** @file result_encoder.h @brief Internal MySQL response packet encoder. */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "internal/packet.h"
#include "mysql_wire/mysql_wire.h"

namespace mysql_wire {

auto MakeOkPayload(uint64_t affected_rows, const std::string &message)
    -> std::vector<uint8_t>;
auto MakeErrPayload(uint16_t error_code, const std::string &message)
    -> std::vector<uint8_t>;
auto MakeEofPayload() -> std::vector<uint8_t>;
auto WriteQueryResult(PacketWriter *writer, const SqlQueryResult &result,
                      uint8_t *sequence_id) -> bool;

} // namespace mysql_wire
