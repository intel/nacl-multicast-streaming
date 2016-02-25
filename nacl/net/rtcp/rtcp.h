// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _RTCP_HANDLER_
#define _RTCP_HANDLER_

#include "base/time/time.h"
#include "net/sharer_transport_defines.h"
#include "net/pacing/paced_sender.h"
#include "net/rtcp/rtcp_builder.h"
#include "net/rtcp/rtcp_defines.h"
#include "net/rtp/rtp_receiver_defines.h"
#include "common/clock_drift_smoother.h"
#include "sharer_environment.h"

#include <memory>
#include <queue>

class RTCP;

using RtcpSendTimePair = std::pair<uint32_t, base::TimeTicks>;
using RtcpSendTimeMap = std::map<uint32_t, base::TimeTicks>;
using RtcpSendTimeQueue = std::queue<RtcpSendTimePair>;

class RtcpHandler {
 public:
  static bool IsRtcpPacket(const uint8_t* packet, size_t length);
  static uint32_t GetSsrcOfSender(const uint8_t* rtcp_bufer, size_t length);

  RtcpHandler(const RtcpSharerMessageCallback& sharer_callback,
              const RtcpRttCallback& rtt_calback, sharer::SharerEnvironment* env,
              UDPSender* transport, sharer::PacedSender* packet_sender,
              uint32_t local_ssrc, uint32_t remote_ssrc);
  virtual ~RtcpHandler();
  bool IncomingRtcpPausedPacket(const std::unique_ptr<RTCP>& packet);
  bool IncomingRtcpPacket(const std::unique_ptr<RTCP>& packet);
  bool IncomingRtcpPacket(const std::string& addr, const uint8_t* data,
                          size_t length);
  bool GetLatestLipSyncTimes(uint32_t* rtp_timestamp,
                             base::TimeTicks* reference_time) const;

  RtcpTimeData ConvertToNTPAndSave(base::TimeTicks now);

  void SendRtcpFromRtpReceiver(
      RtcpTimeData time_data, const RtcpSharerMessage* sharer_message,
      base::TimeDelta target_delay,
      const RtpReceiverStatistics* rtp_receiver_statistics) const;
  void SendRtcpFromRtpSender(base::TimeTicks current_time,
                             uint32_t current_time_as_rtp_timestamp,
                             uint32_t send_packet_count,
                             size_t send_octet_count);
  void SendRtcpPauseResumeFromRtpSender(uint32_t last_sent_frame_id_,
                                        uint32_t local_pause_id_);

  base::TimeDelta current_round_trip_time() const {
    return current_round_trip_time_;
  }

 private:
  void OnReceivedNtp(uint32_t ntp_seconds, uint32_t ntp_fraction);
  void OnReceivedLipSyncInfo(const std::unique_ptr<RTCP>& packet);
  void OnReceivedLipSyncInfo(uint32_t rtp_timestamp, uint32_t ntp_seconds,
                             uint32_t ntp_fraction);
  void OnReceivedSharerFeedback(const std::string& addr,
                                const RtcpSharerMessage& sharer_message);
  void OnReceivedDelaySinceLastReport(uint32_t last_report,
                                      uint32_t delay_since_last_report);
  void SaveLastSentNtpTime(const base::TimeTicks& now,
                           uint32_t last_ntp_seconds,
                           uint32_t last_ntp_fraction);

  const RtcpSharerMessageCallback sharer_callback_;
  const RtcpRttCallback rtt_callback_;
  sharer::SharerEnvironment* const env_;  // non-owning pointer
  RtcpBuilder rtcp_builder_;

  UDPSender* transport_;  // not owning pointer
  sharer::PacedSender* packet_sender_;
  uint32_t local_ssrc_;
  uint32_t remote_ssrc_;

  ClockDriftSmoother local_clock_ahead_by_;

  RtcpSendTimeMap last_reports_sent_map_;
  RtcpSendTimeQueue last_reports_sent_queue_;

  uint32_t last_report_truncated_ntp_;
  base::TimeTicks time_last_report_received_;
  uint32_t lip_sync_rtp_timestamp_;
  uint64_t lip_sync_ntp_timestamp_;

  base::TimeDelta current_round_trip_time_;

  base::TimeTicks largest_seen_timestamp_;

  sharer::FrameIdWrapHelper ack_frame_id_wrap_helper_;
};

#endif  // _RTCP_HANDLER_
