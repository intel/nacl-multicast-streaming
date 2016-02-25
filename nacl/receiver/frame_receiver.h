// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _FRAME_RECEIVER_
#define _FRAME_RECEIVER_

#include "base/time/time.h"
#include "net/rtcp/rtcp.h"
#include "net/rtp/receiver_stats.h"
#include "net/rtp/rtp_receiver_defines.h"
#include "sharer_environment.h"

#include "ppapi/utility/completion_callback_factory.h"

#include <array>
#include <functional>
#include <memory>
#include <queue>
#include <utility>
#include <vector>

struct EncodedFrame;
struct ReceiverConfig;
class Framer;
class RTPBase;
class RTP;

using Packet = std::vector<uint8_t>;
using ReceiveEncodedFrameCallback =
    std::function<void(std::shared_ptr<EncodedFrame>)>;
using OnNetworkTimeoutCallback = std::function<void(void)>;

class FrameReceiver : public RtpPayloadFeedback {
 public:
  FrameReceiver(sharer::SharerEnvironment* env, const ReceiverConfig& config,
                UDPSender* transport);
  ~FrameReceiver();

  void RequestEncodedFrame(const ReceiveEncodedFrameCallback& callback);
  bool ProcessPacket(std::unique_ptr<RTPBase> packet);
  void SharerFeedback(const RtcpSharerMessage& sharer_feedback) override;
  void SetOnNetworkTimeout(const OnNetworkTimeoutCallback& callback);
  void FlushFrames();
  void SendPausedIndication(int last_frame, int pause_id);
  int getLastFrameAck();

 private:
  void ProcessParsedPacket(std::unique_ptr<RTP> packet);
  void ScheduleNextRtcpReport();
  void SendNextRtcpReport(int result);
  void ScheduleNextSharerMessage();
  void SendNextSharerMessage(int result);
  void EmitOneFrame(int result, const ReceiveEncodedFrameCallback& callback,
                    std::shared_ptr<EncodedFrame> encoded_frame) const;
  void EmitAvailableEncodedFrames();
  void EmitAvailableEncodedFramesAfterWaiting(int result);

  base::TimeTicks GetPlayoutTime(const EncodedFrame& frame) const;

  void CheckNetworkTimeout(const base::TimeTicks& now);

  const int rtp_timebase_;
  base::TimeDelta target_playout_delay_;
  const base::TimeDelta expected_frame_duration_;

  pp::CompletionCallbackFactory<FrameReceiver> callback_factory_;

  sharer::SharerEnvironment* const env_;  // non-owning pointer
  RtcpHandler rtcp_;
  ReceiverStats stats_;

  bool reports_are_scheduled_;

  std::unique_ptr<Framer> framer_;  // Use pointer so we can forward declaration

  std::queue<ReceiveEncodedFrameCallback> frame_request_queue_;

  bool is_waiting_for_consecutive_frame_;

  std::array<RtpTimestamp, 256> frame_id_to_rtp_timestamp_;

  RtpTimestamp lip_sync_rtp_timestamp_;
  base::TimeTicks lip_sync_reference_time_;
  ClockDriftSmoother lip_sync_drift_;

  OnNetworkTimeoutCallback on_network_timeout_;
  int network_timeouts_count_;
  base::TimeTicks last_received_time_;
  int last_frame_id_;
  /* uint32_t senderSsrc_; */
  /* uint32_t receiverSsrc_; */
};

#endif  // _FRAME_RECEIVER_
