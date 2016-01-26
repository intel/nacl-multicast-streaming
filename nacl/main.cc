// Copyright 2014 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdio.h>
#include <string.h>

#include <iostream>
#include <queue>
#include <sstream>

#include "ppapi/c/pp_errors.h"
#include "ppapi/c/ppb_console.h"
#include "ppapi/c/ppb_opengles2.h"
#include "ppapi/cpp/graphics_3d.h"
#include "ppapi/cpp/graphics_3d_client.h"
#include "ppapi/cpp/input_event.h"
#include "ppapi/cpp/instance.h"
#include "ppapi/cpp/media_stream_video_track.h"
#include "ppapi/cpp/module.h"
#include "ppapi/cpp/rect.h"
#include "ppapi/cpp/var.h"
#include "ppapi/cpp/var_dictionary.h"
/* #include "ppapi/cpp/video_decoder.h" */
#include "ppapi/utility/completion_callback_factory.h"

#include "base/logger.h"
#include "base/ptr_utils.h"
#include "sharer_config.h"
#include "net/sharer_transport_config.h"
#include "receiver/decoder.h"
#include "receiver/network_handler.h"
#include "sharer_defines.h"
#include "sharer_sender.h"

// Use assert as a poor-man's CHECK, even in non-debug mode.
// Since <assert.h> redefines assert on every inclusion (it doesn't use
// include-guards), make sure this is the last file #include'd in this file.
#undef NDEBUG
#include <assert.h>

// Assert |context_| isn't holding any GL Errors.  Done as a macro instead of a
// function to preserve line number information in the failure message.
#define assertNoGLError() assert(!gles2_if_->GetError(context_->pp_resource()));

namespace {

struct Shader {
  Shader() : program(0), texcoord_scale_location(0) {}
  ~Shader() {}

  GLuint program;
  GLint texcoord_scale_location;
};

class MyInstance;

struct PendingPicture {
  PendingPicture(Decoder* decoder, const PP_VideoPicture& picture)
      : decoder(decoder), picture(picture) {}
  ~PendingPicture() {}

  Decoder* decoder;
  PP_VideoPicture picture;
};

class MyInstance : public pp::Instance, public pp::Graphics3DClient {
 public:
  MyInstance(PP_Instance instance, pp::Module* module);
  virtual ~MyInstance();

  // pp::Instance implementation.
  virtual void DidChangeView(const pp::Rect& position,
                             const pp::Rect& clip_ignored);

  // pp::Graphics3DClient implementation.
  virtual void Graphics3DContextLost() {
    // TODO(vrk/fischman): Properly reset after a lost graphics context.  In
    // particular need to delete context_ and re-create textures.
    // Probably have to recreate the decoder from scratch, because old textures
    // can still be outstanding in the decoder!
    assert(false && "Unexpectedly lost graphics context");
  }

  virtual void HandleMessage(const pp::Var& var_message);

  virtual void PaintPicture(Decoder* decoder, const PP_VideoPicture& picture);
  void RequestFrame();
  virtual void DecodeDone();
  virtual void FrameReceived(std::shared_ptr<EncodedFrame> encoded);

 private:
  // Log an error to the developer console and stderr by creating a temporary
  // object of this type and streaming to it.  Example usage:
  // LogError(this).s() << "Hello world: " << 42;
  class LogError {
   public:
    LogError(MyInstance* instance) : instance_(instance) {}
    ~LogError() {
      const std::string& msg = stream_.str();
      instance_->console_if_->Log(instance_->pp_instance(), PP_LOGLEVEL_ERROR,
                                  pp::Var(msg).pp_var());
      std::cerr << msg << std::endl;
    }
    // Impl note: it would have been nicer to have LogError derive from
    // std::ostringstream so that it can be streamed to directly, but lookup
    // rules turn streamed string literals to hex pointers on output.
    std::ostringstream& s() { return stream_; }

   private:
    MyInstance* instance_;
    std::ostringstream stream_;
  };

  void InitializeDecoder();

