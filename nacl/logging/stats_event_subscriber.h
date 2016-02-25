// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGGING_STATS_EVENT_SUBSCRIBER_H_
#define LOGGING_STATS_EVENT_SUBSCRIBER_H_

#include <vector>

#include "base/macros.h"
#include "logging/raw_event_subscriber.h"

namespace sharer {

// RawEventSubscriber implementation that counts all incoming raw events
class StatsEventSubscriber : public RawEventSubscriber {
 public:
  StatsEventSubscriber();

  ~StatsEventSubscriber() final;

  // RawEventSubscriber implementations.
  void OnReceiveFrameEvent(const FrameEvent& frame_event) final;
  void OnReceivePacketEvent(const PacketEvent& packet_event) final;

  // Custom methods
  void Reset();

  void PrintPackets() const;
  void PrintFrames() const;

  int packets_sent() const { return packets_sent_; }
  int packets_retransmitted() const { return packets_retransmitted_; }

 private:
  int packets_total_;
  int packets_sent_;
  int packets_retransmitted_;
  int packets_rejected_;

  DISALLOW_COPY_AND_ASSIGN(StatsEventSubscriber);
};

}  // namespace sharer

#endif  // LOGGING_STATS_EVENT_SUBSCRIBER_H_
