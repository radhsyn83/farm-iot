#include "ApiService.h"
#include <HTTPClient.h>
#include "../config.h"
#include "../helpers/Logger.h"

bool ApiService::postNotify(const String& title, const String& message) {
  String url = String(API_BASE_URL) + String(API_NOTIF_PATH);
  HTTPClient http;
  http.setTimeout(API_TIMEOUT_MS);
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  String payload = String("{\"title\":\"") + title + "\",\"message\":\"" + message + "\"}";
  int code = http.POST(payload);
  if (code > 0) {
    Logger::info("API notify code=%d", code);
    http.end();
    return code >= 200 && code < 300;
  } else {
    Logger::error("API error: %s", http.errorToString(code).c_str());
    http.end();
    return false;
  }
}
