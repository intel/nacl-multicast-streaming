// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _RTP_
#define _RTP_

#include "net/rtcp/rtcp_defines.h"

#include "ppapi/cpp/instance.h"
#include "sharer_defines.h"

#include <vector>
#include <memory>

class FrameIdWrapHelper {
 public:
  FrameIdWrapHelper() : largest_frame_id_seen_(sharer::kStartFrameId) {}

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
  uint32_t largest_frame_id_seen_;
};

class RTPBase {
 public:
  RTPBase(const unsigned char* data, int32_t size, bool rtcp);
  ~RTPBase();
  bool isRTP() const { return !rtcp_; }
  bool isRTCP() const { return rtcp_; }

 protected:
  std::vector<uint8_t> buffer_;
  bool rtcp_;
};

class RTP : public RTPBase {
 public:
  enum PayloadType { VIDEO = 96, AUDIO = 127 };
  RTP(const unsigned char* data, int32_t size, unsigned char pt);
  bool isValid() const { return valid_; }

  unsigned char getPayloadType() const { return payloadType_; }
  uint16_t sequence() const { return sequence_; }
  const unsigned char* payload() const { return payload_; }
  int32_t payloadSize() const { return payloadSize_; }
  uint32_t ssrc() const { return ssrc_; }
  uint32_t timestamp() const { return timestamp_; }

  bool isKeyFrame() const { return is_key_frame_; }
  uint16_t packetId() const { return packet_id_; }
  uint16_t maxPacketId() const { return max_packet_id_; }
  uint32_t frameId() const { return frame_id_; }
  uint32_t referenceFrameId() const { return reference_frame_id_; }
  uint16_t newPlayoutDelayMs() const { return new_playout_delay_ms_; }

 private:
  unsigned char payloadType_;
  bool valid_;
  uint32_t ssrc_;
  uint32_t timestamp_;
  uint16_t sequence_;
  const unsigned char* payload_;
  int32_t payloadSize_;

  // Sharer protocol
  bool is_key_frame_;
  uint16_t packet_id_;
  uint16_t max_packet_id_;
  uint32_t frame_id_;
  uint32_t reference_frame_id_;
  uint16_t new_playout_delay_ms_;
};

class RTCP : public RTPBase {
 public:
  enum PayloadType {
    SR = 200,
    RR = 201,
    SDES = 202,
    BYE = 203,
    APP = 204,
    RTPFB = 205
  };

  RTCP(const unsigned char* data, int32_t size);
  uint32_t ssrc() const { return ssrc_; }
  uint8_t payloadType() const { return payloadType_; }
  uint32_t ntpSeconds() const { return ntp_seconds_; }
  uint32_t ntpFraction() const { return ntp_fraction_; }
  uint32_t rtpTimestamp() const { return rtp_timestamp_; }
  uint32_t sendPacktCount() const { return send_packet_count_; }
  uint32_t sendOctetCount() const { return send_octet_count_; }

 private:
  uint32_t ssrc_;
  uint8_t payloadType_;
  uint32_t ntp_seconds_;
  uint32_t ntp_fraction_;
  uint32_t rtp_timestamp_;
  uint32_t send_packet_count_;
  uint32_t send_octet_count_;
};

std::unique_ptr<RTPBase> rtpParse(pp::Instance* instance,
                                  const unsigned char* data, int32_t size,
                                  uint32_t* ssrc);

#endif  // _RTP_
