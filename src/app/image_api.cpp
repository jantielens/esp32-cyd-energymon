/*
 * Image API Implementation
 * 
 * REST API handlers for uploading and displaying JPEG images.
 * Uses backend adapter pattern for portability across projects.
 */

#include "board_config.h"

#if HAS_IMAGE_API

#include "image_api.h"
#include "jpeg_preflight.h"
#include "log_manager.h"
#include "device_telemetry.h"

#if HAS_DISPLAY
#include "display_manager.h"
#endif

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

#include "lvgl_jpeg_decoder.h"

#include <esp_heap_caps.h>
#include <soc/soc_caps.h>

static void* image_api_alloc(size_t size) {
    if (size == 0) return nullptr;

#if SOC_SPIRAM_SUPPORTED
    // Prefer PSRAM to reduce internal heap pressure when available.
    void* p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (p) return p;
#endif

    // Fallback: any 8-bit heap.
    // On some no-PSRAM boards, using INTERNAL|8BIT can exclude viable 8-bit regions.
    // Using 8BIT matches ESP.getFreeHeap() behavior and can reduce pressure on the
    // internal heap reserved for decode-time allocations.
    return heap_caps_malloc(size, MALLOC_CAP_8BIT);
}

static void image_api_free(void* p) {
    if (!p) return;
    heap_caps_free(p);
}

static size_t image_api_no_psram_effective_headroom_bytes(size_t base_headroom, size_t free_heap, size_t largest_block) {
    // On no-PSRAM boards, a fixed headroom is often either too strict (false rejects) or too lax.
    // Use a simple fragmentation-based adaptation:
    // - When fragmentation is low (largest block is a good fraction of free heap), allow a smaller headroom.
    // - When fragmentation is higher, fall back to the configured headroom.
    // Keep a minimum safety floor.
    const size_t min_headroom = 24 * 1024;
    size_t headroom = base_headroom;

    size_t frag_pct = 100;
    if (free_heap > 0 && largest_block <= free_heap) {
        frag_pct = 100 - (largest_block * 100) / free_heap;
    }

    if (frag_pct <= 45) {
        headroom = min(headroom, (size_t)(32 * 1024));
    } else if (frag_pct <= 60) {
        headroom = min(headroom, (size_t)(40 * 1024));
    }

    headroom = max(headroom, min_headroom);
    return headroom;
}

// ===== Constants =====

// Note: AsyncWebServer callbacks run on the AsyncTCP task. Do not block
// (e.g., with delay()/busy waits). If we're busy, return 409 and let the
// client retry.

// ===== Internal state =====

static ImageApiConfig g_cfg;
static ImageApiBackend g_backend = {nullptr, nullptr, nullptr};

// Image upload buffer (allocated temporarily during upload)
static uint8_t* image_upload_buffer = nullptr;
static size_t image_upload_size = 0;
static unsigned long image_upload_timeout_ms = 10000;

// Upload state tracking
enum UploadState {
    UPLOAD_IDLE = 0,
    UPLOAD_IN_PROGRESS,
    UPLOAD_READY_TO_DISPLAY
};
static volatile UploadState upload_state = UPLOAD_IDLE;
static volatile unsigned long pending_op_id = 0;  // Incremented when new op is queued

// Pending image display operation (processed by main loop)
struct PendingImageOp {
    uint8_t* buffer;
    size_t size;
    bool dismiss;  // true = dismiss current image, false = show new image
    unsigned long timeout_ms;  // Display timeout in milliseconds
    unsigned long start_time;  // Time when upload completed (for accurate timeout)
};
static PendingImageOp pending_image_op = {nullptr, 0, false, 10000, 0};

// Strip upload state (buffering during HTTP upload)
static uint8_t* current_strip_buffer = nullptr;
static size_t current_strip_size = 0;

// Strip processing state (async decode queue)
struct PendingStripOp {
    uint8_t* buffer;
    size_t size;
    uint8_t strip_index;
    int image_width;
    int image_height;
    int total_strips;
    unsigned long timeout_ms;
    unsigned long start_time;
};
static PendingStripOp pending_strip_op = {nullptr, 0, 0, 0, 0, 0, 10000, 0};

// URL download state (queued by HTTP handler, executed in main loop)
static constexpr size_t IMAGE_API_URL_MAX_LEN = 256;
struct PendingUrlOp {
    bool active;
    char url[IMAGE_API_URL_MAX_LEN];
    unsigned long timeout_ms;
};
static PendingUrlOp pending_url_op = {false, {0}, 0};

// AsyncWebServer handlers run on the AsyncTCP task; the main loop consumes this op.
// Protect cross-task publication/consumption so we don't read stale url/timeout_ms.
static portMUX_TYPE pending_url_op_mux = portMUX_INITIALIZER_UNLOCKED;

// Small body buffer for /api/display/image_url to avoid heap allocation.
// AsyncWebServer body callbacks can be interrupted by client disconnects; keeping this static
// prevents leaks. We also add a timeout so a stalled upload doesn't block future requests.
static bool image_url_body_in_use = false;
static size_t image_url_body_expected_len = 0;
static unsigned long image_url_body_start_ms = 0;
static constexpr size_t IMAGE_URL_BODY_MAX_SIZE = 1024;
static char image_url_body_buf[IMAGE_URL_BODY_MAX_SIZE + 1];

// Track partial uploads so we can reclaim memory if the client disconnects mid-transfer.
static unsigned long image_upload_start_ms = 0;
static unsigned long strip_upload_last_activity_ms = 0;

static bool is_jpeg_magic(const uint8_t* buf, size_t sz) {
    return (buf && sz >= 3 && buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF);
}

static unsigned long parse_timeout_ms(AsyncWebServerRequest* request) {
    // Parse optional timeout parameter from query string (e.g., ?timeout=30)
    unsigned long timeout_seconds = g_cfg.default_timeout_ms / 1000;
    if (request->hasParam("timeout")) {
        String timeout_str = request->getParam("timeout")->value();
        timeout_seconds = (unsigned long)timeout_str.toInt();
        // Clamp to prevent overflow and respect max timeout
        unsigned long max_seconds = g_cfg.max_timeout_ms / 1000;
        if (timeout_seconds > max_seconds) timeout_seconds = max_seconds;
    }
    return timeout_seconds * 1000UL;
}

