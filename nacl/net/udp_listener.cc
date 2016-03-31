// Copyright 2013 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>
#include <sstream>

#include "base/logger.h"
#include "base/ptr_utils.h"
#include "net/udp_listener.h"

#ifdef WIN32
#undef PostMessage
// Allow 'this' in initializer list
#pragma warning(disable : 4355)
#endif

static uint16_t Htons(uint16_t hostshort) {
  uint8_t result_bytes[2];
  result_bytes[0] = (uint8_t)((hostshort >> 8) & 0xFF);
  result_bytes[1] = (uint8_t)(hostshort & 0xFF);

  uint16_t result;
  memcpy(&result, result_bytes, 2);
  return result;
}

UDPListener::UDPListener(pp::Instance* instance, UDPDelegateInterface* delegate,
                         const std::string& host, uint16_t port)
    : instance_(instance),
      delegate_(delegate),
      callback_factory_(this),
      network_monitor_(instance_),
      stop_listening_(false) {
  Start(host, port);
}

UDPListener::~UDPListener() {}

bool UDPListener::IsConnected() {
  if (!udp_socket_.is_null()) return true;

  return false;
}

void UDPListener::Start(const std::string& host, uint16_t port) {
  if (IsConnected()) {
    WRN() << "Already connected.";
    return;
  }

  udp_socket_ = pp::UDPSocket(instance_);
  if (udp_socket_.is_null()) {
    ERR() << "Could not create UDPSocket.";
    return;
  }

  if (!pp::HostResolver::IsAvailable()) {
    ERR() << "HostResolver not available.";
    return;
  }

  resolver_ = pp::HostResolver(instance_);
  if (resolver_.is_null()) {
    ERR() << "Could not create HostResolver.";
    return;
  }

  PP_NetAddress_IPv4 ipv4_addr = {Htons(port), {0, 0, 0, 0}};
  local_host_ = pp::NetAddress(instance_, ipv4_addr);

  pp::CompletionCallback callback =
      callback_factory_.NewCallback(&UDPListener::OnResolveCompletion);
  PP_HostResolver_Hint hint = {PP_NETADDRESS_FAMILY_UNSPECIFIED, 0};
  resolver_.Resolve(host.c_str(), port, hint, callback);
  DINF() << "Resolving...";
}

void UDPListener::OnNetworkListCompletion(int32_t result,
                                          pp::NetworkList network_list) {
  if (result != PP_OK) {
    ERR() << "Update Network List failed: " << result;
    return;
  }

  int count = network_list.GetCount();
  DINF() << "Number of networks found: " << count;

  for (int i = 0; i < network_list.GetCount(); i++) {
    DINF() << "network: " << i << ", name: " << network_list.GetName(i).c_str();
  }

  /* pp::CompletionCallback callback = */
  /*     callback_factory_.NewCallback(&UDPListener::OnSetOptionCompletion); */

  DINF() << "Binding...";

  /* udp_socket_.SetOption(PP_UDPSOCKET_OPTION_MULTICAST_IF, pp::Var(0),
   * callback); */
  pp::CompletionCallback callback =
      callback_factory_.NewCallback(&UDPListener::OnConnectCompletion);
  udp_socket_.Bind(local_host_, callback);
}

void UDPListener::OnJoinedCompletion(int32_t result) {
  DINF() << "OnJoined result: " << result;
  pp::NetAddress addr = udp_socket_.GetBoundAddress();
  INF() << "Bound to: " << addr.DescribeAsString(true).AsString();

  Receive();
}

void UDPListener::OnSetOptionCompletion(int32_t result) {
  if (result != PP_OK) {
    ERR() << "SetOption failed: " << result;
    return;
  }

  pp::CompletionCallback callback =
      callback_factory_.NewCallback(&UDPListener::OnConnectCompletion);
  udp_socket_.Bind(local_host_, callback);
}

void UDPListener::OnResolveCompletion(int32_t result) {
  if (result != PP_OK) {
    ERR() << "Resolve failed: " << result;
    return;
  }

  pp::CompletionCallbackWithOutput<pp::NetworkList> netlist_callback =
      callback_factory_.NewCallbackWithOutput(
          &UDPListener::OnNetworkListCompletion);
  network_monitor_.UpdateNetworkList(netlist_callback);

  pp::NetAddress addr = resolver_.GetNetAddress(0);
  INF() << "Resolved: " << addr.DescribeAsString(true).AsString();
  group_addr_ = addr;
}

void UDPListener::Stop() {
  if (!IsConnected()) {
    WRN() << "Not connected.";
    return;
  }

  udp_socket_.Close();
  udp_socket_ = pp::UDPSocket();

  INF() << "Closed connection.";
}

