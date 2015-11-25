// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sharer_sender.h"

#include "base/logger.h"
#include "base/ptr_utils.h"
#include "sender/video_sender.h"

#include <string>

namespace sharer {

SharerSender::SharerSender(pp::Instance* instance, int id)
    : instance_(instance),
      sender_id_(id),
      clock_(make_unique<base::DefaultTickClock>()),
      initialized_video_(false),
      /* initialized_audio_(false), */
      initialized_cb_(nullptr),
      pauseID(0) {}

SharerSender::~SharerSender() { DINF() << "Destroying SharerSender."; }

void SharerSender::Initialize(const SenderConfig& config,
                              SharerSenderInitializedCb cb) {
  initialized_cb_ = cb;

  auto transport_cb =
      [this](bool result) { this->InitializedTransport(result); };
  transport_ = make_unique<TransportSender>(instance_, clock_.get(), config,
                                            transport_cb);

  auto video_cb = [this](bool result) { this->InitializedVideo(result); };
  auto playout_changed_cb = [this](const base::TimeDelta& playout_delay) {
    this->SetTargetPlayoutDelay(playout_delay);
  };
  video_sender_ =
      make_unique<VideoSender>(instance_, clock_.get(), transport_.get(),
                               config, video_cb, playout_changed_cb);
}

bool SharerSender::SetTracks(const pp::MediaStreamVideoTrack& video_track,
                             const SharerSuccessCb& cb) {
  DINF() << "Setting audio and video tracks.";
  video_sender_->StartSending(video_track, cb);
  return true;
}

bool SharerSender::StopTracks(const SharerSuccessCb& cb) {
  DINF() << "Stop sendng.";
  video_sender_->StopSending(cb);
  return true;
}

void SharerSender::ChangeEncoding(const SenderConfig& config) {
  DINF() << "Changing encoding parameters";
  video_sender_->ChangeEncoding(config);
  return;
}

void SharerSender::InitializedVideo(bool success) {
  if (!success) {
    ERR() << "Failed to initialize video.";
    initialized_cb_(id(), SharerSender::INIT_FAILED_VIDEO);
    return;
  }

  initialized_video_ = true;
  INF() << "Successfully initialized video.";

  CheckInitialized();
}

void SharerSender::InitializedTransport(bool success) {
  if (!success) {
    ERR() << "Failed to initialize transport.";
    initialized_cb_(id(), SharerSender::INIT_FAILED_TRANSPORT);
    return;
  }

  initialized_transport_ = true;
  INF() << "Successfully initialized transport.";
  CheckInitialized();
}

void SharerSender::CheckInitialized() {
  if (initialized_video_ && initialized_transport_) {
    if (initialized_cb_) {
      initialized_cb_(id(), SharerSender::INIT_SUCCESS);
      initialized_cb_ = nullptr;
    }
  }
}

void SharerSender::SetTargetPlayoutDelay(const base::TimeDelta& playout_delay) {
  if (video_sender_) {
    video_sender_->SetTargetPlayoutDelay(playout_delay);
  }
}

}  // namespace sharer
