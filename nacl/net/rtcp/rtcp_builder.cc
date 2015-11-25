// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rtcp_builder.h"

#include "rtcp_defines.h"

#include <sstream>

// Max delta is 4095 milliseconds because we need to be able to encode it in
// 12 bits.
/* const int64_t kMaxWireFormatTimeDeltaMs = INT64_C(0xfff); */

/* static const size_t kRtcpMaxReceiverLogMessages = 256; */
static const size_t kRtcpMaxSharerLossFields = 100;

// A class to build a string representing the NACK list in Sharer message.
//
// The string will look like "23:3-6 25:1,5-6", meaning packets 3 to 6 in frame
// 23 are being NACK'ed (i.e. they are missing from the receiver's point of
// view) and packets 1, 5 and 6 are missing in frame 25. A frame that is
// completely missing will show as "26:65535".
class NackStringBuilder {
 public:
  NackStringBuilder()
      : frame_count_(0),
        packet_count_(0),
        last_frame_id_(-1),
        last_packet_id_(-1),
        contiguous_sequence_(false) {}
  ~NackStringBuilder() {}

  bool Empty() const { return frame_count_ == 0; }

  void PushFrame(int frame_id) {
    if (frame_count_ > 0) {
      if (frame_id == last_frame_id_) {
        return;
      }
      if (contiguous_sequence_) {
        stream_ << "-" << last_packet_id_;
      }
      stream_ << ", ";
    }
    stream_ << frame_id;
    last_frame_id_ = frame_id;
    packet_count_ = 0;
    contiguous_sequence_ = false;
    ++frame_count_;
  }

  void PushPacket(int packet_id) {
    /* DCHECK_GE(last_frame_id_, 0); */
    /* DCHECK_GE(packet_id, 0); */
    if (packet_count_ == 0) {
      stream_ << ":" << packet_id;
    } else if (packet_id == last_packet_id_ + 1) {
      contiguous_sequence_ = true;
    } else {
      if (contiguous_sequence_) {
        stream_ << "-" << last_packet_id_;
        contiguous_sequence_ = false;
      }
      stream_ << "," << packet_id;
    }
    ++packet_count_;
    last_packet_id_ = packet_id;
  }

  std::string GetString() {
    if (contiguous_sequence_) {
      stream_ << "-" << last_packet_id_;
      contiguous_sequence_ = false;
    }
    return stream_.str();
  }

 private:
  std::ostringstream stream_;
  int frame_count_;
  int packet_count_;
  int last_frame_id_;
  int last_packet_id_;
  bool contiguous_sequence_;
};

RtcpBuilder::RtcpBuilder(uint32_t sending_ssrc)
    : writer_(NULL, 0), ssrc_(sending_ssrc), ptr_of_length_(NULL) {}

RtcpBuilder::~RtcpBuilder() {}

void RtcpBuilder::PatchLengthField() {
  if (ptr_of_length_) {
    // Back-patch the packet length. The client must have taken
    // care of proper padding to 32-bit words.
    int this_packet_length = (writer_.ptr() - ptr_of_length_ - 2);
    /* DCHECK_EQ(0, this_packet_length % 4) */
    /*     << "Packets must be a multiple of 32 bits long"; */
    *ptr_of_length_ = this_packet_length >> 10;
    *(ptr_of_length_ + 1) = (this_packet_length >> 2) & 0xFF;
    ptr_of_length_ = NULL;
  }
}

// Set the 5-bit value in the 1st byte of the header
// and the payload type. Set aside room for the length field,
// and make provision for back-patching it.
void RtcpBuilder::AddRtcpHeader(RtcpPacketFields payload, int format_or_count) {
  PatchLengthField();
  writer_.WriteU8(0x80 | (format_or_count & 0x1F));
  writer_.WriteU8(payload);
  ptr_of_length_ = writer_.ptr();

  // Initialize length to "clearly illegal".
  writer_.WriteU16(0xDEAD);
}