static bool starts_with_ignore_case(const char* s, const char* prefix) {
    if (!s || !prefix) return false;
    while (*prefix) {
        const char a = *s;
        const char b = *prefix;
        if (!a) return false;
        const char al = (a >= 'A' && a <= 'Z') ? (char)(a - 'A' + 'a') : a;
        const char bl = (b >= 'A' && b <= 'Z') ? (char)(b - 'A' + 'a') : b;
        if (al != bl) return false;
        s++;
        prefix++;
    }
    return true;
}

static bool equals_ignore_case_n(const char* a, const char* b, size_t n) {
    if (!a || !b) return false;
    for (size_t i = 0; i < n; i++) {
        const char ca = a[i];
        const char cb = b[i];
        if (!ca || !cb) return false;
        const char al = (ca >= 'A' && ca <= 'Z') ? (char)(ca - 'A' + 'a') : ca;
        const char bl = (cb >= 'A' && cb <= 'Z') ? (char)(cb - 'A' + 'a') : cb;
        if (al != bl) return false;
    }
    return true;
}

enum UrlScheme {
    URL_SCHEME_HTTP = 0,
    URL_SCHEME_HTTPS = 1,
};

// Minimal URL parser: http(s)://host[:port]/path
static bool parse_http_or_https_url(
    const char* url,
    UrlScheme* scheme_out,
    char* host_out,
    size_t host_out_len,
    uint16_t* port_out,
    char* path_out,
    size_t path_out_len
) {
    if (!url || !scheme_out || !host_out || !port_out || !path_out) return false;

    UrlScheme scheme;
    size_t scheme_len = 0;
    uint16_t default_port = 0;

    if (starts_with_ignore_case(url, "https://")) {
        scheme = URL_SCHEME_HTTPS;
        scheme_len = 8;
        default_port = 443;
    } else if (starts_with_ignore_case(url, "http://")) {
        scheme = URL_SCHEME_HTTP;
        scheme_len = 7;
        default_port = 80;
    } else {
        return false;
    }

    const char* p = url + scheme_len; // after scheme://
    const char* slash = strchr(p, '/');
    const char* host_end = slash ? slash : (p + strlen(p));
    if (host_end <= p) return false;

    // Copy host[:port]
    const size_t hostport_len = (size_t)(host_end - p);
    if (hostport_len >= host_out_len) return false;
    char hostport[IMAGE_API_URL_MAX_LEN];
    memcpy(hostport, p, hostport_len);
    hostport[hostport_len] = '\0';

    // Split host and optional port
    const char* colon = strchr(hostport, ':');
    uint16_t port = default_port;
    if (colon) {
        const size_t host_len = (size_t)(colon - hostport);
        if (host_len == 0 || host_len >= host_out_len) return false;
        memcpy(host_out, hostport, host_len);
        host_out[host_len] = '\0';
        long parsed_port = strtol(colon + 1, nullptr, 10);
        if (parsed_port <= 0 || parsed_port > 65535) return false;
        port = (uint16_t)parsed_port;
    } else {
        if (strlen(hostport) >= host_out_len) return false;
        strncpy(host_out, hostport, host_out_len);
        host_out[host_out_len - 1] = '\0';
    }

    const char* path = slash ? slash : "/";
    if (strlen(path) >= path_out_len) return false;
    strncpy(path_out, path, path_out_len);
    path_out[path_out_len - 1] = '\0';

    *port_out = port;
    *scheme_out = scheme;
    return true;
}

