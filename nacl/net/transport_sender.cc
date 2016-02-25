// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/transport_sender.h"

#include "base/logger.h"
#include "base/ptr_utils.h"
#include "net/rtcp/rtcp.h"

#include "ppapi/cpp/logging.h"

namespace sharer {

TransportSender::TransportSender(SharerEnvironment* env,
                                 const SenderConfig& config,
                                 const TransportInitializedCb& cb)
    : env_(env),
      // TODO: Figure out the correct send_buffer_size
      transport_(env_, config.remote_address, config.remote_port, 4096, cb),
      pacer_(env_, &transport_) {
  PP_DCHECK(env_->clock());
  if (!env_->clock()) {
    ERR() << "Clock can't be null.";
    return;
  }

  transport_.StartReceiving(
      [this](const std::string& addr, std::unique_ptr<Packet> packet) {
        this->OnReceivedPacket(addr, std::move(packet));
      });
}

TransportSender::~TransportSender() {}

void TransportSender::AddValidSsrc(uint32_t ssrc) { valid_ssrcs_.insert(ssrc); }

void TransportSender::OnReceivedPacket(const std::string& addr,
                                       std::unique_ptr<Packet> packet) {
  uint32_t ssrc;
  const uint8_t* const data = packet->data();
  const size_t length = packet->size();
  if (RtcpHandler::IsRtcpPacket(data, length)) {
    ssrc = RtcpHandler::GetSsrcOfSender(data, length);
  } else {
    DERR() << "Invalid RTCP packet.";
    return;
  }

  if (valid_ssrcs_.find(ssrc) == valid_ssrcs_.end()) {
    DERR() << "Stale packet received from: " << ssrc;
    return;
  }

  if (video_rtcp_session_ &&
      video_rtcp_session_->IncomingRtcpPacket(addr, data, length)) {
    // Received and correctly processed RTCP packet
    return;
  }
}

void TransportSender::InitializeVideo(
    const SharerTransportRtpConfig& config,
    const RtcpSharerMessageCallback& sharer_message_cb,
    const RtcpRttCallback& rtt_cb) {
  video_sender_ = make_unique<RtpSender>(&pacer_);
  if (!video_sender_->Initialize(config)) {
    video_sender_ = nullptr;
    ERR() << "Could not initialize video sender.";
    return;
  }

  auto sharer_cb = [this, config, sharer_message_cb](
      const std::string& addr, const RtcpSharerMessage& msg) {
    this->OnReceivedSharerMessage(config.ssrc, addr, sharer_message_cb, msg);
  };
  video_rtcp_session_ =
      make_unique<RtcpHandler>(sharer_cb, rtt_cb, env_, nullptr, &pacer_,
                               config.ssrc, config.feedback_ssrc);
  pacer_.RegisterVideoSsrc(config.ssrc);
  AddValidSsrc(config.feedback_ssrc);
}

void TransportSender::OnReceivedSharerMessage(
    uint32_t ssrc, const std::string& addr,
    const RtcpSharerMessageCallback& sharer_message_cb,
    const RtcpSharerMessage& sharer_message) {
  if (sharer_message_cb) sharer_message_cb(addr, sharer_message);

  DedupInfo dedup_info;
  if (video_sender_ && video_sender_->ssrc() == ssrc) {
    dedup_info.resend_interval = video_rtcp_session_->current_round_trip_time();
  }

  if (sharer_message.missing_frames_and_packets.empty()) return;

  ResendPackets(ssrc, addr, sharer_message.missing_frames_and_packets, true,
                dedup_info);
}

void TransportSender::ResendPackets(
    uint32_t ssrc, const std::string& addr,
    const MissingFramesAndPacketsMap& missing_packets,
    bool cancel_rtx_if_not_in_list, const DedupInfo& dedup_info) {
  if (video_sender_ && ssrc == video_sender_->ssrc()) {
    video_sender_->ResendPackets(addr, missing_packets,
                                 cancel_rtx_if_not_in_list, dedup_info);
  }
}

void TransportSender::InsertFrame(uint32_t ssrc, const EncodedFrame& frame) {
  if (video_sender_ && ssrc == video_sender_->ssrc()) {
    video_sender_->SendFrame(frame);
  }
}

void TransportSender::SendSenderReport(uint32_t ssrc,
                                       base::TimeTicks current_time,
                                       uint32_t current_time_as_rtp_timestamp) {
  if (video_sender_ && ssrc == video_sender_->ssrc()) {
    video_rtcp_session_->SendRtcpFromRtpSender(
        current_time, current_time_as_rtp_timestamp,
        video_sender_->send_packet_count(), video_sender_->send_octet_count());
  } else {
    PP_NOTREACHED();
  }
}

void TransportSender::SendSenderPauseResume(uint32_t ssrc,
                                            uint32_t last_sent_frame_id_,
                                            uint32_t local_pause_id_) {
  DINF() << "Sending RTCP Pause Resume...";
  if (video_sender_ && ssrc == video_sender_->ssrc()) {
    video_rtcp_session_->SendRtcpPauseResumeFromRtpSender(last_sent_frame_id_,
                                                          local_pause_id_);
  } else {
    PP_NOTREACHED();
  }
}

void TransportSender::ResendFrameForKickstart(uint32_t ssrc,
                                              uint32_t frame_id) {
  if (video_sender_ && ssrc == video_sender_->ssrc()) {
    PP_DCHECK(video_rtcp_session_);
    video_sender_->ResendFrameForKickstart(
        frame_id, video_rtcp_session_->current_round_trip_time());
  } else {
    PP_NOTREACHED();
  }
}

}  // namespace sharer
