// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef _DECODER_
#define _DECODER_

#include "ppapi/cpp/instance.h"
#include "ppapi/cpp/video_decoder.h"
#include "ppapi/utility/completion_callback_factory.h"

#include <string>

struct EncodedFrame;

class Decoder {
 public:
  using DecodeDoneCb = std::function<void()>;
  using ResetDoneCb = std::function<void()>;
  using PictureReadyCb = std::function<void(Decoder*, PP_VideoPicture)>;

  Decoder(pp::Instance* instance, int id, const pp::Graphics3D& graphics_3d);
  ~Decoder();

  int id() const { return id_; }
  bool flushing() const { return flushing_; }
  bool resetting() const { return resetting_; }

  void Reset();
  void RecyclePicture(const PP_VideoPicture& picture);
  void DecodeNextFrame(std::shared_ptr<EncodedFrame> encoded, DecodeDoneCb cb);
  void SetPictureReadyCb(PictureReadyCb cb);
  void SetResetCb(ResetDoneCb cb);

  PP_TimeTicks GetAverageLatency() {
    return num_pictures_ ? total_latency_ / num_pictures_ : 0;
  }

  void Start();

 private:
  void RealStart();
  void InitializeDone(int32_t result);
  /* void DecodeNextFrame(); */
  void DecodeDone(int32_t result);
  void PictureReady(int32_t result, PP_VideoPicture picture);
  void FlushDone(int32_t result);
  void ResetDone(int32_t result);

  int id_;

  pp::VideoDecoder* decoder_;
  pp::CompletionCallbackFactory<Decoder> callback_factory_;

  size_t encoded_data_next_pos_to_decode_;
  int next_picture_id_;
  bool flushing_;
  bool resetting_;
  bool started_;
  bool initialized_;

  DecodeDoneCb decodeDone_;
  ResetDoneCb resetDone_;
  std::shared_ptr<EncodedFrame> encodedFrame_;
  PictureReadyCb pictureReady_;

  const PPB_Core* core_if_;
  static const int kMaxDecodeDelay = 128;
  PP_TimeTicks decode_time_[kMaxDecodeDelay];
  PP_TimeTicks total_latency_;
  int num_pictures_;
};

#endif  // _DECODER_
