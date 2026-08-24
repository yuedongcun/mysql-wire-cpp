// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/** @file result_encoder.h @brief Internal MySQL response packet encoder. */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mysql_wire/mysql_wire.h"
#include "packet.h"

namespace mysql_wire {

auto MakeOkPayload(uint64_t affected_rows, const std::string &message)
    -> std::vector<uint8_t>;
auto MakeErrPayload(uint16_t error_code, const std::string &message)
    -> std::vector<uint8_t>;
auto MakeEofPayload() -> std::vector<uint8_t>;

/** SqlResultSink implementation that writes MySQL packets to one session. */
class MysqlResultSink final : public SqlResultSink {
public:
  MysqlResultSink(PacketWriter *writer, uint8_t first_sequence_id)
      : writer_(writer), next_sequence_id_(first_sequence_id) {}

  auto WriteOk(uint64_t affected_rows, const std::string &message)
      -> bool override;
  auto WriteError(const std::string &message) -> bool override;
  auto BeginRows(const std::vector<SqlColumn> &columns) -> bool override;
  auto WriteRow(const SqlRow &row) -> bool override;
  auto EndRows() -> bool override;

  /** @return whether any response packet has been written */
  auto ResponseStarted() const -> bool { return response_started_; }

private:
  auto WriteNextPacket(const std::vector<uint8_t> &payload) -> bool;

  PacketWriter *writer_;
  uint8_t next_sequence_id_;
  bool response_started_{false};
};

} // namespace mysql_wire
