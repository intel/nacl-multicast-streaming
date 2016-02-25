// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sender/video_encoder.h"

#include "base/logger.h"
#include "base/ptr_utils.h"
#include "net/sharer_transport_config.h"
#include "sharer_defines.h"

#include "ppapi/cpp/instance.h"

namespace sharer {

VideoEncoder::Request::Request() : type(RequestType::NONE) {}

VideoEncoder::Request::~Request() {}

VideoEncoder::RequestEncode::RequestEncode() {
  type = RequestType::ENCODE;
}

VideoEncoder::RequestResize::RequestResize(const pp::Size& size)
    : size(size) {
  type = RequestType::RESIZE;
}

VideoEncoder::VideoEncoder(pp::Instance* instance, const SenderConfig& config)
    : instance_(instance),
      factory_(this),
      config_(config),
      frame_format_(PP_VIDEOFRAME_FORMAT_I420),
      last_encoded_frame_id_(kStartFrameId),
      last_timestamp_(0),
      is_initialized_(false) {
  INF() << "Starting video encoder.";
}

void VideoEncoder::Initialize() {
  thread_loop_ = pp::MessageLoop(instance_);
  encoder_thread_ = std::thread(&VideoEncoder::ThreadInitialize, this);
}

void VideoEncoder::InitializedThread(int32_t result) {
  if (!current_request_)
    WRN() << "No current request. Stop requested during startup?";

  if (current_request_->type != RequestType::RESIZE) {
    WRN() << "Wrong type of request after thread initialized: "
          << (current_request_->type == RequestType::ENCODE ?
              "ENCODE" : "NONE");
    return;
  }

  auto req = dynamic_cast<const RequestResize&>(*current_request_);
  if (req.callback) {
    req.callback(result == PP_OK);
  }

  if (!is_initialized_ && result == PP_OK) is_initialized_ = true;

  current_request_ = nullptr;
  ProcessNextRequest();
}

void VideoEncoder::EncodeFrame(pp::VideoFrame frame,
                               const base::TimeTicks& reference_time,
                               EncoderReleaseCb cb) {
  auto req = make_unique<RequestEncode>();
  req->frame = frame;
  req->callback = cb;
  req->reference_time = reference_time;

  requests_.push(std::move(req));

  ProcessNextRequest();
}

void VideoEncoder::GetEncodedFrame(EncoderEncodedCb cb) {
  if (encoded_cb_) {
    WRN() << "EncodedFrame already requested, ignoring.";
    return;
  }

  encoded_cb_ = cb;

  if (is_initialized_) {
    auto cc = factory_.NewCallback(&VideoEncoder::EmitOneFrame);
    pp::Module::Get()->core()->CallOnMainThread(0, cc);
  } else {
  }
}

void VideoEncoder::EncoderPauseDestructor() {
  uint32_t ret = thread_loop_.PostQuit(PP_TRUE);
  encoder_thread_.join();
  video_encoder_.Close();
  DINF() << "Pausing encoder thread: " << ret;
  is_initialized_ = false;
}

void VideoEncoder::Stop() { EncoderPauseDestructor(); }

/*!!!Emit One Frmae - On EncodingFrame*/
void VideoEncoder::EmitOneFrame(int32_t result) {
  if (!encoded_cb_ || encoded_frames_.empty()) {
    return;
  }

  EncoderEncodedCb cb = encoded_cb_;
  encoded_cb_ = nullptr;

  auto encoded = encoded_frames_.front();
  encoded_frames_.pop();

  cb(true, encoded);
}

void VideoEncoder::ProcessNextRequest() {
  // Already processing a request
  if (current_request_) return;

  bool keep_processing = true;
  while (!requests_.empty() && keep_processing) {
    current_request_ = std::move(requests_.front());
    requests_.pop();

    switch (current_request_->type) {
      case RequestType::NONE:
        current_request_ = nullptr;
        break;
      case RequestType::ENCODE:
        keep_processing = ProcessEncodeRequest();
        break;
      case RequestType::RESIZE:
        keep_processing = ProcessResizeRequest();
        break;
      default:
        ERR() << "Unrecognized command on encoder queue.";
        current_request_ = nullptr;
    }
  }
}

// Returns true if we can continue and process another request
bool VideoEncoder::ProcessResizeRequest() {
  auto req = dynamic_cast<const RequestResize&>(*current_request_);
  if (req.size.width() != requested_size_.width() ||
      req.size.height() != requested_size_.height()) {
    if (is_initialized_) EncoderPauseDestructor();
    requested_size_ = req.size;
  }

  if (!is_initialized_) {
    Initialize();
    // Need to wait for the encoder initialization, so stop processing.
    return false;
  }

  return true;
}

// Returns true if we can continue and process another request
bool VideoEncoder::ProcessEncodeRequest() {
  if (!is_initialized_) {
    ERR() << "Encoder not initialized.";
    current_request_ = nullptr;
    return true;
  }

  auto cc = factory_.NewCallback(&VideoEncoder::ThreadEncode);
  thread_loop_.PostWork(cc);

  // Can't encode more than one frame at once, so stop processing.
  return false;
}

void VideoEncoder::OnFrameReleased(int32_t result) {
  RequestEncode* req = dynamic_cast<RequestEncode*>(current_request_.get());
  if (req->callback) {
    req->callback(req->frame);
  }
  current_request_ = nullptr;

  ProcessNextRequest();
}

void VideoEncoder::OnEncodedFrame(int32_t result,
                                  std::shared_ptr<EncodedFrame> frame) {
  encoded_frames_.push(frame);
  EmitOneFrame(PP_OK);
}

void VideoEncoder::FlushEncodedFrames() {
  while (!encoded_frames_.empty()) {
    encoded_frames_.pop();
  }
  while (!requests_.empty()) {
    requests_.pop();
  }

  encoded_cb_ = nullptr;
}

void VideoEncoder::ChangeEncoding(const SenderConfig& config) {
  INF() << "Changing the encoding to " << config.initial_bitrate << " "
        << config.frame_rate;

  video_encoder_.RequestEncodingParametersChange(config.initial_bitrate * 1000,
                                                 config.frame_rate);
}

void VideoEncoder::Resize(const pp::Size& size, EncoderResizedCb cb) {
  auto req = make_unique<RequestResize>(size);
  req->callback = cb;

  requests_.push(std::move(req));
  ProcessNextRequest();
}

// Encoder thread methods
void VideoEncoder::ThreadInitialize() {
  DINF() << "Thread starting.";
  thread_loop_.AttachToCurrentThread();

  auto req = dynamic_cast<const RequestResize&>(*current_request_);

  auto cc = factory_.NewCallback(&VideoEncoder::ThreadInitialized);

  video_encoder_ = pp::VideoEncoder(instance_);
  // Always use VP8 codec and hardware acceleration, if available
  video_encoder_.Initialize(
      frame_format_, req.size, PP_VIDEOPROFILE_VP8_ANY,
      config_.initial_bitrate * 1000, PP_HARDWAREACCELERATION_WITHFALLBACK, cc);

  thread_loop_.Run();

  DINF() << "Thread finalizing.";
}

void VideoEncoder::ThreadInitialized(int32_t result) {
  auto cc = factory_.NewCallback(&VideoEncoder::InitializedThread);

  if (result != PP_OK) {
    ERR() << "Could not initialize VideoEncoder:" << result;
    pp::Module::Get()->core()->CallOnMainThread(0, cc, PP_ERROR_FAILED);
    return;
  }

  if (video_encoder_.GetFrameCodedSize(&encoder_size_) != PP_OK) {
    ERR() << "Could not get Frame Coded Size.";
    pp::Module::Get()->core()->CallOnMainThread(0, cc, PP_ERROR_FAILED);
    return;
  }

  DINF() << "Video encoder thread initialized.";
  pp::Module::Get()->core()->CallOnMainThread(0, cc, PP_OK);

  auto bitstream_cb = factory_.NewCallbackWithOutput(
      &VideoEncoder::ThreadOnBitstreamBufferReceived);
  video_encoder_.GetBitstreamBuffer(bitstream_cb);
}

void VideoEncoder::ThreadOnBitstreamBufferReceived(int32_t result,
                                                   PP_BitstreamBuffer buffer) {
  if (result == PP_ERROR_ABORTED) return;

  if (result != PP_OK) {
    ERR() << "Could not get bitstream buffer: " << result;
    return;
  }

  auto encoded_frame = ThreadBitstreamToEncodedFrame(buffer);
  if (encoded_frame) {
    auto encoded_main_cb =
        factory_.NewCallback(&VideoEncoder::OnEncodedFrame, encoded_frame);
    pp::Module::Get()->core()->CallOnMainThread(0, encoded_main_cb, PP_OK);
  }

  video_encoder_.RecycleBitstreamBuffer(buffer);

  auto bitstream_cb = factory_.NewCallbackWithOutput(
      &VideoEncoder::ThreadOnBitstreamBufferReceived);
  video_encoder_.GetBitstreamBuffer(bitstream_cb);
}

std::shared_ptr<EncodedFrame> VideoEncoder::ThreadBitstreamToEncodedFrame(
    PP_BitstreamBuffer buffer) {
  auto frame = std::make_shared<EncodedFrame>();
  frame->frame_id = ++last_encoded_frame_id_;
  if (buffer.key_frame) {
    frame->dependency = EncodedFrame::KEY;
    frame->referenced_frame_id = frame->frame_id;
  } else {
    frame->dependency = EncodedFrame::DEPENDENT;
    frame->referenced_frame_id = frame->frame_id - 1;
  }

  frame->rtp_timestamp =
      PP_TimeDeltaToRtpDelta(last_timestamp_, kVideoFrequency);
  frame->reference_time = last_reference_time_;
  frame->data.clear();
  frame->data.reserve(buffer.size);
  frame->data.insert(0, static_cast<char*>(buffer.buffer), buffer.size);

  return frame;
}

void VideoEncoder::ThreadEncode(int32_t result) {
  /* DINF() << "Request to encode frame."; */
  auto cc = factory_.NewCallbackWithOutput(&VideoEncoder::ThreadOnEncoderFrame,
                                           current_request_.get());
  video_encoder_.GetVideoFrame(cc);
}

void VideoEncoder::ThreadInformFrameRelease(int32_t result) {
  auto cc = factory_.NewCallback(&VideoEncoder::OnFrameReleased);
  pp::Module::Get()->core()->CallOnMainThread(0, cc, result);
}

void VideoEncoder::ThreadOnEncoderFrame(int32_t result,
                                        pp::VideoFrame encoder_frame,
                                        Request* req_base) {
  if (result == PP_ERROR_ABORTED) {
    ThreadInformFrameRelease(result);
    return;
  }

  if (result != PP_OK) {
    ERR() << "Could not get frame from encoder: " << result;
    ThreadInformFrameRelease(result);
    return;
  }

  // TODO: Check for frame size

  RequestEncode* req = dynamic_cast<RequestEncode*>(req_base);
  if (ThreadCopyVideoFrame(encoder_frame, req->frame) == PP_OK) {
    PP_TimeDelta timestamp = req->frame.GetTimestamp();

    last_timestamp_ = timestamp;
    last_reference_time_ = req->reference_time;
    auto cc =
        factory_.NewCallback(&VideoEncoder::ThreadOnEncodeDone, timestamp, req);
    video_encoder_.Encode(encoder_frame, PP_FALSE, cc);
  }

  ThreadInformFrameRelease(PP_OK);
}

int32_t VideoEncoder::ThreadCopyVideoFrame(pp::VideoFrame dst,
                                           pp::VideoFrame src) {
  if (dst.GetDataBufferSize() < src.GetDataBufferSize()) {
    ERR() << "Incorrect destination video frame buffer size: "
          << dst.GetDataBufferSize() << " < " << src.GetDataBufferSize();
    return PP_ERROR_FAILED;
  }

  dst.SetTimestamp(src.GetTimestamp());
  memcpy(dst.GetDataBuffer(), src.GetDataBuffer(), src.GetDataBufferSize());
  return PP_OK;
}

void VideoEncoder::ThreadOnEncodeDone(int32_t result, PP_TimeDelta timestamp,
                                      RequestEncode* req) {
  if (result == PP_ERROR_ABORTED) return;

  if (result != PP_OK) {
    ERR() << "Encode failed: " << result;
    return;
  }
}

}  // namespace sharer
