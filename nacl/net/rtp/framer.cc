// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/rtp/framer.h"

#include "base/logger.h"
#include "base/ptr_utils.h"
#include "net/rtp/sharer_message_builder.h"
#include "net/rtp/frame_buffer.h"
#include "net/rtp/rtp.h"
#include "net/sharer_transport_config.h"
#include "sharer_defines.h"

static const uint32_t kOldFrameThreshold = 120;

Framer::Framer(sharer::SharerEnvironment* env,
               RtpPayloadFeedback* incoming_payload_feedback, uint32_t ssrc,
               bool decoder_faster_than_max_frame_rate, int max_unacked_frames)
    : decoder_faster_than_max_frame_rate_(decoder_faster_than_max_frame_rate),
      sharer_msg_builder_(make_unique<SharerMessageBuilder>(
          env, incoming_payload_feedback, this, ssrc,
          decoder_faster_than_max_frame_rate, max_unacked_frames)),
      waiting_for_key_(true),
      last_released_frame_(sharer::kStartFrameId),
      last_key_frame_received_(sharer::kStartFrameId),
      newest_frame_id_(sharer::kStartFrameId) {}

Framer::~Framer() {}

void Framer::ResetMsgBuilder() {
  sharer_msg_builder_->Reset(last_released_frame_);
}

bool Framer::InsertPacket(std::unique_ptr<RTP> packet, bool* duplicate) {
  *duplicate = false;
  uint32_t frame_id = packet->frameId();
  uint16_t packet_id = packet->packetId();

  if (IsOlderFrameId(last_released_frame_ + kOldFrameThreshold, frame_id)) {
    DWRN() << ">>> Last frame id: " << frame_id
           << ", last released: " << last_released_frame_
           << ", last key: " << last_key_frame_received_;
    if (IsOlderFrameId(last_key_frame_received_ + kOldFrameThreshold,
                       frame_id)) {
      waiting_for_key_ = true;
    } else {
      last_released_frame_ = last_key_frame_received_;
      sharer_msg_builder_->Reset(last_released_frame_);
    }
  }

  if (packet->isKeyFrame()) {
    if (IsNewerFrameId(frame_id, last_key_frame_received_))
      last_key_frame_received_ = frame_id;

    if (waiting_for_key_) {
      waiting_for_key_ = false;
      last_released_frame_ = frame_id - 1;
      sharer_msg_builder_->Reset(last_released_frame_);
    }
  }

  if (IsOlderFrameId(frame_id, last_released_frame_) && !waiting_for_key_) {
    // Packet is too old
    return false;
  }

  // Update the last received frame id
  if (IsNewerFrameId(frame_id, newest_frame_id_)) {
    newest_frame_id_ = frame_id;
  }

  // Does this packet belong to a new frame?
  auto it = frames_.find(frame_id);
  if (it == frames_.end()) {
    // New frame
    auto frame_info = std::make_shared<FrameBuffer>();
    auto retval = frames_.insert(std::make_pair(frame_id, frame_info));
    it = retval.first;
  }

  // Insert packet
  if (!it->second->InsertPacket(std::move(packet))) {
    DINF() << "Packet: " << packet_id << ", for frame: " << frame_id
           << " already received. Ignored.";
    *duplicate = true;
    return false;
  }

  return it->second->Complete();
}

bool Framer::GetEncodedFrame(EncodedFrame* frame, bool* next_frame,
                             bool* have_multiple_decodable_frames) {
  *have_multiple_decodable_frames = HaveMultipleDecodableFrames();

  uint32_t frame_id;
  // Find frame id
  if (NextContinuousFrame(&frame_id)) {
    // We have our next frame
    *next_frame = true;
  } else {
    if (!decoder_faster_than_max_frame_rate_) {
      return false;
    }

    if (!NextFrameAllowingSkippingFrames(&frame_id)) {
      return false;
    }
    *next_frame = false;
  }

  auto it = frames_.find(frame_id);
  if (it == frames_.end()) return false;

  return it->second->AssembleEncodedFrame(frame);
}