static bool download_jpeg_to_buffer(
    const char* url,
    unsigned long timeout_ms,
    uint8_t** out_buf,
    size_t* out_sz,
    char* err,
    size_t err_len
) {
    if (!out_buf || !out_sz) return false;
    *out_buf = nullptr;
    *out_sz = 0;

    if (!url || strlen(url) == 0) {
        snprintf(err, err_len, "Missing URL");
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        snprintf(err, err_len, "WiFi not connected");
        return false;
    }

    UrlScheme scheme = URL_SCHEME_HTTPS;
    char host[128];
    char path[256];
    uint16_t port = 0;
    if (!parse_http_or_https_url(url, &scheme, host, sizeof(host), &port, path, sizeof(path))) {
        snprintf(err, err_len, "Invalid URL (must be http:// or https://)");
        return false;
    }

    // Keep a decode headroom guard on PSRAM boards (TLS + decode need internal heap).
#if SOC_SPIRAM_SUPPORTED
    if (psramFound()) {
        if (scheme == URL_SCHEME_HTTPS) {
            const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (internal_free < g_cfg.decode_headroom_bytes) {
                snprintf(err, err_len, "Insufficient internal heap for TLS/decode headroom");
                return false;
            }
        }
    }
#endif

    // Conservative per-operation timeout.
    // Note: `millis()` wraps; use wrap-safe elapsed checks.
    const unsigned long start_ms = millis();
    unsigned long effective_timeout_ms = timeout_ms;
    if (effective_timeout_ms == 0) effective_timeout_ms = 15000UL;
    // Clamp to avoid stalling the main loop for too long.
    if (effective_timeout_ms > 30000UL) effective_timeout_ms = 30000UL;

    const auto timed_out = [&]() -> bool {
        return (unsigned long)(millis() - start_ms) >= effective_timeout_ms;
    };

    WiFiClientSecure client_tls;
    WiFiClient client_plain;
    Client* client = nullptr;
    if (scheme == URL_SCHEME_HTTPS) {
        // SECURITY NOTE:
        // We intentionally use insecure TLS mode for now (no certificate validation)
        // to allow downloads from any host without shipping a CA bundle.
        // This protects against passive eavesdropping but NOT active MITM attacks.
        static bool warned_insecure_tls = false;
        if (!warned_insecure_tls) {
            warned_insecure_tls = true;
            Logger.logMessage(
                "ImageApi",
                "WARNING: HTTPS image_url uses insecure TLS (no certificate validation). A MITM can spoof content. Use only on trusted networks, or implement CA verification/pinning."
            );
        }
        client_tls.setInsecure();
        client = &client_tls;
    } else {
        client = &client_plain;
    }

    if (!client->connect(host, port)) {
        snprintf(err, err_len, "%s connect failed", scheme == URL_SCHEME_HTTPS ? "TLS" : "TCP");
        return false;
    }

    client->printf("GET %s HTTP/1.1\r\n", path);
    client->printf("Host: %s\r\n", host);
    client->print("User-Agent: esp32-template-image-api/1.0\r\n");
    client->print("Accept: image/jpeg, */*\r\n");
    client->print("Connection: close\r\n\r\n");

    // Read status + headers line-by-line to reduce stack usage.
    // (Avoids buffering the entire header block.)
    auto read_http_line = [&](char* out, size_t out_len) -> bool {
        if (!out || out_len == 0) return false;
        size_t n = 0;
        while (!timed_out()) {
            int b = client->read();
            if (b < 0) {
                yield();
                continue;
            }

            if (b == '\r') {
                continue;
            }

            if (b == '\n') {
                out[n] = '\0';
                return true;
            }

            if (n + 1 >= out_len) {
                snprintf(err, err_len, "HTTP header line too long");
                return false;
            }

            out[n++] = (char)b;
        }

        snprintf(err, err_len, "Timeout waiting for headers");
        return false;
    };

    char line[256];
    int status = 0;
    if (!read_http_line(line, sizeof(line))) {
        return false;
    }
    if (line[0] == '\0') {
        snprintf(err, err_len, "Invalid HTTP response");
        return false;
    }
    // Example: HTTP/1.1 200 OK
    if (sscanf(line, "HTTP/%*s %d", &status) != 1) {
        snprintf(err, err_len, "Failed to parse HTTP status");
        return false;
    }

    bool chunked = false;
    size_t content_length = 0;
    while (true) {
        if (!read_http_line(line, sizeof(line))) {
            return false;
        }

        // Empty line = end of headers
        if (line[0] == '\0') {
            break;
        }

        if (starts_with_ignore_case(line, "Content-Length:")) {
            const char* v = line + strlen("Content-Length:");
            while (*v == ' ' || *v == '\t') v++;
            content_length = (size_t)strtoul(v, nullptr, 10);
        } else if (starts_with_ignore_case(line, "Transfer-Encoding:")) {
            // If chunked, we bail for now (keeps implementation small + memory-predictable).
            const char* v = line + strlen("Transfer-Encoding:");
            while (*v == ' ' || *v == '\t') v++;

            // Parse comma-separated transfer-coding tokens.
            // We only match a standalone token "chunked" (case-insensitive) to avoid false positives
            // like "not-chunked".
            const char* p = v;
            while (*p && !chunked) {
                while (*p == ' ' || *p == '\t' || *p == ',') p++;
                if (!*p) break;

                const char* token_start = p;
                while (*p && *p != ',' && *p != ';' && *p != ' ' && *p != '\t') p++;
                const size_t len = (size_t)(p - token_start);

                if (len == 7 && equals_ignore_case_n(token_start, "chunked", 7)) {
                    chunked = true;
                    break;
                }

                while (*p && *p != ',') p++;
            }
        }
    }

    if (status != 200) {
        snprintf(err, err_len, "HTTP status %d", status);
        return false;
    }
    if (chunked) {
        snprintf(err, err_len, "Chunked transfer unsupported");
        return false;
    }
    if (content_length == 0) {
        snprintf(err, err_len, "Missing Content-Length");
        return false;
    }
    if (content_length > g_cfg.max_image_size_bytes) {
        snprintf(err, err_len, "Image too large (%u bytes)", (unsigned)content_length);
        return false;
    }

    uint8_t* buf = (uint8_t*)image_api_alloc(content_length);
    if (!buf) {
        snprintf(err, err_len, "Out of memory allocating %u bytes", (unsigned)content_length);
        return false;
    }

    size_t pos = 0;
    while (pos < content_length && !timed_out()) {
        const int r = client->read(buf + pos, (int)min((size_t)1024, content_length - pos));
        if (r > 0) {
            pos += (size_t)r;
            continue;
        }
        delay(1);
    }

    if (pos != content_length) {
        image_api_free(buf);
        snprintf(err, err_len, "Incomplete body (%u/%u)", (unsigned)pos, (unsigned)content_length);
        return false;
    }

    if (!is_jpeg_magic(buf, content_length)) {
        image_api_free(buf);
        snprintf(err, err_len, "Downloaded data is not a JPEG");
        return false;
    }

    *out_buf = buf;
    *out_sz = content_length;
    return true;
}

// ===== Handlers =====

// POST /api/display/image - Upload and display JPEG image (deferred decode)
static void handleImageUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    (void)filename;

    // First chunk - initialize upload
    if (index == 0) {
        // If upload already in progress OR pending display, reject (client can retry)
        if (upload_state == UPLOAD_IN_PROGRESS || upload_state == UPLOAD_READY_TO_DISPLAY) {
            request->send(409, "application/json", "{\"success\":false,\"message\":\"Upload busy\"}");
            return;
        }

        Logger.logBegin("Image Upload");
        Logger.logLinef("Total size: %u bytes", request->contentLength());

        image_upload_timeout_ms = parse_timeout_ms(request);
        image_upload_start_ms = millis();
        Logger.logLinef("Timeout: %lu ms", image_upload_timeout_ms);

        device_telemetry_log_memory_snapshot("img pre-clear");

        // Free any pending image buffer to make room for new upload
        if (pending_image_op.buffer) {
            Logger.logMessage("Upload", "Freeing pending image buffer");
            image_api_free((void*)pending_image_op.buffer);
            pending_image_op.buffer = nullptr;
            pending_image_op.size = 0;
        }

        device_telemetry_log_memory_snapshot("img post-clear");

        // Check file size
        size_t total_size = request->contentLength();
        if (total_size > g_cfg.max_image_size_bytes) {
            Logger.logEnd("ERROR: Image too large");
            request->send(400, "application/json", "{\"success\":false,\"message\":\"Image too large\"}");
            return;
        }

        // Check memory availability.
        // - Upload uses a single contiguous buffer.
        // - The decode pipeline needs headroom (historically expressed via g_cfg.decode_headroom_bytes).

#if SOC_SPIRAM_SUPPORTED
        // `SOC_SPIRAM_SUPPORTED` means the SoC can use PSRAM, but some boards have no PSRAM fitted.
        // Use a runtime check so no-PSRAM boards don't take PSRAM-specific headroom gating.
        const bool has_psram = psramFound();
        const size_t psram_free = has_psram ? heap_caps_get_free_size(MALLOC_CAP_SPIRAM) : 0;
        const size_t psram_largest = has_psram ? heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) : 0;
        const bool psram_can_hold_upload = has_psram && (psram_free >= total_size) && (psram_largest >= total_size);
#else
        const size_t psram_free = 0;
        const size_t psram_largest = 0;
        const bool psram_can_hold_upload = false;
