// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "receiver/frame_receiver.h"

#include "base/logger.h"
#include "base/ptr_utils.h"
#include "sharer_config.h"
#include "net/sharer_transport_config.h"
#include "net/rtp/framer.h"
#include "net/rtp/rtp.h"
#include "net/rtp/rtp_receiver_defines.h"

#include "ppapi/cpp/module.h"

static const int kMinSchedulingDelayMs = 1;
static const int kDefaultRtcpIntervalMs = 500;
static const int kMaxNetworkTimeoutMs = 2000;

static inline base::TimeDelta RtpDeltaToTimeDelta(int64_t rtp_delta,
                                                  int rtp_timebase) {
  PP_DCHECK(rtp_timebase >= 0);
  return rtp_delta * base::TimeDelta::FromSeconds(1) / rtp_timebase;
}

FrameReceiver::FrameReceiver(sharer::SharerEnvironment* env,
                             const ReceiverConfig& config, UDPSender* transport)
    /* : senderSsrc_(sender_ssrc) { */
    : rtp_timebase_(config.rtp_timebase),
      target_playout_delay_(
          base::TimeDelta::FromMilliseconds(config.rtp_max_delay_ms)),
      expected_frame_duration_(base::TimeDelta::FromSeconds(1) /
                               config.target_frame_rate),
      callback_factory_(this),
      env_(env),
      rtcp_(nullptr, nullptr, env_, transport, nullptr, config.receiver_ssrc,
            config.sender_ssrc),
      stats_(),
      reports_are_scheduled_(false),
      framer_(make_unique<Framer>(
          env_, this, config.sender_ssrc, true,
          config.rtp_max_delay_ms* config.target_frame_rate / 1000)),
      is_waiting_for_consecutive_frame_(false),
      lip_sync_drift_(ClockDriftSmoother::GetDefaultTimeConstant()),
      network_timeouts_count_(0) {}

FrameReceiver::~FrameReceiver() {}

void FrameReceiver::FlushFrames() {
  std::queue<ReceiveEncodedFrameCallback> empty;
  std::swap(frame_request_queue_, empty);
}

void FrameReceiver::RequestEncodedFrame(
    const ReceiveEncodedFrameCallback& callback) {
  frame_request_queue_.push(callback);
  EmitAvailableEncodedFrames();
}

void FrameReceiver::SetOnNetworkTimeout(
    const OnNetworkTimeoutCallback& callback) {
  on_network_timeout_ = callback;
}

bool FrameReceiver::ProcessPacket(std::unique_ptr<RTPBase> packet) {
  if (packet->isRTCP()) {
    // No way to convert from std::unique_ptr<RTPBase> to std::unique_ptr<RTP>,
    // so do this ugly hack.
    std::unique_ptr<RTCP> rtcp_packet(static_cast<RTCP*>(packet.release()));

    bool wait_sender = rtcp_.IncomingRtcpPacket(rtcp_packet);

    if (wait_sender && (rtcp_packet->payloadType() == RTCP::RTPFB)) {
      // TODO: handle paused content
    }
  } else {
    // No way to convert from std::unique_ptr<RTPBase> to std::unique_ptr<RTP>,
    // so do this ugly hack.
    std::unique_ptr<RTP> rtp_packet(static_cast<RTP*>(packet.release()));

    stats_.UpdateStatistics(*rtp_packet);
    ProcessParsedPacket(std::move(rtp_packet));
  }

  if (!reports_are_scheduled_) {
    ScheduleNextRtcpReport();
    ScheduleNextSharerMessage();
    reports_are_scheduled_ = true;
  }

  return true;
}

