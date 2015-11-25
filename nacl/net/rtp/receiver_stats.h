// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _RECEIVER_STATS_H_
#define _RECEIVER_STATS_H_

#include "net/rtp/rtp.h"

#include "ppapi/c/pp_time.h"

class ReceiverStats {
 public:
  explicit ReceiverStats();

  RtpReceiverStatistics GetStatistics();
  void UpdateStatistics(const RTP& header);

 private:
  // Global metrics.
  uint16_t min_sequence_number_;
  uint16_t max_sequence_number_;
  uint32_t total_number_packets_;
  uint16_t sequence_number_cycles_;
  PP_TimeDelta last_received_timestamp_;
  PP_Time last_received_packet_time_;
  PP_TimeDelta jitter_;

  // Intermediate metrics - between RTCP reports.
  int interval_min_sequence_number_;
  int interval_number_packets_;
  int interval_wrap_count_;
};

#endif  // _RECEIVER_STATS_H_
