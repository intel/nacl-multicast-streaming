// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/rtcp/rtcp_utility.h"

#include "base/logger.h"
#include "net/sharer_transport_defines.h"

namespace sharer {

RtcpParser::RtcpParser(uint32_t local_ssrc, uint32_t remote_ssrc)
    : local_ssrc_(local_ssrc),
      remote_ssrc_(remote_ssrc),
      has_sender_report_(false),
      has_last_report_(false),
      has_sharer_message_(false),
      has_receiver_reference_time_report_(false) {}

RtcpParser::~RtcpParser() {}

bool RtcpParser::Parse(BigEndianReader* reader) {
  while (reader->remaining()) {
    RtcpCommonHeader header;
    if (!ParseCommonHeader(reader, &header)) return false;

    base::StringPiece tmp;
    if (!reader->ReadPiece(&tmp, header.length_in_octets - 4)) return false;
    BigEndianReader chunk(tmp.data(), tmp.size());

    switch (header.PT) {
      case kPacketTypeSenderReport:
        if (!ParseSR(&chunk, header)) return false;
        break;

      case kPacketTypeReceiverReport:
        if (!ParseRR(&chunk, header)) return false;
        break;

      case kPacketTypePayloadSpecific:
        if (!ParseFeedbackCommon(&chunk, header)) return false;
        break;

      case kPacketTypeXr:
        if (!ParseExtendedReport(&chunk, header)) return false;
        break;

      case kPacketTypeGenericRtpFeedback:
        if (!ParsePausedIDCommon(&chunk, header)) return false;
        break;
    }
  }
  return true;
}

bool RtcpParser::ParseCommonHeader(BigEndianReader* reader,
                                   RtcpCommonHeader* parsed_header) {
  //  0                   1                   2                   3
  //  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
  // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  // |V=2|P|    IC   |      PT       |             length            |
  // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  //
  // Common header for all Rtcp packets, 4 octets.

  uint8_t byte;
  if (!reader->ReadU8(&byte)) return false;
  parsed_header->V = byte >> 6;
  parsed_header->P = ((byte & 0x20) == 0) ? false : true;

  // Check if RTP version field == 2.
  if (parsed_header->V != 2) return false;

  parsed_header->IC = byte & 0x1f;
  if (!reader->ReadU8(&parsed_header->PT)) return false;

  uint16_t bytes;
  if (!reader->ReadU16(&bytes)) return false;

  parsed_header->length_in_octets = (static_cast<size_t>(bytes) + 1) * 4;

  if (parsed_header->length_in_octets == 0) return false;

  return true;
}

bool RtcpParser::ParseSR(BigEndianReader* reader,
                         const RtcpCommonHeader& header) {
  uint32_t sender_ssrc;
  if (!reader->ReadU32(&sender_ssrc)) return false;

  if (sender_ssrc != remote_ssrc_) return true;

  uint32_t tmp;
  if (!reader->ReadU32(&sender_report_.ntp_seconds) ||
      !reader->ReadU32(&sender_report_.ntp_fraction) ||
      !reader->ReadU32(&sender_report_.rtp_timestamp) ||
      !reader->ReadU32(&sender_report_.send_packet_count) ||
      !reader->ReadU32(&tmp))
    return false;
  sender_report_.send_octet_count = tmp;
  has_sender_report_ = true;

  for (size_t block = 0; block < header.IC; block++)
    if (!ParseReportBlock(reader)) return false;

  return true;
}

bool RtcpParser::ParseRR(BigEndianReader* reader,
                         const RtcpCommonHeader& header) {
  uint32_t receiver_ssrc;
  if (!reader->ReadU32(&receiver_ssrc)) return false;

  if (receiver_ssrc != remote_ssrc_) return true;

  for (size_t block = 0; block < header.IC; block++)
    if (!ParseReportBlock(reader)) return false;

  return true;
}

bool RtcpParser::ParseReportBlock(BigEndianReader* reader) {
  uint32_t ssrc, last_report, delay;
  if (!reader->ReadU32(&ssrc) || !reader->Skip(12) ||
      !reader->ReadU32(&last_report) || !reader->ReadU32(&delay))
    return false;

  if (ssrc == local_ssrc_) {
    last_report_ = last_report;
    delay_since_last_report_ = delay;
    has_last_report_ = true;
  }

  return true;
}

// RFC 4585.
bool RtcpParser::ParseFeedbackCommon(BigEndianReader* reader,
                                     const RtcpCommonHeader& header) {
  // See RTC 4585 Section 6.4 for application specific feedback messages.
  if (header.IC != 15) {
    return true;
  }
  uint32_t remote_ssrc;
  uint32_t media_ssrc;
  if (!reader->ReadU32(&remote_ssrc) || !reader->ReadU32(&media_ssrc))
    return false;

  if (remote_ssrc != remote_ssrc_) return true;

  uint32_t name;
  if (!reader->ReadU32(&name)) return false;

  if (name != kSharer) {
    return true;
  }

  sharer_message_.media_ssrc = remote_ssrc;

  uint32_t last_frame_id;
  uint8_t number_of_lost_fields;
  uint8_t padding;
  if (!reader->ReadU32(&last_frame_id) ||
      !reader->ReadU8(&number_of_lost_fields) ||
      !reader->ReadU8(&padding) ||  // padding
      !reader->ReadU16(&sharer_message_.target_delay_ms))
    return false;

  // Please note, this frame_id is still only 8-bit!
  sharer_message_.ack_frame_id = last_frame_id;

  for (size_t i = 0; i < number_of_lost_fields; i++) {
    uint32_t frame_id;
    uint16_t packet_id;
    uint8_t bitmask;
    if (!reader->ReadU32(&frame_id) || !reader->ReadU16(&packet_id) ||
        !reader->ReadU8(&bitmask) || !reader->ReadU8(&padding))
      return false;
    sharer_message_.missing_frames_and_packets[frame_id].insert(packet_id);
    if (packet_id != kRtcpSharerAllPacketsLost) {
      while (bitmask) {
        packet_id++;
        if (bitmask & 1)
          sharer_message_.missing_frames_and_packets[frame_id].insert(
              packet_id);
        bitmask >>= 1;
      }
    }
  }

  has_sharer_message_ = true;
  return true;
}

bool RtcpParser::ParsePausedIDCommon(BigEndianReader* reader,
                                     const RtcpCommonHeader& header) {
  // TODO parse paused
  DINF() << "PARSED ID COMMON\n";
  return false;
}

bool RtcpParser::ParseExtendedReport(BigEndianReader* reader,
                                     const RtcpCommonHeader& header) {
  uint32_t remote_ssrc;
  if (!reader->ReadU32(&remote_ssrc)) return false;

  // Is it for us?
  if (remote_ssrc != remote_ssrc_) return true;

  while (reader->remaining()) {
    uint8_t block_type;
    uint16_t block_length;
    if (!reader->ReadU8(&block_type) || !reader->Skip(1) ||
        !reader->ReadU16(&block_length))
      return false;

    switch (block_type) {
      case 4:  // RRTR. RFC3611 Section 4.4.
        if (block_length != 2) return false;
        if (!ParseExtendedReportReceiverReferenceTimeReport(reader,
                                                            remote_ssrc))
          return false;
        break;

      default:
        // Skip unknown item.
        if (!reader->Skip(block_length * 4)) return false;
    }
  }

  return true;
}

bool RtcpParser::ParseExtendedReportReceiverReferenceTimeReport(
    BigEndianReader* reader, uint32_t remote_ssrc) {
  receiver_reference_time_report_.remote_ssrc = remote_ssrc;
  if (!reader->ReadU32(&receiver_reference_time_report_.ntp_seconds) ||
      !reader->ReadU32(&receiver_reference_time_report_.ntp_fraction))
    return false;

  has_receiver_reference_time_report_ = true;
  return true;
}

}  // namespace sharer
