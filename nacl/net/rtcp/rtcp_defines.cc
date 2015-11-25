// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rtcp_defines.h"

RtcpSharerMessage::RtcpSharerMessage(uint32_t ssrc)
    : media_ssrc(ssrc),
      ack_frame_id(0u),
      target_delay_ms(0),
      request_key_frame(false) {}
RtcpSharerMessage::RtcpSharerMessage()
    : media_ssrc(0),
      ack_frame_id(0u),
      target_delay_ms(0),
      request_key_frame(false) {}
RtcpSharerMessage::~RtcpSharerMessage() {}
RtcpPauseResumeMessage::RtcpPauseResumeMessage() : last_sent(0), pause_id(0) {}
RtcpPauseResumeMessage::~RtcpPauseResumeMessage() {}
RtcpNackMessage::RtcpNackMessage() : remote_ssrc(0u) {}
RtcpNackMessage::~RtcpNackMessage() {}

RtcpReceiverReferenceTimeReport::RtcpReceiverReferenceTimeReport()
    : remote_ssrc(0u), ntp_seconds(0u), ntp_fraction(0u) {}
RtcpReceiverReferenceTimeReport::~RtcpReceiverReferenceTimeReport() {}

SendRtcpFromRtpReceiver_Params::SendRtcpFromRtpReceiver_Params()
    : ssrc(0), sender_ssrc(0) {}

SendRtcpFromRtpReceiver_Params::~SendRtcpFromRtpReceiver_Params() {}
