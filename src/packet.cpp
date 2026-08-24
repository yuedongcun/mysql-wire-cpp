// Copyright (c) 2015-2025 Carnegie Mellon University Database Group.
// Copyright (c) 2026 mysql-wire-cpp contributors.
// SPDX-License-Identifier: MIT

/**
 * @file packet.cpp
 * @brief Implements exact socket reads/writes and MySQL primitive encodings.
 *
 * Short POSIX I/O operations and EINTR are handled internally so callers see
 * only complete packets. Integer helpers append the little-endian and
 * length-encoded forms used throughout the protocol.
 */

#include "packet.h"

#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace mysql_wire {

auto PacketReader::ReadFully(uint8_t *buffer, size_t length) -> bool {
  size_t offset = 0;
  while (offset < length) {
    ssize_t read_size = recv(fd_, buffer + offset, length - offset, 0);
    if (read_size == 0) {
      return false;
    }
    if (read_size < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    offset += static_cast<size_t>(read_size);
  }
  return true;
}

/**
 * MySQL 8.0.46 Protocol::Packet:
 * https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_basic_packets.html#sect_protocol_basic_packets_packet
 */
auto PacketReader::ReadPacket() -> std::optional<MysqlPacket> {
  uint8_t header[4];
  if (!ReadFully(header, sizeof(header))) {
    return std::nullopt;
  }

  uint32_t payload_length = static_cast<uint32_t>(header[0]) |
                            (static_cast<uint32_t>(header[1]) << 8U) |
                            (static_cast<uint32_t>(header[2]) << 16U);
  if (payload_length == MYSQL_PACKET_FRAGMENT_LENGTH) {
    std::clog << "Rejected fragmented MySQL packet: payload_length=0xFFFFFF\n";
    return std::nullopt;
  }

  MysqlPacket packet;
  packet.sequence_id_ = header[3];
  packet.payload_.resize(payload_length);
  if (payload_length > 0 &&
      !ReadFully(packet.payload_.data(), payload_length)) {
    return std::nullopt;
  }
  return packet;
}

auto PacketWriter::WriteFully(const uint8_t *buffer, size_t length) -> bool {
  size_t offset = 0;
  while (offset < length) {
    ssize_t write_size =
        send(fd_, buffer + offset, length - offset, MSG_NOSIGNAL);
    if (write_size == 0) {
      return false;
    }
    if (write_size < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    offset += static_cast<size_t>(write_size);
  }
  return true;
}

/**
 * MySQL 8.0.46 Protocol::Packet:
 * https://dev.mysql.com/doc/dev/mysql-server/8.0.46/page_protocol_basic_packets.html#sect_protocol_basic_packets_packet
 */
auto PacketWriter::WritePacket(uint8_t sequence_id,
                               const std::vector<uint8_t> &payload) -> bool {
  if (payload.size() >= MYSQL_PACKET_FRAGMENT_LENGTH) {
    std::clog << "Rejected fragmented MySQL response: payload_length="
              << payload.size() << '\n';
    return false;
  }

  std::vector<uint8_t> header;
  header.reserve(4);
  AppendInt3(header, static_cast<uint32_t>(payload.size()));
  AppendInt1(header, sequence_id);

  if (!WriteFully(header.data(), header.size())) {
    return false;
  }
  if (!payload.empty() && !WriteFully(payload.data(), payload.size())) {
    return false;
  }
  return true;
}

void AppendInt1(std::vector<uint8_t> &buffer, uint8_t value) {
  buffer.push_back(value);
}

void AppendInt2(std::vector<uint8_t> &buffer, uint16_t value) {
  buffer.push_back(static_cast<uint8_t>(value & 0xffU));
  buffer.push_back(static_cast<uint8_t>((value >> 8U) & 0xffU));
}

void AppendInt3(std::vector<uint8_t> &buffer, uint32_t value) {
  buffer.push_back(static_cast<uint8_t>(value & 0xffU));
  buffer.push_back(static_cast<uint8_t>((value >> 8U) & 0xffU));
  buffer.push_back(static_cast<uint8_t>((value >> 16U) & 0xffU));
}

void AppendInt4(std::vector<uint8_t> &buffer, uint32_t value) {
  AppendInt2(buffer, static_cast<uint16_t>(value & 0xffffU));
  AppendInt2(buffer, static_cast<uint16_t>((value >> 16U) & 0xffffU));
}

void AppendInt8(std::vector<uint8_t> &buffer, uint64_t value) {
  AppendInt4(buffer, static_cast<uint32_t>(value & 0xffffffffULL));
  AppendInt4(buffer, static_cast<uint32_t>((value >> 32ULL) & 0xffffffffULL));
}

void AppendBytes(std::vector<uint8_t> &buffer, const std::string &value) {
  buffer.insert(buffer.end(), value.begin(), value.end());
}

void AppendNullTerminatedString(std::vector<uint8_t> &buffer,
                                const std::string &value) {
  AppendBytes(buffer, value);
  AppendInt1(buffer, 0);
}

void AppendLenEncodedInteger(std::vector<uint8_t> &buffer, uint64_t value) {
  if (value < 251) {
    AppendInt1(buffer, static_cast<uint8_t>(value));
    return;
  }
  if (value <= 0xffffULL) {
    AppendInt1(buffer, 0xfc);
    AppendInt2(buffer, static_cast<uint16_t>(value));
    return;
  }
  if (value <= 0xffffffULL) {
    AppendInt1(buffer, 0xfd);
    AppendInt3(buffer, static_cast<uint32_t>(value));
    return;
  }
  AppendInt1(buffer, 0xfe);
  AppendInt8(buffer, value);
}

void AppendLenEncodedString(std::vector<uint8_t> &buffer,
                            const std::string &value) {
  AppendLenEncodedInteger(buffer, value.size());
  AppendBytes(buffer, value);
}

} // namespace mysql_wire
