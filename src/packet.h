// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/**
 * @file packet.h
 * @brief Internal socket-level packet I/O and primitive wire encoders.
 *
 * @code{.text}
 * +----------------------+-------------+-------------------+
 * | payload length (3 LE)| sequence id | payload           |
 * +----------------------+-------------+-------------------+
 * @endcode
 *
 * A payload length of 0xFFFFFF marks a continuation fragment in MySQL. This
 * implementation rejects that value instead of reading or writing fragments.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mysql_wire {

/** MySQL continuation-fragment marker rejected by PacketReader/PacketWriter. */
constexpr uint32_t MYSQL_PACKET_FRAGMENT_LENGTH = 0x00ffffffU;

struct MysqlPacket {
  uint8_t sequence_id_{0};
  std::vector<uint8_t> payload_;
};

class PacketReader {
public:
  explicit PacketReader(int fd) : fd_(fd) {}

  auto ReadPacket() -> std::optional<MysqlPacket>;

private:
  auto ReadFully(uint8_t *buffer, size_t length) -> bool;

  int fd_;
};

class PacketWriter {
public:
  explicit PacketWriter(int fd) : fd_(fd) {}

  auto WritePacket(uint8_t sequence_id, const std::vector<uint8_t> &payload)
      -> bool;

private:
  auto WriteFully(const uint8_t *buffer, size_t length) -> bool;

  int fd_;
};

void AppendInt1(std::vector<uint8_t> &buffer, uint8_t value);
void AppendInt2(std::vector<uint8_t> &buffer, uint16_t value);
void AppendInt3(std::vector<uint8_t> &buffer, uint32_t value);
void AppendInt4(std::vector<uint8_t> &buffer, uint32_t value);
void AppendInt8(std::vector<uint8_t> &buffer, uint64_t value);
void AppendBytes(std::vector<uint8_t> &buffer, std::string_view value);
void AppendNullTerminatedString(std::vector<uint8_t> &buffer,
                                const std::string &value);
void AppendLenEncodedInteger(std::vector<uint8_t> &buffer, uint64_t value);
void AppendLenEncodedString(std::vector<uint8_t> &buffer,
                            const std::string &value);

} // namespace mysql_wire
