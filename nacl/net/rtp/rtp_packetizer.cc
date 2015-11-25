// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/rtp/rtp_packetizer.h"

#include "base/big_endian.h"
#include "net/rtp/packet_storage.h"
#include "net/rtp/rtp_defines.h"

#include "ppapi/cpp/logging.h"

namespace sharer {

RtpPacketizerConfig::RtpPacketizerConfig()
    : payload_type(-1),
      max_payload_length(kMaxIpPacketSize - 31),  // Default is IP-v4/UDP.
      sequence_number(0),
      ssrc(0) {}

RtpPacketizerConfig::~RtpPacketizerConfig() {}

RtpPacketizer::RtpPacketizer(PacedSender* const transport,
                             PacketStorage* packet_storage,
                             RtpPacketizerConfig rtp_packetizer_config)
    : config_(rtp_packetizer_config),
      transport_(transport),
      packet_storage_(packet_storage),
      sequence_number_(config_.sequence_number),
      rtp_timestamp_(0),
      packet_id_(0),
      send_packet_count_(0),
      send_octet_count_(0) {
  PP_DCHECK(transport);  // Invalid argument;
}

RtpPacketizer::~RtpPacketizer() {}

uint16_t RtpPacketizer::NextSequenceNumber() {
  ++sequence_number_;
  return sequence_number_ - 1;
}

void RtpPacketizer::SendFrameAsPackets(const EncodedFrame& frame) {
  uint16_t rtp_header_length = kRtpHeaderLength + kSharerHeaderLength;
  uint16_t max_length = config_.max_payload_length - rtp_header_length - 1;
  rtp_timestamp_ = frame.rtp_timestamp;

  // Split the payload evenly (round number up).
  size_t num_packets = (frame.data.size() + max_length) / max_length;
  size_t payload_length = (frame.data.size() + num_packets) / num_packets;
  PP_DCHECK(payload_length <= max_length);  // Invalid argument

  SendPacketVector packets;

  size_t remaining_size = frame.data.size();
  std::string::const_iterator data_iter = frame.data.begin();
  while (remaining_size > 0) {
    PacketRef packet = std::make_shared<Packet>();

    if (remaining_size < payload_length) {
      payload_length = remaining_size;
    }
    remaining_size -= payload_length;
    BuildCommonRTPheader(packet, remaining_size == 0, frame.rtp_timestamp);

    // Build Sharer header.
    // TODO(miu): Should we always set the ref frame bit and the ref_frame_id?
    PP_DCHECK(frame.dependency != EncodedFrame::UNKNOWN_DEPENDENCY);
    uint8_t num_extensions = 0;
    if (frame.new_playout_delay_ms) num_extensions++;
    uint8_t byte0 = kSharerReferenceFrameIdBitMask;
    if (frame.dependency == EncodedFrame::KEY) byte0 |= kSharerKeyFrameBitMask;
    PP_DCHECK(num_extensions <= kSharerExtensionCountmask);
    byte0 |= num_extensions;
    packet->push_back(byte0);
    size_t start_size = packet->size();
    packet->resize(start_size + 12);
    BigEndianWriter big_endian_writer(
        reinterpret_cast<char*>(&(packet->data()[start_size])), 12);
    big_endian_writer.WriteU32(frame.frame_id);
    big_endian_writer.WriteU16(packet_id_);
    big_endian_writer.WriteU16(static_cast<uint16_t>(num_packets - 1));
    big_endian_writer.WriteU32(frame.referenced_frame_id);
    if (frame.new_playout_delay_ms) {
      packet->push_back(kSharerRtpExtensionAdaptiveLatency << 2);
      packet->push_back(2);  // 2 bytes
      packet->push_back(static_cast<uint8_t>(frame.new_playout_delay_ms >> 8));
      packet->push_back(static_cast<uint8_t>(frame.new_playout_delay_ms));
    }

    // Copy payload data.
    packet->insert(packet->end(), data_iter, data_iter + payload_length);
    data_iter += payload_length;

    const PacketKey key = PacedSender::MakePacketKey(
        frame.reference_time, config_.ssrc, packet_id_++);
    packets.push_back(make_pair(key, packet));

    // Update stats.
    ++send_packet_count_;
    send_octet_count_ += payload_length;
  }
  PP_DCHECK(packet_id_ == num_packets);  // Invalid state;

  packet_storage_->StoreFrame(frame.frame_id, packets);

  // Send to network.
  transport_->SendPackets(packets);

  // Prepare for next frame.
  packet_id_ = 0;
}

void RtpPacketizer::BuildCommonRTPheader(PacketRef packet, bool marker_bit,
                                         uint32_t time_stamp) {
  packet->push_back(0x80);
  packet->push_back(static_cast<uint8_t>(config_.payload_type) |
                    (marker_bit ? kRtpMarkerBitMask : 0));
  size_t start_size = packet->size();
  packet->resize(start_size + 10);
  BigEndianWriter big_endian_writer(
      reinterpret_cast<char*>(&((*packet)[start_size])), 10);
  big_endian_writer.WriteU16(sequence_number_);
  big_endian_writer.WriteU32(time_stamp);
  big_endian_writer.WriteU32(config_.ssrc);
  ++sequence_number_;
}

}  // namespace sharer