  // GL-related functions.
  void InitGL();
  void CreateGLObjects();
  void Create2DProgramOnce();
  void CreateRectangleARBProgramOnce();
  void CreateExternalOESProgramOnce();
  Shader CreateProgram(const char* vertex_shader, const char* fragment_shader);
  void CreateShader(GLuint program, GLenum type, const char* source, int size);
  void PaintNextPicture();
  void PaintFinished(int32_t result);
  void StartNetwork();
  void StartPlaying(int cmd_id);
  void StopPlaying(int cmd_id);
  void SharerMessage(int cmd_id, bool success, const pp::Var& payload);
  void StartSharer(int cmd_id, const pp::Var& payload);
  void StopSharer(int cmd_id, const pp::Var& payload);
  void ChangeEncoding(int cmd_id, const pp::Var& payload);
  void SetSharerTracks(int cmd_id, const pp::Var& payload);

  pp::Size plugin_size_;
  bool is_painting_;
  bool is_listening_;

  // When decode outpaces render, we queue up decoded pictures for later
  // painting.
  typedef std::queue<PendingPicture> PendingPictureQueue;
  PendingPictureQueue pending_pictures_;

  int num_frames_rendered_;
  PP_TimeTicks first_frame_delivered_ticks_;
  PP_TimeTicks last_swap_request_ticks_;
  PP_TimeTicks swap_ticks_;
  pp::CompletionCallbackFactory<MyInstance> callback_factory_;

  // Unowned pointers.
  const PPB_Console* console_if_;
  const PPB_Core* core_if_;
  const PPB_OpenGLES2* gles2_if_;

  // Owned data.
  pp::Graphics3D* context_;
  bool gl_initialized_;
  std::unique_ptr<Decoder> video_decoder_;
  std::unique_ptr<NetworkHandler> network_handler_;

  pp::VarDictionary sender_supported_params_;
  std::map<int, std::unique_ptr<sharer::SharerSender>> senders_;
  int next_sender_id_;

