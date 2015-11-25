// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _UDP_LISTENER_
#define _UDP_LISTENER_

#include "net/udp_delegate_interface.h"
#include "net/rtp/rtp_receiver_defines.h"

#include "ppapi/cpp/udp_socket.h"
#include "ppapi/cpp/host_resolver.h"
#include "ppapi/cpp/instance.h"
#include "ppapi/utility/completion_callback_factory.h"
#include "ppapi/cpp/network_list.h"
#include "ppapi/cpp/network_monitor.h"

#include <queue>

static const int kBufferSize = 4096;

class UDPListener : public UDPSender {
 public:
  explicit UDPListener(pp::Instance* instance, UDPDelegateInterface* delegate,
                       const std::string& host, uint16_t port);
  virtual ~UDPListener();

  void SendPacket(PacketRef packet) override;
  void OnNetworkTimeout();
  void StopListening();
  void StartListening();

 private:
  void Start(const std::string& host, uint16_t port);
  bool IsConnected();

  void Stop();
  void Send(const std::string& message);
  void Receive();
  void SendPacketsInternal();

  void OnJoinedCompletion(int32_t result);
  void OnConnectCompletion(int32_t result);
  void OnResolveCompletion(int32_t result);
  void OnReceiveCompletion(int32_t result);
  void OnReceiveFromCompletion(int32_t result, pp::NetAddress source);
  void OnSendCompletion(int32_t result);
  void OnSendPacketCompletion(int32_t result);
  void OnNetworkListCompletion(int32_t result, pp::NetworkList network_list);
  void OnSetOptionCompletion(int32_t result);

  void OnLeaveCompletion(int32_t result);
  void OnRejoinCompletion(int32_t result);

  pp::Instance* instance_;
  UDPDelegateInterface* delegate_;
  pp::CompletionCallbackFactory<UDPListener> callback_factory_;
  pp::UDPSocket udp_socket_;
  pp::HostResolver resolver_;
  pp::NetAddress local_host_;
  pp::NetAddress group_addr_;
  std::unique_ptr<pp::NetAddress> remote_host_;  // use a pointer here so it's
                                                 // possible to check for
                                                 // nullptr,
  // in the case when remote_host_ wasn't set yet
  pp::NetworkMonitor network_monitor_;

  char receive_buffer_[kBufferSize];
  bool send_outstanding_;
  std::queue<PacketRef> packets_;
  bool stop_listening_;
};

#endif  // _UDP_LISTENER_
