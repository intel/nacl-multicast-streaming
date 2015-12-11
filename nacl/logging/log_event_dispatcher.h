// Copyright 2015 The Chromium Authors. All rights reserved.
// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGGING_LOG_EVENT_DISPATCHER_H_
#define LOGGING_LOG_EVENT_DISPATCHER_H_

#include <vector>

#include "base/macros.h"
#include "logging/logging_defines.h"
#include "raw_event_subscriber.h"

namespace sharer {

// A non-thread-safe receiver of logging events that manages an active list of
// EventSubscribers and dispatches the logging events to them on the MAIN
// thread. All methods, constructor, and destructor can be invoked on any
// thread. Thread safety might be added in the future, so we keep the
// implementation of the dispatcher inside the Impl class.
class LogEventDispatcher {
 public:
  explicit LogEventDispatcher();

  ~LogEventDispatcher();

  // Events can only be dispatched from the MAIN thread.
  void DispatchFrameEvent(std::unique_ptr<FrameEvent> event) const;
  void DispatchPacketEvent(std::unique_ptr<PacketEvent> event) const;
  void DispatchBatchOfEvents(
      std::unique_ptr<std::vector<FrameEvent>> frame_events,
      std::unique_ptr<std::vector<PacketEvent>> packet_events) const;

  // Adds |subscriber| from the MAIN thread, to the active list to begin
  // receiving events on MAIN thread. Unsubscribe() must be called before
  // |subscriber| is destroyed.
  void Subscribe(RawEventSubscriber* subscriber);

  // Removes |subscriber| from the active list.  Once this method returns, the
  // |subscriber| is guaranteed not to receive any more events.
  void Unsubscribe(RawEventSubscriber* subscriber);

 private:
  // The part of the implementation that runs exclusively on the MAIN thread.
  class Impl {
   public:
    Impl();
    ~Impl();

    void DispatchFrameEvent(std::unique_ptr<FrameEvent> event) const;
    void DispatchPacketEvent(std::unique_ptr<PacketEvent> event) const;
    void DispatchBatchOfEvents(
        std::unique_ptr<std::vector<FrameEvent>> frame_events,
        std::unique_ptr<std::vector<PacketEvent>> packet_events) const;
    void Subscribe(RawEventSubscriber* subscriber);
    void Unsubscribe(RawEventSubscriber* subscriber);

   private:
    std::vector<RawEventSubscriber*> subscribers_;

    DISALLOW_COPY_AND_ASSIGN(Impl);
  };

  Impl impl_;

  DISALLOW_COPY_AND_ASSIGN(LogEventDispatcher);
};

}  // namespace sharer

#endif  // LOGGING_LOG_EVENT_DISPATCHER_H_
