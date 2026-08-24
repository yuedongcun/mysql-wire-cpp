// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/**
 * @file result_encoder.cpp
 * @brief Encodes logical SQL results as MySQL text-protocol responses.
 *
 * This implementation owns OK, ERR, EOF, ColumnDefinition41, and text-row
 * payload construction. Packet framing and socket I/O remain in packet.cpp.
 */

#include "result_encoder.h"

#include <cstdint>
#include <string>
#include <vector>

#include "protocol_constants.h"

namespace mysql_wire {

namespace {

auto NextSequence(uint8_t *sequence_id) -> uint8_t {
  uint8_t current = *sequence_id;
  *sequence_id = static_cast<uint8_t>(*sequence_id + 1);
  return current;
}

/**
 * MySQL 8.0.46 Protocol::ColumnDefinition41:
 * https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_com_query_response_text_resultset_column_definition.html
 */
auto MakeColumnDefinitionPayload(const SqlColumn &column)
    -> std::vector<uint8_t> {
  std::vector<uint8_t> payload;
  // Text resultsets include catalog, schema, table, and column metadata before
  // rows. The executor result model only exposes a column name here, so the
  // remaining fields use stable placeholders accepted by the mysql CLI.
  AppendLenEncodedString(&payload, "def");        // catalog
  AppendLenEncodedString(&payload, "");           // schema
  AppendLenEncodedString(&payload, "");           // table
  AppendLenEncodedString(&payload, "");           // org_table
  AppendLenEncodedString(&payload, column.name_); // name
  AppendLenEncodedString(&payload, column.name_); // org_name
  AppendInt1(&payload, 0x0c);                  // length of fixed length fields
  AppendInt2(&payload, MYSQL_DEFAULT_CHARSET); // character_set
  AppendInt4(&payload, 1024);                  // column_length
  AppendInt1(&payload, static_cast<uint8_t>(column.type_)); // type
  AppendInt2(&payload, column.nullable_ ? 0 : 1);           // flags
  AppendInt1(&payload, 0);                                  // decimals
  AppendInt2(&payload, 0);                                  // reserved
  return payload;
}

/**
 * MySQL 8.0.46 ProtocolText::ResultsetRow:
 * https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_com_query_response_text_resultset_row.html
 */
auto MakeRowPayload(const std::vector<std::optional<std::string>> &row)
    -> std::vector<uint8_t> {
  std::vector<uint8_t> payload;
  for (const auto &value : row) {
    if (!value.has_value()) {
      AppendInt1(&payload, 0xfb); // NULL
      continue;
    }
    AppendLenEncodedString(&payload, value.value()); // non-NULL column value
  }
  return payload;
}

} // namespace

/**
 * MySQL 8.0.46 OK_Packet:
 * https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_basic_ok_packet.html
 */
auto MakeOkPayload(uint64_t affected_rows, const std::string &message)
    -> std::vector<uint8_t> {
  std::vector<uint8_t> payload;
  AppendInt1(&payload, 0x00);                       // header
  AppendLenEncodedInteger(&payload, affected_rows); // affected_rows
  AppendLenEncodedInteger(&payload, 0);             // last_insert_id
  AppendInt2(&payload, SERVER_STATUS_AUTOCOMMIT);   // status_flags
  AppendInt2(&payload, 0);                          // warnings
  if (!message.empty()) {
    AppendBytes(&payload, message); // info
  }
  return payload;
}

/**
 * MySQL 8.0.46 ERR_Packet:
 * https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_basic_err_packet.html
 */
auto MakeErrPayload(uint16_t error_code, const std::string &message)
    -> std::vector<uint8_t> {
  std::vector<uint8_t> payload;
  AppendInt1(&payload, 0xff);       // header
  AppendInt2(&payload, error_code); // error_code
  AppendInt1(&payload, '#');        // sql_state_marker
  AppendBytes(&payload, "HY000");   // sql_state
  AppendBytes(&payload, message);   // error_message
  return payload;
}

/**
 * MySQL 8.0.46 EOF_Packet:
 * https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_basic_eof_packet.html
 */
auto MakeEofPayload() -> std::vector<uint8_t> {
  std::vector<uint8_t> payload;
  AppendInt1(&payload, 0xfe);                     // header
  AppendInt2(&payload, 0);                        // warnings
  AppendInt2(&payload, SERVER_STATUS_AUTOCOMMIT); // status_flags
  return payload;
}

/**
 * MySQL 8.0.46 Text Resultset:
 * https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_com_query_response_text_resultset.html
 *
 * This frontend does not advertise CLIENT_OPTIONAL_RESULTSET_METADATA or
 * CLIENT_DEPRECATE_EOF, so ROWS results use this packet sequence:
 *
 *   int<lenenc> column_count
 *   column_count x Protocol::ColumnDefinition41
 *   EOF_Packet
 *   0..N x ProtocolText::ResultsetRow
 *   EOF_Packet
 */
auto MysqlResultSink::WriteOk(uint64_t affected_rows,
                              const std::string &message) -> bool {
  const bool written = writer_->WritePacket(
      NextSequence(sequence_id_), MakeOkPayload(affected_rows, message));
  response_started_ = response_started_ || written;
  return written;
}

auto MysqlResultSink::WriteError(const std::string &message) -> bool {
  const bool written = writer_->WritePacket(
      NextSequence(sequence_id_), MakeErrPayload(MYSQL_ERR_UNKNOWN, message));
  response_started_ = response_started_ || written;
  return written;
}

auto MysqlResultSink::BeginRows(const std::vector<SqlColumn> &columns) -> bool {
  std::vector<uint8_t> column_count;
  AppendLenEncodedInteger(&column_count, columns.size()); // column_count
  if (!writer_->WritePacket(NextSequence(sequence_id_), column_count)) {
    return false;
  }
  response_started_ = true;

  for (const auto &column : columns) {
    // Column Definition packet.
    if (!writer_->WritePacket(NextSequence(sequence_id_),
                              MakeColumnDefinitionPayload(column))) {
      return false;
    }
  }

  // EOF packet marking the end of column metadata.
  return writer_->WritePacket(NextSequence(sequence_id_), MakeEofPayload());
}

auto MysqlResultSink::WriteRow(const SqlRow &row) -> bool {
  if (!writer_->WritePacket(NextSequence(sequence_id_), MakeRowPayload(row))) {
    return false;
  }
  response_started_ = true;
  return true;
}

auto MysqlResultSink::EndRows() -> bool {
  // EOF packet marking the end of the resultset.
  const bool written =
      writer_->WritePacket(NextSequence(sequence_id_), MakeEofPayload());
  response_started_ = response_started_ || written;
  return written;
}

} // namespace mysql_wire
