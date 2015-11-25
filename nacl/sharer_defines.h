// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHARER_DEFINES_H_
#define SHARER_DEFINES_H_

#include "base/macros.h"
#include "base/time/time.h"

#include "ppapi/c/pp_time.h"
#include "ppapi/cpp/instance.h"
#include "ppapi/cpp/media_stream_video_track.h"

namespace sharer {

const uint32_t kStartFrameId = UINT32_C(0xffffffff);
const uint32_t kVideoFrequency = 90000;

// This is an important system-wide constant.  This limits how much history the
// implementation must retain in order to process the acknowledgements of past
// frames.
// This value is carefully choosen such that it fits in the 8-bits range for
// frame IDs. It is also less than half of the full 8-bits range such that we
// can handle wrap around and compare two frame IDs.

const int kMaxUnackedFrames = 1000;

enum DefaultSettings {
  kDefaultAudioEncoderBitrate = 0,  // This means "auto," and may mean VBR.
  kDefaultAudioSamplingRate = 48000,
  kDefaultMaxQp = 63,
  kDefaultMinQp = 4,
  kDefaultMaxFrameRate = 30,
  kDefaultNumberOfVideoBuffers = 1,
  kDefaultRtcpIntervalMs = 500,
  kDefaultRtpHistoryMs = 1000,
  kDefaultRtpMaxDelayMs = 100,
};

// kRtcpSharerLastPacket is used in PacketIDSet to ask for
// the last packet of a frame to be retransmitted.
const uint16_t kRtcpSharerLastPacket = 0xfffe;

inline int64_t PP_TimeDeltaToRtpDelta(PP_TimeDelta delta, int rtp_timebase) {
  PP_DCHECK(rtp_timebase > 0);
  return delta * rtp_timebase;
}

inline int64_t TimeDeltaToRtpDelta(base::TimeDelta delta, int rtp_timebase) {
  PP_DCHECK(rtp_timebase > 0);
  return delta * rtp_timebase / base::TimeDelta::FromSeconds(1);
}

const size_t kMinLengthOfRtcp = 8;

inline base::TimeDelta ConvertFromNtpDiff(uint32_t ntp_delay) {
  uint32_t delay_ms = (ntp_delay & 0x0000ffff) * 1000;
  delay_ms >>= 16;
  delay_ms += ((ntp_delay & 0xffff0000) >> 16) * 1000;
  return base::TimeDelta::FromMilliseconds(delay_ms);
}

}  // namespace sharer

#endif  // SHARER_DEFINES_H_
