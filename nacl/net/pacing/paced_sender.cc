// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/pacing/paced_sender.h"

#include "base/big_endian.h"
#include "base/logger.h"
#include "base/ptr_utils.h"

namespace sharer {

namespace {

static const int64_t kPacingIntervalMs = 10;

static const size_t kPacingMaxBurstsPerFrame = 3;
static const size_t kMaxDedupeWindowMs = 500;

static const size_t kTargetBurstSize = 10;
static const size_t kMaxBurstSize = 20;

static const size_t kHugeQueueLengthSeconds = 10;
static const size_t kRidiculousNumberOfPackets =
    kHugeQueueLengthSeconds * (kMaxBurstSize * 1000 / kPacingIntervalMs);
}

DedupInfo::DedupInfo() : last_byte_acked_for_audio(0) {}

// static
PacketKey PacedSender::MakePacketKey(const base::TimeTicks& ticks,
                                     uint32_t ssrc, uint16_t packet_id) {
  return std::make_pair(ticks, std::make_pair(ssrc, packet_id));
}

PacedSender::PacketSendRecord::PacketSendRecord()
    : last_byte_sent(0), last_byte_sent_for_audio(0) {}

PacedSender::PacedSender(SharerEnvironment* env, UdpTransport* udp_sender)
    : env_(env),
      callback_factory_(this),
      transport_(udp_sender),
      audio_ssrc_(0),
      video_ssrc_(0),
      current_max_burst_size_(kTargetBurstSize),
      next_max_burst_size_(kTargetBurstSize),
      next_next_max_burst_size_(kTargetBurstSize),
      current_burst_size_(0),
      state_(State::Unblocked),
      has_reached_upper_bound_once_(false) {}

PacedSender::~PacedSender() {}

void PacedSender::RegisterAudioSsrc(uint32_t audio_ssrc) {
  audio_ssrc_ = audio_ssrc;
}

void PacedSender::RegisterVideoSsrc(uint32_t video_ssrc) {
  video_ssrc_ = video_ssrc;
}

void PacedSender::RegisterPrioritySsrc(uint32_t ssrc) {
  priority_ssrcs_.push_back(ssrc);
}

int64_t PacedSender::GetLastByteSentForPacket(const PacketKey& packet_key) {
  return 0;
}

int64_t PacedSender::GetLastByteSentForSsrc(uint32_t ssrc) {
  std::map<uint32_t, int64_t>::const_iterator it = last_byte_sent_.find(ssrc);
  if (it == last_byte_sent_.end()) return 0;
  return it->second;
}

bool PacedSender::SendPackets(const SendPacketVector& packets) {
  if (packets.empty()) {
    return true;
  }
  std::string addr = "multicast";
  const bool high_priority = IsHighPriority(packets.begin()->first);
  for (size_t i = 0; i < packets.size(); i++) {
    PP_DCHECK(IsHighPriority(packets[i].first) == high_priority);
    if (high_priority) {
      priority_packet_list_[std::make_pair(addr, packets[i].first)] =
          make_pair(PacketType::Normal, packets[i].second);
    } else {
      packet_list_[std::make_pair(addr, packets[i].first)] =
          make_pair(PacketType::Normal, packets[i].second);
    }
  }
  if (state_ == State::Unblocked) {
    SendStoredPackets(PP_OK);
  }
  return true;
}

bool PacedSender::ShouldResend(const PacketWithIP& packet_key,
                               const DedupInfo& dedup_info,
                               const base::TimeTicks& now) {
  auto it = send_history_.find(packet_key);

  // No history of previous transmission. It might be sent too long ago.
  if (it == send_history_.end()) return true;

  /* // Suppose there is request to retransmit X and there is an audio */
  /* // packet Y sent just before X. Reject retransmission of X if ACK for */
  /* // Y has not been received. */
  /* // Only do this for video packets. */
  /* if (packet_key.second.first == video_ssrc_) { */
  /*   if (dedup_info.last_byte_acked_for_audio && */
  /*       it->second.last_byte_sent_for_audio && */
  /*       dedup_info.last_byte_acked_for_audio < */
  /*       it->second.last_byte_sent_for_audio) { */
  /*     return false; */
  /*   } */
  /* } */
  // Retransmission interval has to be greater than |resend_interval|.
  if (now - it->second.time < dedup_info.resend_interval) return false;
  return true;
}

bool PacedSender::ResendPackets(const std::string& addr,
                                const SendPacketVector& packets,
                                const DedupInfo& dedup_info) {
  if (packets.empty()) {
    return true;
  }
  const bool high_priority = IsHighPriority(packets.begin()->first);
  const base::TimeTicks now = env_->clock()->NowTicks();
  for (size_t i = 0; i < packets.size(); i++) {
    PacketWithIP packet_key = std::make_pair(addr, packets[i].first);
    if (!ShouldResend(packet_key, dedup_info, now)) {
      LogPacketEvent(packets[i].second, PACKET_RTX_REJECTED);
      DWRN() << ">> Not resending to: " << addr << ", ["
             << packets[i].first.second.first << ":"
             << packets[i].first.second.second << "]";
      continue;
    }

    PP_DCHECK(IsHighPriority(packets[i].first) == high_priority);
    if (high_priority) {
      priority_packet_list_[std::make_pair(addr, packets[i].first)] =
          make_pair(PacketType::Resend, packets[i].second);
    } else {
      DINF() << ">>> Add resend: addr: " << addr << ", ["
             << packets[i].first.second.first << ":"
             << packets[i].first.second.second
             << "]; list size: " << packet_list_.size();
      packet_list_[std::make_pair(addr, packets[i].first)] =
          make_pair(PacketType::Resend, packets[i].second);
    }
  }
  if (state_ == State::Unblocked) {
    SendStoredPackets(PP_OK);
  }
  return true;
}

bool PacedSender::SendRtcpPacket(uint32_t ssrc, PacketRef packet) {
  std::string addr = "multicast";
  if (state_ == State::TransportBlocked) {
    priority_packet_list_[std::make_pair(
        addr, PacedSender::MakePacketKey(base::TimeTicks(), ssrc, 0))] =
        make_pair(PacketType::RTCP, packet);
  } else {
    // We pass the RTCP packets straight through.
    if (!transport_->SendPacket(
            addr, packet,
            callback_factory_.NewCallback(&PacedSender::SendStoredPackets))) {
      state_ = State::TransportBlocked;
    }
  }
  return true;
}

void PacedSender::CancelSendingPacket(const std::string& addr,
                                      const PacketKey& packet_key) {
  packet_list_.erase(std::make_pair(addr, packet_key));
  priority_packet_list_.erase(std::make_pair(addr, packet_key));
}

PacketRef PacedSender::PopNextPacket(PacketType* packet_type,
                                     PacketWithIP* packet_key) {
  PacketList* list =
      !priority_packet_list_.empty() ? &priority_packet_list_ : &packet_list_;
  PP_DCHECK(!list->empty());
  PacketList::iterator i = list->begin();
  *packet_type = i->second.first;
  *packet_key = i->first;
  PacketRef ret = i->second.second;
  list->erase(i);
  return ret;
}

bool PacedSender::IsHighPriority(const PacketKey& packet_key) const {
  return std::find(priority_ssrcs_.begin(), priority_ssrcs_.end(),
                   packet_key.second.first) != priority_ssrcs_.end();
}

bool PacedSender::empty() const {
  return packet_list_.empty() && priority_packet_list_.empty();
}

size_t PacedSender::size() const {
  return packet_list_.size() + priority_packet_list_.size();
}

// This function can be called from three places:
// 1. User called one of the Send* functions and we were in an unblocked state.
// 2. state_ == State_TransportBlocked and the transport is calling us to
//    let us know that it's ok to send again.
// 3. state_ == State_BurstFull and there are still packets to send. In this
//    case we called PostDelayedTask on this function to start a new burst.
void PacedSender::SendStoredPackets(int32_t result) {
  State previous_state = state_;
  state_ = State::Unblocked;
  if (empty()) {
    return;
  }

  // If the queue ever becomes impossibly long, send a crash dump without
  // actually crashing the process.
  if (size() > kRidiculousNumberOfPackets && !has_reached_upper_bound_once_) {
    PP_NOTREACHED();
    // Please use Cr=Internals-Cast label in bug reports:
    /* base::debug::DumpWithoutCrashing(); */
    has_reached_upper_bound_once_ = true;
  }

  base::TimeTicks now = env_->clock()->NowTicks();
  // I don't actually trust that PostDelayTask(x - now) will mean that
  // now >= x when the call happens, so check if the previous state was
  // State_BurstFull too.
  if (now >= burst_end_ || previous_state == State::BurstFull) {
    // Start a new burst.
    current_burst_size_ = 0;
    burst_end_ = now + base::TimeDelta::FromMilliseconds(kPacingIntervalMs);

    // The goal here is to try to send out the queued packets over the next
    // three bursts, while trying to keep the burst size below 10 if possible.
    // We have some evidence that sending more than 12 packets in a row doesn't
    // work very well, but we don't actually know why yet. Sending out packets
    // sooner is better than sending out packets later as that gives us more
    // time to re-send them if needed. So if we have less than 30 packets, just
    // send 10 at a time. If we have less than 60 packets, send n / 3 at a time.
    // if we have more than 60, we send 20 at a time. 20 packets is ~24Mbit/s
    // which is more bandwidth than the cast library should need, and sending
    // out more data per second is unlikely to be helpful.
    size_t max_burst_size = std::min(
        kTargetBurstSize,  // FIXME: Should set a target_burst_size_ and use it
                           // here. See original implementation for reference.
        std::max(kTargetBurstSize, size() / kPacingMaxBurstsPerFrame));
    current_max_burst_size_ = std::max(next_max_burst_size_, max_burst_size);
    next_max_burst_size_ = std::max(next_next_max_burst_size_, max_burst_size);
    next_next_max_burst_size_ = max_burst_size;
  }

  auto cb = callback_factory_.NewCallback(&PacedSender::SendStoredPackets);

  while (!empty()) {
    if (current_burst_size_ >= current_max_burst_size_) {
      const base::TimeDelta sched = burst_end_ - now;
      pp::Module::Get()->core()->CallOnMainThread(sched.InMilliseconds(), cb);
      state_ = State::BurstFull;
      return;
    }
    PacketType packet_type;
    PacketWithIP packet_key;
    PacketRef packet = PopNextPacket(&packet_type, &packet_key);
    PacketSendRecord send_record;
    send_record.time = now;

    switch (packet_type) {
      case PacketType::Resend:
        LogPacketEvent(packet, PACKET_RETRANSMITTED);
        break;
      case PacketType::Normal:
        LogPacketEvent(packet, PACKET_SENT_TO_NETWORK);
        break;
      case PacketType::RTCP:
        break;
    }

    const bool socket_blocked =
        !transport_->SendPacket(packet_key.first, packet, cb);

    // Save the send record.
    send_record.last_byte_sent = transport_->GetBytesSent();
    send_record.last_byte_sent_for_audio = GetLastByteSentForSsrc(audio_ssrc_);
    send_history_[packet_key] = send_record;
    send_history_buffer_[packet_key] = send_record;
    last_byte_sent_[packet_key.second.second.first] =
        send_record.last_byte_sent;

    if (socket_blocked) {
      state_ = State::TransportBlocked;
      return;
    }
    current_burst_size_++;
  }

  // Keep ~0.5 seconds of data (1000 packets).
  if (send_history_buffer_.size() >=
      max_burst_size_ * kMaxDedupeWindowMs / kPacingIntervalMs) {
    send_history_.swap(send_history_buffer_);
    send_history_buffer_.clear();
  }
  PP_DCHECK(send_history_buffer_.size() <=
            (max_burst_size_ * kMaxDedupeWindowMs / kPacingIntervalMs));
  state_ = State::Unblocked;
}

void PacedSender::LogPacketEvent(PacketRef packet, SharerLoggingEvent type) {
  auto event = make_unique<PacketEvent>();
  event->timestamp = env_->clock()->NowTicks();
  event->type = type;

  BigEndianReader reader(reinterpret_cast<const char*>(packet->data()),
                               packet->size());
  bool success = reader.Skip(4);
  success &= reader.ReadU32(&event->rtp_timestamp);
  uint32_t ssrc;
  success &= reader.ReadU32(&ssrc);
  if (ssrc == audio_ssrc_) {
    event->media_type = AUDIO_EVENT;
  } else if (ssrc == video_ssrc_) {
    event->media_type = VIDEO_EVENT;
  } else {
    DWRN() << "Got unknown ssrc " << ssrc << " when logging packet event";
    return;
  }
  success &= reader.Skip(2);
  success &= reader.ReadU16(&event->packet_id);
  success &= reader.ReadU16(&event->max_packet_id);
  event->size = packet->size();
  PP_DCHECK(success);

  env_->logger()->DispatchPacketEvent(std::move(event));
}

}  // namespace sharer
