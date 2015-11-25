// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_RTP_RTP_PACKETIZER_H_
#define NET_RTP_RTP_PACKETIZER_H_

#include "net/sharer_transport_config.h"

#include <stdint.h>
#include <sys/types.h>

namespace sharer {

class PacedSender;
class PacketStorage;

struct RtpPacketizerConfig {
  RtpPacketizerConfig();
  ~RtpPacketizerConfig();

  int payload_type;
  uint16_t max_payload_length;
  uint16_t sequence_number;

  unsigned int ssrc;
};

class RtpPacketizer {
 public:
  RtpPacketizer(PacedSender* const transport, PacketStorage* packet_storage,
                RtpPacketizerConfig rtp_packetizer_config);
  ~RtpPacketizer();

  void SendFrameAsPackets(const EncodedFrame& frame);
  void SendFramePauseIDAsPackets(const EncodedFrame& frame);
  uint16_t NextSequenceNumber();

  size_t send_packet_count() const { return send_packet_count_; }
  size_t send_octet_count() const { return send_octet_count_; }

 private:
  void BuildCommonRTPheader(PacketRef packet, bool marker_bit,
                            uint32_t timestamp);
  RtpPacketizerConfig config_;
  PacedSender* const transport_;
  PacketStorage* packet_storage_;

  uint16_t sequence_number_;
  uint32_t rtp_timestamp_;
  uint16_t packet_id_;

  size_t send_packet_count_;
  size_t send_octet_count_;
};

}  // namespace sharer

#endif  // NET_RTP_RTP_PACKETIZER_H_
