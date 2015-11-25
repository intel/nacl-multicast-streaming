// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/sharer_transport_config.h"

#include "ppapi/cpp/logging.h"

SharerTransportRtpConfig::SharerTransportRtpConfig()
    : ssrc(0), feedback_ssrc(0), rtp_payload_type(0) {}

SharerTransportRtpConfig::~SharerTransportRtpConfig() {}

EncodedFrame::EncodedFrame()
    : dependency(UNKNOWN_DEPENDENCY),
      frame_id(0),
      referenced_frame_id(0),
      rtp_timestamp(0),
      new_playout_delay_ms(0) {}

EncodedFrame::~EncodedFrame() {}

void EncodedFrame::CopyMetadataTo(EncodedFrame* dest) const {
  PP_DCHECK(dest);
  dest->dependency = this->dependency;
  dest->frame_id = this->frame_id;
  dest->referenced_frame_id = this->referenced_frame_id;
  dest->rtp_timestamp = this->rtp_timestamp;
  dest->reference_time = this->reference_time;
}

RtcpReportBlock::RtcpReportBlock()
    : remote_ssrc(0),
      media_ssrc(0),
      fraction_lost(0),
      cumulative_lost(0),
      extended_high_sequence_number(0),
      jitter(0),
      last_sr(0),
      delay_since_last_sr(0) {}
RtcpReportBlock::~RtcpReportBlock() {}

RtcpSenderInfo::RtcpSenderInfo()
    : ntp_seconds(0),
      ntp_fraction(0),
      rtp_timestamp(0),
      send_packet_count(0),
      send_octet_count(0) {}
RtcpSenderInfo::~RtcpSenderInfo() {}
