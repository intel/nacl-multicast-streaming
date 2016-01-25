// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_PACING_PACED_SENDER_H_
#define NET_PACING_PACED_SENDER_H_

#include "base/macros.h"
#include "base/time/time.h"
#include "net/sharer_transport_config.h"
#include "net/rtp/rtp_receiver_defines.h"
#include "net/udp_transport.h"
#include "sharer_environment.h"

#include "ppapi/utility/completion_callback_factory.h"

#include <vector>

namespace sharer {

using PacketKey = std::pair<base::TimeTicks, std::pair<uint32_t, uint16_t>>;
using SendPacketVector = std::vector<std::pair<PacketKey, PacketRef>>;
using PacketWithIP = std::pair<std::string, PacketKey>;
/* using PacketWithIPVector = std::vector<std::pair<PacketWithIP, PacketRef>>;
 */

struct DedupInfo {
  DedupInfo();
  base::TimeDelta resend_interval;
  int64_t last_byte_acked_for_audio;
};

class PacedSender {
 public:
  PacedSender(SharerEnvironment* env, UdpTransport* udpsender);
  ~PacedSender();

  void RegisterAudioSsrc(uint32_t audio_ssrc);
  void RegisterVideoSsrc(uint32_t video_ssrc);

  void RegisterPrioritySsrc(uint32_t ssrc);

  int64_t GetLastByteSentForPacket(const PacketKey& packet_key);
  int64_t GetLastByteSentForSsrc(uint32_t ssrc);

  bool SendPackets(const SendPacketVector& packets);
  bool ResendPackets(const std::string& addr, const SendPacketVector& packets,
                     const DedupInfo& dedup_info);
  bool SendRtcpPacket(uint32_t ssrc, PacketRef packet);
  void CancelSendingPacket(const std::string& addr,
                           const PacketKey& packet_key);

  static PacketKey MakePacketKey(const base::TimeTicks& ticks, uint32_t ssrc,
                                 uint16_t packet_id);

 private:
  void SendStoredPackets(int32_t result);

  bool ShouldResend(const PacketWithIP& packet_key, const DedupInfo& dedup_info,
                    const base::TimeTicks& now);
  void LogPacketEvent(PacketRef packet, SharerLoggingEvent type);

  enum class PacketType { RTCP, Resend, Normal };

  enum class State { Unblocked, TransportBlocked, BurstFull };

  bool empty() const;
  size_t size() const;

  PacketRef PopNextPacket(PacketType* packet_type, PacketWithIP* packet_key);

  bool IsHighPriority(const PacketKey& packet_key) const;

  SharerEnvironment* const env_;
  pp::CompletionCallbackFactory<PacedSender> callback_factory_;
  UdpTransport* transport_;

  uint32_t audio_ssrc_;
  uint32_t video_ssrc_;
  std::vector<uint32_t> priority_ssrcs_;

  using PacketList = std::map<PacketWithIP, std::pair<PacketType, PacketRef>>;
  PacketList packet_list_;
  PacketList priority_packet_list_;

  struct PacketSendRecord {
    PacketSendRecord();
    base::TimeTicks time;
    int64_t last_byte_sent;

    int64_t last_byte_sent_for_audio;
  };

  using PacketSendHistory = std::map<PacketKey, PacketSendRecord>;
  using PacketSendIPHistory = std::map<PacketWithIP, PacketSendRecord>;
  PacketSendIPHistory send_history_;
  PacketSendIPHistory send_history_buffer_;

  std::map<uint32_t, int64_t> last_byte_sent_;

  /* size_t target_burst_size_; */
  size_t max_burst_size_;

  size_t current_max_burst_size_;
  size_t next_max_burst_size_;
  size_t next_next_max_burst_size_;

  size_t current_burst_size_;

  base::TimeTicks burst_end_;
  State state_;

  bool has_reached_upper_bound_once_;

  DISALLOW_COPY_AND_ASSIGN(PacedSender);
};

}  // namespace sharer

#endif  // NET_PACING_PACED_SENDER_H_