#endif

        // PSRAM boards vs no-PSRAM boards behave very differently.
        // - On PSRAM boards: upload buffer is expected to land in PSRAM; keep an internal headroom guard.
        // - On no-PSRAM boards: keep the historical "total heap" guard (ESP.getFreeHeap) so we don't
        //   falsely reject when internal-only metrics look tight but the system can still operate.
#if SOC_SPIRAM_SUPPORTED
        if (has_psram) {
            const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (internal_free < g_cfg.decode_headroom_bytes) {
            Logger.logLinef(
                "ERROR: Insufficient internal memory for decode headroom (need %u, have %u)",
                (unsigned)g_cfg.decode_headroom_bytes,
                (unsigned)internal_free
            );
            device_telemetry_log_memory_snapshot("img insufficient");
            char error_msg[240];
            snprintf(
                error_msg,
                sizeof(error_msg),
                "{\"success\":false,\"message\":\"Insufficient internal memory: need %uKB decode headroom, have %uKB.\"}",
                (unsigned)(g_cfg.decode_headroom_bytes / 1024),
                (unsigned)(internal_free / 1024)
            );
            Logger.logEnd();
            request->send(507, "application/json", error_msg);
            return;
            }

            if (!psram_can_hold_upload) {
            // We'll fall back to non-PSRAM allocation; be conservative.
            const size_t heap8_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
            const size_t heap8_largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
            const size_t required_heap8 = total_size + g_cfg.decode_headroom_bytes;
            if (heap8_free < required_heap8 || heap8_largest < total_size) {
                Logger.logLinef(
                    "ERROR: Insufficient memory (need %u heap8, have %u; largest %u; internal_free %u; psram_free %u largest %u)",
                    (unsigned)required_heap8,
                    (unsigned)heap8_free,
                    (unsigned)heap8_largest,
                    (unsigned)internal_free,
                    (unsigned)psram_free,
                    (unsigned)psram_largest
                );
                device_telemetry_log_memory_snapshot("img insufficient");
                char error_msg[320];
                snprintf(
                    error_msg,
                    sizeof(error_msg),
                    "{\"success\":false,\"message\":\"Insufficient memory: need %uKB total heap, have %uKB (largest block %uKB).\"}",
                    (unsigned)(required_heap8 / 1024),
                    (unsigned)(heap8_free / 1024),
                    (unsigned)(heap8_largest / 1024)
                );
                Logger.logEnd();
                request->send(507, "application/json", error_msg);
                return;
            }
            }
        } else {
            // No PSRAM fitted: total heap + largest block check (prevents immediate alloc failure).
            const size_t free_heap = ESP.getFreeHeap();
            const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
            const size_t headroom = image_api_no_psram_effective_headroom_bytes(g_cfg.decode_headroom_bytes, free_heap, largest);
            const size_t required = total_size + headroom;
            if (free_heap < required || largest < total_size) {
                Logger.logLinef(
                    "ERROR: Insufficient memory (need %u heap, have %u; largest %u)",
                    (unsigned)required,
                    (unsigned)free_heap,
                    (unsigned)largest
                );
                device_telemetry_log_memory_snapshot("img insufficient");
                char error_msg[256];
                snprintf(
                    error_msg,
                    sizeof(error_msg),
                    "{\"success\":false,\"message\":\"Insufficient memory: need %uKB, have %uKB (largest block %uKB).\"}",
                    (unsigned)(required / 1024),
                    (unsigned)(free_heap / 1024),
                    (unsigned)(largest / 1024)
                );
                Logger.logEnd();
                request->send(507, "application/json", error_msg);
                return;
            }
        }
#else
        // No PSRAM: total heap + largest block check (prevents immediate alloc failure).
        const size_t free_heap = ESP.getFreeHeap();
        const size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
        const size_t headroom = image_api_no_psram_effective_headroom_bytes(g_cfg.decode_headroom_bytes, free_heap, largest);
        const size_t required = total_size + headroom;
        if (free_heap < required || largest < total_size) {
            Logger.logLinef(
                "ERROR: Insufficient memory (need %u heap, have %u; largest %u)",
                (unsigned)required,
                (unsigned)free_heap,
                (unsigned)largest
            );
            device_telemetry_log_memory_snapshot("img insufficient");
            char error_msg[256];
            snprintf(
                error_msg,
                sizeof(error_msg),
                "{\"success\":false,\"message\":\"Insufficient memory: need %uKB, have %uKB (largest block %uKB).\"}",
                (unsigned)(required / 1024),
                (unsigned)(free_heap / 1024),
                (unsigned)(largest / 1024)
            );
            Logger.logEnd();
            request->send(507, "application/json", error_msg);
            return;
        }
