// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sender/video_sender.h"

#include "base/logger.h"
#include "base/ptr_utils.h"
#include "net/sharer_transport_config.h"
#include "net/transport_sender.h"
#include "sender/congestion_control.h"
#include "sharer_defines.h"

static int32_t roundTo4(int32_t value) {
  int32_t rest = value % 4;
  return value - rest;
}

namespace sharer {

const int kRoundTripsNeeded = 4;
const int kConstantTimeMs = 75;

VideoSender::VideoSender(SharerEnvironment* env,
                         TransportSender* const transport_sender,
                         const SenderConfig& config, SharerSuccessCb cb,
                         PlayoutDelayChangeCb playout_delay_change_cb)
    : FrameSender(env->clock(), false, transport_sender, kVideoFrequency,
                  11, /* config.ssrc, */
                  config.frame_rate,
                  base::TimeDelta(), /* config.min_playout_delay, */
                  base::TimeDelta::FromMilliseconds(
                      kDefaultRtpMaxDelayMs), /* config.max_playout_delay, */
                  NewFixedCongestionControl(2000000)),
      env_(env),
      initialized_(false),
      initialized_cb_(cb),
      playout_delay_change_cb_(playout_delay_change_cb),
      factory_(this),
      frame_rate_(config.frame_rate),
      frames_in_encoder_(0),
      pause_delta_(0.1),
      querying_size_(false),
      skip_resize_(true),
      is_receiving_track_frames_(false),
      is_sending_(false) {
  encoder_ = make_unique<VideoEncoder>(env->instance(), config);

  auto sharer_feedback_cb =
      [this](const std::string& addr, const RtcpSharerMessage& sharer_message) {
    this->OnReceivedSharerFeedback(sharer_message);
  };

  auto rtt_cb =
      [this](base::TimeDelta rtt) { this->OnMeasuredRoundTripTime(rtt); };

  SharerTransportRtpConfig transport_config;
  transport_config.ssrc = 11;
  transport_config.feedback_ssrc = 12;
  transport_config.rtp_payload_type = 96;
  transport_sender->InitializeVideo(transport_config, sharer_feedback_cb,
                                    rtt_cb);

  initialized_ = true;
  cb(true);
}

VideoSender::~VideoSender() { DINF() << "Destroying VideoSender."; }

void VideoSender::SetSize(const pp::Size& size) {
  requested_size_ = size;
}

int VideoSender::GetNumberOfFramesInEncoder() const {
  return frames_in_encoder_;
}

base::TimeDelta VideoSender::GetInFlightMediaDuration() const {
  /* if (GetUnacknowledgedFrameCount() > 0) { */
  /*   const uint32_t oldest_unacked_frame_id = latest_acked_frame_id_ + 1; */
  /*   return last_reference_time_ - */
  /*       GetRecordedReferenceTime(oldest_unacked_frame_id); */
  /* } else { */
  return duration_in_encoder_;
  /* } */
}

void VideoSender::OnAck(uint32_t frame_id) {}

void VideoSender::StartSending(const pp::MediaStreamVideoTrack& video_track,
                               const SharerSuccessCb& cb) {
  if (!video_track_.is_null()) {
    ERR() << "Already sending or trying to send track.";
    cb(false);
    return;
  }

  if (!initialized_) {
    ERR() << "Did not initialize video sender yet. Can't start sending.";
    cb(false);
    return;
  }

  video_track_ = video_track;

  start_sending_cb_ = cb;

  ConfigureForFirstFrame();

  is_sending_ = true;
}

void VideoSender::StopSending(const SharerSuccessCb& cb) {
  if (!initialized_) {
    ERR() << "Did not initialize video sender yet. There is nothing to stop";
    return;
  }

  StopTrackFrames();
  encoder_->Stop();

  video_track_.Close();
  video_track_ = pp::MediaStreamVideoTrack();
  frames_in_encoder_ = 0;
  encoder_->FlushEncodedFrames();

  last_reference_time_ = base::TimeTicks();
  duration_in_encoder_ = duration_in_encoder_.FromInternalValue(0);
  DINF() << "Stopped sending frames.\n";
  is_sending_ = false;
  cb(true);
}

void VideoSender::ChangeEncoding(const SenderConfig& config) {
  DINF() << "Changing encoding";
  encoder_->ChangeEncoding(config);
}

void VideoSender::ConfigureForFirstFrame() {
  int32_t attrib_list[]{
      PP_MEDIASTREAMVIDEOTRACK_ATTRIB_FORMAT, encoder_->format(),
      PP_MEDIASTREAMVIDEOTRACK_ATTRIB_NONE};

  auto cc = factory_.NewCallback(&VideoSender::OnConfiguredForFirstFrame);
  video_track_.Configure(attrib_list, cc);
}

void VideoSender::OnConfiguredForFirstFrame(int32_t result) {
  if (result != PP_OK) {
    ERR() << "Could not configure video track: " << result;
    if (start_sending_cb_) start_sending_cb_(false);
    start_sending_cb_ = nullptr;
    return;
  }

  auto cc = factory_.NewCallbackWithOutput(&VideoSender::OnFirstFrame);
  video_track_.GetFrame(cc);
}

void VideoSender::OnFirstFrame(int32_t result, pp::VideoFrame frame) {
  // TODO: on aborted, stop everything related to encoding and sending frames
  if (result == PP_ERROR_ABORTED) return;

  if (result != PP_OK) {
    ERR() << "Cannot get frame from video track: " << result;
    return;
  }

  pp::Size size;
  if (!frame.GetSize(&size)) {
    ERR() << "Cannot get size of first frame.";
    return;
  }

  video_track_.RecycleFrame(frame);

  stream_size_ = size;

  if (requested_size_.IsEmpty())
    skip_resize_ = true;
  else {
    pp::Size calc_size = CalculateSize();
    if (calc_size.width() == stream_size_.width() ||
        calc_size.height() == stream_size_.height())
      skip_resize_ = true;
    else {
      size = calc_size;
      skip_resize_ = false;
    }
  }

  auto resized_cb = [this](bool success) {
    this->OnEncoderResized(success);
  };
  encoder_->Resize(size, resized_cb);
}

pp::Size VideoSender::CalculateSize() const {
  // First check if original size is smaller than requested size. If so, do not
  // scale up.
  if (stream_size_.width() <= requested_size_.width() &&
      stream_size_.height() <= requested_size_.height())
    return stream_size_;

  // Otherwise, calculate the new size so that it still fits "inside" the
  // requested size.
  float ratio = (float)stream_size_.height() / stream_size_.width();
  float new_height = requested_size_.width() * ratio;
  if (new_height <= requested_size_.height()) {
    pp::Size size(roundTo4(requested_size_.width()), roundTo4(new_height));
    return size;
  }

  float new_width = requested_size_.height() / ratio;
  if (new_width <= requested_size_.width()) {
    pp::Size size(roundTo4(new_width), roundTo4(requested_size_.height()));
    return size;
  }

  ERR() << "Something went wrong with size calculation. stream size: "
        << stream_size_.width() << "x" << stream_size_.height()
        << ", requested size: "
        << requested_size_.width() << "x" << requested_size_.height();

  return pp::Size();
}

void VideoSender::OnEncoderResized(bool success) {
  if (!success) {
    ERR() << "Could not resize encoder.";
    return;
  }

  if (skip_resize_)
    OnConfiguredTrack(PP_OK);
  else
    // Reconfigure stream
    ConfigureTrack();
}

void VideoSender::ConfigureTrack() {
  int32_t attrib_list[]{
      PP_MEDIASTREAMVIDEOTRACK_ATTRIB_FORMAT, encoder_->format(),
      PP_MEDIASTREAMVIDEOTRACK_ATTRIB_WIDTH, encoder_->size().width(),
      PP_MEDIASTREAMVIDEOTRACK_ATTRIB_HEIGHT, encoder_->size().height(),
      PP_MEDIASTREAMVIDEOTRACK_ATTRIB_NONE};

  DINF() << "Configuring track to: " << encoder_->size().width()
         << "x" << encoder_->size().height();

  auto cc = factory_.NewCallback(&VideoSender::OnConfiguredTrack);
  video_track_.Configure(attrib_list, cc);
}

void VideoSender::OnConfiguredTrack(int32_t result) {
  if (result != PP_OK) {
    ERR() << "Could not configure video track: " << result;
    if (start_sending_cb_) start_sending_cb_(false);
    start_sending_cb_ = nullptr;
    return;
  }

  RequestEncodedFrame();
  StartTrackFrames();
  ScheduleNextEncode();
  if (start_sending_cb_) start_sending_cb_(true);
  start_sending_cb_ = nullptr;
}

void VideoSender::StartTrackFrames() {
  DINF() << "Starting to track frames.";
  is_receiving_track_frames_ = true;
  auto cc = factory_.NewCallbackWithOutput(&VideoSender::OnTrackFrame);
  video_track_.GetFrame(cc);
}

void VideoSender::StopTrackFrames() {
  is_receiving_track_frames_ = false;
  if (!current_track_frame_.is_null()) {
    video_track_.RecycleFrame(current_track_frame_);
    current_track_frame_.detach();
  }
}

void VideoSender::OnTrackFrame(int32_t result, pp::VideoFrame frame) {
  if (result == PP_ERROR_ABORTED) return;

  if (!current_track_frame_.is_null()) {
    video_track_.RecycleFrame(current_track_frame_);
    current_track_frame_.detach();
  }

  if (result != PP_OK) {
    ERR() << "Cannot get frame from video track: " << result;
    return;
  }

  if (is_receiving_track_frames_) {
    current_track_frame_ = frame;
    auto cc = factory_.NewCallbackWithOutput(&VideoSender::OnTrackFrame);
    video_track_.GetFrame(cc);
  }
}

void VideoSender::ScheduleNextEncode() {
  auto cc = factory_.NewCallback(&VideoSender::GetEncoderFrameTick);
  pp::Module::Get()->core()->CallOnMainThread(1000 / frame_rate_, cc, 0);
}

void VideoSender::GetEncoderFrameTick(int32_t result) {
  if (!current_track_frame_.is_null()) {
    pp::VideoFrame frame = current_track_frame_;
    current_track_frame_.detach();

    if (!InsertRawVideoFrame(frame)) {
      RecycleFrame(frame);
    }
  }

  ScheduleNextEncode();
}

bool VideoSender::InsertRawVideoFrame(const pp::VideoFrame& frame) {
  if (!encoder_) {
    PP_NOTREACHED();
    return false;
  }
  PP_TimeTicks time_sticks = frame.GetTimestamp();

  const base::TimeTicks reference_time = env_->clock()->NowTicks();

  const RtpTimestamp rtp_timestamp =
      PP_TimeDeltaToRtpDelta(time_sticks, kVideoFrequency);

  if (!last_reference_time_.is_null() &&
      (!IsNewerRtpTimestamp(rtp_timestamp,
                            last_enqueued_frame_rtp_timestamp_) ||
       reference_time < last_reference_time_)) {
    DWRN() << "Dropping video frame: RTP or reference time did not increase.";
    return false;
  }
  const base::TimeDelta duration_added_by_next_frame =
      frames_in_encoder_ > 0 ? reference_time - last_reference_time_ :
                             // FIXME: Remove this hack, needed because the
          // frame duration increases
          // when we decrease the fps.
          base::TimeDelta::FromSecondsD(0.01 / frame_rate_);

  if (ShouldDropNextFrame(duration_added_by_next_frame)) {
    base::TimeDelta new_target_delay =
        std::min(current_round_trip_time_ * kRoundTripsNeeded +
                     base::TimeDelta::FromMilliseconds(kConstantTimeMs),
                 max_playout_delay_);
    if (new_target_delay > target_playout_delay_) {
      DWRN() << "New target delay: " << new_target_delay.InMilliseconds();
      playout_delay_change_cb_(new_target_delay);
    }

    return false;
  }

  // Send frame to encoder, and add a callback to when it can be released.
  auto release_cb = [this](pp::VideoFrame frame) { this->RecycleFrame(frame); };
  frames_in_encoder_++;
  duration_in_encoder_ += duration_added_by_next_frame;
  last_reference_time_ = reference_time;
  last_enqueued_frame_rtp_timestamp_ = rtp_timestamp;
  pause_delta_ = time_sticks + 0.1;
  encoder_->EncodeFrame(frame, reference_time, release_cb);
  return true;
}

void VideoSender::RecycleFrame(pp::VideoFrame frame) {
  video_track_.RecycleFrame(frame);
}

void VideoSender::RequestEncodedFrame() {
  auto encoded_cb = [this](bool success, std::shared_ptr<EncodedFrame> frame) {
    this->OnEncodedFrame(success, frame);
  };
  encoder_->GetEncodedFrame(encoded_cb);
}

void VideoSender::OnEncodedFrame(bool success,
                                 std::shared_ptr<EncodedFrame> frame) {
  /* DINF() << "Encoded frame received: " << frame->frame_id; */
  duration_in_encoder_ = last_reference_time_ - frame->reference_time;
  frames_in_encoder_--;

  SendEncodedFrame(frame);

  RequestEncodedFrame();
}

}  // namespace sharer
