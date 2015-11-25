// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/rtp/rtp_sender.h"

#include "base/big_endian.h"
#include "base/logger.h"
#include "base/ptr_utils.h"
#include "base/rand_util.h"
#include "sharer_defines.h"

namespace sharer {

namespace {

// If there is only one reference to the packet then copy the
// reference and return.
// Otherwise return a deep copy of the packet.
PacketRef FastCopyPacket(const PacketRef& packet) {
  if (packet.unique()) return packet;
  return std::make_shared<Packet>(*packet);
}

}  // namespace

RtpSender::RtpSender(PacedSender* const transport) : transport_(transport) {
  // Randomly set sequence number start value.
  config_.sequence_number = base::RandInt(0, 65535);
}

RtpSender::~RtpSender() {}

bool RtpSender::Initialize(const SharerTransportRtpConfig& config) {
  config_.ssrc = config.ssrc;
  config_.payload_type = config.rtp_payload_type;
  packetizer_ = make_unique<RtpPacketizer>(transport_, &storage_, config_);
  return true;
}

void RtpSender::SendFrame(const EncodedFrame& frame) {
  PP_DCHECK(packetizer_);
  packetizer_->SendFrameAsPackets(frame);
  if (storage_.GetNumberOfStoredFrames() > kMaxUnackedFrames) {
    // TODO: Change to LOG_IF
    DERR()
        << "Possible bug: Frames are not being actively released from storage.";
  }
}

void RtpSender::ResendPackets(
    const std::string& addr,
    const MissingFramesAndPacketsMap& missing_frames_and_packets,
    bool cancel_rtx_if_not_in_list, const DedupInfo& dedup_info) {
  // Iterate over all frames in the list.
  for (MissingFramesAndPacketsMap::const_iterator it =
           missing_frames_and_packets.begin();
       it != missing_frames_and_packets.end(); ++it) {
    SendPacketVector packets_to_resend;
    uint32_t frame_id = it->first;
    // Set of packets that the receiver wants us to re-send.
    // If empty, we need to re-send all packets for this frame.
    const PacketIdSet& missing_packet_set = it->second;

    bool resend_all = missing_packet_set.find(kRtcpSharerAllPacketsLost) !=
                      missing_packet_set.end();
    bool resend_last = missing_packet_set.find(kRtcpSharerLastPacket) !=
                       missing_packet_set.end();

    const SendPacketVector* stored_packets = storage_.GetFrame32(frame_id);
    if (!stored_packets) {
      DERR() << "Can't resend " << missing_packet_set.size()
             << " packets for frame:" << frame_id;
      continue;
    }

    for (SendPacketVector::const_iterator it = stored_packets->begin();
         it != stored_packets->end(); ++it) {
      const PacketKey& packet_key = it->first;
      const uint16_t packet_id = packet_key.second.second;

      // Should we resend the packet?
      bool resend = resend_all;

      // Should we resend it because it's in the missing_packet_set?
      if (!resend &&
          missing_packet_set.find(packet_id) != missing_packet_set.end()) {
        resend = true;
      }

      // If we were asked to resend the last packet, check if it's the
      // last packet.
      if (!resend && resend_last && (it + 1) == stored_packets->end()) {
        resend = true;
      }

      if (resend) {
        // Resend packet to the network.
        DINF() << "Resend " << static_cast<int>(frame_id) << ":" << packet_id
               << ", dest: " << addr;
        // Set a unique incremental sequence number for every packet.
        PacketRef packet_copy = FastCopyPacket(it->second);
        UpdateSequenceNumber(packet_copy);
        packets_to_resend.push_back(std::make_pair(packet_key, packet_copy));
      } else if (cancel_rtx_if_not_in_list) {
        transport_->CancelSendingPacket(addr, it->first);
      }
    }
    transport_->ResendPackets(addr, packets_to_resend, dedup_info);
  }
}

void RtpSender::ResendFrameForKickstart(uint32_t frame_id,
                                        base::TimeDelta dedupe_window) {
  // Send the last packet of the encoded frame to kick start
  // retransmission. This gives enough information to the receiver what
  // packets and frames are missing.
  MissingFramesAndPacketsMap missing_frames_and_packets;
  PacketIdSet missing;
  missing.insert(kRtcpSharerLastPacket);
  missing_frames_and_packets.insert(std::make_pair(frame_id, missing));

  // Sending this extra packet is to kick-start the session. There is
  // no need to optimize re-transmission for this case.
  DedupInfo dedup_info;
  dedup_info.resend_interval = dedupe_window;
  // ResendPackets(missing_frames_and_packets, false, dedup_info);
}

void RtpSender::UpdateSequenceNumber(PacketRef packet) {
  // TODO(miu): This is an abstraction violation.  This needs to be a part of
  // the overall packet (de)serialization consolidation.
  static const int kByteOffsetToSequenceNumber = 2;
  BigEndianWriter big_endian_writer(
      reinterpret_cast<char*>((packet->data()) + kByteOffsetToSequenceNumber),
      sizeof(uint16_t));
  big_endian_writer.WriteU16(packetizer_->NextSequenceNumber());
}

int64_t RtpSender::GetLastByteSentForFrame(uint32_t frame_id) {
  const SendPacketVector* stored_packets = storage_.GetFrame32(frame_id);
  if (!stored_packets) return 0;
  PacketKey last_packet_key = stored_packets->rbegin()->first;
  return transport_->GetLastByteSentForPacket(last_packet_key);
}

}  // namespace sharer
