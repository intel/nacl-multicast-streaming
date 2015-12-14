// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_UDP_TRANSPORT_H_
#define NET_UDP_TRANSPORT_H_

#include "base/macros.h"
#include "net/sharer_transport_config.h"
#include "net/sharer_transport_defines.h"
#include "sharer_environment.h"

#include "ppapi/utility/completion_callback_factory.h"
#include "ppapi/cpp/host_resolver.h"
#include "ppapi/cpp/udp_socket.h"

namespace sharer {

using PacketReceiverCallback =
    std::function<void(const std::string&, std::unique_ptr<Packet>)>;

class UdpTransport : public PacketSender {
 public:
  UdpTransport(SharerEnvironment* env,
               /* const std::string& local_host, */
               /* uint16_t local_port, */
               const std::string& remote_host, uint16_t remote_port,
               int32_t send_buffer_size, const TransportInitializedCb& cb);
  ~UdpTransport() final;

  bool SendPacket(const std::string& addr, PacketRef packet,
                  const pp::CompletionCallback& cb) final;
  int64_t GetBytesSent() final;

  void StartReceiving(const PacketReceiverCallback& cb);

 private:
  void OnResolveCompletion(int32_t result, const TransportInitializedCb& cb);
  void OnSent(int32_t result, PacketRef packet, pp::CompletionCallback cb);
  void OnBound(int32_t result);

  void ReceiveNextPacket();
  void OnReceiveFromCompletion(int32_t result, pp::NetAddress source);

  SharerEnvironment* env_;

  /* uint16_t local_port_; */
  pp::UDPSocket udp_socket_;
  pp::NetAddress remote_addr_;
  bool resolved_;
  bool send_pending_;
  bool receive_pending_;
  std::unique_ptr<Packet> next_packet_;

  PacketReceiverCallback packet_receiver_;

  std::map<const std::string, pp::NetAddress> addr_from_str_;

  pp::CompletionCallbackFactory<UdpTransport> callback_factory_;
  pp::HostResolver resolver_;

  /* int32_t send_buffer_size_; */
  int bytes_sent_;

  DISALLOW_COPY_AND_ASSIGN(UdpTransport);
};

}  // namespace sharer

#endif  // NET_UDP_TRANSPORT_H_