void FrameReceiver::ProcessParsedPacket(std::unique_ptr<RTP> packet) {
  uint16_t packet_id = packet->packetId();
  uint32_t frame_id = packet->frameId();
  RtpTimestamp timestamp = packet->timestamp();
  if (packet->isKeyFrame())
    DINF() << "Received key packet: " << frame_id << ":" << packet_id;
  else
    DINF() << "Received packet: " << frame_id << ":" << packet_id;

  const base::TimeTicks now = env_->clock()->NowTicks();

  last_received_time_ = now;
  network_timeouts_count_ = 0;

  frame_id_to_rtp_timestamp_[frame_id & 0xff] = packet->timestamp();

  bool duplicate = false;
  const bool complete = framer_->InsertPacket(std::move(packet), &duplicate);

  if (duplicate) return;

  if (packet_id == 0 || lip_sync_reference_time_.is_null()) {
    RtpTimestamp fresh_sync_rtp;
    base::TimeTicks fresh_sync_reference;
    if (!rtcp_.GetLatestLipSyncTimes(&fresh_sync_rtp, &fresh_sync_reference)) {
      WRN() << "Lip sync info missing. Falling-back to local clock.";
      fresh_sync_rtp = timestamp;
      fresh_sync_reference = now;
    }

    if (lip_sync_reference_time_.is_null()) {
      lip_sync_reference_time_ = fresh_sync_reference;
    } else {
      lip_sync_reference_time_ += RtpDeltaToTimeDelta(
          static_cast<int32_t>(fresh_sync_rtp - lip_sync_rtp_timestamp_),
          rtp_timebase_);
    }
    lip_sync_rtp_timestamp_ = fresh_sync_rtp;
    lip_sync_drift_.Update(now,
                           fresh_sync_reference - lip_sync_reference_time_);
  }

  if (complete) EmitAvailableEncodedFrames();
}

int FrameReceiver::getLastFrameAck() { return last_frame_id_; }

void FrameReceiver::ScheduleNextRtcpReport() {
  pp::CompletionCallback cc =
      callback_factory_.NewCallback(&FrameReceiver::SendNextRtcpReport);
  pp::Module::Get()->core()->CallOnMainThread(kDefaultRtcpIntervalMs, cc);
}

void FrameReceiver::CheckNetworkTimeout(const base::TimeTicks& now) {
  int timeout = kMaxNetworkTimeoutMs * (1 + network_timeouts_count_);
  base::TimeDelta delta = now - last_received_time_;
  if (delta > base::TimeDelta::FromMilliseconds(timeout)) {
    ERR() << "Not receiving network packets for " << delta.InMilliseconds()
          << " ms.";
    network_timeouts_count_ += network_timeouts_count_ < 5 ? 1 : 0;
    if (on_network_timeout_) on_network_timeout_();
  }
}

void FrameReceiver::SendNextRtcpReport(int result) {
  const base::TimeTicks now = env_->clock()->NowTicks();

  CheckNetworkTimeout(now);

  RtpReceiverStatistics stats = stats_.GetStatistics();
  rtcp_.SendRtcpFromRtpReceiver(rtcp_.ConvertToNTPAndSave(now), nullptr,
                                base::TimeDelta(), &stats);
  ScheduleNextRtcpReport();
}

void FrameReceiver::ScheduleNextSharerMessage() {
  base::TimeTicks send_time;
  framer_->TimeToSendNextSharerMessage(&send_time);

  base::TimeDelta time_to_send = send_time - env_->clock()->NowTicks();
  time_to_send = std::max(
      time_to_send, base::TimeDelta::FromMilliseconds(kMinSchedulingDelayMs));

  pp::CompletionCallback cc =
      callback_factory_.NewCallback(&FrameReceiver::SendNextSharerMessage);
  pp::Module::Get()->core()->CallOnMainThread(time_to_send.InMilliseconds(),
                                              cc);
}

void FrameReceiver::SendNextSharerMessage(int result) {
  framer_->SendSharerMessage();
  ScheduleNextSharerMessage();
}

void FrameReceiver::SendPausedIndication(int last_frame, int pause_id) {
  framer_->ResetMsgBuilder();
}