#endif

        // Allocate buffer
        device_telemetry_log_memory_snapshot("img pre-alloc");
        image_upload_buffer = (uint8_t*)image_api_alloc(total_size);
        if (!image_upload_buffer) {
            Logger.logEnd("ERROR: Memory allocation failed");
            device_telemetry_log_memory_snapshot("img alloc-fail");
            request->send(507, "application/json", "{\"success\":false,\"message\":\"Memory allocation failed\"}");
            return;
        }
        device_telemetry_log_memory_snapshot("img post-alloc");

        image_upload_size = 0;
        upload_state = UPLOAD_IN_PROGRESS;
    }

    // Receive data chunks
    if (len && image_upload_buffer && upload_state == UPLOAD_IN_PROGRESS) {
        memcpy(image_upload_buffer + image_upload_size, data, len);
        image_upload_size += len;

        // Log progress every 10KB
        static size_t last_logged_size = 0;
        if (image_upload_size - last_logged_size >= 10240) {
            Logger.logLinef("Received: %u bytes", image_upload_size);
            last_logged_size = image_upload_size;
        }
    }

    // Final chunk - validate and queue for display
    if (final) {
        if (image_upload_buffer && image_upload_size > 0 && upload_state == UPLOAD_IN_PROGRESS) {
            Logger.logLinef("Upload complete: %u bytes", image_upload_size);

            if (!is_jpeg_magic(image_upload_buffer, image_upload_size)) {
                Logger.logLinef("Invalid header: %02X %02X %02X %02X",
                                image_upload_buffer[0], image_upload_buffer[1],
                                image_upload_buffer[2], image_upload_buffer[3]);
                Logger.logEnd("ERROR: Not a valid JPEG file");
                image_api_free(image_upload_buffer);
                image_upload_buffer = nullptr;
                image_upload_size = 0;
                upload_state = UPLOAD_IDLE;
                request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JPEG file\"}");
                return;
            }

            // Best-effort header preflight so we can return a descriptive 400 before queuing
            char preflight_err[160];
            if (!jpeg_preflight_tjpgd_supported(
                    image_upload_buffer,
                    image_upload_size,
                    g_cfg.lcd_width,
                    g_cfg.lcd_height,
                    preflight_err,
                    sizeof(preflight_err))) {
                Logger.logLinef("ERROR: JPEG preflight failed: %s", preflight_err);
                Logger.logEnd();
                image_api_free(image_upload_buffer);
                image_upload_buffer = nullptr;
                image_upload_size = 0;
                upload_state = UPLOAD_IDLE;

                char resp[256];
                snprintf(resp, sizeof(resp), "{\"success\":false,\"message\":\"%s\"}", preflight_err);
                request->send(400, "application/json", resp);
                return;
            }

            // Queue image for display by main loop (deferred operation)
            if (pending_image_op.buffer) {
                Logger.logMessage("Upload", "Replacing pending image");
                image_api_free((void*)pending_image_op.buffer);
            }

            pending_image_op.buffer = image_upload_buffer;
            pending_image_op.size = image_upload_size;
            pending_image_op.dismiss = false;
            pending_image_op.timeout_ms = image_upload_timeout_ms;
            pending_image_op.start_time = millis();
            pending_op_id++;
            upload_state = UPLOAD_READY_TO_DISPLAY;

            // main loop owns the buffer now
            image_upload_buffer = nullptr;
            image_upload_size = 0;

            Logger.logEnd("Image queued for display");

            char response_msg[160];
            snprintf(response_msg, sizeof(response_msg),
                     "{\"success\":true,\"message\":\"Image queued for display (%lus timeout)\"}",
                     (unsigned long)(image_upload_timeout_ms / 1000));
            request->send(200, "application/json", response_msg);
        } else {
            Logger.logEnd("ERROR: No data received");
            upload_state = UPLOAD_IDLE;
            request->send(400, "application/json", "{\"success\":false,\"message\":\"No data received\"}");
        }
    }
}

// DELETE /api/display/image - Manually dismiss image
static void handleImageDelete(AsyncWebServerRequest *request) {
    Logger.logMessage("Portal", "Image dismiss requested");

    if (pending_image_op.buffer) {
        image_api_free((void*)pending_image_op.buffer);
    }
    pending_image_op.buffer = nullptr;
    pending_image_op.size = 0;
    pending_image_op.dismiss = true;
    upload_state = UPLOAD_READY_TO_DISPLAY;
    pending_op_id++;

    request->send(200, "application/json", "{\"success\":true,\"message\":\"Image dismiss queued\"}");
}

// POST /api/display/image_url - Queue HTTP(S) JPEG download for display
// Body: {"url":"https://example.com/image.jpg"}
static void handleImageUrl(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    // Only accept small JSON payloads.
    if (index == 0) {
        bool url_op_active = false;
        portENTER_CRITICAL(&pending_url_op_mux);
        url_op_active = pending_url_op.active;
        portEXIT_CRITICAL(&pending_url_op_mux);

        if (upload_state == UPLOAD_IN_PROGRESS || upload_state == UPLOAD_READY_TO_DISPLAY || url_op_active || pending_strip_op.buffer) {
            request->send(409, "application/json", "{\"success\":false,\"message\":\"Busy\"}");
            return;
        }
        if (total == 0 || total > IMAGE_URL_BODY_MAX_SIZE) {
            request->send(413, "application/json", "{\"success\":false,\"message\":\"Body too large\"}");
            return;
        }
        // If a previous request stalled (e.g., disconnect mid-body), reclaim after a short timeout.
        if (image_url_body_in_use) {
            const unsigned long elapsed = (unsigned long)(millis() - image_url_body_start_ms);
            if (elapsed > 3000UL) {
                image_url_body_in_use = false;
                image_url_body_expected_len = 0;
                image_url_body_buf[0] = '\0';
            } else {
                request->send(409, "application/json", "{\"success\":false,\"message\":\"Busy\"}");
                return;
            }
        }

        image_url_body_in_use = true;
        image_url_body_expected_len = total;
        image_url_body_start_ms = millis();
        image_url_body_buf[0] = '\0';
    }

    if (!image_url_body_in_use || total == 0 || total != image_url_body_expected_len) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid body state\"}");
        return;
    }

    // Never accept more bytes than the buffer's data capacity (excluding the null terminator).
    if (index + len > IMAGE_URL_BODY_MAX_SIZE) {
        image_url_body_in_use = false;
        image_url_body_expected_len = 0;
        request->send(413, "application/json", "{\"success\":false,\"message\":\"Body too large\"}");
        return;
    }

    memcpy(image_url_body_buf + index, data, len);
    if (index + len >= total) {
        image_url_body_buf[total] = '\0';

        StaticJsonDocument<512> doc;
        const DeserializationError jerr = deserializeJson(doc, image_url_body_buf);
        image_url_body_in_use = false;
        image_url_body_expected_len = 0;

        if (jerr) {
            request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
            return;
        }

        const char* url = doc["url"] | "";
        if (!url || strlen(url) == 0) {
            request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing url\"}");
            return;
        }
        if (strlen(url) >= IMAGE_API_URL_MAX_LEN) {
            request->send(400, "application/json", "{\"success\":false,\"message\":\"URL too long\"}");
            return;
        }

        // Free any pending image buffer to make room.
        if (pending_image_op.buffer) {
            image_api_free((void*)pending_image_op.buffer);
            pending_image_op.buffer = nullptr;
            pending_image_op.size = 0;
        }

        // Publish the URL op: fill fields first, then flip `active` last.
        // This is shared between the AsyncTCP task and the main loop.
        portENTER_CRITICAL(&pending_url_op_mux);
        strncpy(pending_url_op.url, url, sizeof(pending_url_op.url));
        pending_url_op.url[sizeof(pending_url_op.url) - 1] = '\0';
        pending_url_op.timeout_ms = parse_timeout_ms(request);
        pending_url_op.active = true;
        portEXIT_CRITICAL(&pending_url_op_mux);

        upload_state = UPLOAD_READY_TO_DISPLAY;
        pending_op_id++;

        request->send(200, "application/json", "{\"success\":true,\"message\":\"Image URL queued\"}");
    }
}

