// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/rtp/rtp.h"

#include "base/big_endian.h"
#include "base/logger.h"
#include "base/ptr_utils.h"

#include "ppapi/cpp/var.h"

#include <string>
#include <sstream>

static FrameIdWrapHelper videoId = FrameIdWrapHelper();
static FrameIdWrapHelper audioId = FrameIdWrapHelper();

RtpReceiverStatistics::RtpReceiverStatistics()
    : fraction_lost(0),
      cumulative_lost(0),
      extended_high_sequence_number(0),
      jitter(0) {}

RTPBase::RTPBase(const unsigned char* data, int32_t size, bool rtcp)
    : buffer_(&data[0], &data[0] + size), rtcp_(rtcp) {}

RTPBase::~RTPBase() {}

RTP::RTP(const unsigned char* data, int32_t size, unsigned char pt)
    : RTPBase(data, size, false),
      payloadType_(pt),
      valid_(true),
      ssrc_(0),
      timestamp_(0),
      sequence_(0),
      payload_(0),
      payloadSize_(0),
      is_key_frame_(false),
      packet_id_(0),
      max_packet_id_(0),
      frame_id_(0),
      reference_frame_id_(0),
      new_playout_delay_ms_(0) {
  BigEndianReader reader(reinterpret_cast<const char*>(buffer_.data()), size);
  reader.Skip(2);

  if (!reader.ReadU16(&sequence_)) {
    valid_ = false;
    return;
  }

  if (!reader.ReadU32(&timestamp_)) {
    valid_ = false;
    return;
  }

  if (!reader.ReadU32(&ssrc_)) {
    valid_ = false;
    return;
  }

  if (payloadType_ == RTP::VIDEO && ssrc_ != 11) {
    valid_ = false;
    return;
  }

  if (payloadType_ == RTP::AUDIO && ssrc_ != 1) {
    valid_ = false;
    return;
  }

  uint8_t bits;
  if (!reader.ReadU8(&bits)) {
    valid_ = false;
    return;
  }

  is_key_frame_ = !!(bits & 0x80);
  const bool includes_specific_frame_reference = !!(bits & 0x40);

  /* uint8_t truncated_frame_id; */
  /* if (!reader.ReadU8(&truncated_frame_id) || */
  if (!reader.ReadU32(&frame_id_) || !reader.ReadU16(&packet_id_) ||
      !reader.ReadU16(&max_packet_id_)) {
    valid_ = false;
    return;
  }

  if (max_packet_id_ < packet_id_) {
    valid_ = false;
    return;
  }

  /* uint8_t truncated_reference_frame_id; */
  if (!includes_specific_frame_reference) {
    reference_frame_id_ = frame_id_;
    /* truncated_reference_frame_id = truncated_frame_id; */
    if (!is_key_frame_) {
      --reference_frame_id_;
    }
  } else if (!reader.ReadU32(&reference_frame_id_)) {
    valid_ = false;
    return;
  }

  for (int i = 0; i < (bits & 0x3f); i++) {
    uint16_t type_and_size;
    if (!reader.ReadU16(&type_and_size)) {
      valid_ = false;
      return;
    }

    base::StringPiece tmp;
    if (!reader.ReadPiece(&tmp, type_and_size & 0x3ff)) {
      valid_ = false;
      return;
    }

    BigEndianReader chunk(tmp.data(), tmp.size());
    switch (type_and_size >> 10) {
      case 1:
        if (!chunk.ReadU16(&new_playout_delay_ms_)) {
          valid_ = false;
          return;
        }
    }
  }

  payload_ = reinterpret_cast<const unsigned char*>(reader.ptr());
  payloadSize_ = reader.remaining();
}

RTCP::RTCP(const unsigned char* data, int32_t size)
    : RTPBase(data, size, true) {
  BigEndianReader reader(reinterpret_cast<const char*>(data), size);

  uint8_t bits;
  reader.ReadU8(&bits);
  reader.ReadU8(&payloadType_);
  uint16_t length;
  reader.ReadU16(&length);  // not used, just read and discard
  reader.ReadU32(&ssrc_);
  reader.ReadU32(&ntp_seconds_);
  reader.ReadU32(&ntp_fraction_);
  reader.ReadU32(&rtp_timestamp_);
  reader.ReadU32(&send_packet_count_);
  reader.ReadU32(&send_octet_count_);
}

static bool parseVersion(unsigned char byte0) { return (byte0 >> 6) == 2; }

static std::unique_ptr<RTCP> parseRTCP(pp::Instance* instance,
                                       const unsigned char* data, int32_t size,
                                       uint32_t* ssrc) {
  if ((data[1] != RTCP::SR) && (data[1] != RTCP::RR) &&
      (data[1] != RTCP::RTPFB)) {
    return NULL;
  }

  if (size < 28) {
    ERR() << "header is RTCP, but packet size is too small.";
    return NULL;
  }

  int32_t length = ((data[2] << 8) + data[3] + 1) * 4;
  if (length != size) {
    ERR() << "wrong size: " << size << ", parsed size: " << length;
    return NULL;
  }

  auto rtcp = make_unique<RTCP>(data, size);
  if (ssrc) *ssrc = rtcp->ssrc();

  return std::move(rtcp);
}

static std::unique_ptr<RTP> parseRTP(pp::Instance* instance,
                                     const unsigned char* data, int32_t size,
                                     uint32_t* ssrc) {
  unsigned char pt = data[1];

  pt = pt & 0x7f;
  if ((pt != RTP::VIDEO) && (pt != RTP::AUDIO)) {
    WRN() << "Not video or audio packet. Payload type: "
          << static_cast<int>(pt);
    return nullptr;
  }

  auto rtp = make_unique<RTP>(data, size, pt);
  if (!rtp->isValid()) {
    WRN() << "Created packet is not valid.";
    return nullptr;
  }

  if (ssrc) *ssrc = rtp->ssrc();

  return std::move(rtp);
}

std::unique_ptr<RTPBase> rtpParse(pp::Instance* instance,
                                  const unsigned char* data, int32_t size,
                                  uint32_t* ssrc) {
  std::unique_ptr<RTPBase> ret;

  if (size <= 8) {
    ERR() << "Packet too small: " << size;
    return NULL;
  }

  if (!parseVersion(data[0])) {
    ERR() << "wrong RTP version.";
    return NULL;
  }

  ret = parseRTCP(instance, data, size, ssrc);
  if (ret) {
    return std::move(ret);
  }

  ret = parseRTP(instance, data, size, ssrc);

  return std::move(ret);
}
