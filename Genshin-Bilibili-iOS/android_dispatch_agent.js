'use strict';
// Frida 16.x Java bridge. Observe only official dispatch; preserve calls and streams.
function scoped(url) {
  return /^https:\/\/(dispatchcnglobal\.yuanshen\.com|cngfdispatch\.yuanshen\.com|cnqddispatch\.yuanshen\.com)\/(query_region_list|query_cur_region)(?:\?|$)/.test(url);
}

function snapshot(connection) {
  try {
    const url = String(connection.getURL().toString());
    if (!scoped(url)) return null;
    const context = {url: url, method: String(connection.getRequestMethod()), headers: {}};
    try {
      const properties = connection.getRequestProperties();
      const keys = properties.keySet().iterator();
      while (keys.hasNext()) {
        const key = keys.next();
        const values = properties.get(key);
        if (values !== null && values.size() > 0) context.headers[String(key)] = String(values.get(0));
      }
    } catch (_) {
      context.headers_available = false;
    }
    return context;
  } catch (_) {
    return null;
  }
}

if (typeof Java === 'undefined' || !Java.available) {
  send({kind: 'diagnostic_state', state: 'java_bridge_unavailable'});
} else {
  Java.perform(function () {
    let hooks = 0;
    try {
      const Http = Java.use('com.android.okhttp.internal.huc.HttpURLConnectionImpl');
      ['getInputStream', 'getResponseCode'].forEach(function (name) {
        try {
          const original = Http[name].overload();
          original.implementation = function () {
            const context = snapshot(this);
            // Let the real request execute first. Never consume its response stream.
            const result = original.call(this);
            if (context !== null) {
              const event = {kind: 'dispatch_context', phase: name, context: context};
              if (name === 'getResponseCode') event.http_status = Number(result);
              send(event);
            }
            return result;
          };
          hooks++;
        } catch (_) {
          send({kind: 'diagnostic_state', state: 'hook_unavailable', method: name});
        }
      });
    } catch (_) {
      send({kind: 'diagnostic_state', state: 'http_class_unavailable'});
    }
    // A fallback shows URI construction, not a successful network exchange.
    try {
      const URLClass = Java.use('java.net.URL');
      const original = URLClass.openConnection.overload();
      original.implementation = function () {
        const result = original.call(this);
        try {
          const url = String(this.toString());
          if (scoped(url)) send({kind: 'dispatch_constructed', url: url});
        } catch (_) {}
        return result;
      };
      hooks++;
    } catch (_) {
      send({kind: 'diagnostic_state', state: 'url_hook_unavailable'});
    }
    send({kind: 'diagnostic_state', state: 'hooks_prepared', hook_count: hooks});
  });
}
