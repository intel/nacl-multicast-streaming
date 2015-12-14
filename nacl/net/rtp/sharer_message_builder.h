// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _CAST_MESSAGE_BUILDER_H_
#define _CAST_MESSAGE_BUILDER_H_

#include "net/rtcp/rtcp.h"
#include "net/rtp/rtp_receiver_defines.h"

#include "ppapi/c/pp_time.h"

#include <deque>
#include <map>

class Framer;
class RtpPayloadFeedback;

using TimeLastNackMap = std::map<uint32_t, base::TimeTicks>;

class SharerMessageBuilder {
 public:
  SharerMessageBuilder(sharer::SharerEnvironment* env,
                       RtpPayloadFeedback* incoming_payload_feedback,
                       const Framer* framer, uint32_t media_ssrc,
                       bool decoder_faster_than_max_frame_rate,
                       int max_unacked_frames);
  ~SharerMessageBuilder();

  void CompleteFrameReceived(uint32_t frame_id);
  bool TimeToSendNextSharerMessage(base::TimeTicks* time_to_send);
  void UpdateSharerMessage();
  void Reset();
  void Reset(uint32_t frame_id);

 private:
  bool UpdateAckMessage(uint32_t frame_id);
  void BuildPacketList();
  bool UpdateSharerMessageInternal(RtcpSharerMessage* message);

  sharer::SharerEnvironment* const env_;
  RtpPayloadFeedback* const sharer_feedback_;

  // SharerMessageBuilder has only const access to the framer.
  const Framer* const framer_;
  const uint32_t media_ssrc_;
  /* const bool decoder_faster_than_max_frame_rate_; */
  /* const int max_unacked_frames_; */

  RtcpSharerMessage sharer_msg_;
  base::TimeTicks last_update_time_;

  TimeLastNackMap time_last_nacked_map_;

  /* bool slowing_down_ack_; */
  /* bool acked_last_frame_; */
  uint32_t last_completed_frame_id_;
  std::deque<uint32_t> ack_queue_;
};

#endif  // _CAST_MESSAGE_BUILDER_H_
