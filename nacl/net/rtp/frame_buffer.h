// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _FRAME_BUFFER_H_
#define _FRAME_BUFFER_H_

#include "net/rtp/rtp_receiver_defines.h"

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

struct EncodedFrame;
class RTP;

using PacketMap = std::map<uint16_t, std::unique_ptr<RTP>>;

class FrameBuffer {
 public:
  FrameBuffer();
  ~FrameBuffer();

  bool InsertPacket(std::unique_ptr<RTP> packet);
  bool Complete() const;

  void GetMissingPackets(bool newest_frame, PacketIdSet* missing_packets) const;
  bool AssembleEncodedFrame(EncodedFrame* frame) const;

  bool is_key_frame() const { return is_key_frame_; }
  uint32_t last_referenced_frame_id() const {
    return last_referenced_frame_id_;
  }
  uint32_t frame_id() const { return frame_id_; }

 private:
  uint32_t frame_id_;
  uint16_t max_packet_id_;
  uint16_t num_packets_received_;
  uint16_t max_seen_packet_id_;
  uint16_t new_playout_delay_ms_;
  bool is_key_frame_;
  size_t total_data_size_;
  uint32_t last_referenced_frame_id_;
  uint32_t rtp_timestamp_;
  PacketMap packets_;
};

#endif  // _FRAME_BUFFER_H_