  // Shader program to draw GL_TEXTURE_2D target.
  Shader shader_2d_;
  // Shader program to draw GL_TEXTURE_RECTANGLE_ARB target.
  Shader shader_rectangle_arb_;
  // Shader program to draw GL_TEXTURE_EXTERNAL_OES target.
  Shader shader_external_oes_;
};

MyInstance::MyInstance(PP_Instance instance, pp::Module* module)
    : pp::Instance(instance),
      pp::Graphics3DClient(this),
      is_painting_(false),
      is_listening_(false),
      num_frames_rendered_(0),
      first_frame_delivered_ticks_(-1),
      last_swap_request_ticks_(-1),
      swap_ticks_(0),
      callback_factory_(this),
      context_(NULL),
      gl_initialized_(false),
      next_sender_id_(0) {
  LogInit(this, LOGINFO);
  console_if_ = static_cast<const PPB_Console*>(
      pp::Module::Get()->GetBrowserInterface(PPB_CONSOLE_INTERFACE));
  core_if_ = static_cast<const PPB_Core*>(
      pp::Module::Get()->GetBrowserInterface(PPB_CORE_INTERFACE));
  gles2_if_ = static_cast<const PPB_OpenGLES2*>(
      pp::Module::Get()->GetBrowserInterface(PPB_OPENGLES2_INTERFACE));

  RequestInputEvents(PP_INPUTEVENT_CLASS_MOUSE);
}

MyInstance::~MyInstance() {
  if (!context_) return;

  PP_Resource graphics_3d = context_->pp_resource();
  if (shader_2d_.program)
    gles2_if_->DeleteProgram(graphics_3d, shader_2d_.program);
  if (shader_rectangle_arb_.program)
    gles2_if_->DeleteProgram(graphics_3d, shader_rectangle_arb_.program);
  if (shader_external_oes_.program)
    gles2_if_->DeleteProgram(graphics_3d, shader_external_oes_.program);

  delete context_;
}

void MyInstance::DidChangeView(const pp::Rect& position,
                               const pp::Rect& clip_ignored) {
  if (position.width() == 0 || position.height() == 0) return;
  if (plugin_size_.width()) {
    INF() << "Changing view size to: " << position.width() << "x"
          << position.height();
  }
  plugin_size_ = position.size();

  // Resize buffers only if the GL context was initialized
  if (gl_initialized_) {
    int32_t result =
        context_->ResizeBuffers(plugin_size_.width(), plugin_size_.height());
    if (result != PP_OK) {
      ERR() << "Could not resize buffers: " << result;
    }
  }
}

void MyInstance::StartPlaying(int cmd_id) {
  if (is_listening_) {
    WRN() << "Playback already started.";
    SharerMessage(cmd_id, false, pp::Var());
    return;
  }

  if (!gl_initialized_) {
    gl_initialized_ = true;
    InitGL();
    INF() << "StartPlaying: Initialized GL context";
  }

  InitializeDecoder();
  StartNetwork();
  is_listening_ = true;
  SharerMessage(cmd_id, true, pp::Var());
}

void MyInstance::StopPlaying(int cmd_id) {
  network_handler_ = nullptr;
  video_decoder_ = nullptr;
  is_listening_ = false;
  SharerMessage(cmd_id, true, pp::Var());
}

void MyInstance::SharerMessage(int cmd_id, bool success,
                               const pp::Var& payload) {
  pp::VarDictionary dict;
  dict.Set("cmd_id", cmd_id);
  dict.Set("success", success);
  dict.Set("payload", payload);
  this->PostMessage(dict);
}

void MyInstance::StopSharer(int cmd_id, const pp::Var& payload) {
  INF() << "Stop sharer.";

  if (!payload.is_dictionary()) {
    ERR() << "Couldn't stop sharer: missing payload.";
    SharerMessage(cmd_id, false, pp::Var());
  }
  pp::VarDictionary dict(payload);

  pp::Var var_id = dict.Get("sharer_id");
  if (!var_id.is_int()) {
    ERR() << "Need a sharer_id to stop tracks.";
    return;
  }

  int sharer_id = var_id.AsInt();
  auto it = senders_.find(sharer_id);

  if (it == senders_.end()) {
    ERR() << "Couldn't find sharer with id: " << sharer_id;
    return;
  }

  DINF() << "Stop sending.";
  auto stop_cb = [this, cmd_id](bool success) {
    SharerMessage(cmd_id, success, pp::Var());
  };
  it->second->StopTracks(stop_cb);
}

void MyInstance::ChangeEncoding(int cmd_id, const pp::Var& payload) {
  sharer::SenderConfig config;

  if (!payload.is_dictionary()) {
    ERR() << "Couldn't change encoding: missing payload.";
    SharerMessage(cmd_id, false, pp::Var());
  }
  pp::VarDictionary dict(payload);

  DINF() << "Requested encoding change to "
         << "bitrate " << dict.Get(pp::Var("bitrate")).AsString() << ", fps "
         << dict.Get(pp::Var("fps")).AsString();

  /* Updating config values */
  if (dict.HasKey(pp::Var("bitrate")))
    config.initial_bitrate = std::stoi(dict.Get(pp::Var("bitrate")).AsString());
  if (dict.HasKey(pp::Var("fps")))
    config.frame_rate = std::stoi(dict.Get(pp::Var("fps")).AsString());

  pp::Var var_id = dict.Get("sharer_id");

  int sharer_id = var_id.AsInt();
  auto it = senders_.find(sharer_id);

  if (it == senders_.end()) {
    ERR() << "Couldn't find sharer with id: " << sharer_id;
    return;
  }

  it->second->ChangeEncoding(config);
}

void MyInstance::StartSharer(int cmd_id, const pp::Var& payload) {
  sharer::SenderConfig config;

  if (!payload.is_dictionary()) {
    ERR() << "Couldn't start sharer: missing payload.";
    SharerMessage(cmd_id, false, pp::Var());
  }
  pp::VarDictionary dict(payload);

  if (dict.HasKey(pp::Var("ip")))
    config.remote_address = dict.Get(pp::Var("ip")).AsString();
  if (dict.HasKey(pp::Var("bitrate")))
    config.initial_bitrate = std::stoi(dict.Get(pp::Var("bitrate")).AsString());
  if (dict.HasKey(pp::Var("fps")))
    config.frame_rate = std::stoi(dict.Get(pp::Var("fps")).AsString());

  INF() << "Starting content sharing.";

  auto sender = make_unique<sharer::SharerSender>(this, next_sender_id_++);
  auto inserted =
      senders_.insert(std::make_pair(sender->id(), std::move(sender)));

  auto initialized_cb =
      [this, cmd_id](int id, sharer::SharerSender::InitResult result) {
    if (result == sharer::SharerSender::INIT_SUCCESS) {
      INF() << "Initialized SharedSender.";
      pp::VarDictionary dict;
      dict.Set("sharer_id", id);
      SharerMessage(cmd_id, true, dict);
    } else {
      ERR() << "Could not initialize sender: " << id << ", error: " << result;
      senders_.erase(id);
      SharerMessage(cmd_id, false, pp::Var());
    }
  };

  if (inserted.second) {
    const auto& sender = inserted.first->second;
    DINF() << "Initializing SharerSender: ", inserted.first->first;
    sender->Initialize(config, initialized_cb);
  } else {
    ERR() << "Could not insert SharerSender: " << inserted.first->first
          << ", maybe already in use?";
    SharerMessage(cmd_id, false, pp::Var());
  }
}

void MyInstance::SetSharerTracks(int cmd_id, const pp::Var& payload) {
  if (!payload.is_dictionary()) {
    ERR() << "Couldn't start sharer: missing payload.";
    SharerMessage(cmd_id, false, pp::Var());
  }
  pp::VarDictionary dict(payload);

  pp::Var var_id = dict.Get("sharer_id");

  if (var_id.is_null()) {
    ERR() << "Can't find sharer with null id.";
    return;
  }

  int sharer_id = var_id.AsInt();

  auto it = senders_.find(sharer_id);
  if (it == senders_.end()) {
    ERR() << "Couldn't find sharer with id: " << sharer_id;
    return;
  }

  pp::Var var_video = dict.Get("video_track");
  if (!var_video.is_resource()) {
    ERR() << "Given track is not a resource.";
    return;
  }

  pp::Resource video_resource = var_video.AsResource();
  auto video_track = pp::MediaStreamVideoTrack(video_resource);

  auto set_tracks_cb = [this, cmd_id](bool success) {
    SharerMessage(cmd_id, success, pp::Var());
  };
  if (!it->second->SetTracks(video_track, set_tracks_cb)) {
    ERR() << "Could not set tracks.";
    return;
  }
}

void MyInstance::HandleMessage(const pp::Var& var_message) {
  if (!var_message.is_dictionary()) return;

  pp::VarDictionary dict(var_message);

  if (!dict.HasKey("cmd_id")) {
    DERR() << "Can't parse command without command id.";
    return;
  }

  pp::Var var_cmd_id = dict.Get("cmd_id");
  if (var_cmd_id.is_null()) {
    DERR() << "Can't parse command with cmd_id == null.";
    return;
  }

  int cmd_id = var_cmd_id.AsInt();

  if (!dict.HasKey("cmd")) {
    ERR() << "Can't parse message: " << cmd_id << ", without a command.";
    SharerMessage(cmd_id, false, pp::Var());
    return;
  }

  pp::Var var_cmd = dict.Get("cmd");
  if (var_cmd.is_null()) {
    ERR() << "Can't parse message: " << cmd_id << ", command == null.";
    SharerMessage(cmd_id, false, pp::Var());
    return;
  }

  std::string cmd = var_cmd.AsString();

  DINF() << "* Received command: " << cmd << ", cmd id: " << cmd_id;

  pp::Var var_payload;
  if (dict.HasKey("payload")) var_payload = dict.Get("payload");

  if (cmd == "startUDP" || cmd == "startReceiver") {
    StartPlaying(cmd_id);
  } else if (cmd == "stopReceiver") {
    StopPlaying(cmd_id);
  } else if (cmd == "startSharer") {
    StartSharer(cmd_id, var_payload);
  } else if (cmd == "setSharerTracks") {
    SetSharerTracks(cmd_id, var_payload);
  } else if (cmd == "stopSharer") {
    StopSharer(cmd_id, var_payload);
  } else if (cmd == "changeEncoding") {
    ChangeEncoding(cmd_id, var_payload);
  } else {
    ERR() << "Unknown command: " << cmd;
  }
}

void MyInstance::InitializeDecoder() {
  assert(video_decoder_ == nullptr);
  video_decoder_ = make_unique<Decoder>(this, 0, *context_);
  video_decoder_->SetPictureReadyCb([this](
      Decoder* decoder,
      PP_VideoPicture picture) { this->PaintPicture(decoder, picture); });
}

void MyInstance::PaintPicture(Decoder* decoder,
                              const PP_VideoPicture& picture) {
  if (first_frame_delivered_ticks_ == -1)
    assert((first_frame_delivered_ticks_ = core_if_->GetTimeTicks()) != -1);

  pending_pictures_.push(PendingPicture(decoder, picture));
  if (!is_painting_) PaintNextPicture();
}

void MyInstance::RequestFrame() {
  network_handler_->GetNextFrame([this](std::shared_ptr<EncodedFrame> encoded) {
    this->FrameReceived(encoded);
  });
}

void MyInstance::DecodeDone() {
  if (!network_handler_) return;
  network_handler_->ReleaseFrame();
  RequestFrame();
}

void MyInstance::FrameReceived(std::shared_ptr<EncodedFrame> encoded) {
  /* DINF() << "Frame received: " << encoded->frame_id; */
  video_decoder_->DecodeNextFrame(encoded, [this]() { this->DecodeDone(); });
}

void MyInstance::StartNetwork() {
  ReceiverConfig audio_config;
  ReceiverConfig video_config;

  audio_config.target_frame_rate = 100;
  audio_config.rtp_timebase = 48000;
  audio_config.receiver_ssrc = 2;
  audio_config.sender_ssrc = 1;

  video_config.target_frame_rate = 30;
  video_config.rtp_timebase = 90000;
  video_config.receiver_ssrc = 12;
  video_config.sender_ssrc = 11;

  network_handler_ =
      make_unique<NetworkHandler>(this, audio_config, video_config);
  RequestFrame();
}

void MyInstance::PaintNextPicture() {
  assert(!is_painting_);
  is_painting_ = true;

  const PendingPicture& next = pending_pictures_.front();
  const PP_VideoPicture& picture = next.picture;

  /* if (picture.texture_target == 0) { */
  /*   PaintFinished(PP_OK); */
  /*   return; */
  /* } */

  int x = 0;
  int y = 0;

  PP_Resource graphics_3d = context_->pp_resource();
  if (picture.texture_target == GL_TEXTURE_2D) {
    Create2DProgramOnce();
    gles2_if_->UseProgram(graphics_3d, shader_2d_.program);
    gles2_if_->Uniform2f(graphics_3d, shader_2d_.texcoord_scale_location, 1.0,
                         1.0);
  } else if (picture.texture_target == GL_TEXTURE_RECTANGLE_ARB) {
    CreateRectangleARBProgramOnce();
    gles2_if_->UseProgram(graphics_3d, shader_rectangle_arb_.program);
    gles2_if_->Uniform2f(
        graphics_3d, shader_rectangle_arb_.texcoord_scale_location,
        picture.texture_size.width, picture.texture_size.height);
  } else {
    assert(picture.texture_target == GL_TEXTURE_EXTERNAL_OES);
    CreateExternalOESProgramOnce();
    gles2_if_->UseProgram(graphics_3d, shader_external_oes_.program);
    gles2_if_->Uniform2f(
        graphics_3d, shader_external_oes_.texcoord_scale_location, 1.0, 1.0);
  }

  DINF() << ">>>>>> Texture size: " << picture.texture_size.width << " x " << picture.texture_size.height;

  gles2_if_->Viewport(graphics_3d, x, y, plugin_size_.width(),
                      plugin_size_.height());
  gles2_if_->ActiveTexture(graphics_3d, GL_TEXTURE0);
  gles2_if_->BindTexture(graphics_3d, picture.texture_target,
                         picture.texture_id);
  gles2_if_->DrawArrays(graphics_3d, GL_TRIANGLE_STRIP, 0, 4);

  gles2_if_->UseProgram(graphics_3d, 0);

  last_swap_request_ticks_ = core_if_->GetTimeTicks();
  context_->SwapBuffers(
      callback_factory_.NewCallback(&MyInstance::PaintFinished));
}

void MyInstance::PaintFinished(int32_t result) {
  if (result != PP_OK) {
    DINF() << "Stopping painting, result = " << result;
    return;
  }

  swap_ticks_ += core_if_->GetTimeTicks() - last_swap_request_ticks_;
  is_painting_ = false;
  ++num_frames_rendered_;
  if (num_frames_rendered_ % 500 == 0) {
    double elapsed = core_if_->GetTimeTicks() - first_frame_delivered_ticks_;
    double fps = (elapsed > 0) ? num_frames_rendered_ / elapsed : 1000;
    double ms_per_swap = (swap_ticks_ * 1e3) / num_frames_rendered_;
    double secs_average_latency = 0;
    secs_average_latency += video_decoder_->GetAverageLatency();
    /* secs_average_latency /= video_decoders_.size(); */
    double ms_average_latency = 1000 * secs_average_latency;
    LogError(this).s() << "Rendered frames: " << num_frames_rendered_
                       << ", fps: " << fps
                       << ", with average ms/swap of: " << ms_per_swap
                       << ", with average latency (ms) of: "
                       << ms_average_latency;
  }

  // If the decoders were reset, this will be empty.
  if (pending_pictures_.empty()) return;

  const PendingPicture& next = pending_pictures_.front();
  Decoder* decoder = next.decoder;
  const PP_VideoPicture& picture = next.picture;
  decoder->RecyclePicture(picture);
  pending_pictures_.pop();

  // Keep painting as long as we have pictures.
  if (!pending_pictures_.empty()) {
    PaintNextPicture();
  }
}

void MyInstance::InitGL() {
  assert(plugin_size_.width() && plugin_size_.height());
  is_painting_ = false;

  assert(!context_);
  int32_t context_attributes[] = {
      PP_GRAPHICS3DATTRIB_ALPHA_SIZE,     8,
      PP_GRAPHICS3DATTRIB_BLUE_SIZE,      8,
      PP_GRAPHICS3DATTRIB_GREEN_SIZE,     8,
      PP_GRAPHICS3DATTRIB_RED_SIZE,       8,
      PP_GRAPHICS3DATTRIB_DEPTH_SIZE,     0,
      PP_GRAPHICS3DATTRIB_STENCIL_SIZE,   0,
      PP_GRAPHICS3DATTRIB_SAMPLES,        0,
      PP_GRAPHICS3DATTRIB_SAMPLE_BUFFERS, 0,
      PP_GRAPHICS3DATTRIB_WIDTH,          plugin_size_.width(),
      PP_GRAPHICS3DATTRIB_HEIGHT,         plugin_size_.height(),
      PP_GRAPHICS3DATTRIB_NONE,
  };
  context_ = new pp::Graphics3D(this, context_attributes);
  assert(!context_->is_null());
  assert(BindGraphics(*context_));

  // Clear color bit.
  gles2_if_->ClearColor(context_->pp_resource(), 1, 0, 0, 1);
  gles2_if_->Clear(context_->pp_resource(), GL_COLOR_BUFFER_BIT);

  assertNoGLError();

  CreateGLObjects();
}

void MyInstance::CreateGLObjects() {
  // Assign vertex positions and texture coordinates to buffers for use in
  // shader program.
  static const float kVertices[] = {
      -1, -1, -1, 1, 1, -1, 1, 1,  // Position coordinates.
      0,  1,  0,  0, 1, 1,  1, 0,  // Texture coordinates.
  };

  GLuint buffer;
  gles2_if_->GenBuffers(context_->pp_resource(), 1, &buffer);
  gles2_if_->BindBuffer(context_->pp_resource(), GL_ARRAY_BUFFER, buffer);

  gles2_if_->BufferData(context_->pp_resource(), GL_ARRAY_BUFFER,
                        sizeof(kVertices), kVertices, GL_STATIC_DRAW);
  assertNoGLError();
}

static const char kVertexShader[] =
    "varying vec2 v_texCoord;            \n"
    "attribute vec4 a_position;          \n"
    "attribute vec2 a_texCoord;          \n"
    "uniform vec2 v_scale;               \n"
    "void main()                         \n"
    "{                                   \n"
    "    v_texCoord = v_scale * a_texCoord; \n"
    "    gl_Position = a_position;       \n"
    "}";

void MyInstance::Create2DProgramOnce() {
  if (shader_2d_.program) return;
  static const char kFragmentShader2D[] =
      "precision mediump float;            \n"
      "varying vec2 v_texCoord;            \n"
      "uniform sampler2D s_texture;        \n"
      "void main()                         \n"
      "{"
      "    gl_FragColor = texture2D(s_texture, v_texCoord); \n"
      "}";
  shader_2d_ = CreateProgram(kVertexShader, kFragmentShader2D);
  assertNoGLError();
}

void MyInstance::CreateRectangleARBProgramOnce() {
  if (shader_rectangle_arb_.program) return;
  static const char kFragmentShaderRectangle[] =
      "#extension GL_ARB_texture_rectangle : require\n"
      "precision mediump float;            \n"
      "varying vec2 v_texCoord;            \n"
      "uniform sampler2DRect s_texture;    \n"
      "void main()                         \n"
      "{"
      "    gl_FragColor = texture2DRect(s_texture, v_texCoord).rgba; \n"
      "}";
  shader_rectangle_arb_ =
      CreateProgram(kVertexShader, kFragmentShaderRectangle);
  assertNoGLError();
}

void MyInstance::CreateExternalOESProgramOnce() {
  if (shader_external_oes_.program) return;
  static const char kFragmentShaderExternal[] =
      "#extension GL_OES_EGL_image_external : require\n"
      "precision mediump float;            \n"
      "varying vec2 v_texCoord;            \n"
      "uniform samplerExternalOES s_texture; \n"
      "void main()                         \n"
      "{"
      "    gl_FragColor = texture2D(s_texture, v_texCoord); \n"
      "}";
  shader_external_oes_ = CreateProgram(kVertexShader, kFragmentShaderExternal);
  assertNoGLError();
}

Shader MyInstance::CreateProgram(const char* vertex_shader,
                                 const char* fragment_shader) {
  Shader shader;

  // Create shader program.
  shader.program = gles2_if_->CreateProgram(context_->pp_resource());
  CreateShader(shader.program, GL_VERTEX_SHADER, vertex_shader,
               strlen(vertex_shader));
  CreateShader(shader.program, GL_FRAGMENT_SHADER, fragment_shader,
               strlen(fragment_shader));
  gles2_if_->LinkProgram(context_->pp_resource(), shader.program);
  gles2_if_->UseProgram(context_->pp_resource(), shader.program);
  gles2_if_->Uniform1i(
      context_->pp_resource(),
      gles2_if_->GetUniformLocation(context_->pp_resource(), shader.program,
                                    "s_texture"),
      0);
  assertNoGLError();

  shader.texcoord_scale_location = gles2_if_->GetUniformLocation(
      context_->pp_resource(), shader.program, "v_scale");

  GLint pos_location = gles2_if_->GetAttribLocation(
      context_->pp_resource(), shader.program, "a_position");
  GLint tc_location = gles2_if_->GetAttribLocation(
      context_->pp_resource(), shader.program, "a_texCoord");
  assertNoGLError();

  gles2_if_->EnableVertexAttribArray(context_->pp_resource(), pos_location);
  gles2_if_->VertexAttribPointer(context_->pp_resource(), pos_location, 2,
                                 GL_FLOAT, GL_FALSE, 0, 0);
  gles2_if_->EnableVertexAttribArray(context_->pp_resource(), tc_location);
  gles2_if_->VertexAttribPointer(
      context_->pp_resource(), tc_location, 2, GL_FLOAT, GL_FALSE, 0,
      static_cast<float*>(0) + 8);  // Skip position coordinates.

  gles2_if_->UseProgram(context_->pp_resource(), 0);
  assertNoGLError();
  return shader;
}

void MyInstance::CreateShader(GLuint program, GLenum type, const char* source,
                              int size) {
  GLuint shader = gles2_if_->CreateShader(context_->pp_resource(), type);
  gles2_if_->ShaderSource(context_->pp_resource(), shader, 1, &source, &size);
  gles2_if_->CompileShader(context_->pp_resource(), shader);
  gles2_if_->AttachShader(context_->pp_resource(), program, shader);
  gles2_if_->DeleteShader(context_->pp_resource(), shader);
}

// This object is the global object representing this plugin library as long
// as it is loaded.
class MyModule : public pp::Module {
 public:
  MyModule() : pp::Module() {}
  virtual ~MyModule() {}

  virtual pp::Instance* CreateInstance(PP_Instance instance) {
    return new MyInstance(instance, this);
  }
};

}  // anonymous namespace

namespace pp {
// Factory function for your specialization of the Module object.
Module* CreateModule() { return new MyModule(); }
}  // namespace pp
