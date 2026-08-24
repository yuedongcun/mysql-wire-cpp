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

/**
 * MySQL 8.0.46 Protocol::ColumnDefinition41:
 * https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_com_query_response_text_resultset_column_definition.html
 *
 * ColumnDefinition41 payload emitted by this frontend:
 *
 * @code{.text}
 * +----------------------+--------------+-----------------------+
 * | field                | encoding     | value                 |
 * +----------------------+--------------+-----------------------+
 * | catalog              | lenenc str   | "def"                 |
 * | schema               | lenenc str   | empty                 |
 * | table                | lenenc str   | empty                 |
 * | original table       | lenenc str   | empty                 |
 * | name                 | lenenc str   | column name           |
 * | original name        | lenenc str   | column name           |
 * | fixed fields length  | 1 byte       | 0x0C                  |
 * | character set        | 2 bytes LE   | default character set |
 * | column length        | 4 bytes LE   | 1024                  |
 * | type                 | 1 byte       | column type           |
 * | flags                | 2 bytes LE   | 0 or NOT_NULL_FLAG    |
 * | decimals             | 1 byte       | 0                     |
 * | reserved             | 2 bytes      | 0                     |
 * +----------------------+--------------+-----------------------+
 * @endcode
 */
auto MakeColumnDefinitionPayload(const SqlColumn &column)
    -> std::vector<uint8_t> {
  std::vector<uint8_t> payload;
  // Text resultsets include catalog, schema, table, and column metadata before
  // rows. The executor result model only exposes a column name here, so the
  // remaining fields use stable placeholders accepted by the mysql CLI.
  AppendLenEncodedString(payload, "def");        // catalog
  AppendLenEncodedString(payload, "");           // schema
  AppendLenEncodedString(payload, "");           // table
  AppendLenEncodedString(payload, "");           // org_table
  AppendLenEncodedString(payload, column.name_); // name
  AppendLenEncodedString(payload, column.name_); // org_name
  AppendInt1(payload, 0x0c);                  // length of fixed length fields
  AppendInt2(payload, MYSQL_DEFAULT_CHARSET); // character_set
  AppendInt4(payload, 1024);                  // column_length
  AppendInt1(payload, static_cast<uint8_t>(column.type_)); // type
  AppendInt2(payload, column.nullable_ ? 0 : 1);           // flags
  AppendInt1(payload, 0);                                  // decimals
  AppendInt2(payload, 0);                                  // reserved
  return payload;
}

/**
 * MySQL 8.0.46 ProtocolText::ResultsetRow:
 * https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_com_query_response_text_resultset_row.html
 *
 * Each cell is encoded in column order:
 *
 * @code{.text}
 * +----------------------+---------------------------+
 * | SQL value            | encoding                  |
 * +----------------------+---------------------------+
 * | NULL                 | 1 byte, 0xFB              |
 * | non-NULL             | length-encoded string     |
 * +----------------------+---------------------------+
 * @endcode
 */
auto MakeRowPayload(const std::vector<std::optional<std::string>> &row)
    -> std::vector<uint8_t> {
  std::vector<uint8_t> payload;
  for (const auto &value : row) {
    if (!value.has_value()) {
      AppendInt1(payload, 0xfb); // NULL
      continue;
    }
    AppendLenEncodedString(payload, value.value()); // non-NULL column value
  }
  return payload;
}

} // namespace

/**
 * MySQL 8.0.46 OK_Packet:
 * https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_basic_ok_packet.html
 *
 * @code{.text}
 * +----------------------+---------------------------+
 * | field                | encoding                  |
 * +----------------------+---------------------------+
 * | header               | 1 byte, 0x00              |
 * | affected rows        | length-encoded integer    |
 * | last insert id       | length-encoded integer    |
 * | status flags         | 2 bytes LE                |
 * | warnings             | 2 bytes LE                |
 * | info                 | remaining bytes, optional |
 * +----------------------+---------------------------+
 * @endcode
 */
auto MakeOkPayload(uint64_t affected_rows, const std::string &message)
    -> std::vector<uint8_t> {
  std::vector<uint8_t> payload;
  AppendInt1(payload, 0x00);                       // header
  AppendLenEncodedInteger(payload, affected_rows); // affected_rows
  AppendLenEncodedInteger(payload, 0);             // last_insert_id
  AppendInt2(payload, SERVER_STATUS_AUTOCOMMIT);   // status_flags
  AppendInt2(payload, 0);                          // warnings
  if (!message.empty()) {
    AppendBytes(payload, message); // info
  }
  return payload;
}

/**
 * MySQL 8.0.46 ERR_Packet:
 * https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_basic_err_packet.html
 *
 * @code{.text}
 * +----------------------+---------------------------+
 * | field                | encoding                  |
 * +----------------------+---------------------------+
 * | header               | 1 byte, 0xFF              |
 * | error code           | 2 bytes LE                |
 * | SQL state marker     | 1 byte, '#'               |
 * | SQL state            | 5 bytes                   |
 * | error message        | remaining bytes           |
 * +----------------------+---------------------------+
 * @endcode
 */
