// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _FRAMER_H_
#define _FRAMER_H_

#include "base/time/time.h"
#include "net/rtp/rtp_receiver_defines.h"
#include "sharer_environment.h"

#include <map>
#include <memory>

class SharerMessageBuilder;
class RTP;
class FrameBuffer;

struct EncodedFrame;

using FrameList = std::map<uint32_t, std::shared_ptr<FrameBuffer> >;

class Framer {
 public:
  Framer(sharer::SharerEnvironment* env, RtpPayloadFeedback* incoming_payload_feedback,
         uint32_t ssrc, bool decoder_faster_than_max_frame_rate,
         int max_unacked_frames);
  ~Framer();

  bool InsertPacket(std::unique_ptr<RTP> packet, bool* duplicate);
  bool GetEncodedFrame(EncodedFrame* frame, bool* next_frame,
                       bool* have_multiple_decodable_frames);

  bool Empty() const;
  bool FrameExists(uint32_t frame_id) const;
  uint32_t NewestFrameId() const;

  int NumberOfCompleteFrames() const;

  bool NextContinuousFrame(uint32_t* frame_id) const;

  bool NextFrameAllowingSkippingFrames(uint32_t* frame_id) const;
  bool HaveMultipleDecodableFrames() const;

  void AckFrame(uint32_t frame_id);
  void ReleaseFrame(uint32_t frame_id);

  void Reset();
  bool TimeToSendNextSharerMessage(base::TimeTicks* time_to_send);
  void SendSharerMessage();

  void GetMissingPackets(uint32_t frame_id, bool last_frame,
                         PacketIdSet* missing_packets) const;
  void ResetMsgBuilder();
  bool IsWaitingForKey() const { return waiting_for_key_; }
  int GetFrame() const { return last_key_frame_received_; }
  int GetKeyFrame() const { return last_key_frame_received_; }

 private:
  bool ContinuousFrame(const FrameBuffer& frame) const;
  bool DecodableFrame(const FrameBuffer& frame) const;

  const bool decoder_faster_than_max_frame_rate_;

  FrameList frames_;

  std::unique_ptr<SharerMessageBuilder> sharer_msg_builder_;

  // use a pointer to allow forward declaration
  /* std::unique_ptr<SharerMessageBuilder> sharer_msg_builder_; */
  bool waiting_for_key_;
  uint32_t last_released_frame_;
  uint32_t last_key_frame_received_;
  uint32_t newest_frame_id_;
};

#endif  // _FRAMER_H_
