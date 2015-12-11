// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logging/stats_event_subscriber.h"

#include "ppapi/cpp/logging.h"

namespace sharer {

StatsEventSubscriber::StatsEventSubscriber()
    : packets_sent_(0), packets_retransmitted_(0) {}

StatsEventSubscriber::~StatsEventSubscriber() {}

void StatsEventSubscriber::OnReceiveFrameEvent(const FrameEvent& frame_event) {
  // Not handling frame events yet
}

void StatsEventSubscriber::OnReceivePacketEvent(
    const PacketEvent& packet_event) {
  if (packet_event.type == PACKET_SENT_TO_NETWORK)
    packets_sent_++;
  else if (packet_event.type == PACKET_RETRANSMITTED)
    packets_retransmitted_++;
}

void StatsEventSubscriber::Reset() {
  packets_sent_ = 0;
  packets_retransmitted_ = 0;
}

}  // namespace sharer
