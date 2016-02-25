// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHARER_SENDER_H_
#define SHARER_SENDER_H_

#include "base/macros.h"
#include "logging/stats_event_subscriber.h"
#include "sharer_config.h"
#include "sharer_environment.h"
#include "net/transport_sender.h"
#include "sender/video_sender.h"

#include "ppapi/cpp/media_stream_video_track.h"

#include <functional>

namespace sharer {

class SharerSender {
 public:
  enum InitResult {
    INIT_SUCCESS,
    INIT_FAILED_VIDEO,
    INIT_FAILED_AUDIO,
    INIT_FAILED_TRANSPORT,
    INIT_LAST
  };

  using SharerSenderInitializedCb =
      std::function<void(int id, InitResult result)>;
  using SharerSuccessCb = std::function<void(bool success)>;

  explicit SharerSender(pp::Instance* instance, int id);
  ~SharerSender();

  void Initialize(const SenderConfig& config, SharerSenderInitializedCb cb);
  bool SetTracks(const pp::MediaStreamVideoTrack& video_track,
                 const SharerSuccessCb& cb);
  bool StopTracks(const SharerSuccessCb& cb);
  void SetPauseID() { pauseID++; }
  void ChangeEncoding(const SenderConfig& config);

  int id() const { return sender_id_; }
  int SetPauseID() const { return pauseID; }

 private:
  void InitializedVideo(bool success);
  void InitializedTransport(bool success);
  void CheckInitialized();
  void SetTargetPlayoutDelay(const base::TimeDelta& playout_delay);
  void ScheduleReport();
  void RunReport(int32_t);

  SharerEnvironment env_;
  StatsEventSubscriber stats_;

  int sender_id_;
  pp::CompletionCallbackFactory<SharerSender> factory_;
  bool report_scheduled_;
  bool stream_sharing_;

  std::unique_ptr<base::TickClock> clock_;

  bool initialized_video_;
  bool initialized_transport_;
  SharerSenderInitializedCb initialized_cb_;
  /*TODO id to be sent to receivers in order to notify the pause in
   * stransmission*/
  int pauseID;

  SenderConfig config_;

  std::unique_ptr<TransportSender> transport_;

  std::unique_ptr<VideoSender> video_sender_;

  DISALLOW_COPY_AND_ASSIGN(SharerSender);
};

}  // namespace sharer

#endif  // SHARER_SENDER_H_
