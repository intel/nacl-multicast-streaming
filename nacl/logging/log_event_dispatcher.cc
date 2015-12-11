// Copyright 2015 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/logger.h"
#include "logging/log_event_dispatcher.h"

#include <algorithm>

#include "ppapi/cpp/module.h"

namespace sharer {

LogEventDispatcher::LogEventDispatcher() : impl_() {}

LogEventDispatcher::~LogEventDispatcher() {}

void LogEventDispatcher::DispatchFrameEvent(
    std::unique_ptr<FrameEvent> event) const {
  if (pp::Module::Get()->core()->IsMainThread()) {
    impl_.DispatchFrameEvent(std::move(event));
  } else {
    // TODO: This is how dispatching event from a different thread was done in
    // original Chromecast code. So far we don't support it, but leave this
    // commented code as a reference for the future.
    //
    // env_->PostTask(CastEnvironment::MAIN, FROM_HERE,
    //                std::bind(&LogEventDispatcher::Impl::DispatchFrameEvent,
    //                           impl_, base::Passed(&event)));
    ERR() << "Only supported on main thread.";
  }
}

void LogEventDispatcher::DispatchPacketEvent(
    std::unique_ptr<PacketEvent> event) const {
  if (pp::Module::Get()->core()->IsMainThread()) {
    impl_.DispatchPacketEvent(std::move(event));
  } else {
    // TODO: Add support to call this from other threads.
    ERR() << "Only supported on main thread.";
  }
}

void LogEventDispatcher::DispatchBatchOfEvents(
    std::unique_ptr<std::vector<FrameEvent>> frame_events,
    std::unique_ptr<std::vector<PacketEvent>> packet_events) const {
  if (pp::Module::Get()->core()->IsMainThread()) {
    impl_.DispatchBatchOfEvents(std::move(frame_events),
                                std::move(packet_events));
  } else {
    // TODO: Add support to call this from other threads.
    ERR() << "Only supported on main thread.";
  }
}

void LogEventDispatcher::Subscribe(RawEventSubscriber* subscriber) {
  if (pp::Module::Get()->core()->IsMainThread()) {
    impl_.Subscribe(subscriber);
  } else {
    // TODO: Add support to call this from other threads.
    ERR() << "Only supported on main thread.";
  }
}

void LogEventDispatcher::Unsubscribe(RawEventSubscriber* subscriber) {
  if (pp::Module::Get()->core()->IsMainThread()) {
    impl_.Unsubscribe(subscriber);
  } else {
    // TODO: Add support to call this from other threads.
    ERR() << "Only supported on main thread.";
  }
}

LogEventDispatcher::Impl::Impl() {}

LogEventDispatcher::Impl::~Impl() { PP_DCHECK(subscribers_.empty()); }

void LogEventDispatcher::Impl::DispatchFrameEvent(
    std::unique_ptr<FrameEvent> event) const {
  for (RawEventSubscriber* s : subscribers_) s->OnReceiveFrameEvent(*event);
}

void LogEventDispatcher::Impl::DispatchPacketEvent(
    std::unique_ptr<PacketEvent> event) const {
  for (RawEventSubscriber* s : subscribers_) s->OnReceivePacketEvent(*event);
}

void LogEventDispatcher::Impl::DispatchBatchOfEvents(
    std::unique_ptr<std::vector<FrameEvent>> frame_events,
    std::unique_ptr<std::vector<PacketEvent>> packet_events) const {
  for (RawEventSubscriber* s : subscribers_) {
    for (const FrameEvent& e : *frame_events) s->OnReceiveFrameEvent(e);
    for (const PacketEvent& e : *packet_events) s->OnReceivePacketEvent(e);
  }
}

void LogEventDispatcher::Impl::Subscribe(RawEventSubscriber* subscriber) {
  PP_DCHECK(std::find(subscribers_.begin(), subscribers_.end(), subscriber) ==
            subscribers_.end());
  subscribers_.push_back(subscriber);
}

void LogEventDispatcher::Impl::Unsubscribe(RawEventSubscriber* subscriber) {
  const auto it =
      std::find(subscribers_.begin(), subscribers_.end(), subscriber);
  PP_DCHECK(it != subscribers_.end());
  subscribers_.erase(it);
}

}  // namespace sharer
