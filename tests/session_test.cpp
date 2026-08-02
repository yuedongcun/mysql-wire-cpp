// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "mysql_wire/constants.h"
#include "mysql_wire/packet.h"
#include "mysql_wire/session.h"
#include "mysql_wire/sql_executor.h"

namespace {

void Require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class FakeSqlExecutor : public mysql_wire::SqlExecutor {
public:
  auto Execute(const std::string &sql,
               const mysql_wire::MysqlQueryContext &context)
      -> mysql_wire::SqlQueryResult override {
    last_sql_ = sql;
    last_context_ = context;
    std::vector<mysql_wire::SqlColumn> columns{
        {"value", mysql_wire::ColumnType::VAR_STRING, false}};
    std::vector<std::vector<std::optional<std::string>>> rows{{"fake-result"}};
    return mysql_wire::SqlQueryResult::Rows(std::move(columns),
                                            std::move(rows));
  }

  auto DatabaseName() const -> std::string_view override { return "testdb"; }

  std::string last_sql_;
  mysql_wire::MysqlQueryContext last_context_;
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
  mysql_wire::AppendInt4(&payload, capabilities);
  mysql_wire::AppendInt4(&payload, 1U << 24U);
  mysql_wire::AppendInt1(&payload, mysql_wire::MYSQL_DEFAULT_CHARSET);
  for (int i = 0; i < 23; i++) {
    mysql_wire::AppendInt1(&payload, 0);
  }
  mysql_wire::AppendNullTerminatedString(&payload, "test-user");
  mysql_wire::AppendInt1(&payload, 0);
  mysql_wire::AppendNullTerminatedString(&payload, "testdb");
  mysql_wire::AppendNullTerminatedString(&payload,
                                         mysql_wire::MYSQL_AUTH_PLUGIN_NAME);
  return payload;
}

void TestSessionUsesInjectedExecutor() {
  auto executor = std::make_shared<FakeSqlExecutor>();
  SessionHarness harness(executor);
  mysql_wire::PacketReader reader(harness.ClientFd());
  mysql_wire::PacketWriter writer(harness.ClientFd());

  auto handshake = reader.ReadPacket();
  Require(handshake.has_value() && !handshake->payload_.empty(),
          "missing handshake");
  Require(handshake->sequence_id_ == 0, "handshake sequence mismatch");
  Require(handshake->payload_[0] == mysql_wire::MYSQL_PROTOCOL_VERSION,
          "protocol version mismatch");

  Require(writer.WritePacket(1, MakeHandshakeResponse()),
          "handshake response write failed");
  auto auth_result = reader.ReadPacket();
  Require(auth_result.has_value() && !auth_result->payload_.empty(),
          "missing auth result");
  Require(auth_result->sequence_id_ == 2 && auth_result->payload_[0] == 0x00,
          "authentication failed");

  std::vector<uint8_t> query;
  mysql_wire::AppendInt1(&query,
                         static_cast<uint8_t>(mysql_wire::Command::QUERY));
  mysql_wire::AppendBytes(&query, "  SELECT delegated;  ");
  Require(writer.WritePacket(0, query), "query write failed");

  auto column_count = reader.ReadPacket();
  auto column_definition = reader.ReadPacket();
  auto metadata_eof = reader.ReadPacket();
  auto row = reader.ReadPacket();
  auto resultset_eof = reader.ReadPacket();
  Require(column_count.has_value() &&
              column_count->payload_ == std::vector<uint8_t>({1}),
          "column count mismatch");
  Require(column_definition.has_value(), "missing column definition");
  Require(metadata_eof.has_value() && metadata_eof->payload_[0] == 0xfe,
          "missing metadata EOF");
  Require(row.has_value() && !row->payload_.empty(), "missing result row");
  Require(resultset_eof.has_value() && resultset_eof->payload_[0] == 0xfe,
          "missing resultset EOF");
  Require(std::string(row->payload_.begin() + 1, row->payload_.end()) ==
              "fake-result",
          "row mismatch");

  const std::vector<uint8_t> quit{
      static_cast<uint8_t>(mysql_wire::Command::QUIT)};
  Require(writer.WritePacket(0, quit), "quit write failed");
  harness.Join();

  Require(executor->last_sql_ == "SELECT delegated", "delegated SQL mismatch");
  Require(executor->last_context_.connection_id_ == 42,
          "connection id mismatch");
  Require(executor->last_context_.current_database_ == "testdb",
          "database context mismatch");
}

} // namespace

auto main() -> int {
  try {
    TestSessionUsesInjectedExecutor();
  } catch (const std::exception &error) {
    std::cerr << "session_test failed: " << error.what() << '\n';
    return 1;
  }
  std::cout << "session_test passed\n";
  return 0;
}
