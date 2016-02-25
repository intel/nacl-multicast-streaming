// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logging/stats_event_subscriber.h"

#include "base/logger.h"

#include "ppapi/cpp/logging.h"
#include <iomanip>

namespace sharer {

StatsEventSubscriber::StatsEventSubscriber()
    : packets_total_(0), packets_sent_(0), packets_retransmitted_(0), packets_rejected_(0) {}

StatsEventSubscriber::~StatsEventSubscriber() {}

void StatsEventSubscriber::OnReceiveFrameEvent(const FrameEvent& frame_event) {
  // Not handling frame events yet
}

void StatsEventSubscriber::OnReceivePacketEvent(
    const PacketEvent& packet_event) {
  packets_total_++;

  if (packet_event.type == PACKET_SENT_TO_NETWORK)
    packets_sent_++;
  else if (packet_event.type == PACKET_RETRANSMITTED)
    packets_retransmitted_++;
  else if (packet_event.type == PACKET_RTX_REJECTED)
    packets_rejected_++;
}

void StatsEventSubscriber::Reset() {
  packets_total_ = 0;
  packets_sent_ = 0;
  packets_retransmitted_ = 0;
  packets_rejected_ = 0;
}

void StatsEventSubscriber::PrintPackets() const {
  float sent = 0, retransmitted = 0, rejected = 0;

  if (packets_total_ > 0) {
    sent = (float)packets_sent_ / packets_total_;
    retransmitted = (float)packets_retransmitted_ / packets_total_;
    rejected = (float)packets_rejected_ / packets_total_;
  }

  DINF() << "Packets Sent Info";
  DINF() << "Total Packets: " << packets_total_;
  DINF() << "Multicast Packets: " << packets_sent_
         << std::fixed << std::setprecision(2)
         << " (" << sent * 100 << "%)";
  DINF() << "Retransmitted Packets: " << packets_retransmitted_
         << std::fixed << std::setprecision(2)
         << " (" << retransmitted * 100 << "%)";
  DINF() << "Rejected Packets: " << packets_rejected_
         << std::fixed << std::setprecision(2)
         << " (" << rejected * 100 << "%)";
}

void StatsEventSubscriber::PrintFrames() const {
}

}  // namespace sharer
