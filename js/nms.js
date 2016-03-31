// Copyright 2015 Intel Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

"use strict";

var nms = (function() {

  var sharers = {};

  //////////////////////////////////////////////////////////////////////////
  // Async command list
  // The following methods take care of creating an ID for a command that is
  // being issued, and saving the respective resolve/reject methods in a
  // dictionary. Once a "success" or "failure", with a respective command ID,
  // is received, the resolve/reject method is called.
  var _cmdList = {};
  var _cmdCount = 0;

  function _cmdNew(resolve, reject) {
    var sharer = this;
    var cmd = {
      id: _cmdCount,
      resolve: resolve,
      reject: reject
    };

    _cmdCount += 1;
    _cmdList[cmd.id] = cmd;

    return cmd.id;
  }

  function _handleMessage(data) {
    if (typeof data.cmd_id === 'undefined') {
      common.logFunc("Error: can't handle message without cmd_id");
      return;
    }

    if (!_cmdList.hasOwnProperty(data.cmd_id)) {
      common.logFunc("Error: can't handle message id: " + data.cmd_id);
      return;
    }

    var cmd = _cmdList[data.cmd_id];
    if (data.success === true && (typeof cmd.resolve === 'function'))
      cmd.resolve(data.payload);
    else if (typeof cmd.reject === 'function')
      cmd.reject(data.payload);

    delete _cmdList[data.cmd_id];
  }

  function _postMessage(cmd, payload, resolve, reject) {
    var msg = {
      cmd: cmd,
      cmd_id: _cmdNew(resolve, reject),
      payload: payload
    };
    common.naclModule.postMessage(msg);
  }

  // End of async command list

  var SharerProto = {
    shareTracks: function(stream) {
      var sharer = this;
      return new Promise(function(resolve, reject) {
        if (sharer._stream !== null) {
          reject("Already sharing a stream.");
          return;
        }

        if ((typeof stream === 'undefined') || (stream === null)) {
          reject("Need a valid stream to share.");
          return;
        }

        if (!stream.active) {
          reject("Need an active stream to share.");
          return;
        }

        sharer._stream = stream;
        sharer._videoTrack = stream.getVideoTracks()[0];
        if (sharer._videoTrack === null) {
          sharer._stream = null;
          reject("Stream has no video tracks.");
          return;
        }

        sharer._videoTrack.addEventListener('ended', function() {
          if (sharer._stream !== null) {
            sharer._sendStop();
            sharer._stream = null;
            sharer._videoTrack = null;
          }

          while (sharer._onStoppedPromises.length > 0) {
            var p = sharer._onStoppedPromises.shift();
            if (typeof p === 'function')
              p();
          }
        });

        function _resolved(data) {
          resolve();
        }

        function _rejected(data) {
          reject();
        }

        var msg = {
          sharer_id: sharer.sharer_id,
          video_track: sharer._videoTrack
        };
        _postMessage('setSharerTracks', msg, _resolved, _rejected);
      });
    },

    stop: function() {
      var sharer = this;
      return new Promise(function(resolve, reject) {
        if (sharer._stream === null || sharer._videoTrack === null) {
          reject();
          return;
        }

        var videoTrack = sharer._videoTrack;
        sharer._stream = null;
        sharer._videoTrack = null;

        function _resolved(data) {
          var finished = new Promise(function(resolve2) {
            videoTrack.addEventListener('ended', resolve2);
          });
          resolve(finished);
          videoTrack.stop();
        }

        function _rejected(data) {
          reject();
        }

        sharer._sendStop(_resolved, _rejected);
      });
    },

    changeEncoding: function(options) {
      var sharer = this;
      return new Promise(function(resolve, reject) {
        function _resolved(data) {
          resolve();
        }

        function _rejected(data) {
          reject();
        }

        var payload = {
          sharer_id: sharer.sharer_id,
          bitrate: options.bitrate,
          fps: options.fps
        };

        _postMessage('changeEncoding', payload, _resolved, _rejected);
      });
    },

    _sendStop: function(resolve, reject) {
      var sharer = this;
      var msg = {
        sharer_id: sharer.sharer_id,
      };
      _postMessage('stopSharer', msg, resolve, reject);
    },

    _stream: null,
    _videoTrack: null,
    _onStoppedPromises: [],

    onStopped: function() {
      var sharer = this;
      return new Promise(function(resolve) {
        // If the sharer is already stopped, resolve immediately
        if (sharer._stream === null) {
          resolve();
          return;
        }

        sharer._onStoppedPromises.push(resolve);
      });
    }
  };

  function newSharer(sharer_id) {
    if (typeof sharer_id === 'undefined')
      return null;

    var sharer = Object.create(SharerProto);

    sharer.sharer_id = sharer_id;

    return sharer;
  }

  function loadModuleImpl(w, body, logFunc) {
    // The data-* attributes on the body can be referenced via body.dataset.
    if (body.dataset) {
      // From https://developer.mozilla.org/en-US/docs/DOM/window.location
      var searchVars = {};
      if (w.location.search.length > 1) {
        var pairs = w.location.search.substr(1).split('&');
        for (var key_ix = 0; key_ix < pairs.length; key_ix++) {
          var keyValue = pairs[key_ix].split('=');
          searchVars[unescape(keyValue[0])] =
            keyValue.length > 1 ? unescape(keyValue[1]) : '';
        }
      }

      var toolchains = body.dataset.tools.split(' ');
      var configs = body.dataset.configs.split(' ');

      var attrs = {};
      if (body.dataset.attrs) {
        var attr_list = body.dataset.attrs.split(' ');
        for (var key in attr_list) {
          var attr = attr_list[key].split('=');
          var key = attr[0];
          var value = attr[1];
          attrs[key] = value;
        }
      }

      var tc = toolchains.indexOf(searchVars.tc) !== -1 ?
        searchVars.tc : toolchains[0];

      // If the config value is included in the search vars, use that.
      // Otherwise default to Release if it is valid, or the first value if
      // Release is not valid.
      if (configs.indexOf(searchVars.config) !== -1)
        var config = searchVars.config;
      else if (configs.indexOf('Release') !== -1)
        var config = 'Release';
      else
        var config = configs[0];

      var pathFormat = body.dataset.path;
      var path = pathFormat.replace('{tc}', tc).replace('{config}', config);

      isTest = searchVars.test === 'true';
      isRelease = path.toLowerCase().indexOf('release') != -1;

      common.logFunc = logFunc;

      loadFunction(body.dataset.name, tc, path,
                   body.dataset.width,
                   body.dataset.height, attrs);
    }
  }

  function loadModule(w, body, logFunc) {
    return new Promise(function(resolve, reject) {
      loadModuleImpl(w, body, logFunc);

      common.customModuleDidLoad = function() {
        resolve();
      };

      common.customModuleError = function(lastError) {
        reject(lastError);
      }
    });
  }

  function loadFunction(name, tool, path, width, height, attrs) {
    // If the page loads before the Native Client module loads, then set the
    // status message indicating that the module is still loading.  Otherwise,
    // do not change the status message.
    common.updateStatus('Page loaded.');
    if (common.naclModule == null) {
      common.updateStatus('Creating embed: ' + tool);

      // We use a non-zero sized embed to give Chrome space to place the bad
      // plug-in graphic, if there is a problem.
      width = typeof width !== 'undefined' ? width : 200;
      height = typeof height !== 'undefined' ? height : 200;
      common.attachDefaultListeners();
      common.createNaClModule(name, tool, path, width, height, attrs);
      common.customHandleMessage = handleMessage;
    } else {
      // It's possible that the Native Client module onload event fired
      // before the page's onload event.  In this case, the status message
      // will reflect 'SUCCESS', but won't be displayed.  This call will
      // display the current message.
      common.updateStatus('Waiting.');
    }
  }

  function handleMessage(message) {
    if ('log' in message.data) {
      console.log(message);
      common.logFunc(message.data.log);
      return;
    }

    _handleMessage(message.data);
  }

  function createSharer(options) {
    return new Promise(function(resolve, reject) {
      function _resolved(data) {
        var sharer_id = data.sharer_id;
        var sharer = newSharer(sharer_id);
        resolve(sharer);
      }

      function _rejected(data) {
        reject("Failed to initialize sharer");
      }

      var msg = {
        ip: options.ip,
        bitrate: options.bitrate,
        fps: options.fps,
        port: options.port,
      };

      console.log(msg);
      _postMessage('startSharer', msg, _resolved, _rejected);
    });
  }

  function startPlayer(options) {
    return new Promise(function(resolve, reject) {
      function _resolved(data) {
        resolve();
      }

      function _rejected(data) {
        reject();
      }

      var msg = {
        ip: options.ip,
        port: options.port,
      };

      _postMessage('startUDP', msg, _resolved, _rejected);
    });
  }

  function stopPlayer() {
    return new Promise(function(resolve, reject) {
      function _resolved(data) {
        resolve();
      }

      function _rejected(data) {
        reject();
      }

      _postMessage('stopReceiver', null, _resolved, _rejected);
    });
  }

  // The symbols to export
  return {
    loadModule: loadModule,
    createSharer: createSharer,
    startPlayer: startPlayer,
    stopPlayer: stopPlayer
  };

}());
