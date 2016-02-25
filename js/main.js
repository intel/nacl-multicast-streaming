// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

"use strict";

var kMinMajor = 46;
var kMinMinor = 0;
var kMinBuild = 2472;

var logBlock = null;
var kMaxLogMessageLength = 20;
var logMessageArray = [];
var logMessage = logMessageLimit;
var is_sharing_ = false;
var shareMode = null;
var sharer_ = null;
var streamTrack_ = null;

var moduleWidth = null;
var moduleHeight = null;

navigator.getUserMedia = navigator.getUserMedia ||
  navigator.webkitGetUserMedia || navigator.mozGetUserMedia;

function getOptions() {
    var ip = document.getElementById('ipinput').value;
    var bitrate = document.getElementById('bitrate').value;
    var fps = document.getElementById('fps').value;

    return {
      ip: ip,
      bitrate: bitrate,
      fps: fps,
    }
}

function saveOptions(options) {
    chrome.storage.local.set(options);
}

function loadOptions(cb) {
    chrome.storage.local.get(['ip', 'bitrate', 'fps'],
                             function(options) {
        cb(options);
    });
}

function desktopShare(cbStarted) {
    chrome.desktopCapture.chooseDesktopMedia(["screen", "window"], function (streamId) {
        if (!streamId) {
            localLog(2, "couldn't get capture stream.");
            return;
        }

        var constraints = {
            audio: false,
            video: {
                mandatory: {
                    chromeMediaSource: 'desktop',
                    chromeMediaSourceId: streamId,
                    maxWidth: 4096,
                    maxHeight: 4096
                }
            }
        };

        navigator.getUserMedia(constraints, function(captureStream) {
          cbStarted(captureStream);

          is_sharing_ = true;
        }, function(error) {
            localLog(2, error.name + ": " + error.message);
        });
    });
}

function cameraShare(cbStarted) {
  var constraints = {
    video: {
      mandatory: {
        maxWidth: 4096,
        maxHeight: 4096
      }
    }
  };

  navigator.getUserMedia(constraints, function(captureStream) {
    cbStarted(captureStream);

    is_sharing_ = true;
  }, function(error) {
    localLog(2, error.name + ": " + error.message);
  });
}

function captureStartLocal(type) {
  shareMode = type;
  if (streamTrack_ === null) {
    startCapture(sharer_);
    return;
  }

  sharer_.stop().then(function() {
    startCapture(sharer_);
  });
}

function startSharerCommon(type) {
  if (sharer_ !== null) {
    captureStartLocal(type);
    return;
  }

  var options = getOptions();
  saveOptions(options);

  nms.createSharer(options).then(function(sharer) {
    localLog(0, "Created sharer: " + sharer.sharer_id);
    sharer_ = sharer;
    captureStartLocal(type);
  }).catch(function(lastError) {
    localLog(2, "Could not create sharer: " + lastError);
  });
}

function startSharerDesktop() {
  startSharerCommon("desktop");
}

function startSharerCamera() {
  startSharerCommon("camera");
}

function startCapture(sharer) {
  var shareFunc;
  if (shareMode == "desktop")
    shareFunc = desktopShare;
  else if (shareMode == "camera")
    shareFunc = cameraShare;
  else {
    localLog(2, "ERROR: Can't share find which mode to share: " + mode);
    return;
  }

  shareFunc(function(captureStream) {
    var track = captureStream.getVideoTracks()[0];
    streamTrack_ = track;
    localLog(0, "Starting stream.");
    console.log(sharer);

    sharer.shareTracks(captureStream).then(function() {
      localLog(0, "Finally started streaming.");
      document.getElementById('stopSharing').disabled = false;
      return sharer.onStopped(); // Returns a promise that resolves when the
                                 // stream is stopped
    }).then(function() {
      is_sharing_ = false;
      streamTrack_ = null;
      document.getElementById('stopSharing').disabled = true;
      localLog(0, "Finished sharing stream.");
    });
  });
}

function stopSharer() {
  if (sharer_ !== null) {
    localLog(0, "Stopping stream.");
    sharer_.stop().then(function() {
      localLog(0, "Finally stopped stream.");
    });
  }
}

function fullscreenHandler() {
  var elm = document.webkitFullscreenElement;

  if (elm == null) {
    // returning from fullscreen, restore size
    var moduleEl = document.getElementById('nacl_module');
    localLog(1, "Reloading module size: " + moduleWidth + "x" + moduleHeight);
    moduleEl.setAttribute('width', moduleWidth);
    moduleEl.setAttribute('height', moduleHeight);
  }
}

function setFullscreen() {
  var moduleEl = document.getElementById('nacl_module');
  if (!moduleEl) {
    localLog(2, "Failed to go fullscreen: can't find nacl module element.");
    return;
  }

  // Save current size before going to fullscreen
  moduleWidth = moduleEl.getAttribute('width');
  moduleHeight = moduleEl.getAttribute('height');
  localLog(1, "Saving module size: " + moduleWidth + "x" + moduleHeight);

  var newWidth = window.screen.width;
  var newHeight = window.screen.height;
  moduleEl.setAttribute('width', newWidth);
  moduleEl.setAttribute('height', newHeight);

  moduleEl.webkitRequestFullScreen();

  document.addEventListener("webkitfullscreenchange", fullscreenHandler);
}

