// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "receiver_stats.h"

#include "net/rtp/rtp.h"
#include "net/rtp/rtp_receiver_defines.h"

#include "ppapi/cpp/core.h"
#include "ppapi/cpp/module.h"

#include <cstdlib>

ReceiverStats::ReceiverStats()
    : min_sequence_number_(0),
      max_sequence_number_(0),
      total_number_packets_(0),
      sequence_number_cycles_(0),
      interval_min_sequence_number_(0),
      interval_number_packets_(0),
      interval_wrap_count_(0) {}

RtpReceiverStatistics ReceiverStats::GetStatistics() {
  RtpReceiverStatistics ret;
  // Compute losses.
  if (interval_number_packets_ == 0) {
    ret.fraction_lost = 0;
  } else {
    int diff = 0;
    if (interval_wrap_count_ == 0) {
      diff = max_sequence_number_ - interval_min_sequence_number_ + 1;
    } else {
      diff = kMaxSequenceNumber * (interval_wrap_count_ - 1) +
             (max_sequence_number_ - interval_min_sequence_number_ +
              kMaxSequenceNumber + 1);
    }

    if (diff < 1) {
      ret.fraction_lost = 0;
    } else {
      float tmp_ratio =
          (1 - static_cast<float>(interval_number_packets_) / std::abs(diff));
      ret.fraction_lost = static_cast<uint8_t>(256 * tmp_ratio);
    }
  }

  int expected_packets_num = max_sequence_number_ - min_sequence_number_ + 1;
  if (total_number_packets_ == 0) {
    ret.cumulative_lost = 0;
  } else if (sequence_number_cycles_ == 0) {
    ret.cumulative_lost = expected_packets_num - total_number_packets_;
  } else {
    ret.cumulative_lost =
        kMaxSequenceNumber * (sequence_number_cycles_ - 1) +
        (expected_packets_num - total_number_packets_ + kMaxSequenceNumber);
  }

  // Extended high sequence number consists of the highest seq number and the
  // number of cycles (wrap).
  ret.extended_high_sequence_number =
      (sequence_number_cycles_ << 16) + max_sequence_number_;

  // Jitter in milliseconds
  int jit_ms = jitter_ * 1000;
  ret.jitter = static_cast<uint32_t>(std::abs(jit_ms));

  // Reset interval values.
  interval_min_sequence_number_ = 0;
  interval_number_packets_ = 0;
  interval_wrap_count_ = 0;

  return ret;
}

void ReceiverStats::UpdateStatistics(const RTP& packet) {
  const uint16_t new_seq_num = packet.sequence();

  if (interval_number_packets_ == 0) {
    // First packet in the interval.
    interval_min_sequence_number_ = new_seq_num;
  }
  if (total_number_packets_ == 0) {
    // First incoming packet.
    min_sequence_number_ = new_seq_num;
    max_sequence_number_ = new_seq_num;
  }

  if (IsNewerSequenceNumber(new_seq_num, max_sequence_number_)) {
    // Check wrap.
    if (new_seq_num < max_sequence_number_) {
      ++sequence_number_cycles_;
      ++interval_wrap_count_;
    }
    max_sequence_number_ = new_seq_num;
  }

  // Compute Jitter.
  const PP_Time now = pp::Module::Get()->core()->GetTime();
  PP_TimeDelta delta_new_timestamp = packet.timestamp();
  delta_new_timestamp /= 1000;  // store in seconds, not milisecods
  if (total_number_packets_ > 0) {
    // Update jitter.
    PP_TimeDelta delta =
        (now - last_received_packet_time_) -
        ((delta_new_timestamp - last_received_timestamp_) / 90);
    jitter_ += (delta - jitter_) / 16;
  }
  last_received_timestamp_ = delta_new_timestamp;
  last_received_packet_time_ = now;

  // Increment counters.
  ++total_number_packets_;
  ++interval_number_packets_;
}
