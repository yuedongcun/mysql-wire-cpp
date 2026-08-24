// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/**
 * @file session_test.cpp
 * @brief End-to-end socket tests for handshake, commands, and resultsets.
 *
 * @code{.text}
 * test client <-> socketpair <-> MysqlSession thread <-> FakeSqlExecutor
 * @endcode
 *
 * The harness speaks packets directly, keeping the protocol tests independent
 * of an external mysql binary and real TCP networking.
 */

#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "mysql_wire/mysql_wire.h"
#include "packet.h"
#include "protocol_constants.h"
#include "session.h"

namespace {

void Require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class FakeSqlExecutor : public mysql_wire::SqlExecutor {
public:
  auto Execute(const std::string &sql, mysql_wire::SqlResultSink &sink)
      -> bool override {
    last_sql_ = sql;
    const std::vector<mysql_wire::SqlColumn> columns{
        {"value", mysql_wire::ColumnType::VAR_STRING, false}};
    const mysql_wire::SqlRow first_row{"first-result"};
    const mysql_wire::SqlRow second_row{"second-result"};
    return sink.BeginRows(columns) && sink.WriteRow(first_row) &&
           sink.WriteRow(second_row) && sink.EndRows();
  }

  auto DatabaseName() const -> std::string override { return "testdb"; }

  std::string last_sql_;
};

class FailingRowExecutor : public mysql_wire::SqlExecutor {
public:
  auto Execute(const std::string &, mysql_wire::SqlResultSink &sink)
      -> bool override {
    const std::vector<mysql_wire::SqlColumn> columns{
        {"value", mysql_wire::ColumnType::VAR_STRING, false}};
    if (!sink.BeginRows(columns) ||
        !sink.WriteRow(mysql_wire::SqlRow{"first-result"})) {
      return false;
    }
    throw std::runtime_error("row production failed");
  }

  auto DatabaseName() const -> std::string override { return "testdb"; }
};

class SessionHarness {
public:
  explicit SessionHarness(std::shared_ptr<mysql_wire::SqlExecutor> executor) {
    Require(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets_) == 0,
            "socketpair failed");
    session_thread_ = std::thread(
        [server_fd = sockets_[1], executor = std::move(executor)]() mutable {
          mysql_wire::MysqlSession session(server_fd, std::move(executor), 42);
          session.Run();
        });
  }

  ~SessionHarness() {
    if (sockets_[0] >= 0) {
      shutdown(sockets_[0], SHUT_RDWR);
      close(sockets_[0]);
    }
    if (session_thread_.joinable()) {
      session_thread_.join();
    }
  }

  SessionHarness(const SessionHarness &) = delete;
  auto operator=(const SessionHarness &) -> SessionHarness & = delete;

  auto ClientFd() const -> int { return sockets_[0]; }

  void Join() {
    session_thread_.join();
    close(sockets_[0]);
    sockets_[0] = -1;
  }

private:
  int sockets_[2]{-1, -1};
  std::thread session_thread_;
};

auto MakeHandshakeResponse() -> std::vector<uint8_t> {
  constexpr uint32_t capabilities =
      mysql_wire::CLIENT_CONNECT_WITH_DB | mysql_wire::CLIENT_PROTOCOL_41 |
      mysql_wire::CLIENT_SECURE_CONNECTION | mysql_wire::CLIENT_PLUGIN_AUTH;
  std::vector<uint8_t> payload;
  mysql_wire::AppendInt4(payload, capabilities);
  mysql_wire::AppendInt4(payload, 1U << 24U);
  mysql_wire::AppendInt1(payload, mysql_wire::MYSQL_DEFAULT_CHARSET);
  for (int i = 0; i < 23; i++) {
    mysql_wire::AppendInt1(payload, 0);
  }
  mysql_wire::AppendNullTerminatedString(payload, "test-user");
  mysql_wire::AppendInt1(payload, 0);
  mysql_wire::AppendNullTerminatedString(payload, "testdb");
  mysql_wire::AppendNullTerminatedString(payload,
                                         mysql_wire::MYSQL_AUTH_PLUGIN_NAME);
  return payload;
}