// POST /api/display/image/strips?strip_index=N&strip_count=T&width=W&height=H[&timeout=seconds]
// Upload a single JPEG strip; decode is deferred to the main loop.
static void handleStripUpload(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    // Validate required params
    if (index == 0) {
        const bool has_required =
            request->hasParam("strip_index", false) &&
            request->hasParam("strip_count", false) &&
            request->hasParam("width", false) &&
            request->hasParam("height", false);

        if (!has_required) {
            request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing required parameters: strip_index, strip_count, width, height\"}");
            return;
        }
    }

    if (!request->hasParam("strip_index", false) || !request->hasParam("strip_count", false) ||
        !request->hasParam("width", false) || !request->hasParam("height", false)) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing required parameters: strip_index, strip_count, width, height\"}");
        return;
    }

    const int stripIndex = request->getParam("strip_index", false)->value().toInt();
    const int totalStrips = request->getParam("strip_count", false)->value().toInt();
    const int imageWidth = request->getParam("width", false)->value().toInt();
    const int imageHeight = request->getParam("height", false)->value().toInt();
    const unsigned long timeoutMs = request->hasParam("timeout", false)
        ? (unsigned long)request->getParam("timeout", false)->value().toInt() * 1000UL
        : g_cfg.default_timeout_ms;

    if (index == 0) {
        // Reject if we're busy. AsyncWebServer runs on AsyncTCP task; do not block.
        if (upload_state == UPLOAD_IN_PROGRESS || upload_state == UPLOAD_READY_TO_DISPLAY || pending_strip_op.buffer) {
            request->send(409, "application/json", "{\"success\":false,\"message\":\"Busy\"}");
            return;
        }

        // Only log first strip to reduce verbosity
        if (stripIndex == 0) {
            Logger.logMessagef("Strip Mode", "Uploading %dx%d image (%d strips)", imageWidth, imageHeight, totalStrips);
            device_telemetry_log_memory_snapshot("strip pre-alloc");
        }

        if (stripIndex < 0 || stripIndex >= totalStrips) {
            Logger.logEnd("ERROR: Invalid strip index");
            request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid strip index\"}");
            return;
        }

        if (imageWidth <= 0 || imageHeight <= 0 || imageWidth > g_cfg.lcd_width || imageHeight > g_cfg.lcd_height) {
            Logger.logLinef("ERROR: Invalid dimensions %dx%d", imageWidth, imageHeight);
            Logger.logEnd();
            request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid image dimensions\"}");
            return;
        }

        if (current_strip_buffer) {
            image_api_free((void*)current_strip_buffer);
            current_strip_buffer = nullptr;
        }

        current_strip_buffer = (uint8_t*)image_api_alloc(total);
        if (!current_strip_buffer) {
            Logger.logLinef("ERROR: Out of memory (requested %u bytes, free heap: %u)", (unsigned)total, ESP.getFreeHeap());
            device_telemetry_log_memory_snapshot("strip alloc-fail");
            Logger.logEnd();
            request->send(507, "application/json", "{\"success\":false,\"message\":\"Out of memory\"}");
            return;
        }

        current_strip_size = 0;
        strip_upload_last_activity_ms = millis();
    }

    if (current_strip_buffer && current_strip_size + len <= total) {
        memcpy((uint8_t*)current_strip_buffer + current_strip_size, data, len);
        current_strip_size += len;
        strip_upload_last_activity_ms = millis();
    }

    // Final chunk: decode synchronously before returning
    if (index + len >= total) {
        if (current_strip_size != total) {
            image_api_free((void*)current_strip_buffer);
            current_strip_buffer = nullptr;
            Logger.logEnd();
            request->send(500, "application/json", "{\"success\":false,\"message\":\"Incomplete upload\"}");
            return;
        }

        if (!is_jpeg_magic(current_strip_buffer, current_strip_size)) {
            image_api_free((void*)current_strip_buffer);
            current_strip_buffer = nullptr;
            Logger.logEnd();
            request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JPEG data\"}");
            return;
        }

        // Best-effort header preflight
        char preflight_err[160];
        const int remaining_height = imageHeight;
        if (!jpeg_preflight_tjpgd_fragment_supported(
                current_strip_buffer,
                current_strip_size,
                imageWidth,
                remaining_height,
                g_cfg.lcd_height,
                preflight_err,
                sizeof(preflight_err))) {
            Logger.logLinef("ERROR: JPEG fragment preflight failed: %s", preflight_err);
            image_api_free((void*)current_strip_buffer);
            current_strip_buffer = nullptr;
            Logger.logEnd();

            char resp[256];
            snprintf(resp, sizeof(resp), "{\"success\":false,\"message\":\"%s\"}", preflight_err);
            request->send(400, "application/json", resp);
            return;
        }

        // Queue strip for async decode (don't decode in HTTP handler)
        // If we're busy, reject and let client retry.
        if (upload_state == UPLOAD_IN_PROGRESS || upload_state == UPLOAD_READY_TO_DISPLAY || pending_strip_op.buffer) {
            image_api_free((void*)current_strip_buffer);
            current_strip_buffer = nullptr;
            Logger.logEnd();
            request->send(409, "application/json", "{\"success\":false,\"message\":\"Busy\"}" );
            return;
        }

        // Transfer strip buffer to pending operation
        upload_state = UPLOAD_IN_PROGRESS;
        pending_strip_op.buffer = current_strip_buffer;
        pending_strip_op.size = current_strip_size;
        pending_strip_op.strip_index = (uint8_t)stripIndex;
        pending_strip_op.image_width = imageWidth;
        pending_strip_op.image_height = imageHeight;
        pending_strip_op.total_strips = totalStrips;
        pending_strip_op.timeout_ms = timeoutMs;
        pending_strip_op.start_time = millis();
        
        current_strip_buffer = nullptr;
        current_strip_size = 0;
        
        upload_state = UPLOAD_READY_TO_DISPLAY;
        pending_op_id++;
        
        Logger.logMessagef("Strip", "Strip %d/%d queued for decode", stripIndex, totalStrips - 1);
        Logger.logEnd();

        char response[160];
        snprintf(response, sizeof(response),
                 "{\"success\":true,\"strip_index\":%d,\"strip_count\":%d,\"complete\":%s}",
                 stripIndex, totalStrips, (stripIndex == totalStrips - 1) ? "true" : "false");
        request->send(200, "application/json", response);
    }
}

// ===== Public API =====

