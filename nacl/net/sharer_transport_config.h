// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _CAST_TRANSPORT_CONFIG_H_
#define _CAST_TRANSPORT_CONFIG_H_

#include "base/time/time.h"

#include "ppapi/cpp/completion_callback.h"

#include <cstdint>
#include <string>
#include <vector>

// Return a mutable char* pointing to a string's internal buffer,
// which may not be null-terminated. Writing through this pointer will
// modify the string.
//
// string_as_array(&str)[i] is valid for 0 <= i < str.size() until the
// next call to a string method that invalidates iterators.
//
// As of 2006-04, there is no standard-blessed way of getting a
// mutable reference to a string's internal buffer. However, issue 530
// (http://www.open-std.org/JTC1/SC22/WG21/docs/lwg-active.html#530)
// proposes this as the method. According to Matt Austern, this should
// already work on all current implementations.
inline char* string_as_array(std::string* str) {
  // DO NOT USE const_cast<char*>(str->data())
  return str->empty() ? NULL : &*str->begin();
}

struct SharerTransportRtpConfig {
  SharerTransportRtpConfig();
  ~SharerTransportRtpConfig();

  uint32_t ssrc;
  uint32_t feedback_ssrc;
  int rtp_payload_type;
};

// A combination of metadata and data for one encoded frame.  This can contain
// audio data or video data or other.
struct EncodedFrame {
  enum Dependency {
    // "null" value, used to indicate whether |dependency| has been set.
    UNKNOWN_DEPENDENCY,

    // Not decodable without the reference frame indicated by
    // |referenced_frame_id|.
    DEPENDENT,

    // Independently decodable.
    INDEPENDENT,

    // Independently decodable, and no future frames will depend on any frames
    // before this one.
    KEY,

    DEPENDENCY_LAST = KEY
  };

  EncodedFrame();
  ~EncodedFrame();

  // Convenience accessors to data as an array of uint8 elements.
  const uint8_t* bytes() const {
    return reinterpret_cast<uint8_t*>(
        string_as_array(const_cast<std::string*>(&data)));
  }
  uint8_t* mutable_bytes() {
    return reinterpret_cast<uint8_t*>(string_as_array(&data));
  }

  // Copies all data members except |data| to |dest|.
  // Does not modify |dest->data|.
  void CopyMetadataTo(EncodedFrame* dest) const;

  // This frame's dependency relationship with respect to other frames.
  Dependency dependency;

  // The label associated with this frame.  Implies an ordering relative to
  // other frames in the same stream.
  uint32_t frame_id;

  // The label associated with the frame upon which this frame depends.  If
  // this frame does not require any other frame in order to become decodable
  // (e.g., key frames), |referenced_frame_id| must equal |frame_id|.
  uint32_t referenced_frame_id;

  // The stream timestamp, on the timeline of the signal data.  For example, RTP
  // timestamps for audio are usually defined as the total number of audio
  // samples encoded in all prior frames.  A playback system uses this value to
  // detect gaps in the stream, and otherwise stretch the signal to match
  // playout targets.
  uint32_t rtp_timestamp;

  // The common reference clock timestamp for this frame.  This value originates
  // from a sender and is used to provide lip synchronization between streams in
  // a receiver.  Thus, in the sender context, this is set to the time at which
  // the frame was captured/recorded.  In the receiver context, this is set to
  // the target playout time.  Over a sequence of frames, this time value is
  // expected to drift with respect to the elapsed time implied by the RTP
  // timestamps; and it may not necessarily increment with precise regularity.
  base::TimeTicks reference_time;

  // Playout delay for this and all future frames. Used by the Adaptive
  // Playout delay extension. Zero means no change.
  uint16_t new_playout_delay_ms;

  // The encoded signal data.
  std::string data;
};

using Packet = std::vector<uint8_t>;
using PacketRef = std::shared_ptr<Packet>;
using PacketList = std::vector<PacketRef>;

struct RtcpReportBlock {
  RtcpReportBlock();
  ~RtcpReportBlock();
  uint32_t remote_ssrc;  // SSRC of sender of this report.
  uint32_t media_ssrc;   // SSRC of the RTP packet sender.
  uint8_t fraction_lost;
  uint32_t cumulative_lost;  // 24 bits valid.
  uint32_t extended_high_sequence_number;
  uint32_t jitter;
  uint32_t last_sr;
  uint32_t delay_since_last_sr;
};

class PacketSender {
 public:
  virtual bool SendPacket(const std::string& addr, PacketRef packet,
                          const pp::CompletionCallback& cb) = 0;

  virtual int64_t GetBytesSent() = 0;

  virtual ~PacketSender() {}
};

struct RtcpSenderInfo {
  RtcpSenderInfo();
  ~RtcpSenderInfo();
  // First three members are used for lipsync.
  // First two members are used for rtt.
  uint32_t ntp_seconds;
  uint32_t ntp_fraction;
  uint32_t rtp_timestamp;
  uint32_t send_packet_count;
  size_t send_octet_count;
};

#endif  // _CAST_TRANSPORT_CONFIG_H_
