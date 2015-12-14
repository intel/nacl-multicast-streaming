// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/rtcp/rtcp.h"

#include "base/logger.h"
#include "net/sharer_transport_config.h"
#include "net/rtcp/rtcp_builder.h"
#include "net/rtcp/rtcp_utility.h"
#include "net/rtp/rtp.h"

#include "ppapi/cpp/module.h"

// Magic fractional unit. Used to convert time (in microseconds) to/from
// fractional NTP seconds.
static const double kMagicFractionalUnit = 4.294967296E3;
static const int64_t kUnixEpochInNtpSeconds = INT64_C(2208988800);
static const int32_t kStatsHistoryWindowMs = 10000;

static uint32_t ConvertToNtpDiff(uint32_t delay_seconds,
                                 uint32_t delay_fraction) {
  return ((delay_seconds & 0x0000FFFF) << 16) +
         ((delay_fraction & 0xFFFF0000) >> 16);
}

static inline base::TimeTicks ConvertNtpToTimeTicks(uint32_t ntp_seconds,
                                                    uint32_t ntp_fractions) {
  int64_t ntp_time_us =
      ntp_seconds * base::Time::kMicrosecondsPerSecond +
      static_cast<int64_t>(std::ceil(ntp_fractions / kMagicFractionalUnit));

  base::TimeDelta elapsed_since_unix_epoch = base::TimeDelta::FromMicroseconds(
      ntp_time_us -
      (kUnixEpochInNtpSeconds * base::Time::kMicrosecondsPerSecond));
  return base::TimeTicks::UnixEpoch() + elapsed_since_unix_epoch;
}

void ConvertTimeToFractions(int64_t ntp_time_us, uint32_t* seconds,
                            uint32_t* fractions) {
  PP_DCHECK(ntp_time_us >= 0);  // Time must NOT be negative
  const int64_t seconds_component =
      ntp_time_us / base::Time::kMicrosecondsPerSecond;

  // One year left to fix the NTP year 2036 wrap-around issue!
  PP_DCHECK(seconds_component < INT64_C(4263431296));

  *seconds = static_cast<uint32_t>(seconds_component);
  *fractions =
      static_cast<uint32_t>((ntp_time_us % base::Time::kMicrosecondsPerSecond) *
                            kMagicFractionalUnit);
}

static inline void ConvertTimeTicksToNtp(const base::TimeTicks& time,
                                         uint32_t* ntp_seconds,
                                         uint32_t* ntp_fractions) {
  base::TimeDelta elapsed_since_unix_epoch =
      time - base::TimeTicks::UnixEpoch();
  int64_t ntp_time_us =
      elapsed_since_unix_epoch.InMicroseconds() +
      (kUnixEpochInNtpSeconds * base::Time::kMicrosecondsPerSecond);
  ConvertTimeToFractions(ntp_time_us, ntp_seconds, ntp_fractions);
}

// static
bool RtcpHandler::IsRtcpPacket(const uint8_t* packet, size_t length) {
  if (length < sharer::kMinLengthOfRtcp) {
    DERR() << "Invalid RTCP packet received.";
    return false;
  }

  uint8_t packet_type = packet[1];
  return packet_type >= kPacketTypeLow && packet_type <= kPacketTypeHigh;
}

uint32_t RtcpHandler::GetSsrcOfSender(const uint8_t* rtcp_buffer,
                                      size_t length) {
  if (length < sharer::kMinLengthOfRtcp) return 0;
  uint32_t ssrc_of_sender;
  BigEndianReader big_endian_reader(reinterpret_cast<const char*>(rtcp_buffer),
                                    length);
  big_endian_reader.Skip(4);
  big_endian_reader.ReadU32(&ssrc_of_sender);
  return ssrc_of_sender;
}

RtcpHandler::RtcpHandler(const RtcpSharerMessageCallback& sharer_callback,
                         const RtcpRttCallback& rtt_callback,
                         sharer::SharerEnvironment* env, UDPSender* transport,
                         sharer::PacedSender* packet_sender,
                         uint32_t local_ssrc, uint32_t remote_ssrc)
    : sharer_callback_(sharer_callback),
      rtt_callback_(rtt_callback),
      env_(env),
      rtcp_builder_(local_ssrc),
      transport_(transport),
      packet_sender_(packet_sender),
      local_ssrc_(local_ssrc),
      remote_ssrc_(remote_ssrc),
      local_clock_ahead_by_(ClockDriftSmoother::GetDefaultTimeConstant()),
      last_report_truncated_ntp_(0),
      lip_sync_rtp_timestamp_(0),
      lip_sync_ntp_timestamp_(0) {}

RtcpHandler::~RtcpHandler() {}

bool RtcpHandler::IncomingRtcpPausedPacket(
    const std::unique_ptr<RTCP>& packet) {
  DINF() << "Sender is paused";
  /*returns true ==> sender is paused*/
  // TODO
  /*int ssrc = packet->ssrc();
  int pauseID = packet->ntpSeconds();
  int last_frame_received = packet->ntpFraction();
  */

  return true;
}

