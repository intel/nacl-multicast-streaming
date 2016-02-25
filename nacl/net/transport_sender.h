// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// SharerTransportSender              RTP                      RTCP
// ------------------------------------------------------------------
//                      TransportEncryptionHandler (A/V)
//                      RtpSender (A/V)                   Rtcp (A/V)
//                                      PacedSender (Shared)
//                                      UdpTransport (Shared)
//
// There are objects of TransportEncryptionHandler, RtpSender and Rtcp
// for each audio and video stream.
// PacedSender and UdpTransport are shared between all RTP and RTCP
// streams

#ifndef NET_TRANSPORT_SENDER_H_
#define NET_TRANSPORT_SENDER_H_

#include "base/macros.h"
#include "base/time/default_tick_clock.h"
#include "sharer_config.h"
#include "sharer_environment.h"
#include "net/sharer_transport_defines.h"
#include "net/udp_transport.h"
#include "net/pacing/paced_sender.h"
#include "net/rtp/rtp_sender.h"

#include "ppapi/cpp/instance.h"

#include <set>

class RtcpHandler;

namespace sharer {

class UdpTransport;

class TransportSender {
 public:
  TransportSender(SharerEnvironment* env,
                  const SenderConfig& config, const TransportInitializedCb& cb);
  ~TransportSender();

  void AddValidSsrc(uint32_t ssrc);

  void InitializeVideo(const SharerTransportRtpConfig& config,
                       const RtcpSharerMessageCallback& sharer_message_cb,
                       const RtcpRttCallback& rtt_cb);
  void InsertFrame(uint32_t ssrc, const EncodedFrame& frame);
  void SendSenderReport(uint32_t ssrc, base::TimeTicks current_time,
                        uint32_t current_time_as_rtp_timestamp);
  void SendSenderPauseResume(uint32_t ssrc, uint32_t last_sent_frame_id_,
                             uint32_t local_pause_id_);
  /* void CancelSendingFrames(uint32_t ssrc, */
  /* const std::vector<uint32_t>& frame_ids); */

  void ResendFrameForKickstart(uint32_t ssrc, uint32_t frame_id);

 private:
  void OnReceivedPacket(const std::string& addr,
                        std::unique_ptr<Packet> packet);
  void OnReceivedSharerMessage(
      uint32_t ssrc, const std::string& addr,
      const RtcpSharerMessageCallback& sharer_message_cb,
      const RtcpSharerMessage& sharer_message);

  void ResendPackets(uint32_t ssrc, const std::string& addr,
                     const MissingFramesAndPacketsMap& missing_packets,
                     bool cancel_rtx_if_not_in_list,
                     const DedupInfo& dedup_info);

  SharerEnvironment* env_;

  UdpTransport transport_;
  PacedSender pacer_;

  std::unique_ptr<RtpSender> video_sender_;

  std::unique_ptr<RtcpHandler> video_rtcp_session_;

  std::set<uint32_t> valid_ssrcs_;

  DISALLOW_COPY_AND_ASSIGN(TransportSender);
};

}  // namespace sharer

#endif  // NET_TRANSPORT_SENDER_H_