void CompleteHandshake(mysql_wire::PacketReader *reader,
                       mysql_wire::PacketWriter *writer) {
  auto handshake = reader->ReadPacket();
  Require(handshake.has_value() && !handshake->payload_.empty(),
          "missing handshake");
  Require(handshake->sequence_id_ == 0, "handshake sequence mismatch");
  Require(handshake->payload_[0] == mysql_wire::MYSQL_PROTOCOL_VERSION,
          "protocol version mismatch");
  const size_t auth_plugin_data_len_offset =
      1 + std::string(mysql_wire::MYSQL_SERVER_VERSION).size() + 1 + 4 + 8 + 1 +
      2 + 1 + 2 + 2;
  Require(handshake->payload_.size() > auth_plugin_data_len_offset,
          "truncated handshake payload");
  Require(handshake->payload_[auth_plugin_data_len_offset] == 21,
          "auth plugin data length mismatch");
  const size_t auth_plugin_data_terminator_offset =
      auth_plugin_data_len_offset + 1 + 10 + 12;
  Require(handshake->payload_.size() > auth_plugin_data_terminator_offset,
          "truncated auth plugin data");
  Require(handshake->payload_[auth_plugin_data_terminator_offset] == 0,
          "auth plugin data is not terminated");

  Require(writer->WritePacket(1, MakeHandshakeResponse()),
          "handshake response write failed");
  auto auth_result = reader->ReadPacket();
  Require(auth_result.has_value() && !auth_result->payload_.empty(),
          "missing auth result");
  Require(auth_result->sequence_id_ == 2 && auth_result->payload_[0] == 0x00,
          "authentication failed");
}

auto MakeCommand(mysql_wire::Command command, const std::string &body = {})
    -> std::vector<uint8_t> {
  std::vector<uint8_t> payload{static_cast<uint8_t>(command)};
  mysql_wire::AppendBytes(payload, body);
  return payload;
}

void RequireError(const mysql_wire::MysqlPacket &packet, uint8_t sequence_id,
                  const mysql_wire::MysqlError &error) {
  Require(packet.sequence_id_ == sequence_id, "ERR sequence mismatch");
  Require(packet.payload_.size() >= 9 && packet.payload_[0] == 0xff,
          "malformed ERR packet");
  const uint16_t error_code = static_cast<uint16_t>(packet.payload_[1]) |
                              static_cast<uint16_t>(packet.payload_[2] << 8U);
  Require(error_code == error.code_, "ERR code mismatch");
  Require(packet.payload_[3] == '#', "ERR SQLSTATE marker mismatch");
  const std::string sql_state(packet.payload_.begin() + 4,
                              packet.payload_.begin() + 9);
  Require(sql_state == error.sql_state_, "ERR SQLSTATE mismatch");
}

auto ReadSingleColumnRows(mysql_wire::PacketReader *reader, size_t row_count)
    -> std::vector<std::string> {
  auto column_count = reader->ReadPacket();
  auto column_definition = reader->ReadPacket();
  auto metadata_eof = reader->ReadPacket();
  Require(column_count.has_value() && column_count->sequence_id_ == 1 &&
              column_count->payload_ == std::vector<uint8_t>({1}),
          "column count mismatch");
  Require(column_definition.has_value() && column_definition->sequence_id_ == 2,
          "column definition mismatch");
  Require(metadata_eof.has_value() && metadata_eof->sequence_id_ == 3 &&
              metadata_eof->payload_[0] == 0xfe,
          "missing metadata EOF");

  std::vector<std::string> values;
  for (size_t i = 0; i < row_count; i++) {
    auto row = reader->ReadPacket();
    Require(row.has_value() && !row->payload_.empty(), "missing result row");
    Require(row->sequence_id_ == static_cast<uint8_t>(4 + i),
            "result row sequence mismatch");
    Require(row->payload_[0] == row->payload_.size() - 1,
            "unexpected row encoding");
    values.emplace_back(row->payload_.begin() + 1, row->payload_.end());
  }

  auto resultset_eof = reader->ReadPacket();
  Require(resultset_eof.has_value() &&
              resultset_eof->sequence_id_ ==
                  static_cast<uint8_t>(4 + row_count) &&
              resultset_eof->payload_[0] == 0xfe,
          "missing resultset EOF");
  return values;
}

