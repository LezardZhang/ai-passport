#pragma once

// Local credentials are deliberately kept outside the tracked source tree.
#if defined(__has_include)
#  if __has_include("quota_config_local.h")
#    include "quota_config_local.h"
#  endif
#endif

#ifndef QUOTA_WIFI_SSID
#define QUOTA_WIFI_SSID ""
#endif
#ifndef QUOTA_WIFI_PASSWORD
#define QUOTA_WIFI_PASSWORD ""
#endif
#ifndef QUOTA_API_BASE_URL
#define QUOTA_API_BASE_URL ""
#endif
#ifndef QUOTA_API_USERNAME
#define QUOTA_API_USERNAME ""
#endif
#ifndef QUOTA_API_PASSWORD
#define QUOTA_API_PASSWORD ""
#endif
#ifndef QUOTA_ALLOW_INSECURE_HTTP
#define QUOTA_ALLOW_INSECURE_HTTP 0
#endif

#define QUOTA_MAX_ACCOUNTS 8
#define QUOTA_HTTP_BUFFER_SIZE 32768
