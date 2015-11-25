// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _RTP_RECEIVER_DEFINES_H_
#define _RTP_RECEIVER_DEFINES_H_

#include "net/sharer_transport_config.h"
#include "net/rtcp/rtcp_defines.h"

#include <cstdint>
#include <map>
#include <set>

static const uint32_t kMaxSequenceNumber = 65536;

inline bool IsNewerFrameId(uint32_t frame_id, uint32_t prev_frame_id) {
  return (frame_id != prev_frame_id) &&
         static_cast<uint32_t>(frame_id - prev_frame_id) < 0x80000000;
}

inline bool IsNewerPacketId(uint16_t packet_id, uint16_t prev_packet_id) {
  return (packet_id != prev_packet_id) &&
         static_cast<uint16_t>(packet_id - prev_packet_id) < 0x8000;
}

inline bool IsNewerSequenceNumber(uint16_t sequence_number,
                                  uint16_t prev_sequence_number) {
  // Same function as IsNewerPacketId just different data and name
  return IsNewerPacketId(sequence_number, prev_sequence_number);
}

inline bool IsOlderFrameId(uint32_t frame_id, uint32_t prev_frame_id) {
  return (frame_id == prev_frame_id) || IsNewerFrameId(prev_frame_id, frame_id);
}

inline bool IsNewerRtpTimestamp(uint32_t timestamp, uint32_t prev_timestamp) {
  return (timestamp != prev_timestamp) &&
         static_cast<uint32_t>(timestamp - prev_timestamp) < 0x80000000;
}

struct RtpSharerHeader {
  RtpSharerHeader();

  // Elements from RTP packet header.
  bool marker;
  uint8_t payload_type;
  uint16_t sequence_number;
  uint32_t rtp_timestamp;
  uint32_t sender_ssrc;

  // Elements from Sharer header (at beginning of RTP payload).
  bool is_key_frame;
  uint32_t frame_id;
  uint16_t packet_id;
  uint16_t max_packet_id;
  uint32_t reference_frame_id;

  uint16_t new_playout_delay_ms;
};

class RtpPayloadFeedback {
 public:
  virtual void SharerFeedback(const RtcpSharerMessage& sharer_feedback) = 0;

 protected:
  virtual ~RtpPayloadFeedback();
};

class UDPSender {
 public:
  virtual void SendPacket(PacketRef packet) = 0;
};

#endif  // _RTP_RECEIVER_DEFINES_H_