void RtcpBuilder::Start() {
  packet_ = std::make_shared<Packet>();
  packet_->resize(kMaxIpPacketSize);
  writer_ = BigEndianWriter(reinterpret_cast<char*>(&((*packet_)[0])),
                            kMaxIpPacketSize);
}

PacketRef RtcpBuilder::Finish() {
  PatchLengthField();
  packet_->resize(kMaxIpPacketSize - writer_.remaining());
  writer_ = BigEndianWriter(nullptr, 0);
  PacketRef ret = packet_;
  packet_ = nullptr;
  return ret;
}

PacketRef RtcpBuilder::BuildRtcpFromReceiver(
    const RtcpReportBlock* report_block,
    const RtcpReceiverReferenceTimeReport* rrtr,
    const RtcpSharerMessage* sharer_message, base::TimeDelta target_delay) {
  Start();

  if (report_block) AddRR(report_block);
  if (rrtr) AddRrtr(rrtr);
  if (sharer_message) AddSharer(sharer_message, target_delay);

  return Finish();
}

PacketRef RtcpBuilder::BuildRtcpFromSender(const RtcpSenderInfo& sender_info) {
  Start();
  AddSR(sender_info);
  return Finish();
}

PacketRef RtcpBuilder::BuildPauseRtcpFromSender(
    const RtcpPauseResumeMessage& pause_info) {
  Start();
  AddPausedIndication(pause_info);
  return Finish();
}

void RtcpBuilder::AddSR(const RtcpSenderInfo& sender_info) {
  AddRtcpHeader(kPacketTypeSenderReport, 0);
  writer_.WriteU32(ssrc_);
  writer_.WriteU32(sender_info.ntp_seconds);
  writer_.WriteU32(sender_info.ntp_fraction);
  writer_.WriteU32(sender_info.rtp_timestamp);
  writer_.WriteU32(sender_info.send_packet_count);
  writer_.WriteU32(static_cast<uint32_t>(sender_info.send_octet_count));
}

void RtcpBuilder::AddRR(const RtcpReportBlock* report_block) {
  AddRtcpHeader(kPacketTypeReceiverReport, report_block ? 1 : 0);
  writer_.WriteU32(ssrc_);
  if (report_block) {
    AddReportBlocks(*report_block);  // Adds 24 bytes.
  }
}

void RtcpBuilder::AddReportBlocks(const RtcpReportBlock& report_block) {
  writer_.WriteU32(report_block.media_ssrc);
  writer_.WriteU8(report_block.fraction_lost);
  writer_.WriteU8(report_block.cumulative_lost >> 16);
  writer_.WriteU8(report_block.cumulative_lost >> 8);
  writer_.WriteU8(report_block.cumulative_lost);

  // Extended highest seq_no, contain the highest sequence number received.
  writer_.WriteU32(report_block.extended_high_sequence_number);
  writer_.WriteU32(report_block.jitter);

  // Last SR timestamp; our NTP time when we received the last report.
  // This is the value that we read from the send report packet not when we
  // received it.
  writer_.WriteU32(report_block.last_sr);

  // Delay since last received report, time since we received the report.
  writer_.WriteU32(report_block.delay_since_last_sr);
}

void RtcpBuilder::AddRrtr(const RtcpReceiverReferenceTimeReport* rrtr) {
  AddRtcpHeader(kPacketTypeXr, 0);
  writer_.WriteU32(ssrc_);  // Add our own SSRC.
  writer_.WriteU8(4);       // Add block type.
  writer_.WriteU8(0);       // Add reserved.
  writer_.WriteU16(2);      // Block length.

  // Add the media (received RTP) SSRC.
  writer_.WriteU32(rrtr->ntp_seconds);
  writer_.WriteU32(rrtr->ntp_fraction);
}

