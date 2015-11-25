// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BASE_LOGGER_H_
#define BASE_LOGGER_H_

#define DEBUG 1

#include "base/log_impl.h"

#include "ppapi/cpp/instance.h"

enum LogLevel {
  LOGINFO = 0,
  LOGWARNING,
  LOGERROR,
  LOGDISABLED
};

void LogInit(pp::Instance* instance, LogLevel level);
base::LoggedStream LogPrint(LogLevel level);

#define LOG(level) LogPrint(level).s() << __FILE__ << ":" << __LINE__ << " "

#ifdef DEBUG
#define DLOG(level) LOG(level)
#else
#define DLOG(level) while (false) LogPrint(level).s()
#endif

#define INF() LOG(LOGINFO)
#define WRN() LOG(LOGWARNING)
#define ERR() LOG(LOGERROR)

#define DINF() DLOG(LOGINFO)
#define DWRN() DLOG(LOGWARNING)
#define DERR() DLOG(LOGERROR)

#endif // BASE_LOGGER_H_
