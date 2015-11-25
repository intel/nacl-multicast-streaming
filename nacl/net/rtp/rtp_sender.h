// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains the interface to the cast RTP sender.

#ifndef NET_RTP_RTP_SENDER_H_
#define NET_RTP_RTP_SENDER_H_

#include "base/time/time.h"
#include "sharer_config.h"
/* #include "net/sharer_transport_defines.h" */
/* #include "net/sharer_transport_sender.h" */
#include "net/pacing/paced_sender.h"
#include "net/rtp/packet_storage.h"
#include "net/rtp/rtp_packetizer.h"

#include <map>
#include <set>

namespace sharer {

// This object is only called from the main cast thread.
// This class handles splitting encoded audio and video frames into packets and
// add an RTP header to each packet. The sent packets are stored until they are
// acknowledged by the remote peer or timed out.
class RtpSender {
 public:
  RtpSender(PacedSender* const transport);

  ~RtpSender();

  // This must be called before sending any frames. Returns false if
  // configuration is invalid.
  bool Initialize(const SharerTransportRtpConfig& config);

  void SendFrame(const EncodedFrame& frame);

  void ResendPackets(const std::string& addr,
                     const MissingFramesAndPacketsMap& missing_packets,
                     bool cancel_rtx_if_not_in_list,
                     const DedupInfo& dedup_info);

  // Returns the total number of bytes sent to the socket when the specified
  // frame was just sent.
  // Returns 0 if the frame cannot be found or the frame was only sent
  // partially.
  int64_t GetLastByteSentForFrame(uint32_t frame_id);

  /* void CancelSendingFrames(const std::vector<uint32_t>& frame_ids); */

  void ResendFrameForKickstart(uint32_t frame_id,
                               base::TimeDelta dedupe_window);

  size_t send_packet_count() const {
    return packetizer_ ? packetizer_->send_packet_count() : 0;
  }
  size_t send_octet_count() const {
    return packetizer_ ? packetizer_->send_octet_count() : 0;
  }
  uint32_t ssrc() const { return config_.ssrc; }

 private:
  void UpdateSequenceNumber(PacketRef packet);

  RtpPacketizerConfig config_;
  PacketStorage storage_;
  std::unique_ptr<RtpPacketizer> packetizer_;
  PacedSender* const transport_;

  DISALLOW_COPY_AND_ASSIGN(RtpSender);
};

}  // namespace sharer

#endif  // NET_RTP_RTP_SENDER_H_