void FrameReceiver::SharerFeedback(const RtcpSharerMessage& sharer_message) {
  base::TimeTicks now = env_->clock()->NowTicks();

  rtcp_.SendRtcpFromRtpReceiver(rtcp_.ConvertToNTPAndSave(now), &sharer_message,
                                target_playout_delay_, nullptr);
}

void FrameReceiver::EmitAvailableEncodedFrames() {
  while (!frame_request_queue_.empty()) {
    // Right now we create a shared pointer because we will pass it to the
    // CompletionCallback, which doesn't support smart pointers. Parameters to
    // it are passed as copies, so a shared pointer will have no problem when
    // copied there.
    // FIXME: Change this to a unique ptr.
    auto encoded_frame = std::make_shared<EncodedFrame>();
    bool is_consecutively_next_frame = false;
    bool have_multiple_complete_frames = false;
    if (!framer_->GetEncodedFrame(encoded_frame.get(),
                                  &is_consecutively_next_frame,
                                  &have_multiple_complete_frames)) {
      return;
    }

    const base::TimeTicks now = env_->clock()->NowTicks();
    const base::TimeTicks playout_time = GetPlayoutTime(*encoded_frame);

    if (have_multiple_complete_frames && now > playout_time) {
      framer_->ReleaseFrame(encoded_frame->frame_id);
      continue;
    }

    if (!is_consecutively_next_frame) {
      const base::TimeTicks earliest_possible_end_time_of_missing_frame =
          now + expected_frame_duration_ * 2;
      if (earliest_possible_end_time_of_missing_frame < playout_time) {
        if (!is_waiting_for_consecutive_frame_) {
          is_waiting_for_consecutive_frame_ = true;
          pp::CompletionCallback cc = callback_factory_.NewCallback(
              &FrameReceiver::EmitAvailableEncodedFramesAfterWaiting);
          pp::Module::Get()->core()->CallOnMainThread(
              (playout_time - now).InMilliseconds(), cc);
        }
        return;
      }
    }

    last_frame_id_ = encoded_frame->frame_id;
    framer_->AckFrame(encoded_frame->frame_id);

    // TODO: Decrypt frame

    // At this point, we have a decrypted EncodedFrame ready to be emitted
    encoded_frame->reference_time = playout_time;
    framer_->ReleaseFrame(encoded_frame->frame_id);
    if (encoded_frame->new_playout_delay_ms) {
      target_playout_delay_ = base::TimeDelta::FromMilliseconds(
          encoded_frame->new_playout_delay_ms);
    }

    pp::CompletionCallback cc_emit_one = callback_factory_.NewCallback(
        &FrameReceiver::EmitOneFrame, frame_request_queue_.front(),
        encoded_frame);
    pp::Module::Get()->core()->CallOnMainThread(0, cc_emit_one);

    frame_request_queue_.pop();
  }
}

void FrameReceiver::EmitOneFrame(
    int result, const ReceiveEncodedFrameCallback& callback,
    std::shared_ptr<EncodedFrame> encoded_frame) const {
  callback(encoded_frame);
}

void FrameReceiver::EmitAvailableEncodedFramesAfterWaiting(int result) {
  is_waiting_for_consecutive_frame_ = false;
  EmitAvailableEncodedFrames();
}

base::TimeTicks FrameReceiver::GetPlayoutTime(const EncodedFrame& frame) const {
  base::TimeDelta target_playout_delay = target_playout_delay_;
  if (frame.new_playout_delay_ms) {
    target_playout_delay =
        base::TimeDelta::FromMilliseconds(frame.new_playout_delay_ms);
  }

  return lip_sync_reference_time_ + lip_sync_drift_.Current() +
         RtpDeltaToTimeDelta(static_cast<int32_t>(frame.rtp_timestamp -
                                                  lip_sync_rtp_timestamp_),
                             rtp_timebase_) +
         target_playout_delay;
}
