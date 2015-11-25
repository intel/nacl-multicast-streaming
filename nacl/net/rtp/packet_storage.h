// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_RTP_PACKET_STORAGE_H_
#define NET_RTP_PACKET_STORAGE_H_

#include <deque>

#include "net/pacing/paced_sender.h"

namespace sharer {

class PacketStorage {
 public:
  PacketStorage();
  virtual ~PacketStorage();

  // Store all the packets for a frame
  void StoreFrame(uint32_t frame_id, const SendPacketVector& packets);

  // Release all the packets for a frame
  void ReleaseFrame(uint32_t frame_id);

  // Returns a list of packets for a frame indexed by a 8-bits ID
  // It is the lowest 8 bits of a frame ID.
  // Returns nullptr if the frame cannot be found.
  const SendPacketVector* GetFrame32(uint32_t frame_id) const;

  // Get the number of stored frames
  size_t GetNumberOfStoredFrames() const;

 private:
  std::deque<SendPacketVector> frames_;
  uint32_t first_frame_id_in_list_;

  // The number of frames whose packets have been released, but the entry in
  // the |frames_| queue has not yet been popped.
  size_t zombie_count_;

  DISALLOW_COPY_AND_ASSIGN(PacketStorage);
};

}  // namespace sharer

#endif  // NET_RTP_PACKET_STORAGE_H_