bool RtcpHandler::IncomingRtcpPacket(const std::unique_ptr<RTCP>& packet) {
  if (packet->payloadType() != RTCP::SR) {
    if (packet->payloadType() == RTCP::RTPFB) {
      return IncomingRtcpPausedPacket(packet);
    }
    return false;
  }

  OnReceivedNtp(packet->ntpSeconds(), packet->ntpFraction());
  OnReceivedLipSyncInfo(packet);
  return true;
}

bool RtcpHandler::IncomingRtcpPacket(const std::string& addr,
                                     const uint8_t* data, size_t length) {
  if (!IsRtcpPacket(data, length)) {
    DWRN() << "Rtcp@" << this << "::IncomingRtcpPacket() -- "
           << "Received an invalid (non-RTCP?) packet.";
    return false;
  }

  uint32_t ssrc_of_sender = GetSsrcOfSender(data, length);
  if (ssrc_of_sender != remote_ssrc_) {
    return false;
  }

  sharer::RtcpParser parser(local_ssrc_, remote_ssrc_);
  BigEndianReader reader(reinterpret_cast<const char*>(data), length);
  if (parser.Parse(&reader)) {
    if (parser.has_sender_report()) {
      OnReceivedNtp(parser.sender_report().ntp_seconds,
                    parser.sender_report().ntp_fraction);
      OnReceivedLipSyncInfo(parser.sender_report().rtp_timestamp,
                            parser.sender_report().ntp_seconds,
                            parser.sender_report().ntp_fraction);
    }
    if (parser.has_last_report()) {
      OnReceivedDelaySinceLastReport(parser.last_report(),
                                     parser.delay_since_last_report());
    }
    if (parser.has_sharer_message()) {
      OnReceivedSharerFeedback(addr, parser.sharer_message());
    }
  }
  return true;
}

void RtcpHandler::OnReceivedNtp(uint32_t ntp_seconds, uint32_t ntp_fraction) {
  last_report_truncated_ntp_ = ConvertToNtpDiff(ntp_seconds, ntp_fraction);

  const base::TimeTicks now = env_->clock()->NowTicks();
  time_last_report_received_ = now;

  const base::TimeDelta measured_offset =
      now - ConvertNtpToTimeTicks(ntp_seconds, ntp_fraction);
  local_clock_ahead_by_.Update(now, measured_offset);
  if (measured_offset < local_clock_ahead_by_.Current()) {
    local_clock_ahead_by_.Reset(now, measured_offset);
  }
}

void RtcpHandler::OnReceivedLipSyncInfo(const std::unique_ptr<RTCP>& packet) {
  if (packet->ntpSeconds() == 0) {
    PP_NOTREACHED();
    return;
  }

  lip_sync_rtp_timestamp_ = packet->rtpTimestamp();
  lip_sync_ntp_timestamp_ =
      (static_cast<uint64_t>(packet->ntpSeconds()) << 32) |
      packet->ntpFraction();
}

void RtcpHandler::OnReceivedLipSyncInfo(uint32_t rtp_timestamp,
                                        uint32_t ntp_seconds,
                                        uint32_t ntp_fraction) {
  if (ntp_seconds == 0) {
    PP_NOTREACHED();
    return;
  }

  lip_sync_rtp_timestamp_ = rtp_timestamp;
  lip_sync_ntp_timestamp_ =
      (static_cast<uint64_t>(ntp_seconds) << 32) | ntp_fraction;
}

void RtcpHandler::OnReceivedDelaySinceLastReport(
    uint32_t last_report, uint32_t delay_since_last_report) {
  auto it = last_reports_sent_map_.find(last_report);
  if (it == last_reports_sent_map_.end()) {
    return;  // Feedback on another report
  }

  const base::TimeDelta sender_delay = env_->clock()->NowTicks() - it->second;
  const base::TimeDelta receiver_delay =
      sharer::ConvertFromNtpDiff(delay_since_last_report);
  current_round_trip_time_ = sender_delay - receiver_delay;

  current_round_trip_time_ =
      std::max(current_round_trip_time_, base::TimeDelta::FromMilliseconds(1));

  if (rtt_callback_) rtt_callback_(current_round_trip_time_);
}

void RtcpHandler::OnReceivedSharerFeedback(
    const std::string& addr, const RtcpSharerMessage& sharer_message) {
  DINF() << "Received cast feedback. Missing frames: "
         << sharer_message.missing_frames_and_packets.size();
  if (!sharer_callback_) return;
  sharer_callback_(addr, sharer_message);
}

bool RtcpHandler::GetLatestLipSyncTimes(uint32_t* rtp_timestamp,
                                        base::TimeTicks* reference_time) const {
  if (!lip_sync_ntp_timestamp_) return false;

  const base::TimeTicks local_reference_time =
      ConvertNtpToTimeTicks(
          static_cast<uint32_t>(lip_sync_ntp_timestamp_ >> 32),
          static_cast<uint32_t>(lip_sync_ntp_timestamp_)) +
      local_clock_ahead_by_.Current();

  // Sanity-check: Getting regular lip sync updates?
  PP_DCHECK((env_->clock()->NowTicks() - local_reference_time) <
            base::TimeDelta::FromMinutes(1));

  *rtp_timestamp = lip_sync_rtp_timestamp_;
  *reference_time = local_reference_time;
  return true;
}

