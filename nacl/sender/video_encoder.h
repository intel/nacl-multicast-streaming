// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SENDER_VIDEO_ENCODER_H_
#define SENDER_VIDEO_ENCODER_H_

#include "base/macros.h"
#include "base/time/time.h"
#include "sharer_config.h"

#include "ppapi/c/pp_time.h"
#include "ppapi/cpp/instance.h"
#include "ppapi/cpp/message_loop.h"
#include "ppapi/cpp/video_encoder.h"
#include "ppapi/utility/completion_callback_factory.h"

#include <queue>
#include <thread>

struct EncodedFrame;

namespace sharer {

struct SenderConfig;

class VideoEncoder {
 public:
  using VideoEncoderInitializedCb = std::function<void(bool result)>;
  using EncoderReleaseCb = std::function<void(pp::VideoFrame frame)>;
  using EncoderEncodedCb =
      std::function<void(bool success, std::shared_ptr<EncodedFrame> frame)>;
  using EncoderResizedCb = std::function<void(bool success)>;

  explicit VideoEncoder(pp::Instance* instance, const SenderConfig& config);

  const pp::Size& size() { return encoder_size_; }
  const PP_VideoFrame_Format format() { return frame_format_; }

  void EncodeFrame(pp::VideoFrame frame, const base::TimeTicks& timestamp,
                   EncoderReleaseCb cb);
  void GetEncodedFrame(EncoderEncodedCb cb);
  void FlushEncodedFrames();
  void Stop();
  void ChangeEncoding(const SenderConfig& config);
  void Resize(const pp::Size& size, EncoderResizedCb cb);

 private:
  enum class RequestType {
    NONE,
    ENCODE,
    RESIZE
  };

  struct Request {
    Request();
    virtual ~Request();
    RequestType type;
  };

  struct RequestEncode : Request {
    RequestEncode();
    pp::VideoFrame frame;
    EncoderReleaseCb callback;
    base::TimeTicks reference_time;
  };

  struct RequestResize : Request {
    explicit RequestResize(const pp::Size& size);
    pp::Size size;
    EncoderResizedCb callback;
  };

  void Initialize();
  void InitializedThread(int32_t result);
  void ProcessNextRequest();
  bool ProcessEncodeRequest();
  bool ProcessResizeRequest();
  void EncodeOneFrame();
  void OnFrameReleased(int32_t result);
  void OnEncodedFrame(int32_t result, std::shared_ptr<EncodedFrame> frame);
  void EmitOneFrame(int32_t result);
  void EncoderPauseDestructor();

  void ThreadInitialize();
  void ThreadInitialized(int32_t result);
  void ThreadEncode(int32_t result);
  void ThreadInformFrameRelease(int32_t result);
  void ThreadOnEncoderFrame(int32_t result, pp::VideoFrame encoder_frame,
                            Request* req);
  int32_t ThreadCopyVideoFrame(pp::VideoFrame dest, pp::VideoFrame src);
  void ThreadOnBitstreamBufferReceived(int32_t result,
                                       PP_BitstreamBuffer buffer);
  std::shared_ptr<EncodedFrame> PauseStreamToEncodedFrame();
  std::shared_ptr<EncodedFrame> ThreadBitstreamToEncodedFrame(
      PP_BitstreamBuffer buffer);
  void ThreadOnEncodeDone(int32_t result, PP_TimeDelta timestamp, RequestEncode* req);

  pp::Instance* instance_;
  pp::CompletionCallbackFactory<VideoEncoder> factory_;
  SenderConfig config_;
  pp::Size encoder_size_;

  PP_VideoFrame_Format frame_format_;

  std::queue<std::unique_ptr<Request>> requests_;
  std::unique_ptr<Request> current_request_;
  EncoderEncodedCb encoded_cb_;

  std::queue<std::shared_ptr<EncodedFrame>> encoded_frames_;

  pp::MessageLoop thread_loop_;
  std::thread encoder_thread_;

  pp::VideoEncoder video_encoder_;

  uint32_t last_encoded_frame_id_;
  PP_TimeDelta last_timestamp_;
  base::TimeTicks last_reference_time_;
  pp::Size requested_size_;
  bool is_initialized_;
  bool is_initializing_;

  DISALLOW_COPY_AND_ASSIGN(VideoEncoder);
};

}  // namespace sharer

#endif  // SENDER_VIDEO_ENCODER_H_
