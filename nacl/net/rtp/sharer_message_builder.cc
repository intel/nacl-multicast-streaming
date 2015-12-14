// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/rtp/sharer_message_builder.h"

#include "base/logger.h"
#include "net/rtp/framer.h"
#include "sharer_defines.h"

#include "ppapi/cpp/module.h"

static const int64_t kSharerMessageUpdateIntervalMs = 33;
static const int64_t kNackRepeatIntervalMs = 30;

SharerMessageBuilder::SharerMessageBuilder(
    sharer::SharerEnvironment* env, RtpPayloadFeedback* incoming_payload_feedback,
    const Framer* framer, uint32_t media_ssrc,
    bool decoder_faster_than_max_frame_rate, int max_unacked_frames)
    : env_(env),
      sharer_feedback_(incoming_payload_feedback),
      framer_(framer),
      media_ssrc_(media_ssrc),
      /* decoder_faster_than_max_frame_rate_(decoder_faster_than_max_frame_rate),
         */
      /* max_unacked_frames_(max_unacked_frames), */
      sharer_msg_(media_ssrc),
      /* slowing_down_ack_(false), */
      /* acked_last_frame_(true), */
      last_completed_frame_id_(sharer::kStartFrameId) {
  sharer_msg_.ack_frame_id = sharer::kStartFrameId;
}

SharerMessageBuilder::~SharerMessageBuilder() {}

void SharerMessageBuilder::CompleteFrameReceived(uint32_t frame_id) {
  PP_DCHECK(static_cast<int32_t>(frame_id - last_completed_frame_id_) >= 0);
  if (last_update_time_.is_null()) {
    // Our first update.
    last_update_time_ = env_->clock()->NowTicks();
  }

  if (!UpdateAckMessage(frame_id)) {
    return;
  }
}

bool SharerMessageBuilder::UpdateAckMessage(uint32_t frame_id) {
  // Is it a new frame?
  if (last_completed_frame_id_ == frame_id) {
    return false;
  }

  // If we have nacked this frame before, remove it from the nacked map
  time_last_nacked_map_.erase(frame_id);

  /* acked_last_frame_ = true; */
  last_completed_frame_id_ = frame_id;
  sharer_msg_.ack_frame_id = last_completed_frame_id_;  // not used anymore
  sharer_msg_.missing_frames_and_packets.clear();
  last_update_time_ = env_->clock()->NowTicks();
  return true;
}

bool SharerMessageBuilder::TimeToSendNextSharerMessage(
    base::TimeTicks* time_to_send) {
  // We haven't received any packets.
  if (last_update_time_.is_null() && framer_->Empty()) return false;

  *time_to_send = last_update_time_ + base::TimeDelta::FromMilliseconds(
                                          kSharerMessageUpdateIntervalMs);
  return true;
}

void SharerMessageBuilder::UpdateSharerMessage() {
  RtcpSharerMessage message(media_ssrc_);
  if (!UpdateSharerMessageInternal(&message)) return;

  // Do not send cast message if no packet is missing
  if (message.missing_frames_and_packets.empty()) return;

  // Send cast message.
  sharer_feedback_->SharerFeedback(message);
}

void SharerMessageBuilder::Reset() {
  /* sharer_msg_.ack_frame_id = sharer::kStartFrameId; */
  sharer_msg_.missing_frames_and_packets.clear();
  time_last_nacked_map_.clear();
}

void SharerMessageBuilder::Reset(uint32_t frame_id) {
  sharer_msg_.ack_frame_id = frame_id;
  sharer_msg_.missing_frames_and_packets.clear();

  // TODO: Only clear nacks older than frame_id.
  // TODO: Confirm that we are not leaking some nacks on this map.
  time_last_nacked_map_.clear();
}

bool SharerMessageBuilder::UpdateSharerMessageInternal(
    RtcpSharerMessage* message) {
  if (last_update_time_.is_null()) {
    if (!framer_->Empty()) {
      // We have received packets.
      last_update_time_ = env_->clock()->NowTicks();
    }
    return false;
  }
  // Is it time to update the cast message?
  base::TimeTicks now = env_->clock()->NowTicks();
  if (now - last_update_time_ <
      base::TimeDelta::FromMilliseconds(kSharerMessageUpdateIntervalMs)) {
    return false;
  }
  last_update_time_ = now;

  // Needed to cover when a frame is skipped.
  /* UpdateAckMessage(last_completed_frame_id_); */
  BuildPacketList();
  *message = sharer_msg_;
  return true;
}

void SharerMessageBuilder::BuildPacketList() {
  base::TimeTicks now = env_->clock()->NowTicks();

  // Clear message NACK list.
  sharer_msg_.missing_frames_and_packets.clear();

  // Are we missing packets?
  if (framer_->Empty()) return;

  sharer_msg_.request_key_frame = framer_->IsWaitingForKey();

  if (framer_->IsWaitingForKey()) {
    return;
  }

  uint32_t newest_frame_id = framer_->NewestFrameId();
  uint32_t next_expected_frame_id = sharer_msg_.ack_frame_id + 1;

  // Iterate over all frames.
  for (; !IsNewerFrameId(next_expected_frame_id, newest_frame_id);
       ++next_expected_frame_id) {
    auto it = time_last_nacked_map_.find(next_expected_frame_id);
    if (it != time_last_nacked_map_.end()) {
      // We have sent a NACK in this frame before, make sure enough time have
      // passed.
      if (now - it->second <
          base::TimeDelta::FromMilliseconds(kNackRepeatIntervalMs)) {
        continue;
      }
    }

    PacketIdSet missing;
    if (framer_->FrameExists(next_expected_frame_id)) {
      bool last_frame = (newest_frame_id == next_expected_frame_id);
      framer_->GetMissingPackets(next_expected_frame_id, last_frame, &missing);
      if (!missing.empty()) {
        time_last_nacked_map_[next_expected_frame_id] = now;
        sharer_msg_.missing_frames_and_packets.insert(
            std::make_pair(next_expected_frame_id, missing));
        DWRN() << "Requesting resend of " << missing.size()
               << " packets from frame: " << next_expected_frame_id;
      }
    } else {
      time_last_nacked_map_[next_expected_frame_id] = now;
      missing.insert(kRtcpSharerAllPacketsLost);
      sharer_msg_.missing_frames_and_packets[next_expected_frame_id] = missing;
      DWRN() << "Requesting resend of all packets from frame: "
             << next_expected_frame_id;
    }
  }
}