RtcpTimeData RtcpHandler::ConvertToNTPAndSave(base::TimeTicks now) {
  RtcpTimeData ret;
  ret.timestamp = now;

  ConvertTimeTicksToNtp(now, &ret.ntp_seconds, &ret.ntp_fraction);
  // TODO: Check if this is really necessary and remove it if not
  SaveLastSentNtpTime(now, ret.ntp_seconds, ret.ntp_fraction);
  return ret;
}

void RtcpHandler::SaveLastSentNtpTime(const base::TimeTicks& now,
                                      uint32_t last_ntp_seconds,
                                      uint32_t last_ntp_fraction) {
  // Make sure |now| is always greater than the last element in
  // |last_reports_sent_queue_|.
  if (!last_reports_sent_queue_.empty()) {
    PP_DCHECK(now >= last_reports_sent_queue_.back().second);
  }

  uint32_t last_report = ConvertToNtpDiff(last_ntp_seconds, last_ntp_fraction);
  last_reports_sent_map_[last_report] = now;
  last_reports_sent_queue_.push(std::make_pair(last_report, now));

  const base::TimeTicks timeout =
      now - base::TimeDelta::FromMilliseconds(kStatsHistoryWindowMs);

  // Cleanup old statistics older than |timeout|.
  while (!last_reports_sent_queue_.empty()) {
    RtcpSendTimePair oldest_report = last_reports_sent_queue_.front();
    if (oldest_report.second < timeout) {
      last_reports_sent_map_.erase(oldest_report.first);
      last_reports_sent_queue_.pop();
    } else {
      break;
    }
  }
}

void RtcpHandler::SendRtcpFromRtpReceiver(
    RtcpTimeData time_data, const RtcpSharerMessage* sharer_message,
    base::TimeDelta target_delay,
    const RtpReceiverStatistics* rtp_receiver_statistics) const {
  RtcpReportBlock report_block;
  RtcpReceiverReferenceTimeReport rrtr;
  rrtr.ntp_seconds = time_data.ntp_seconds;
  rrtr.ntp_fraction = time_data.ntp_fraction;

  if (rtp_receiver_statistics) {
    report_block.remote_ssrc = 0;
    report_block.media_ssrc = remote_ssrc_;
    report_block.fraction_lost = rtp_receiver_statistics->fraction_lost;
    report_block.cumulative_lost = rtp_receiver_statistics->cumulative_lost;
    report_block.extended_high_sequence_number =
        rtp_receiver_statistics->extended_high_sequence_number;
    report_block.jitter = rtp_receiver_statistics->jitter;
    report_block.last_sr = last_report_truncated_ntp_;
    if (!time_last_report_received_.is_null()) {
      uint32_t delay_seconds = 0;
      uint32_t delay_fraction = 0;
      base::TimeDelta delta = time_data.timestamp - time_last_report_received_;
      ConvertTimeToFractions(delta.InMicroseconds(), &delay_seconds,
                             &delay_fraction);
      report_block.delay_since_last_sr =
          ConvertToNtpDiff(delay_seconds, delay_fraction);
    } else {
      report_block.delay_since_last_sr = 0;
    }
  }

  RtcpBuilder rtcp_builder(local_ssrc_);
  transport_->SendPacket(rtcp_builder.BuildRtcpFromReceiver(
      rtp_receiver_statistics ? &report_block : NULL, &rrtr, sharer_message,
      target_delay));
}

void RtcpHandler::SendRtcpFromRtpSender(base::TimeTicks current_time,
                                        uint32_t current_time_as_rtp_timestamp,
                                        uint32_t send_packet_count,
                                        size_t send_octet_count) {
  uint32_t current_ntp_seconds = 0;
  uint32_t current_ntp_fractions = 0;
  ConvertTimeTicksToNtp(current_time, &current_ntp_seconds,
                        &current_ntp_fractions);
  SaveLastSentNtpTime(current_time, current_ntp_seconds, current_ntp_fractions);

  RtcpSenderInfo sender_info;
  sender_info.ntp_seconds = current_ntp_seconds;
  sender_info.ntp_fraction = current_ntp_fractions;
  sender_info.rtp_timestamp = current_time_as_rtp_timestamp;
  sender_info.send_packet_count = send_packet_count;
  sender_info.send_octet_count = send_octet_count;

  packet_sender_->SendRtcpPacket(
      local_ssrc_, rtcp_builder_.BuildRtcpFromSender(sender_info));
}

void RtcpHandler::SendRtcpPauseResumeFromRtpSender(uint32_t last_sent_frame_id_,
                                                   uint32_t local_pause_id_) {
  DINF() << "Sending RTCP Pause Resume...";
  RtcpPauseResumeMessage pause_msg;
  pause_msg.last_sent = last_sent_frame_id_;
  pause_msg.pause_id = local_pause_id_;

  packet_sender_->SendRtcpPacket(
      local_ssrc_, rtcp_builder_.BuildPauseRtcpFromSender(pause_msg));
}
