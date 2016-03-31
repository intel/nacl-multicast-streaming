// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sharer_config.h"

ReceiverConfig::ReceiverConfig()
    : sender_ssrc(0),
      rtp_max_delay_ms(100),
      target_frame_rate(0),
      rtp_timebase(1) {}

ReceiverConfig::~ReceiverConfig() {}

namespace sharer {

ReceiverNetConfig::ReceiverNetConfig()
    : address("127.0.0.1"),
      port(5004) {}

ReceiverNetConfig::~ReceiverNetConfig() {}

SenderConfig::SenderConfig()
    : initial_bitrate(1000),
      frame_rate(30),
      remote_address("127.0.0.1"),
      remote_port(5004),
      multicast(false) {}
SenderConfig::~SenderConfig() {}

}  // namespace sharer