function moduleDidLoad() {
    document.getElementById('startSharing').onclick = startSharerDesktop;
    document.getElementById('startCamSharing').onclick = startSharerCamera;
    document.getElementById('stopSharing').onclick = stopSharer;
    document.getElementById('stopSharing').disabled = true;

    document.getElementById('bitrate').oninput = bitRateChangedText;
    document.getElementById('bitrate').onchange = bitRateChanged;
    document.getElementById('fps').oninput = fpsChangedText;
    document.getElementById('fps').onchange = fpsChanged;
    document.getElementById('localhost').onchange = localhostChanged;

    document.getElementById('playStream').onclick = startPlay;
    document.getElementById('detachLog').onclick = detachLog;
    document.getElementById('fullscreen').onclick = setFullscreen;

    checkVersion();
}

function stopStreamReceiver() {
  localLog(2, "STOP....");
  nms.stopPlayer().then(function() {
    document.getElementById('playStream').innerText = 'Play stream';
    document.getElementById('playStream').onclick = startPlay;

    // Resize the NaCl module to a minimum size so that it doesn't take too much
    // space
    var moduleEl = document.getElementById('nacl_module');
    moduleEl.setAttribute('width', 1);
    moduleEl.setAttribute('height', 1);
  });
}

function logDocumentSet(doc, func) {
  logBlock = doc.getElementById('log');
  logMessage = func;
}

function logMessageLimit(message) {
  logMessageArray.push(message.log);
  if (logMessageArray.length > kMaxLogMessageLength)
    logMessageArray.shift();

  if (logBlock) {
    logBlock.textContent = logMessageArray.join('\n');
  }
}

function logMessageAppend(message) {
  var span = document.createElement("span");
  span.innerText = message.log + '\n';

  if (message.level == 0) {
    span.className = "loginfo";
  } else if (message.level == 1) {
    span.className = "logwarning";
  } else if (message.level == 2) {
    span.className = "logerror";
  }

  logBlock.appendChild(span);
  logBlock.scrollTop = logBlock.scrollHeight;
}

function localLog(level, msg) {
  var logmsg = {
    level: level,
    log: msg
  };
  logMessage(logmsg);
}

function bitRateChanged() {
    var bitrate = document.getElementById('bitrate').value;
    var bitratevalue = document.getElementById('bitratevalue');
    bitratevalue.innerHTML = bitrate + " kbps";

    if (is_sharing_) { /* Change parameters on runtime */
      var options = getOptions();
      saveOptions(options);
      sharer_.changeEncoding(options);
    }
}

function fpsChanged() {
    var fps = document.getElementById('fps').value;
    var fpsvalue = document.getElementById('fpsvalue');
    fpsvalue.innerHTML = fps;
}

function bitRateChangedText() {
    var bitrate = document.getElementById('bitrate').value;
    var bitratevalue = document.getElementById('bitratevalue');
    bitratevalue.innerHTML = bitrate + " kbps";
}

function fpsChangedText() {
    var fps = document.getElementById('fps').value;
    var fpsvalue = document.getElementById('fpsvalue');
    fpsvalue.innerHTML = fps;
}


function localhostChanged() {
    var localhost = document.getElementById('localhost').checked;
    var ip = document.getElementById('ipinput');
    if (localhost) {
        ip.disabled = true;
        ip.value = '127.0.0.1';
    } else {
        ip.disabled = false;
    }
}

function startPlay() {
  nms.startPlayer().then(function() {
    document.getElementById('playStream').innerText = 'Stop stream';
    document.getElementById('playStream').onclick = stopStreamReceiver;
  });

  // Resize the NaCl module when stream playing is started
  var moduleEl = document.getElementById('nacl_module');
  moduleEl.setAttribute('width', 640);
  moduleEl.setAttribute('height', 340);
}

function detachLog() {
  chrome.runtime.getBackgroundPage(function(bg) {
    bg.createLogWindow(
      function(dom) {
        logDocumentSet(dom, logMessageAppend);
      },
      function() {
        logDocumentSet(document, logMessageLimit);
        console.log('INFO: log window closed.');
      }
    );
  });
}

function customlog(msg) {
  localLog(0, msg);
}

function checkVersion() {
  chrome.runtime.getPlatformInfo(function(platformInfo) {
    // If the app is running on ChromeOS, check if the browser version is at
    // least 46.0.2472.0
    if (platformInfo.os.toString() == "cros") {
      // The user agent will contain a string in the form of "Chrome/A.B.C.D",
      // where A, B, C, D are numbers
      var browserVersion = navigator.userAgent
                      .match(/Chrome\/([0-9]+)\.([0-9]+)\.([0-9]+)\.([0-9]+)/);

      var major = parseInt(browserVersion[1], 10);
      var minor = parseInt(browserVersion[2], 10);
      var build = parseInt(browserVersion[3], 10);

      if ((major < kMinMajor) ||
          (major == kMinMajor && minor < kMinMinor) ||
          (major == kMinMajor && minor == kMinMinor && build < kMinBuild)) {
        chrome.app.window.create('version_popup.html', {
          id: "version_popup",
          width: 300,
          height: 200
        });
      }
    }
  });
}

window.onload = function() {
    logDocumentSet(document, logMessageLimit);
}

document.addEventListener('DOMContentLoaded', function() {
  var loadpromise = nms.loadModule(window, document.body, customlog);

  loadpromise.then(function() {
    moduleDidLoad();
  }).catch(function(lastError) {
    customlog('Error when loading module: ' + lastError);
  });
});