bool Framer::Empty() const { return frames_.empty(); }

int Framer::NumberOfCompleteFrames() const {
  int count = 0;
  for (auto it = frames_.begin(); it != frames_.end(); ++it) {
    if (it->second->Complete()) {
      ++count;
    }
  }
  return count;
}

bool Framer::FrameExists(uint32_t frame_id) const {
  return frames_.end() != frames_.find(frame_id);
}

uint32_t Framer::NewestFrameId() const { return newest_frame_id_; }

void Framer::GetMissingPackets(uint32_t frame_id, bool last_frame,
                               PacketIdSet* missing_packets) const {
  auto it = frames_.find(frame_id);
  if (it == frames_.end()) return;

  it->second->GetMissingPackets(last_frame, missing_packets);
}

bool Framer::NextContinuousFrame(uint32_t* frame_id) const {
  for (auto it = frames_.begin(); it != frames_.end(); ++it) {
    if (it->second->Complete() && ContinuousFrame(*it->second)) {
      *frame_id = it->first;
      return true;
    }
  }
  return false;
}

bool Framer::HaveMultipleDecodableFrames() const {
  // Find the oldest decodable frame
  bool found_one = false;
  for (auto it = frames_.begin(); it != frames_.end(); ++it) {
    if (it->second->Complete() && DecodableFrame(*it->second)) {
      if (found_one) {
        return true;
      } else {
        found_one = true;
      }
    }
  }
  return false;
}

bool Framer::NextFrameAllowingSkippingFrames(uint32_t* frame_id) const {
  auto it_best_match = frames_.end();
  for (auto it = frames_.begin(); it != frames_.end(); ++it) {
    if (it->second->Complete() && DecodableFrame(*it->second)) {
      if (it_best_match == frames_.end() ||
          IsOlderFrameId(it->first, it_best_match->first)) {
        it_best_match = it;
      }
    }
  }
  if (it_best_match == frames_.end()) return false;

  *frame_id = it_best_match->first;
  return true;
}

void Framer::AckFrame(uint32_t frame_id) {
  sharer_msg_builder_->CompleteFrameReceived(frame_id);
}

void Framer::ReleaseFrame(uint32_t frame_id) {
  frames_.erase(frame_id);

  // We have a frame - remove all frames with lower frame id
  bool skipped_old_frame = false;
  for (auto it = frames_.begin(); it != frames_.end();) {
    if (IsOlderFrameId(it->first, frame_id)) {
      frames_.erase(it++);
      skipped_old_frame = true;
    } else {
      ++it;
    }
  }

  last_released_frame_ = frame_id;

  if (skipped_old_frame) {
    sharer_msg_builder_->UpdateSharerMessage();
  }
}

void Framer::Reset() {
  waiting_for_key_ = true;
  last_released_frame_ = sharer::kStartFrameId;
  newest_frame_id_ = sharer::kStartFrameId;
  frames_.clear();
  sharer_msg_builder_->Reset();
}

bool Framer::TimeToSendNextSharerMessage(base::TimeTicks* time_to_send) {
  return sharer_msg_builder_->TimeToSendNextSharerMessage(time_to_send);
}

void Framer::SendSharerMessage() { sharer_msg_builder_->UpdateSharerMessage(); }

bool Framer::ContinuousFrame(const FrameBuffer& frame) const {
  if (waiting_for_key_ && !frame.is_key_frame()) {
    return false;
  }

  return static_cast<uint32_t>(last_released_frame_ + 1) == frame.frame_id();
}

bool Framer::DecodableFrame(const FrameBuffer& frame) const {
  if (frame.is_key_frame()) return true;

  if (waiting_for_key_ && !frame.is_key_frame()) return false;

  if (frame.last_referenced_frame_id() == frame.frame_id()) return true;

  if (IsOlderFrameId(frame.last_referenced_frame_id(), last_released_frame_))
    return true;

  return frame.last_referenced_frame_id() == last_released_frame_;
}
