// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rtp_receiver_defines.h"

RtpSharerHeader::RtpSharerHeader()
    : marker(false),
      payload_type(0),
      sequence_number(0),
      rtp_timestamp(0),
      sender_ssrc(0),
      is_key_frame(false),
      frame_id(0),
      packet_id(0),
      max_packet_id(0),
      reference_frame_id(0),
      new_playout_delay_ms(0) {}

RtpPayloadFeedback::~RtpPayloadFeedback() {}
