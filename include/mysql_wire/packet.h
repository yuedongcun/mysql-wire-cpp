// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mysql_wire {

/**
 * A single MySQL packet after the 4-byte packet header has been decoded.
 */
struct MysqlPacket {
  /** Sequence id from the MySQL packet header. */
  uint8_t sequence_id_{0};
  /** Packet payload bytes. */
  std::vector<uint8_t> payload_;
};

/**
 * PacketReader reads complete MySQL packets from a connected socket.
 */
class PacketReader {
public:
  explicit PacketReader(int fd) : fd_(fd) {}

  /** @return the next packet, or std::nullopt if the socket is closed or
   * invalid */
  auto ReadPacket() -> std::optional<MysqlPacket>;

private:
  /** @return true if exactly length bytes were read into buffer */
  auto ReadFully(uint8_t *buffer, size_t length) -> bool;

  int fd_;
};

/**
 * PacketWriter writes complete MySQL packets to a connected socket.
 */
class PacketWriter {
public:
  explicit PacketWriter(int fd) : fd_(fd) {}

  /** @return true if the packet header and payload were written successfully */
  auto WritePacket(uint8_t sequence_id, const std::vector<uint8_t> &payload)
      -> bool;

private:
  /** @return true if exactly length bytes were written from buffer */
  auto WriteFully(const uint8_t *buffer, size_t length) -> bool;

  int fd_;
};

/** Append a 1-byte integer to the packet buffer. */
void AppendInt1(std::vector<uint8_t> *buffer, uint8_t value);
/** Append a 2-byte little-endian integer to the packet buffer. */
void AppendInt2(std::vector<uint8_t> *buffer, uint16_t value);
/** Append a 3-byte little-endian integer to the packet buffer. */
void AppendInt3(std::vector<uint8_t> *buffer, uint32_t value);
/** Append a 4-byte little-endian integer to the packet buffer. */
void AppendInt4(std::vector<uint8_t> *buffer, uint32_t value);
/** Append an 8-byte little-endian integer to the packet buffer. */
void AppendInt8(std::vector<uint8_t> *buffer, uint64_t value);
/** Append raw string bytes to the packet buffer. */
void AppendBytes(std::vector<uint8_t> *buffer, const std::string &value);
/** Append a null-terminated string to the packet buffer. */
void AppendNullTerminatedString(std::vector<uint8_t> *buffer,
                                const std::string &value);
/** Append a MySQL length-encoded integer to the packet buffer. */
void AppendLenEncodedInteger(std::vector<uint8_t> *buffer, uint64_t value);
/** Append a MySQL length-encoded string to the packet buffer. */
void AppendLenEncodedString(std::vector<uint8_t> *buffer,
                            const std::string &value);

} // namespace mysql_wire