/*From Sender to Receiver*/
void RtcpBuilder::AddPausedIndication(
    const RtcpPauseResumeMessage& pause_message) {
  AddRtcpHeader(kPacketTypeGenericRtpFeedback, 4);
  writer_.WriteU32(ssrc_);  // Add our own SSRC.
  writer_.WriteU32(0);      // shall not be used - Remote SSRC.
  writer_.WriteU32(2);
  writer_.WriteU32(2);  //(0010 0001)length of type specific - 32 bit word
  writer_.WriteU32(pause_message.pause_id);   // the pause identification
  writer_.WriteU32(pause_message.last_sent);  // sending the last frame sent
}

void RtcpBuilder::AddSharer(const RtcpSharerMessage* cast,
                            base::TimeDelta target_delay) {
  // See RTC 4585 Section 6.4 for application specific feedback messages.
  AddRtcpHeader(kPacketTypePayloadSpecific, 15);
  writer_.WriteU32(ssrc_);             // Add our own SSRC.
  writer_.WriteU32(cast->media_ssrc);  // Remote SSRC.
  writer_.WriteU32(kSharer);
  writer_.WriteU32(static_cast<uint32_t>(cast->ack_frame_id));
  uint8_t* sharer_loss_field_pos = reinterpret_cast<uint8_t*>(writer_.ptr());
  writer_.WriteU8(0);  // Overwritten with number_of_loss_fields.
  writer_.WriteU8(0);  // padding
  PP_DCHECK(target_delay.InMilliseconds() <=
            std::numeric_limits<uint16_t>::max());
  writer_.WriteU16(target_delay.InMilliseconds());

  size_t number_of_loss_fields = 0;
  size_t max_number_of_loss_fields =
      std::min<size_t>(kRtcpMaxSharerLossFields, writer_.remaining() / 8);

  MissingFramesAndPacketsMap::const_iterator frame_it =
      cast->missing_frames_and_packets.begin();

  NackStringBuilder nack_string_builder;
  for (; frame_it != cast->missing_frames_and_packets.end() &&
             number_of_loss_fields < max_number_of_loss_fields;
       ++frame_it) {
    nack_string_builder.PushFrame(frame_it->first);
    // Iterate through all frames with missing packets.
    if (frame_it->second.empty()) {
      // Special case all packets in a frame is missing.
      writer_.WriteU32(static_cast<uint32_t>(frame_it->first));
      writer_.WriteU16(kRtcpSharerAllPacketsLost);
      writer_.WriteU8(0);
      writer_.WriteU8(0);  // padding
      nack_string_builder.PushPacket(kRtcpSharerAllPacketsLost);
      ++number_of_loss_fields;
    } else {
      PacketIdSet::const_iterator packet_it = frame_it->second.begin();
      while (packet_it != frame_it->second.end()) {
        uint16_t packet_id = *packet_it;
        // Write frame and packet id to buffer before calculating bitmask.
        writer_.WriteU32(static_cast<uint32_t>(frame_it->first));
        writer_.WriteU16(packet_id);
        nack_string_builder.PushPacket(packet_id);

        uint8_t bitmask = 0;
        ++packet_it;
        while (packet_it != frame_it->second.end()) {
          int shift = static_cast<uint8_t>(*packet_it - packet_id) - 1;
          if (shift >= 0 && shift <= 7) {
            nack_string_builder.PushPacket(*packet_it);
            bitmask |= (1 << shift);
            ++packet_it;
          } else {
            break;
          }
        }
        writer_.WriteU8(bitmask);
        writer_.WriteU8(0);  // padding
        ++number_of_loss_fields;
      }
    }
  }
  /* VLOG_IF(1, !nack_string_builder.Empty()) */
  /*     << "SSRC: " << cast->media_ssrc */
  /*     << ", ACK: " << cast->ack_frame_id */
  /*     << ", NACK: " << nack_string_builder.GetString(); */
  /* DCHECK_LE(number_of_loss_fields, kRtcpMaxSharerLossFields); */
  *sharer_loss_field_pos = static_cast<uint8_t>(number_of_loss_fields);
}
