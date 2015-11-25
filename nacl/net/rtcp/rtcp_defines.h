// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _RTCP_DEFINES_H_
#define _RTCP_DEFINES_H_

/* #include "media/cast/sharer_config.h" */
/* #include "media/cast/sharer_defines.h" */
/* #include "media/cast/logging/logging_defines.h" */
#include "base/time/time.h"

#include "ppapi/c/ppb_net_address.h"

#include <list>
#include <map>
#include <memory>
#include <set>
#include <vector>

/* static const size_t kRtcpSharerLogHeaderSize = 12; */
/* static const size_t kRtcpReceiverFrameLogSize = 8; */
/* static const size_t kRtcpReceiverEventLogSize = 4; */

const size_t kMaxIpPacketSize = 1500;

using PacketIdSet = std::set<uint16_t>;
using MissingFramesAndPacketsMap = std::map<uint32_t, PacketIdSet>;

static const uint16_t kRtcpSharerAllPacketsLost = 0xffff;

// Handle the per frame ACK and NACK messages.
struct RtcpSharerMessage {
  explicit RtcpSharerMessage(uint32_t ssrc);
  RtcpSharerMessage();
  ~RtcpSharerMessage();

  uint32_t media_ssrc;
  uint32_t ack_frame_id;
  uint16_t target_delay_ms;
  bool request_key_frame;
  MissingFramesAndPacketsMap missing_frames_and_packets;
};

/* // Log messages from receiver to sender. */
/* struct RtcpReceiverEventLogMessage { */
/*   RtcpReceiverEventLogMessage(); */
/*   ~RtcpReceiverEventLogMessage(); */

/*   SharerLoggingEvent type; */
/*   base::TimeTicks event_timestamp; */
/*   base::TimeDelta delay_delta; */
/*   uint16 packet_id; */
/* }; */

/* typedef std::list<RtcpReceiverEventLogMessage> RtcpReceiverEventLogMessages;
 */

/* struct RtcpReceiverFrameLogMessage { */
/*   explicit RtcpReceiverFrameLogMessage(uint32 rtp_timestamp); */
/*   ~RtcpReceiverFrameLogMessage(); */

/*   uint32 rtp_timestamp_; */
/*   RtcpReceiverEventLogMessages event_log_messages_; */

/*   // TODO(mikhal): Investigate what's the best way to allow adding */
/*   // DISALLOW_COPY_AND_ASSIGN, as currently it contradicts the implementation
 */
/*   // and possible changes have a big impact on design. */
/* }; */

/* typedef std::list<RtcpReceiverFrameLogMessage> RtcpReceiverLogMessage; */

struct RtcpPauseResumeMessage {
  RtcpPauseResumeMessage();
  ~RtcpPauseResumeMessage();

  uint32_t last_sent;
  uint32_t pause_id;
};

struct RtcpNackMessage {
  RtcpNackMessage();
  ~RtcpNackMessage();

  uint32_t remote_ssrc;
  std::list<uint16_t> nack_list;
};

struct RtcpReceiverReferenceTimeReport {
  RtcpReceiverReferenceTimeReport();
  ~RtcpReceiverReferenceTimeReport();

  uint32_t remote_ssrc;
  uint32_t ntp_seconds;
  uint32_t ntp_fraction;
};

inline bool operator==(RtcpReceiverReferenceTimeReport lhs,
                       RtcpReceiverReferenceTimeReport rhs) {
  return lhs.remote_ssrc == rhs.remote_ssrc &&
         lhs.ntp_seconds == rhs.ntp_seconds &&
         lhs.ntp_fraction == rhs.ntp_fraction;
}

/* // Struct used by raw event subscribers as an intermediate format before */
/* // sending off to the other side via RTCP. */
/* // (i.e., {Sender,Receiver}RtcpEventSubscriber) */
/* struct RtcpEvent { */
/*   RtcpEvent(); */
/*   ~RtcpEvent(); */

/*   SharerLoggingEvent type; */

/*   // Time of event logged. */
/*   base::TimeTicks timestamp; */

/*   // Render/playout delay. Only set for FRAME_PLAYOUT events. */
/*   base::TimeDelta delay_delta; */

/*   // Only set for packet events. */
/*   uint16 packet_id; */
/* }; */

using RtcpSharerMessageCallback =
    std::function<void(const std::string& addr, const RtcpSharerMessage&)>;
using RtcpRttCallback = std::function<void(base::TimeDelta)>;
/* typedef base::Callback<void(const RtcpSharerMessage&)>
 * RtcpSharerMessageCallback; */
/* typedef base::Callback<void(base::TimeDelta)> RtcpRttCallback; */
/* typedef */
/* base::Callback<void(const RtcpReceiverLogMessage&)> RtcpLogMessageCallback;
 */

// TODO(hubbe): Document members of this struct.
struct RtpReceiverStatistics {
  RtpReceiverStatistics();
  uint8_t fraction_lost;
  uint32_t cumulative_lost;  // 24 bits valid.
  uint32_t extended_high_sequence_number;
  uint32_t jitter;
};

// These are intended to only be created using Rtcp::ConvertToNTPAndSave.
struct RtcpTimeData {
  uint32_t ntp_seconds;
  uint32_t ntp_fraction;
  base::TimeTicks timestamp;
};

typedef uint32_t RtpTimestamp;

// This struct is used to encapsulate all the parameters of the
// SendRtcpFromRtpReceiver for IPC transportation.
struct SendRtcpFromRtpReceiver_Params {
  SendRtcpFromRtpReceiver_Params();
  ~SendRtcpFromRtpReceiver_Params();
  uint32_t ssrc;
  uint32_t sender_ssrc;
  RtcpTimeData time_data;
  std::unique_ptr<RtcpSharerMessage> sharer_message;
  base::TimeDelta target_delay;
  /* std::unique_ptr<std::vector<std::pair<RtpTimestamp, RtcpEvent> > >
   * rtcp_events; */
  std::unique_ptr<RtpReceiverStatistics> rtp_receiver_statistics;
};

#endif  // _RTCP_DEFINES_H_
