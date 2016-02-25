// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "decoder.h"

#include "net/sharer_transport_config.h"

#include <stdio.h>
#include <sstream>

Decoder::Decoder(pp::Instance* instance, int id,
                 const pp::Graphics3D& graphics_3d)
    : id_(id),
      decoder_(new pp::VideoDecoder(instance)),
      callback_factory_(this),
      encoded_data_next_pos_to_decode_(0),
      next_picture_id_(0),
      flushing_(false),
      resetting_(false),
      started_(false),
      initialized_(false),
      total_latency_(0.0),
      num_pictures_(0) {
  core_if_ = static_cast<const PPB_Core*>(
      pp::Module::Get()->GetBrowserInterface(PPB_CORE_INTERFACE));

  assert(!decoder_->is_null());
  decoder_->Initialize(graphics_3d, PP_VIDEOPROFILE_VP8_ANY,
                       PP_HARDWAREACCELERATION_WITHFALLBACK, 0,
                       callback_factory_.NewCallback(&Decoder::InitializeDone));
}

Decoder::~Decoder() { delete decoder_; }

void Decoder::InitializeDone(int32_t result) {
  assert(decoder_);
  assert(result == PP_OK);
  initialized_ = true;

  RealStart();
}

void Decoder::Start() {
  assert(decoder_);

  started_ = true;
  if (initialized_) RealStart();
}

void Decoder::RealStart() {
  encoded_data_next_pos_to_decode_ = 0;

  // Register callback to get the first picture. We call GetPicture again in
  // PictureReady to continuously receive pictures as they're decoded.
  decoder_->GetPicture(
      callback_factory_.NewCallbackWithOutput(&Decoder::PictureReady));
}

void Decoder::SetResetCb(ResetDoneCb cb) { resetDone_ = cb; }

void Decoder::Reset() {
  assert(decoder_);
  assert(!resetting_);
  resetting_ = true;
  decoder_->Reset(callback_factory_.NewCallback(&Decoder::ResetDone));
}

void Decoder::RecyclePicture(const PP_VideoPicture& picture) {
  assert(decoder_);
  decoder_->RecyclePicture(picture);
}

void Decoder::DecodeNextFrame(std::shared_ptr<EncodedFrame> encoded,
                              DecodeDoneCb cb) {
  assert(decoder_);
  decodeDone_ = cb;
  encodedFrame_ = encoded;
  decode_time_[next_picture_id_ % kMaxDecodeDelay] = core_if_->GetTimeTicks();
  decoder_->Decode(next_picture_id_++, encoded->data.length(),
                   encoded->data.data(),
                   callback_factory_.NewCallback(&Decoder::DecodeDone));
}

void Decoder::DecodeDone(int32_t result) {
  assert(decoder_);
  // Break out of the decode loop on abort.
  if (result == PP_ERROR_ABORTED) return;
  assert(result == PP_OK);
  if (decodeDone_) {
    // We want to erase the callback after using it. But calling it might
    // result on a new callback being set. So, we need to erase it first
    // (saving the callback), and then call it. If a new callback gets set
    // during this process, it will be correctly stored on decodeDone_;
    DecodeDoneCb cb = decodeDone_;
    decodeDone_ = nullptr;
    encodedFrame_ = nullptr;
    cb();
  }
}

void Decoder::SetPictureReadyCb(PictureReadyCb cb) { pictureReady_ = cb; }

void Decoder::PictureReady(int32_t result, PP_VideoPicture picture) {
  assert(decoder_);
  // Break out of the get picture loop on abort.
  if (result == PP_ERROR_ABORTED) return;
  assert(result == PP_OK);

  num_pictures_++;
  PP_TimeTicks latency = core_if_->GetTimeTicks() -
                         decode_time_[picture.decode_id % kMaxDecodeDelay];
  total_latency_ += latency;

  decoder_->GetPicture(
      callback_factory_.NewCallbackWithOutput(&Decoder::PictureReady));
  if (pictureReady_) {
    pictureReady_(this, picture);
  }
}

void Decoder::FlushDone(int32_t result) {
  assert(decoder_);
  assert(result == PP_OK || result == PP_ERROR_ABORTED);
  assert(flushing_);
  flushing_ = false;
}

void Decoder::ResetDone(int32_t result) {
  assert(decoder_);
  assert(result == PP_OK);
  assert(resetting_);
  resetting_ = false;

  Start();
  resetDone_();
}