void image_api_init(const ImageApiConfig& cfg, const ImageApiBackend& backend) {
    g_cfg = cfg;
    g_backend = backend;

    image_upload_timeout_ms = g_cfg.default_timeout_ms;

    // Best-effort: reset state
    upload_state = UPLOAD_IDLE;
    pending_op_id = 0;
    pending_image_op = {nullptr, 0, false, g_cfg.default_timeout_ms, 0};

    if (current_strip_buffer) {
        image_api_free((void*)current_strip_buffer);
        current_strip_buffer = nullptr;
    }
    current_strip_size = 0;

    if (pending_strip_op.buffer) {
        image_api_free((void*)pending_strip_op.buffer);
        pending_strip_op.buffer = nullptr;
    }
    pending_strip_op = {nullptr, 0, 0, 0, 0, 0, g_cfg.default_timeout_ms, 0};

    if (image_upload_buffer) {
        image_api_free((void*)image_upload_buffer);
        image_upload_buffer = nullptr;
    }
    image_upload_size = 0;
}

void image_api_register_routes(AsyncWebServer* server) {
    // Register the more specific /strips endpoint before /image.
    server->on(
        "/api/display/image/strips",
        HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        handleStripUpload
    );

    server->on(
        "/api/display/image",
        HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        handleImageUpload
    );

    server->on(
        "/api/display/image_url",
        HTTP_POST,
        [](AsyncWebServerRequest *request) {},
        NULL,
        handleImageUrl
    );

    server->on("/api/display/image", HTTP_DELETE, handleImageDelete);
}

