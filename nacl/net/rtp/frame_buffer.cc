// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/rtp/frame_buffer.h"

#include "net/sharer_transport_config.h"
#include "net/rtp/rtp.h"
#include "net/rtp/rtp_receiver_defines.h"

FrameBuffer::FrameBuffer()
    : frame_id_(0),
      max_packet_id_(0),
      num_packets_received_(0),
      max_seen_packet_id_(0),
      new_playout_delay_ms_(0),
      is_key_frame_(0),
      total_data_size_(0),
      last_referenced_frame_id_(0),
      packets_() {}

FrameBuffer::~FrameBuffer() {}

bool FrameBuffer::InsertPacket(std::unique_ptr<RTP> packet) {
  // Is this the first packet in the frame?
  if (packets_.empty()) {
    frame_id_ = packet->frameId();
    max_packet_id_ = packet->maxPacketId();
    is_key_frame_ = packet->isKeyFrame();
    new_playout_delay_ms_ = packet->newPlayoutDelayMs();
    if (is_key_frame_) {
      PP_DCHECK(packet->frameId() == packet->referenceFrameId());
    }
    last_referenced_frame_id_ = packet->referenceFrameId();
    rtp_timestamp_ = packet->timestamp();
  }

  // Is this the correct frame?
  if (packet->frameId() != frame_id_) return false;

  // Insert every packet only once
  if (packets_.find(packet->packetId()) != packets_.end()) return false;

  // Insert packet
  int32_t payload_size = packet->payloadSize();
  uint16_t packet_id = packet->packetId();
  packets_.insert(make_pair(packet->packetId(), std::move(packet)));

  ++num_packets_received_;
  max_seen_packet_id_ = std::max(max_seen_packet_id_, packet_id);
  total_data_size_ += payload_size;
  return true;
}

bool FrameBuffer::Complete() const {
  return num_packets_received_ - 1 == max_packet_id_;
}

bool FrameBuffer::AssembleEncodedFrame(EncodedFrame* frame) const {
  if (!Complete()) return false;

  if (is_key_frame_)
    frame->dependency = EncodedFrame::KEY;
  else if (frame_id_ == last_referenced_frame_id_)
    frame->dependency = EncodedFrame::INDEPENDENT;
  else
    frame->dependency = EncodedFrame::DEPENDENT;
  frame->frame_id = frame_id_;
  frame->referenced_frame_id = last_referenced_frame_id_;
  frame->rtp_timestamp = rtp_timestamp_;
  frame->new_playout_delay_ms = new_playout_delay_ms_;

  // Build the data vector
  frame->data.clear();
  frame->data.reserve(total_data_size_);
  for (auto it = packets_.begin(); it != packets_.end(); ++it) {
    const RTP& packet = *it->second;
    frame->data.insert(frame->data.end(), packet.payload(),
                       packet.payload() + packet.payloadSize());
  }

  return true;
}

void FrameBuffer::GetMissingPackets(bool newest_frame,
                                    PacketIdSet* missing_packets) const {
  // Missing packets capped by max_seen_packet_id_.
  // (If it's the latest frame)
  int maximum = newest_frame ? max_seen_packet_id_ : max_packet_id_;
  int packet = 0;
  for (auto it = packets_.begin(); it != packets_.end() && packet <= maximum;
       ++it) {
    int end = std::min<int>(it->first, maximum + 1);
    while (packet < end) {
      missing_packets->insert(packet);
      packet++;
    }
    packet++;
  }
  while (packet <= maximum) {
    missing_packets->insert(packet);
    packet++;
  }
}
