// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _NETWORK_HANDLER_
#define _NETWORK_HANDLER_

#include "receiver/frame_receiver.h"
#include "net/udp_delegate_interface.h"
#include "net/udp_listener.h"
#include "sharer_environment.h"

#include "ppapi/cpp/instance.h"

#include <queue>

class RTP;
struct ReceiverConfig;

class NetworkHandler : public UDPDelegateInterface {
 public:
  explicit NetworkHandler(pp::Instance* instance,
                          const ReceiverConfig& audio_config,
                          const ReceiverConfig& video_config);
  virtual ~NetworkHandler();

  void GetNextFrame(const ReceiveEncodedFrameCallback& callback);
  void ReleaseFrame();
  void OnPaused();
  void OnResumed();

  virtual void OnReceived(const char* buffer, int32_t size);

 private:
  void storePacket(uint32_t ssrc, std::unique_ptr<RTPBase> packet);
  /* void checkFlush(std::queue<RTP*>& queue); */
  /* int checkSequence(RTP* received, RTP* last); */
  /* bool ParseFrame(); */
  /* bool FindFrameStart(); */
  /* bool FindFrameEnd(); */
  /* void DeliverFrame(); */
  /* void FillBuffer(const RTP* packet, int32_t start, int32_t size); */
  /* void DropFrame(); */

  sharer::SharerEnvironment env_;
  /* NetworkDelegateInterface* delegate_; */
  UDPListener udp_listener_;

  FrameReceiver videoReceiver_;
  FrameReceiver audioReceiver_;
  /* SourceHandler srcVideo_; */
  /* SourceHandler srcAudio_; */

  bool frameRequested_;
  /* bool frameStarted_; */
  /* bool bufferInUse_; */
  /* bool frameBeginning_; */
  /* char buffer_[4 * 1024 * 1024]; */
  /* int32_t bufferSize_; */

  /* int currentFramePacketPos_; */

  /* uint32_t packetCount_; */
};

#endif  // _NETWORK_HANDLER_
