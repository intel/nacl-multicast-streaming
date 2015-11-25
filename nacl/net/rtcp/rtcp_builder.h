// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _RTCP_BUILDER_H_
#define _RTCP_BUILDER_H_

#include "base/big_endian.h"
#include "base/time/time.h"
#include "net/sharer_transport_config.h"
#include "net/rtcp/rtcp_defines.h"

static const uint32_t kSharer = ('C' << 24) + ('A' << 16) + ('S' << 8) + 'T';

enum RtcpPacketFields {
  kPacketTypeLow = 194,  // SMPTE time-code mapping.
  kPacketTypeSenderReport = 200,
  kPacketTypeReceiverReport = 201,
  kPacketTypeApplicationDefined = 204,
  kPacketTypeGenericRtpFeedback = 205,
  kPacketTypePayloadSpecific = 206,
  kPacketTypeXr = 207,
  kPacketTypeHigh = 210,  // Port Mapping.
};

class RtcpBuilder {
 public:
  explicit RtcpBuilder(uint32_t sending_ssrc);
  ~RtcpBuilder();

  PacketRef BuildRtcpFromReceiver(const RtcpReportBlock* report_block,
                                  const RtcpReceiverReferenceTimeReport* rrtr,
                                  const RtcpSharerMessage* sharer_message,
                                  base::TimeDelta target_delay);
  PacketRef BuildRtcpFromSender(const RtcpSenderInfo& sender_info);
  PacketRef BuildPauseRtcpFromSender(const RtcpPauseResumeMessage& pause_info);

 private:
  void AddRtcpHeader(RtcpPacketFields payload, int format_or_count);
  void PatchLengthField();
  void AddSR(const RtcpSenderInfo& sender_info);
  void AddRR(const RtcpReportBlock* report_block);
  void AddReportBlocks(const RtcpReportBlock& report_block);
  void AddRrtr(const RtcpReceiverReferenceTimeReport* rrtr);
  void AddSharer(const RtcpSharerMessage* sharer_message,
                 base::TimeDelta target_delay);
  void AddPausedIndication(const RtcpPauseResumeMessage& pause_message);

  /* void AddDlrrRb(const RtcpDlrrReportBlock& dlrr); */

  /* bool GetRtcpReceiverLogMessage( */
  /*     const ReceiverRtcpEventSubscriber::RtcpEvents& rtcp_events, */
  /*     RtcpReceiverLogMessage* receiver_log_message, */
  /*     size_t* total_number_of_messages_to_send); */

  void Start();
  PacketRef Finish();

  BigEndianWriter writer_;
  const uint32_t ssrc_;
  char* ptr_of_length_;
  PacketRef packet_;
};

#endif  // _RTCP_BUILDER_H_
