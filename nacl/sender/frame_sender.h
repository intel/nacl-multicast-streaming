// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SENDER_FRAME_SENDER_H_
#define SENDER_FRAME_SENDER_H_

#include "base/macros.h"
#include "base/time/default_tick_clock.h"
#include "net/sharer_transport_config.h"
#include "net/rtcp/rtcp_defines.h"

#include "ppapi/utility/completion_callback_factory.h"

#include <memory>

namespace sharer {

class TransportSender;
class CongestionControl;

class FrameSender {
 public:
  explicit FrameSender(base::TickClock* clock, bool is_audio,
                       TransportSender* const transport_sender,
                       int rtp_timebase, uint32_t ssrc, double max_frame_rate,
                       base::TimeDelta min_playout_delay,
                       base::TimeDelta max_playout_delay,
                       CongestionControl* congestion_control);
  virtual ~FrameSender();

  int rtp_timebase() const { return rtp_timebase_; }

  void SetTargetPlayoutDelay(base::TimeDelta new_target_playout_delay);

  base::TimeDelta GetTargetPlayoutDelay() const {
    return target_playout_delay_;
  }

  void SendEncodedFrame(std::shared_ptr<EncodedFrame> encoded_frame);

 private:
  base::TimeDelta GetAllowedInFlightMediaDuration() const;
  base::TickClock* clock_;
  pp::CompletionCallbackFactory<FrameSender> callback_factory_;

 protected:
  virtual int GetNumberOfFramesInEncoder() const = 0;
  virtual base::TimeDelta GetInFlightMediaDuration() const = 0;
  virtual void OnAck(uint32_t frame_id) = 0;

  void OnReceivedSharerFeedback(const RtcpSharerMessage& sharer_feedback);
  void ScheduleNextRtcpReport();
  void SendRtcpPauseResume();
  void SendRtcpReport(int32_t result, bool schedule_future_reports);
  void OnMeasuredRoundTripTime(base::TimeDelta rtt);

  int GetUnacknowledgedFrameCount() const;
  base::TimeTicks GetRecordedReferenceTime(uint32_t frame_id) const;
  bool ShouldDropNextFrame(base::TimeDelta frame_duration) const;

  TransportSender* const transport_sender_;

  const uint32_t ssrc_;

 private:
  void ScheduleNextResendCheck();
  void ResendCheck(int32_t result);
  void ResendForKickstart();

  void RecordLatestFrameTimestamps(uint32_t frame_id,
                                   base::TimeTicks reference_time,
                                   RtpTimestamp rtp_timestamp);
  RtpTimestamp GetRecordedRtpTimestamp(uint32_t frame_id) const;

  bool send_target_playout_delay_;

  int num_aggressive_rtcp_reports_sent_;

  base::TimeTicks last_send_time_;
  uint32_t last_sent_frame_id_;
  uint32_t local_pause_id_;

 protected:
  base::TimeDelta target_playout_delay_;
  base::TimeDelta min_playout_delay_;
  base::TimeDelta max_playout_delay_;

  double max_frame_rate_;
  /* uint32_t latest_acked_frame_id_; */

  base::TimeDelta current_round_trip_time_;

 private:
  /* int duplicate_ack_counter_; */

  /* std::unique_ptr<CongestionControl> congestion_control_; */

  std::unique_ptr<CongestionControl> congestion_control_;

  const int rtp_timebase_;

  const bool is_audio_;

  base::TimeTicks frame_reference_times_[256];
  RtpTimestamp frame_rtp_timestamps_[256];

  DISALLOW_COPY_AND_ASSIGN(FrameSender);
};

}  // namespace sharer

#endif  // SENDER_FRAME_SENDER_H_
