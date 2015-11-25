// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sender/frame_sender.h"

#include "base/logger.h"
#include "net/transport_sender.h"
#include "sender/congestion_control.h"
#include "sharer_defines.h"

#include "ppapi/cpp/logging.h"

namespace sharer {

namespace {

const int kNumAggressiveReportsSentAtStart = 100;

// The additional number of frames that can be in-flight when input exceeds the
// maximum frame rate.
const int kMaxFrameBurst = 5;

}  // namespace

// Convenience macro used in logging statements throughout this file.
#define SENDER_SSRC (is_audio_ ? "AUDIO[" : "VIDEO[") << ssrc_ << "] "

FrameSender::FrameSender(base::TickClock* clock, bool is_audio,
                         TransportSender* const transport_sender,
                         int rtp_timebase, uint32_t ssrc, double max_frame_rate,
                         base::TimeDelta min_playout_delay,
                         base::TimeDelta max_playout_delay,
                         CongestionControl* congestion_control)
    : clock_(clock),
      callback_factory_(this),
      transport_sender_(transport_sender),
      ssrc_(ssrc),
      send_target_playout_delay_(false),
      num_aggressive_rtcp_reports_sent_(0),
      last_sent_frame_id_(0),
      min_playout_delay_(min_playout_delay == base::TimeDelta()
                             ? max_playout_delay
                             : min_playout_delay),
      max_playout_delay_(max_playout_delay),
      max_frame_rate_(max_frame_rate),
      congestion_control_(congestion_control),
      rtp_timebase_(rtp_timebase),
      is_audio_(is_audio) {
  PP_DCHECK(transport_sender_);
  PP_DCHECK(rtp_timebase_ > 0);
  SetTargetPlayoutDelay(min_playout_delay_);
  send_target_playout_delay_ = false;
  memset(frame_rtp_timestamps_, 0, sizeof(frame_rtp_timestamps_));
}

FrameSender::~FrameSender() {}

void FrameSender::ScheduleNextRtcpReport() {
  auto cb = callback_factory_.NewCallback(&FrameSender::SendRtcpReport, true);
  pp::Module::Get()->core()->CallOnMainThread(kDefaultRtcpIntervalMs, cb);
}

void FrameSender::SendRtcpPauseResume() {
  DINF() << "Sending RTCP Pause Resume...";

  PP_DCHECK(!last_send_time_.is_null());

  transport_sender_->SendSenderPauseResume(ssrc_, last_sent_frame_id_,
                                           local_pause_id_);
  local_pause_id_ = (++local_pause_id_) % 65536;

  ScheduleNextRtcpReport();
}

void FrameSender::SendRtcpReport(int32_t result, bool schedule_future_reports) {
  // Sanity-check: We should have sent at least the first frame by this point.
  PP_DCHECK(!last_send_time_.is_null());

  // Create lip-sync info for the sender report.  The last sent frame's
  // reference time and RTP timestamp are used to estimate an RTP timestamp in
  // terms of "now."  Note that |now| is never likely to be precise to an exact
  // frame boundary; and so the computation here will result in a
  // |now_as_rtp_timestamp| value that is rarely equal to any one emitted by the
  // encoder.
  const base::TimeTicks now = clock_->NowTicks();
  const base::TimeDelta time_delta =
      now - GetRecordedReferenceTime(last_sent_frame_id_);
  const int64_t rtp_delta = TimeDeltaToRtpDelta(time_delta, rtp_timebase_);
  const uint32_t now_as_rtp_timestamp =
      GetRecordedRtpTimestamp(last_sent_frame_id_) +
      static_cast<uint32_t>(rtp_delta);
  transport_sender_->SendSenderReport(ssrc_, now, now_as_rtp_timestamp);

  if (schedule_future_reports) ScheduleNextRtcpReport();
}

void FrameSender::OnMeasuredRoundTripTime(base::TimeDelta rtt) {
  PP_DCHECK(rtt > base::TimeDelta());
  current_round_trip_time_ = rtt;
}

void FrameSender::SetTargetPlayoutDelay(
    base::TimeDelta new_target_playout_delay) {
  if (send_target_playout_delay_ &&
      target_playout_delay_ == new_target_playout_delay) {
    return;
  }
  new_target_playout_delay =
      std::max(new_target_playout_delay, min_playout_delay_);
  new_target_playout_delay =
      std::min(new_target_playout_delay, max_playout_delay_);
  DINF() << SENDER_SSRC << "Target playout delay changing from "
         << target_playout_delay_.InMilliseconds() << " ms to "
         << new_target_playout_delay.InMilliseconds() << " ms.";
  target_playout_delay_ = new_target_playout_delay;
  send_target_playout_delay_ = true;
  congestion_control_->UpdateTargetPlayoutDelay(target_playout_delay_);
}

void FrameSender::ResendForKickstart() {
  PP_DCHECK(!last_send_time_.is_null());
  DINF() << SENDER_SSRC << "Resending last packet of frame "
         << last_sent_frame_id_ << " to kick-start.";
  last_send_time_ = clock_->NowTicks();
  transport_sender_->ResendFrameForKickstart(ssrc_, last_sent_frame_id_);
}

void FrameSender::RecordLatestFrameTimestamps(uint32_t frame_id,
                                              base::TimeTicks reference_time,
                                              RtpTimestamp rtp_timestamp) {
  PP_DCHECK(!reference_time.is_null());
  frame_reference_times_[frame_id % arraysize(frame_reference_times_)] =
      reference_time;
  frame_rtp_timestamps_[frame_id % arraysize(frame_rtp_timestamps_)] =
      rtp_timestamp;
}

base::TimeTicks FrameSender::GetRecordedReferenceTime(uint32_t frame_id) const {
  return frame_reference_times_[frame_id % arraysize(frame_reference_times_)];
}

RtpTimestamp FrameSender::GetRecordedRtpTimestamp(uint32_t frame_id) const {
  return frame_rtp_timestamps_[frame_id % arraysize(frame_rtp_timestamps_)];
}

base::TimeDelta FrameSender::GetAllowedInFlightMediaDuration() const {
  // The total amount allowed in-flight media should equal the amount that fits
  // within the entire playout delay window, plus the amount of time it takes to
  // receive an ACK from the receiver.
  // TODO(miu): Research is needed, but there is likely a better formula.
  return target_playout_delay_ + (current_round_trip_time_ / 2);
}

void FrameSender::SendEncodedFrame(
    std::shared_ptr<EncodedFrame> encoded_frame) {
  /* DINF() << SENDER_SSRC << "About to send another frame: last_sent=" */
  /*        << last_sent_frame_id_; //  << ", latest_acked=" <<
   * latest_acked_frame_id_; */

  const uint32_t frame_id = encoded_frame->frame_id;

  /* const bool is_first_frame_to_be_sent = last_send_time_.is_null(); */
  last_send_time_ = clock_->NowTicks();
  last_sent_frame_id_ = frame_id;

  /* VLOG_IF(1, !is_audio_ && encoded_frame->dependency == EncodedFrame::KEY) */
  /*     << SENDER_SSRC << "Sending encoded key frame, id=" << frame_id; */

  RecordLatestFrameTimestamps(frame_id, encoded_frame->reference_time,
                              encoded_frame->rtp_timestamp);

  if (!is_audio_) {
    // Used by chrome/browser/extension/api/sharer_streaming/performance_test.cc
    /* TRACE_EVENT_INSTANT1( */
    /*     "sharer_perf_test", "VideoFrameEncoded", */
    /*     TRACE_EVENT_SCOPE_THREAD, */
    /*     "rtp_timestamp", encoded_frame->rtp_timestamp); */
  }

  // At the start of the session, it's important to send reports before each
  // frame so that the receiver can properly compute playout times.  The reason
  // more than one report is sent is because transmission is not guaranteed,
  // only best effort, so send enough that one should almost certainly get
  // through.
  if (num_aggressive_rtcp_reports_sent_ < kNumAggressiveReportsSentAtStart) {
    // SendRtcpReport() will schedule future reports to be made if this is the
    // last "aggressive report."
    ++num_aggressive_rtcp_reports_sent_;
    const bool is_last_aggressive_report =
        (num_aggressive_rtcp_reports_sent_ == kNumAggressiveReportsSentAtStart);
    /* VLOG_IF(1, is_last_aggressive_report) */
    /*     << SENDER_SSRC << "Sending last aggressive report."; */
    SendRtcpReport(PP_OK, is_last_aggressive_report);
  }

  congestion_control_->SendFrameToTransport(
      frame_id, encoded_frame->data.size() * 8, last_send_time_);

  if (send_target_playout_delay_) {
    encoded_frame->new_playout_delay_ms =
        target_playout_delay_.InMilliseconds();
  }
  transport_sender_->InsertFrame(ssrc_, *encoded_frame);
}

void FrameSender::OnReceivedSharerFeedback(
    const RtcpSharerMessage& sharer_feedback) {
  const bool have_valid_rtt = current_round_trip_time_ > base::TimeDelta();
  if (have_valid_rtt) {
    congestion_control_->UpdateRtt(current_round_trip_time_);

    // Having the RTT value implies the receiver sent back a receiver report
    // based on it having received a report from here.  Therefore, ensure this
    // sender stops aggressively sending reports.
    if (num_aggressive_rtcp_reports_sent_ < kNumAggressiveReportsSentAtStart) {
      DINF() << SENDER_SSRC
             << "No longer a need to send reports aggressively (sent "
             << num_aggressive_rtcp_reports_sent_ << ").";
      num_aggressive_rtcp_reports_sent_ = kNumAggressiveReportsSentAtStart;
      ScheduleNextRtcpReport();
    }
  }

  if (last_send_time_.is_null())
    return;  // Cannot get an ACK without having first sent a frame.
}

bool FrameSender::ShouldDropNextFrame(base::TimeDelta frame_duration) const {
  // Check that accepting the next frame won't cause more frames to become
  // in-flight than the system's design limit.
  const int count_frames_in_flight = GetNumberOfFramesInEncoder();
  /* GetUnacknowledgedFrameCount() + GetNumberOfFramesInEncoder(); */
  if (count_frames_in_flight >= kMaxUnackedFrames) {
    DWRN() << SENDER_SSRC << "Dropping: Too many frames would be in-flight."
           << ", in Encoder: " << GetNumberOfFramesInEncoder();
    return true;
  }

  // Check that accepting the next frame won't exceed the configured maximum
  // frame rate, allowing for short-term bursts.
  base::TimeDelta duration_in_flight = GetInFlightMediaDuration();
  const double max_frames_in_flight =
      max_frame_rate_ * duration_in_flight.InSecondsF();
  if (count_frames_in_flight >= max_frames_in_flight + kMaxFrameBurst) {
    DWRN() << SENDER_SSRC << "Dropping: Burst threshold would be exceeded.";
    return true;
  }

  // Check that accepting the next frame won't exceed the allowed in-flight
  // media duration.
  const base::TimeDelta duration_would_be_in_flight =
      duration_in_flight + frame_duration;
  const base::TimeDelta allowed_in_flight = GetAllowedInFlightMediaDuration();
  if (duration_would_be_in_flight > allowed_in_flight) {
    DWRN() << SENDER_SSRC << "Dropping: In-flight duration would be too high: "
           << duration_in_flight.InMilliseconds() << "ms in flight + "
           << frame_duration.InMilliseconds() << "ms frame duration, "
           << GetNumberOfFramesInEncoder() << " frames in encoder.";
    return true;
  }

  // Next frame is accepted.
  return false;
}

}  // namespace sharer
