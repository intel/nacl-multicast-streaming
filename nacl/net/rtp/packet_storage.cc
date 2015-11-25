// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/macros.h"
#include "net/rtp/packet_storage.h"
#include "sharer_defines.h"

#include "ppapi/cpp/logging.h"

namespace sharer {

PacketStorage::PacketStorage() : first_frame_id_in_list_(0), zombie_count_(0) {}

PacketStorage::~PacketStorage() {}

size_t PacketStorage::GetNumberOfStoredFrames() const {
  return frames_.size() - zombie_count_;
}

void PacketStorage::StoreFrame(uint32_t frame_id,
                               const SendPacketVector& packets) {
  if (packets.empty()) {
    PP_NOTREACHED();
    return;
  }

  if (frames_.empty()) {
    first_frame_id_in_list_ = frame_id;
  } else {
    // Make sure frame IDs are consecutive.
    PP_DCHECK((first_frame_id_in_list_ +
               static_cast<uint32_t>(frames_.size())) == frame_id);

    while (frames_.size() >= static_cast<size_t>(kMaxUnackedFrames)) {
      frames_.front().clear();
      frames_.pop_front();
      ++first_frame_id_in_list_;
    }
    // Make sure we aren't being asked to store more frames than the system's
    // design limit.
    PP_DCHECK(frames_.size() < static_cast<size_t>(kMaxUnackedFrames));
  }

  // Save new frame to the end of the list.
  frames_.push_back(packets);
}

void PacketStorage::ReleaseFrame(uint32_t frame_id) {
  const uint32_t offset = frame_id - first_frame_id_in_list_;
  if (static_cast<int32_t>(offset) < 0 || offset >= frames_.size() ||
      frames_[offset].empty()) {
    return;
  }

  frames_[offset].clear();
  ++zombie_count_;

  while (!frames_.empty() && frames_.front().empty()) {
    PP_DCHECK(zombie_count_ > 0u);
    --zombie_count_;
    frames_.pop_front();
    ++first_frame_id_in_list_;
  }
}

const SendPacketVector* PacketStorage::GetFrame32(uint32_t frame_id) const {
  uint32_t index = frame_id - first_frame_id_in_list_;
  if (index >= frames_.size()) return NULL;
  const SendPacketVector& packets = frames_[index];
  return packets.empty() ? NULL : &packets;
}

}  // namespace sharer
