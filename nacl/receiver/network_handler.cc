// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "receiver/network_handler.h"

#include "base/logger.h"
#include "base/ptr_utils.h"
#include "sharer_config.h"
#include "net/rtp/rtp.h"

#include <sstream>
#include <stdio.h>

NetworkHandler::NetworkHandler(pp::Instance* instance,
                               const ReceiverConfig& audio_config,
                               const ReceiverConfig& video_config)
    : env_(instance),
      udp_listener_(instance, this, "0.0.0.0", 5004),
      videoReceiver_(&env_, video_config, &udp_listener_),
      audioReceiver_(&env_, audio_config, &udp_listener_),
      frameRequested_(false) {
  auto cb = [this]() { udp_listener_.OnNetworkTimeout(); };
  videoReceiver_.SetOnNetworkTimeout(cb);
}

NetworkHandler::~NetworkHandler() {}

void NetworkHandler::OnReceived(const char* buffer, int32_t size) {
  const unsigned char* data = reinterpret_cast<const unsigned char*>(buffer);
  uint32_t ssrc;
  std::unique_ptr<RTPBase> packet = rtpParse(env_.instance(), data, size, &ssrc);
  if (!packet) {
    return;
  }

  storePacket(ssrc, std::move(packet));
}

void NetworkHandler::storePacket(uint32_t ssrc,
                                 std::unique_ptr<RTPBase> packet) {
  if (ssrc == 11)
    videoReceiver_.ProcessPacket(std::move(packet));
  else if (ssrc == 1) {
    /* audioReceiver_.ProcessPacket(std::move(packet)); */
  } else {
    ERR() << "Packet from unknown source: " << ssrc;
    return;
  }
}

void NetworkHandler::GetNextFrame(const ReceiveEncodedFrameCallback& callback) {
  frameRequested_ = true;

  videoReceiver_.RequestEncodedFrame(callback);
}

void NetworkHandler::ReleaseFrame() {}

void NetworkHandler::OnPaused() {
  udp_listener_.StopListening();
  videoReceiver_.FlushFrames();
  // videoReceiver_.SendPausedIndication(videoReceiver_.getLastFrameAck(), 0);
}

void NetworkHandler::OnResumed() { udp_listener_.StartListening(); }