void UDPListener::Send(const std::string& message) {
  if (!IsConnected()) {
    WRN() << "Cant send, not connected.";
    return;
  }

  if (send_outstanding_) {
    WRN() << "Already sending.";
    return;
  }

  if (!remote_host_) {
    ERR() << "Can't send packet: remote host not set yet.";
    return;
  }

  uint32_t size = message.size();
  const char* data = message.c_str();
  pp::CompletionCallback callback =
      callback_factory_.NewCallback(&UDPListener::OnSendCompletion);
  int32_t result;
  result = udp_socket_.SendTo(data, size, *remote_host_, callback);
  if (result < 0) {
    if (result == PP_OK_COMPLETIONPENDING) {
      DINF() << "Sending bytes.";
      send_outstanding_ = true;
    } else {
      WRN() << "Send return error: " << result;
    }
  } else {
    DINF() << "Sent bytes synchronously: " << result;
  }
}

void UDPListener::SendPacket(PacketRef packet) {
  if (!IsConnected()) {
    ERR() << "Can't send packet: not connected.";
    return;
  }

  packets_.push(packet);
  SendPacketsInternal();
}

void UDPListener::SendPacketsInternal() {
  if (!remote_host_) {
    ERR() << "Can't send packet: remote host not set yet.";
    return;
  }

  if (send_outstanding_ || packets_.empty()) return;

  while (!send_outstanding_ && !packets_.empty()) {
    PacketRef packet = packets_.front();

    uint32_t size = packet->size();
    const char* data = reinterpret_cast<char*>(packet->data());

    pp::CompletionCallback callback =
        callback_factory_.NewCallback(&UDPListener::OnSendPacketCompletion);
    int32_t result;
    result = udp_socket_.SendTo(data, size, *remote_host_, callback);
    if (result < 0) {
      if (result == PP_OK_COMPLETIONPENDING) {
        // will send, just wait completion now
        send_outstanding_ = true;
      } else {
        ERR() << "Error sending packet: " << result;
      }
    } else {
      // packet sent synchronously
      packets_.pop();
    }
  }
}

void UDPListener::Receive() {
  memset(receive_buffer_, 0, kBufferSize);
  pp::CompletionCallbackWithOutput<pp::NetAddress> callback =
      callback_factory_.NewCallbackWithOutput(
          &UDPListener::OnReceiveFromCompletion);
  udp_socket_.RecvFrom(receive_buffer_, kBufferSize, callback);
}

void UDPListener::OnConnectCompletion(int32_t result) {
  if (result != PP_OK) {
    ERR() << "Connection failed: " << result;
    return;
  }

  pp::CompletionCallback joinCallback =
      callback_factory_.NewCallback(&UDPListener::OnJoinedCompletion);
  udp_socket_.JoinGroup(group_addr_, joinCallback);
}

void UDPListener::OnReceiveFromCompletion(int32_t result,
                                          pp::NetAddress source) {
  if (!remote_host_) {
    INF() << "Setting remote host to: "
          << source.DescribeAsString(true).AsString();
    remote_host_ = make_unique<pp::NetAddress>(source);
  }
  OnReceiveCompletion(result);
}

void UDPListener::OnReceiveCompletion(int32_t result) {
  if (result < 0) {
    ERR() << "Receive failed with error: " << result;
    return;
  }

  delegate_->OnReceived(receive_buffer_, result);
  /* PostMessage(std::string("Received: ") + std::string(receive_buffer_,
   * result)); */
  if (!stop_listening_) Receive();
}

void UDPListener::OnSendCompletion(int32_t result) {
  if (result < 0) {
    ERR() << "Send failed with error: " << result;
  } else {
    DINF() << "Sent " << result << " bytes.";
  }
  send_outstanding_ = false;
}

void UDPListener::OnSendPacketCompletion(int32_t result) {
  if (result < 0) {
    ERR() << "SendPacket failed with error: " << result;
  }

  packets_.pop();
  send_outstanding_ = false;

  SendPacketsInternal();
}

void UDPListener::OnLeaveCompletion(int32_t result) {
  if (result != PP_OK) {
    ERR() << "Could not rejoin multicast group: " << result;
    return;
  }
  auto rejoinCallback =
      callback_factory_.NewCallback(&UDPListener::OnRejoinCompletion);
  udp_socket_.JoinGroup(group_addr_, rejoinCallback);
}

void UDPListener::OnRejoinCompletion(int32_t result) {
  if (result != PP_OK) {
    ERR() << "Could not leave multicast group: " << result;
    return;
  }
}

void UDPListener::OnNetworkTimeout() {
  auto leaveCallback =
      callback_factory_.NewCallback(&UDPListener::OnLeaveCompletion);
  udp_socket_.LeaveGroup(group_addr_, leaveCallback);
}

void UDPListener::StopListening() { stop_listening_ = true; }

void UDPListener::StartListening() {
  stop_listening_ = false;
  Receive();
}