void image_api_process_pending(bool ota_in_progress) {
    static unsigned long last_processed_id = 0;

    // Reclaim memory from interrupted uploads.
    // AsyncWebServer may stop calling the upload handlers if the client disconnects mid-transfer.
    if (upload_state == UPLOAD_IN_PROGRESS && !ota_in_progress) {
        // Full-image multipart upload stuck.
        if (image_upload_buffer && image_upload_timeout_ms > 0) {
            const unsigned long elapsed = (unsigned long)(millis() - image_upload_start_ms);
            // Give the client a little extra beyond the configured display timeout.
            if (elapsed > (image_upload_timeout_ms + 3000UL)) {
                Logger.logMessage("ImageApi", "WARNING: Aborting stuck image upload; freeing buffer");
                image_api_free((void*)image_upload_buffer);
                image_upload_buffer = nullptr;
                image_upload_size = 0;
                upload_state = UPLOAD_IDLE;
            }
        }

        // Strip upload stuck.
        if (current_strip_buffer) {
            const unsigned long elapsed = (unsigned long)(millis() - strip_upload_last_activity_ms);
            if (elapsed > 3000UL) {
                Logger.logMessage("ImageApi", "WARNING: Aborting stuck strip upload; freeing buffer");
                image_api_free((void*)current_strip_buffer);
                current_strip_buffer = nullptr;
                current_strip_size = 0;
                upload_state = UPLOAD_IDLE;
            }
        }
    }

    if (upload_state != UPLOAD_READY_TO_DISPLAY || ota_in_progress) {
        return;
    }

    if (pending_op_id == last_processed_id) {
        return;
    }

    last_processed_id = pending_op_id;

    // Handle queued HTTP(S) download
    char url_to_download[IMAGE_API_URL_MAX_LEN];
    unsigned long url_timeout_ms = 0;
    bool has_url_op = false;
    portENTER_CRITICAL(&pending_url_op_mux);
    if (pending_url_op.active) {
        strncpy(url_to_download, pending_url_op.url, sizeof(url_to_download));
        url_to_download[sizeof(url_to_download) - 1] = '\0';
        url_timeout_ms = pending_url_op.timeout_ms;
        pending_url_op.active = false;
        pending_url_op.url[0] = '\0';
        has_url_op = true;
    }
    portEXIT_CRITICAL(&pending_url_op_mux);

    if (has_url_op) {
        Logger.logMessagef("Portal", "Downloading image URL (%s)", url_to_download);
        device_telemetry_log_memory_snapshot("urlimg pre-download");

        upload_state = UPLOAD_IN_PROGRESS;

        const unsigned long timeout_ms = url_timeout_ms;

        uint8_t* downloaded = nullptr;
        size_t downloaded_sz = 0;
        char err[128];
        const bool ok = download_jpeg_to_buffer(url_to_download, timeout_ms, &downloaded, &downloaded_sz, err, sizeof(err));

        device_telemetry_log_memory_snapshot("urlimg post-download");

        if (!ok) {
            Logger.logMessagef("Portal", "ERROR: URL download failed: %s", err);
            device_telemetry_log_memory_snapshot("urlimg download-fail");
            upload_state = UPLOAD_IDLE;
            if (g_backend.hide_current_image) {
                g_backend.hide_current_image();
            }
            return;
        }

        // Queue the downloaded JPEG for decode on the next main-loop tick.
        if (pending_image_op.buffer) {
            image_api_free((void*)pending_image_op.buffer);
        }
        pending_image_op.buffer = downloaded;
        pending_image_op.size = downloaded_sz;
        pending_image_op.dismiss = false;
        pending_image_op.timeout_ms = timeout_ms > 0 ? timeout_ms : g_cfg.default_timeout_ms;
        pending_image_op.start_time = millis();
        pending_op_id++;
        upload_state = UPLOAD_READY_TO_DISPLAY;
        return;
    }

    // Handle dismiss operation
    if (pending_image_op.dismiss) {
        device_telemetry_log_memory_snapshot("img dismiss");
        if (g_backend.hide_current_image) {
            g_backend.hide_current_image();
        }
        pending_image_op.dismiss = false;
        upload_state = UPLOAD_IDLE;
        return;
    }

    // Handle strip operation
    if (pending_strip_op.buffer && pending_strip_op.size > 0) {
        const uint8_t* buf = pending_strip_op.buffer;
        const size_t sz = pending_strip_op.size;
        const uint8_t strip_index = pending_strip_op.strip_index;
        const int total_strips = pending_strip_op.total_strips;

        Logger.logMessagef("Portal", "Processing strip %d/%d (%u bytes)", strip_index, total_strips - 1, (unsigned)sz);

        if (strip_index == 0) {
            device_telemetry_log_memory_snapshot("strip pre-decode");
        }

        // Initialize strip session on first strip
        if (strip_index == 0) {
            if (!g_backend.start_strip_session) {
                Logger.logMessage("Portal", "ERROR: No strip session handler");
                if (g_backend.hide_current_image) {
                    g_backend.hide_current_image();
                }
                image_api_free((void*)pending_strip_op.buffer);
                pending_strip_op.buffer = nullptr;
                upload_state = UPLOAD_IDLE;
                return;
            }

            if (!g_backend.start_strip_session(pending_strip_op.image_width, pending_strip_op.image_height, 
                                                pending_strip_op.timeout_ms, pending_strip_op.start_time)) {
                Logger.logMessage("Portal", "ERROR: Failed to init strip session");
                if (g_backend.hide_current_image) {
                    g_backend.hide_current_image();
                }
                image_api_free((void*)pending_strip_op.buffer);
                pending_strip_op.buffer = nullptr;
                upload_state = UPLOAD_IDLE;
                return;
            }
        }

        // Decode strip
        bool success = false;
        if (g_backend.decode_strip) {
            #if HAS_DISPLAY
            // Serialize with LVGL task to protect buffered backends (Arduino_GFX canvas)
            // and prevent overlapping present()/SPI polling transactions.
            display_manager_lock();
            #endif
            success = g_backend.decode_strip(buf, sz, strip_index, false);
            #if HAS_DISPLAY
            display_manager_unlock();
            #endif
        }

        if (strip_index == (uint8_t)(total_strips - 1)) {
            device_telemetry_log_memory_snapshot("strip post-decode");
        }

        image_api_free((void*)pending_strip_op.buffer);
        pending_strip_op.buffer = nullptr;
        pending_strip_op.size = 0;
        upload_state = UPLOAD_IDLE;

        if (!success) {
            Logger.logMessagef("Portal", "ERROR: Failed to decode strip %d", strip_index);
            device_telemetry_log_memory_snapshot("strip decode-fail");
            if (g_backend.hide_current_image) {
                g_backend.hide_current_image();
            }
        } else if (strip_index == total_strips - 1) {
            Logger.logMessagef("Portal", "\u2713 All %d strips decoded", total_strips);
        }
        return;
    }

    // Handle full image operation (fallback for full mode)
    if (pending_image_op.buffer && pending_image_op.size > 0) {
        const uint8_t* buf = pending_image_op.buffer;
        const size_t sz = pending_image_op.size;

        Logger.logMessagef("Portal", "Processing pending image (%u bytes)", (unsigned)sz);

        device_telemetry_log_memory_snapshot("img pre-decode");

        #if HAS_DISPLAY && LV_USE_IMG
        // PoC: if the user is currently on the LVGL image screen, decode JPEG to RGB565 and
        // show via LVGL (lv_img) instead of direct LCD writes.
        const char* current_screen = display_manager_get_current_screen_id();
        Logger.logMessagef("Portal", "Current screen: %s", current_screen ? current_screen : "(none)");
        if (current_screen && strcmp(current_screen, "lvgl_image") == 0) {
            uint16_t* pixels = nullptr;
            int w = 0;
            int h = 0;
            int scale_used = -1;
            char derr[96];

            // Decode without holding the LVGL mutex.
            const bool ok = lvgl_jpeg_decode_to_rgb565(buf, sz, &pixels, &w, &h, &scale_used, derr, sizeof(derr));
            if (!ok) {
                Logger.logMessagef("Portal", "ERROR: LVGL JPEG decode failed: %s", derr);
                device_telemetry_log_memory_snapshot("img lvgl-decode-fail");

                image_api_free((void*)pending_image_op.buffer);
                pending_image_op.buffer = nullptr;
                pending_image_op.size = 0;
                upload_state = UPLOAD_IDLE;

                if (g_backend.hide_current_image) {
                    g_backend.hide_current_image();
                }
                return;
            }

            LvglImageScreen* screen = display_manager_get_lvgl_image_screen();
            bool set_ok = false;
            display_manager_lock();
            if (screen) set_ok = screen->setImageRgb565(pixels, w, h);
            display_manager_unlock();

            // Helpful runtime diagnostics: show whether we decoded at reduced resolution.
            // The screen will scale this to a fixed 200x200 box via lv_img zoom.
            const int div = (scale_used >= 0 && scale_used <= 7) ? (1 << scale_used) : 0;
            const double zoom = (double)200.0 / (double)((w > h) ? w : h);
            if (div) {
                Logger.logMessagef(
                    "Portal",
                    "LVGL img: decoded %dx%d (tjpgd scale %d (1/%d)) -> target 200x200 (zoom %.2fx)",
                    w,
                    h,
                    scale_used,
                    div,
                    zoom
                );
            } else {
                Logger.logMessagef(
                    "Portal",
                    "LVGL img: decoded %dx%d (tjpgd scale %d) -> target 200x200 (zoom %.2fx)",
                    w,
                    h,
                    scale_used,
                    zoom
                );
            }

            if (!set_ok) {
                heap_caps_free(pixels);
                Logger.logMessage("Portal", "ERROR: Failed to set LVGL image");

                image_api_free((void*)pending_image_op.buffer);
                pending_image_op.buffer = nullptr;
                pending_image_op.size = 0;
                upload_state = UPLOAD_IDLE;

                if (g_backend.hide_current_image) {
                    g_backend.hide_current_image();
                }
                return;
            }

            device_telemetry_log_memory_snapshot("img lvgl-post");

            image_api_free((void*)pending_image_op.buffer);
            pending_image_op.buffer = nullptr;
            pending_image_op.size = 0;
            upload_state = UPLOAD_IDLE;
            return;
        }
        #endif

        bool success = false;
        if (g_backend.start_strip_session && g_backend.decode_strip) {
            #if HAS_DISPLAY
            // Serialize with LVGL task for the duration of the full decode.
            display_manager_lock();
            #endif

            if (!g_backend.start_strip_session(g_cfg.lcd_width, g_cfg.lcd_height, pending_image_op.timeout_ms, pending_image_op.start_time)) {
                Logger.logMessage("Portal", "ERROR: Failed to init image display");
                success = false;
            } else {
                success = g_backend.decode_strip(buf, sz, 0, false);
            }

            #if HAS_DISPLAY
            display_manager_unlock();
            #endif
        }

        device_telemetry_log_memory_snapshot("img post-decode");

        image_api_free((void*)pending_image_op.buffer);
        pending_image_op.buffer = nullptr;
        pending_image_op.size = 0;
        upload_state = UPLOAD_IDLE;

        if (!success) {
            Logger.logMessage("Portal", "ERROR: Failed to display image");
            device_telemetry_log_memory_snapshot("img decode-fail");
            if (g_backend.hide_current_image) {
                g_backend.hide_current_image();
            }
        }
        return;
    }

    // Invalid state
    upload_state = UPLOAD_IDLE;
}

#endif // HAS_IMAGE_API
