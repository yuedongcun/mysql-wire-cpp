// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/**
 * @file packet_test.cpp
 * @brief Unit tests for primitive encodings and framed packet socket I/O.
 *
 * A local socketpair verifies the same byte stream contract without opening a
 * TCP port or depending on a MySQL client installation.
 */

#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "packet.h"

namespace {

void Require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void TestIntegerEncoding() {
  std::vector<uint8_t> buffer;
  mysql_wire::AppendInt1(buffer, 0x12);
  mysql_wire::AppendInt2(buffer, 0x3456);
  mysql_wire::AppendInt3(buffer, 0x789abc);
  mysql_wire::AppendInt4(buffer, 0xdef01234);
  const std::vector<uint8_t> expected{0x12, 0x56, 0x34, 0xbc, 0x9a,
                                      0x78, 0x34, 0x12, 0xf0, 0xde};
  Require(buffer == expected, "little-endian integer encoding mismatch");
}

void TestLengthEncodedInteger() {
  std::vector<uint8_t> buffer;
  mysql_wire::AppendLenEncodedInteger(buffer, 250);
  mysql_wire::AppendLenEncodedInteger(buffer, 251);
  mysql_wire::AppendLenEncodedInteger(buffer, 0x10000);
  const std::vector<uint8_t> expected{250,  0xfc, 0xfb, 0x00,
                                      0xfd, 0x00, 0x00, 0x01};
  Require(buffer == expected, "length-encoded integer mismatch");
}

void TestPacketRoundTrip() {
  int sockets[2];
  Require(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
          "socketpair failed");

  mysql_wire::PacketWriter writer(sockets[0]);
  mysql_wire::PacketReader reader(sockets[1]);
  const std::vector<uint8_t> payload{1, 2, 3};
  Require(writer.WritePacket(7, payload), "packet write failed");
  auto packet = reader.ReadPacket();
  Require(packet.has_value(), "packet read failed");
  Require(packet->sequence_id_ == 7, "sequence id mismatch");
  Require(packet->payload_ == payload, "payload mismatch");

  close(sockets[0]);
  close(sockets[1]);
}

void TestFragmentedPacketWriteIsRejected() {
  mysql_wire::PacketWriter writer(-1);
  const std::vector<uint8_t> payload(mysql_wire::MYSQL_PACKET_FRAGMENT_LENGTH);
  Require(!writer.WritePacket(0, payload),
          "fragment-sized payload should be rejected");
}

} // namespace

auto main() -> int {
  try {
    TestIntegerEncoding();
    TestLengthEncodedInteger();
    TestPacketRoundTrip();
    TestFragmentedPacketWriteIsRejected();
  } catch (const std::exception &error) {
    std::cerr << "packet_test failed: " << error.what() << '\n';
    return 1;
  }
  std::cout << "packet_test passed\n";
  return 0;
}
