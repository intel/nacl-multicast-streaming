// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/udp_transport.h"

#include "base/logger.h"
#include "base/ptr_utils.h"

namespace sharer {

static const int kMaxPacketSize = 4096;

static uint16_t Htons(uint16_t hostshort) {
  uint8_t result_bytes[2];
  result_bytes[0] = (uint8_t)((hostshort >> 8) & 0xFF);
  result_bytes[1] = (uint8_t)(hostshort & 0xFF);

  uint16_t result;
  memcpy(&result, result_bytes, 2);
  return result;
}

UdpTransport::UdpTransport(SharerEnvironment* env,
                           /* const std::string& local_host, */
                           /* uint16_t local_port, */
                           const std::string& remote_host, uint16_t remote_port,
                           int32_t send_buffer_size,
                           const TransportInitializedCb& cb)
    : env_(env),
      resolved_(false),
      send_pending_(false),
      receive_pending_(false),
      callback_factory_(this),
      /* send_buffer_size_(send_buffer_size), */
      bytes_sent_(0) {
  udp_socket_ = pp::UDPSocket(env_->instance());
  if (udp_socket_.is_null()) {
    ERR() << "Could not create UDPSocket.";
    return;
  }

  if (!pp::HostResolver::IsAvailable()) {
    ERR() << "HostResolver not available.";
    return;
  }

  resolver_ = pp::HostResolver(env_->instance());
  if (resolver_.is_null()) {
    ERR() << "Could not create HostResolver.";
    return;
  }

  auto callback =
      callback_factory_.NewCallback(&UdpTransport::OnResolveCompletion, cb);
  PP_HostResolver_Hint hint = {PP_NETADDRESS_FAMILY_UNSPECIFIED, 0};
  resolver_.Resolve(remote_host.c_str(), remote_port, hint, callback);
  DINF() << "Resolving...";
}

UdpTransport::~UdpTransport() {}

void UdpTransport::OnResolveCompletion(int32_t result,
                                       const TransportInitializedCb& cb) {
  if (result != PP_OK) {
    ERR() << "Resolve failed: " << result;
    cb(resolved_);
    return;
  }

  pp::NetAddress addr = resolver_.GetNetAddress(0);
  INF() << "Resolved: " << addr.DescribeAsString(true).AsString();
  remote_addr_ = addr;
  resolved_ = true;
  cb(resolved_);
}

void UdpTransport::StartReceiving(const PacketReceiverCallback& cb) {
  packet_receiver_ = cb;

  PP_NetAddress_IPv4 ipv4_addr = {Htons(5679), {0, 0, 0, 0}};
  auto callback = callback_factory_.NewCallback(&UdpTransport::OnBound);
  udp_socket_.Bind(pp::NetAddress(env_->instance(), ipv4_addr), callback);
}

void UdpTransport::OnBound(int32_t result) {
  if (result != PP_OK) {
    ERR() << "Could not bind to local address:" << result;
    return;
  }

  ReceiveNextPacket();
}

void UdpTransport::ReceiveNextPacket() {
  // TODO: Receive packet and check return value from RecvFrom

  next_packet_ = make_unique<Packet>(kMaxPacketSize);
  auto callback = callback_factory_.NewCallbackWithOutput(
      &UdpTransport::OnReceiveFromCompletion);
  udp_socket_.RecvFrom(reinterpret_cast<char*>(next_packet_->data()),
                       kMaxPacketSize, callback);
  receive_pending_ = true;
}

void UdpTransport::OnReceiveFromCompletion(int32_t result,
                                           pp::NetAddress source) {
  if (result < PP_OK) {
    ERR() << "Problem when receiving packet: " << result;
    receive_pending_ = false;
    return;
  }

  if (packet_receiver_) {
    next_packet_->resize(result);
    std::string addr = source.DescribeAsString(false).AsString();
    if (addr_from_str_.find(addr) == addr_from_str_.end()) {
      addr_from_str_.insert(std::make_pair(addr, source));
    }
    packet_receiver_(addr, std::move(next_packet_));
  }
  ReceiveNextPacket();
}

bool UdpTransport::SendPacket(const std::string& addr, PacketRef packet,
                              const pp::CompletionCallback& cb) {
  bytes_sent_ += packet->size();
  if (!resolved_) {
    DERR() << "Can't send packet: remote host not resolved yet.";
    return true;
  }

  PP_DCHECK(!send_pending_);
  if (send_pending_) {
    WRN() << "Cannot send because of pending request.";
    return true;
  }

  pp::NetAddress net_addr;

  if (addr == "multicast") {
    net_addr = remote_addr_;
  } else {
    auto it = addr_from_str_.find(addr);
    if (it == addr_from_str_.end()) {
      DERR() << "Can't find address for: " << addr;
      return true;
    }
    net_addr = it->second;
  }

  auto callback =
      callback_factory_.NewCallback(&UdpTransport::OnSent, packet, cb);
  int32_t result = udp_socket_.SendTo(reinterpret_cast<char*>(packet->data()),
                                      packet->size(), net_addr, callback);

  if (result == PP_OK_COMPLETIONPENDING) {
    send_pending_ = true;
    return false;
  }

  OnSent(result, packet, cb);
  return true;
}

void UdpTransport::OnSent(int32_t result, PacketRef packet,
                          pp::CompletionCallback cb) {
  send_pending_ = false;
  if (result < 0) {
    DERR() << "Failed to send packet: " << result;
  }

  cb.Run(result);
}

int64_t UdpTransport::GetBytesSent() { return bytes_sent_; }

}  // namespace sharer