auto MakeErrPayload(const MysqlError &error, const std::string &message)
    -> std::vector<uint8_t> {
  std::vector<uint8_t> payload;
  AppendInt1(payload, 0xff);              // header
  AppendInt2(payload, error.code_);       // error_code
  AppendInt1(payload, '#');               // sql_state_marker
  AppendBytes(payload, error.sql_state_); // sql_state
  AppendBytes(payload, message);          // error_message
  return payload;
}

/**
 * MySQL 8.0.46 EOF_Packet:
 * https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_basic_eof_packet.html
 *
 * @code{.text}
 * +----------------------+---------------------------+
 * | field                | encoding                  |
 * +----------------------+---------------------------+
 * | header               | 1 byte, 0xFE              |
 * | warnings             | 2 bytes LE                |
 * | status flags         | 2 bytes LE                |
 * +----------------------+---------------------------+
 * @endcode
 */
auto MakeEofPayload() -> std::vector<uint8_t> {
  std::vector<uint8_t> payload;
  AppendInt1(payload, 0xfe);                     // header
  AppendInt2(payload, 0);                        // warnings
  AppendInt2(payload, SERVER_STATUS_AUTOCOMMIT); // status_flags
  return payload;
}

auto MysqlResultSink::WriteOk(uint64_t affected_rows,
                              const std::string &message) -> bool {
  if (state_ != State::NOT_STARTED) {
    return false;
  }
  if (!WriteNextPacket(MakeOkPayload(affected_rows, message))) {
    return false;
  }
  state_ = State::FINISHED;
  return true;
}

auto MysqlResultSink::WriteError(const std::string &message) -> bool {
  return WriteError(MYSQL_ERROR_UNKNOWN, message);
}

auto MysqlResultSink::WriteError(const MysqlError &error,
                                 const std::string &message) -> bool {
  if (state_ != State::NOT_STARTED && state_ != State::WRITING_ROWS) {
    return false;
  }
  if (!WriteNextPacket(MakeErrPayload(error, message))) {
    return false;
  }
  state_ = State::FINISHED;
  return true;
}

/**
 * MySQL 8.0.46 Text Resultset:
 * https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_com_query_response_text_resultset.html
 *
 * This frontend does not advertise CLIENT_OPTIONAL_RESULTSET_METADATA or
 * CLIENT_DEPRECATE_EOF, so a row response uses this packet sequence:
 *
 * @code{.text}
 * +----------------------+--------------------------------------+
 * | packet order         | payload                              |
 * +----------------------+--------------------------------------+
 * | first                | column count, length-encoded integer |
 * | next, per column     | ColumnDefinition41                   |
 * | next                 | EOF, end of column metadata          |
 * | next, per row        | ProtocolText::ResultsetRow           |
 * | last                 | EOF, or ERR if row production fails  |
 * +----------------------+--------------------------------------+
 * @endcode
 *
 * WriteNextPacket increments the sequence id for every packet in this order.
 * An ERR written while rows are being produced replaces the final EOF and
 * terminates the resultset.
 */
auto MysqlResultSink::BeginRows(const std::vector<SqlColumn> &columns) -> bool {
  if (state_ != State::NOT_STARTED) {
    return false;
  }
  state_ = State::WRITING_METADATA;

  std::vector<uint8_t> column_count;
  AppendLenEncodedInteger(column_count, columns.size()); // column_count
  if (!WriteNextPacket(column_count)) {
    return false;
  }

  for (const auto &column : columns) {
    // Column Definition packet.
    if (!WriteNextPacket(MakeColumnDefinitionPayload(column))) {
      return false;
    }
  }

  // EOF packet marking the end of column metadata.
  if (!WriteNextPacket(MakeEofPayload())) {
    return false;
  }
  state_ = State::WRITING_ROWS;
  return true;
}

auto MysqlResultSink::WriteRow(const SqlRow &row) -> bool {
  if (state_ != State::WRITING_ROWS) {
    return false;
  }
  return WriteNextPacket(MakeRowPayload(row));
}

auto MysqlResultSink::EndRows() -> bool {
  if (state_ != State::WRITING_ROWS) {
    return false;
  }
  // EOF packet marking the end of the resultset.
  if (!WriteNextPacket(MakeEofPayload())) {
    return false;
  }
  state_ = State::FINISHED;
  return true;
}

auto MysqlResultSink::WriteNextPacket(const std::vector<uint8_t> &payload)
    -> bool {
  const uint8_t sequence_id = next_sequence_id_;
  next_sequence_id_ = static_cast<uint8_t>(next_sequence_id_ + 1);
  if (!writer_->WritePacket(sequence_id, payload)) {
    state_ = State::WRITE_FAILED;
    return false;
  }
  return true;
}

} // namespace mysql_wire
