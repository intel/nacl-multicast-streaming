// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_CAST_TRANSPORT_DEFINES_H_
#define NET_CAST_TRANSPORT_DEFINES_H_

#include "base/macros.h"
#include "base/time/time.h"

#include <stdint.h>

#include <map>
#include <set>
#include <string>

namespace sharer {

// TODO(mikhal): Implement and add more types.
enum SharerTransportStatus {
  TRANSPORT_AUDIO_UNINITIALIZED = 0,
  TRANSPORT_VIDEO_UNINITIALIZED,
  TRANSPORT_AUDIO_INITIALIZED,
  TRANSPORT_VIDEO_INITIALIZED,
  TRANSPORT_INVALID_CRYPTO_CONFIG,
  TRANSPORT_SOCKET_ERROR,
  CAST_TRANSPORT_STATUS_LAST = TRANSPORT_SOCKET_ERROR
};

// Rtcp defines.

enum RtcpPacketFields {
  kPacketTypeLow = 194,  // SMPTE time-code mapping.
  kPacketTypeSenderReport = 200,
  kPacketTypeReceiverReport = 201,
  kPacketTypeApplicationDefined = 204,
  kPacketTypeGenericRtpFeedback = 205,
  kPacketTypePayloadSpecific = 206,
  kPacketTypeXr = 207,
  kPacketTypeHigh = 210,  // Port Mapping.
};

class FrameIdWrapHelperTest;

// TODO(miu): UGLY IN-LINE DEFINITION IN HEADER FILE!  Move to appropriate
// location, separated into .h and .cc files.
class FrameIdWrapHelper {
 public:
  FrameIdWrapHelper() : largest_frame_id_seen_(kStartFrameId) {}

  uint32_t MapTo32bitsFrameId(const uint8_t over_the_wire_frame_id) {
    uint32_t ret = (largest_frame_id_seen_ & ~0xff) | over_the_wire_frame_id;
    // Add 1000 to both sides to avoid underflows.
    if (1000 + ret - largest_frame_id_seen_ > 1000 + 127) {
      ret -= 0x100;
    } else if (1000 + ret - largest_frame_id_seen_ < 1000 - 128) {
      ret += 0x100;
    }
    if (1000 + ret - largest_frame_id_seen_ > 1000) {
      largest_frame_id_seen_ = ret;
    }
    return ret;
  }

 private:
  friend class FrameIdWrapHelperTest;
  static const uint32_t kStartFrameId = UINT32_C(0xffffffff);

  uint32_t largest_frame_id_seen_;

  DISALLOW_COPY_AND_ASSIGN(FrameIdWrapHelper);
};

using TransportInitializedCb = std::function<void(bool result)>;

}  // namespace sharer

#endif  // NET_CAST_TRANSPORT_DEFINES_H_
