/* A natív teszt hamis HTTP/RTOS rétege nem igazolja az IDF fordítási kapcsolóit.
 * A valódi firmware nem fordulhat le hitelesítés vagy külső stack nélkül. */
#pragma once
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#if !defined(CONFIG_ESP_HTTP_CLIENT_ENABLE_BASIC_AUTH) || !defined(CONFIG_ESP_HTTP_CLIENT_ENABLE_DIGEST_AUTH)
#error "Intercom requires ESP HTTP client Basic and Digest authentication"
#endif
#if !defined(CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM) || !defined(CONFIG_SPIRAM_XIP_FROM_PSRAM)
#error "Intercom requires PSRAM task stacks and PSRAM XIP"
#endif
#endif