void TestSessionUsesInjectedExecutor() {
  auto executor = std::make_shared<FakeSqlExecutor>();
  SessionHarness harness(executor);
  mysql_wire::PacketReader reader(harness.ClientFd());
  mysql_wire::PacketWriter writer(harness.ClientFd());

  CompleteHandshake(&reader, &writer);

  std::vector<uint8_t> query;
  mysql_wire::AppendInt1(query,
                         static_cast<uint8_t>(mysql_wire::Command::QUERY));
  mysql_wire::AppendBytes(query, "  SELECT delegated;  ");
  Require(writer.WritePacket(0, query), "query write failed");

  const auto rows = ReadSingleColumnRows(&reader, 2);
  Require(rows == std::vector<std::string>({"first-result", "second-result"}),
          "rows mismatch");

  const std::vector<uint8_t> quit{
      static_cast<uint8_t>(mysql_wire::Command::QUIT)};
  Require(writer.WritePacket(0, quit), "quit write failed");
  harness.Join();

  Require(executor->last_sql_ == "SELECT delegated", "delegated SQL mismatch");
}

void TestSessionHandlesControlAndCompatibilityCommands() {
  auto executor = std::make_shared<FakeSqlExecutor>();
  SessionHarness harness(executor);
  mysql_wire::PacketReader reader(harness.ClientFd());
  mysql_wire::PacketWriter writer(harness.ClientFd());
  CompleteHandshake(&reader, &writer);

  Require(writer.WritePacket(0, MakeCommand(mysql_wire::Command::PING)),
          "ping write failed");
  auto ping_result = reader.ReadPacket();
  Require(ping_result.has_value() && ping_result->sequence_id_ == 1 &&
              !ping_result->payload_.empty() &&
              ping_result->payload_[0] == 0x00,
          "ping response mismatch");

  Require(writer.WritePacket(
              0, MakeCommand(mysql_wire::Command::QUERY, "SET NAMES utf8mb4")),
          "SET query write failed");
  auto set_result = reader.ReadPacket();
  Require(set_result.has_value() && set_result->sequence_id_ == 1 &&
              !set_result->payload_.empty() && set_result->payload_[0] == 0x00,
          "ignored SET statement should return OK");

  Require(writer.WritePacket(
              0, MakeCommand(mysql_wire::Command::INIT_DB, "missing")),
          "invalid database command write failed");
  auto bad_database = reader.ReadPacket();
  Require(bad_database.has_value(), "invalid database should return ERR");
  RequireError(*bad_database, 1, mysql_wire::MYSQL_ERROR_BAD_DATABASE);

  Require(writer.WritePacket(
              0, MakeCommand(mysql_wire::Command::QUERY, "USE missing")),
          "invalid USE query write failed");
  auto bad_use = reader.ReadPacket();
  Require(bad_use.has_value(), "invalid USE should return ERR");
  RequireError(*bad_use, 1, mysql_wire::MYSQL_ERROR_BAD_DATABASE);

  Require(writer.WritePacket(
              0, MakeCommand(mysql_wire::Command::INIT_DB, "testdb")),
          "database command write failed");
  auto database_result = reader.ReadPacket();
  Require(database_result.has_value() && database_result->sequence_id_ == 1 &&
              !database_result->payload_.empty() &&
              database_result->payload_[0] == 0x00,
          "database selection should return OK");

  Require(writer.WritePacket(
              0, MakeCommand(mysql_wire::Command::QUERY, "SELECT DATABASE()")),
          "compatibility query write failed");
  Require(ReadSingleColumnRows(&reader, 1)[0] == "testdb",
          "selected database result mismatch");

  Require(writer.WritePacket(0, MakeCommand(mysql_wire::Command::QUERY,
                                            "SELECT CONNECTION_ID()")),
          "connection id query write failed");
  Require(ReadSingleColumnRows(&reader, 1)[0] == "42",
          "connection id result mismatch");
  Require(executor->last_sql_.empty(),
          "compatibility query should not reach the executor");

  Require(writer.WritePacket(0, std::vector<uint8_t>{0x7f}),
          "unsupported command write failed");
  auto unsupported_command = reader.ReadPacket();
  Require(unsupported_command.has_value(),
          "unsupported command should return ERR");
  RequireError(*unsupported_command, 1,
               mysql_wire::MYSQL_ERROR_UNKNOWN_COMMAND);

  Require(writer.WritePacket(0, MakeCommand(mysql_wire::Command::QUIT)),
          "quit write failed");
  harness.Join();
}

