// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/logger.h"
#include "ppapi/cpp/instance.h"

static pp::Instance* instance_ = nullptr; // non-owning pointer
static LogLevel log_level_ = LOGDISABLED;

void LogInit(pp::Instance* instance, LogLevel level) {
  instance_ = instance;
  log_level_ = level;

  DINF() << "Initializing log system with level: " << level;
}

base::LoggedStream LogPrint(LogLevel level) {
  return base::LoggedStream(instance_, log_level_, level);
}
