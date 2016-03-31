// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAST_CONFIG_H_
#define CAST_CONFIG_H_

#include "ppapi/cpp/var_dictionary.h"

#include <functional>
#include <stdint.h>
#include <string>

struct ReceiverConfig {
  ReceiverConfig();
  ~ReceiverConfig();
  uint32_t receiver_ssrc;
  uint32_t sender_ssrc;
  int rtp_max_delay_ms;
  int target_frame_rate;
  int rtp_timebase;
};

// TODO: Expand namespace to receiver too
namespace sharer {

struct ReceiverNetConfig {
  ReceiverNetConfig();
  ~ReceiverNetConfig();
  std::string address;
  uint16_t port;
};

struct SenderConfig {
  SenderConfig();
  ~SenderConfig();
  uint32_t initial_bitrate;
  double frame_rate;

  std::string remote_address;
  uint16_t remote_port;
  bool multicast;
};

}  // namespace sharer

#endif  // CAST_CONFIG_H_