void TestSessionRejectsNonzeroCommandSequence() {
  auto executor = std::make_shared<FakeSqlExecutor>();
  SessionHarness harness(executor);
  mysql_wire::PacketReader reader(harness.ClientFd());
  mysql_wire::PacketWriter writer(harness.ClientFd());
  CompleteHandshake(&reader, &writer);

  Require(writer.WritePacket(7, MakeCommand(mysql_wire::Command::PING)),
          "invalid sequence command write failed");
  auto error = reader.ReadPacket();
  Require(error.has_value(), "invalid command sequence should return ERR");
  RequireError(*error, 1, mysql_wire::MYSQL_ERROR_PACKETS_OUT_OF_ORDER);
  harness.Join();
}

void TestSessionUsesErrToTerminateResultset() {
  auto executor = std::make_shared<FailingRowExecutor>();
  SessionHarness harness(executor);
  mysql_wire::PacketReader reader(harness.ClientFd());
  mysql_wire::PacketWriter writer(harness.ClientFd());
  CompleteHandshake(&reader, &writer);

  Require(writer.WritePacket(
              0, MakeCommand(mysql_wire::Command::QUERY, "SELECT failing")),
          "failing query write failed");
  for (uint8_t sequence_id = 1; sequence_id <= 4; sequence_id++) {
    auto packet = reader.ReadPacket();
    Require(packet.has_value() && packet->sequence_id_ == sequence_id,
            "partial resultset packet mismatch");
  }
  auto error = reader.ReadPacket();
  Require(error.has_value(), "resultset should end with ERR");
  RequireError(*error, 5, mysql_wire::MYSQL_ERROR_UNKNOWN);

  Require(writer.WritePacket(0, MakeCommand(mysql_wire::Command::PING)),
          "ping after resultset error write failed");
  auto ping_result = reader.ReadPacket();
  Require(ping_result.has_value() && ping_result->sequence_id_ == 1 &&
              !ping_result->payload_.empty() &&
              ping_result->payload_[0] == 0x00,
          "session should continue after resultset ERR");

  Require(writer.WritePacket(0, MakeCommand(mysql_wire::Command::QUIT)),
          "quit write failed");
  harness.Join();
}

} // namespace

auto main() -> int {
  try {
    TestSessionUsesInjectedExecutor();
    TestSessionHandlesControlAndCompatibilityCommands();
    TestSessionRejectsNonzeroCommandSequence();
    TestSessionUsesErrToTerminateResultset();
  } catch (const std::exception &error) {
    std::cerr << "session_test failed: " << error.what() << '\n';
    return 1;
  }
  std::cout << "session_test passed\n";
  return 0;
}
