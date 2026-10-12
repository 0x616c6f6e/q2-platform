#define _POSIX_C_SOURCE 200809L
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
// clang-format off
/* Winsock must precede Windows declarations. */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
// clang-format on
#define Q2_SEND_FLAGS 0
#else
#include <fcntl.h>
#include <netinet/in.h>
#include <strings.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket close
#define _strnicmp strncasecmp
#define Q2_SEND_FLAGS MSG_NOSIGNAL
#endif
#include "key_input.h"
#include "pc_time.h"
#include "q2/log.h"
#include "q2_platform_config.h"
#include "web.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const unsigned char page[] = {
#include "web_page.inc"
};
#define CLIENTS 4
#define EVENT_HISTORY 16u
#define INPUT_LEASE_MS 2000u
#define REQUEST_MAX 4096
#define RESPONSE_MAX (64u * 1024u)
typedef struct {
    SOCKET socket;
    uint64_t started;
    char request[REQUEST_MAX + 1];
    size_t used, sent, length;
    char *response;
} client_t;
struct q2_pc_web {
    SOCKET listener;
    bool wsa;
    q2_kernel_t *kernel;
    q2_keys_t *keys;
    q2_lvgl_t *display;
    q2_pc_device_data_t *device_data;
    bool wake_gesture[Q2_KEYS_MAX];
    unsigned port;
    char authority[64], token[65];
    client_t clients[CLIENTS];
    uint64_t reports;
    char last_key[32];
    q2_key_action_t last_value;
    q2_pc_key_input_t input[Q2_KEYS_MAX];
    uint64_t input_seen;
    struct {
        char name[32];
        q2_key_action_t action;
        q2_result_t result;
    } events[EVENT_HISTORY];
};
static void close_client(client_t *c) {
    if (c->socket != INVALID_SOCKET)
        closesocket(c->socket);
    free(c->response);
    memset(c, 0, sizeof(*c));
    c->socket = INVALID_SOCKET;
}
static bool socket_retry(void) {
#ifdef _WIN32
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}
static bool nonblocking(SOCKET socket) {
#ifdef _WIN32
    u_long enabled = 1;
    return ioctlsocket(socket, FIONBIO, &enabled) == 0;
#else
    int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0 &&
           fcntl(socket, F_SETFD, FD_CLOEXEC) == 0;
#endif
}
static void response(client_t *c, unsigned status, const char *type, const void *body, size_t size) {
    if (size > RESPONSE_MAX) {
        close_client(c);
        return;
    }
    const char *reason = status == 200   ? "OK"
                         : status == 403 ? "Forbidden"
                         : status == 404 ? "Not Found"
                         : status == 429 ? "Too Many Requests"
                                         : "Bad Request";
    char head[512];
    int n = snprintf(
        head, sizeof(head),
        "HTTP/1.1 %u %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: "
        "no-store\r\nX-Content-Type-Options: nosniff\r\nReferrer-Policy: no-referrer\r\n\r\n",
        status, reason, type, size);
    c->response = malloc((size_t)n + size);
    if (!c->response) {
        close_client(c);
        return;
    }
    memcpy(c->response, head, (size_t)n);
    memcpy(c->response + n, body, size);
    c->length = (size_t)n + size;
}
static void result(client_t *c, q2_result_t e) {
    const char *name;
    q2_result_name(e, &name);
    char body[160];
    int n = snprintf(body, sizeof(body), "{\"ok\":%s,\"code\":\"%s\",\"number\":%d}",
                     e == Q2_OK ? "true" : "false", name, e);
    response(c,
             e == Q2_OK               ? 200
             : e == Q2_ERR_PERMISSION ? 403
             : e == Q2_ERR_NOT_FOUND  ? 404
             : e == Q2_ERR_LIMIT      ? 429
                                      : 400,
             "application/json; charset=utf-8", body, (size_t)n);
}
/* Reject duplicate/oversized headers. Do not accept ambiguous HTTP framing on a debug control endpoint. */
static bool header(const char *req, const char *name, char *out, size_t capacity) {
    const char *p = strstr(req, "\r\n");
    bool found = false;
    out[0] = 0;
    if (!p)
        return false;
    p += 2;
    while (*p && strncmp(p, "\r\n", 2)) {
        const char *end = strstr(p, "\r\n"), *colon = strchr(p, ':');
        if (!end || !colon || colon >= end)
            return false;
        if ((size_t)(colon - p) == strlen(name) && !_strnicmp(p, name, strlen(name))) {
            if (found)
                return false;
            found = true;
            const char *value = colon + 1;
            while (value < end && (*value == ' ' || *value == '\t'))
                value++;
            size_t n = (size_t)(end - value);
            if (n >= capacity)
                return false;
            memcpy(out, value, n);
            out[n] = 0;
        }
        p = end + 2;
    }
    return true;
}
static bool append(char *buffer, size_t cap, size_t *offset, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = vsnprintf(buffer + *offset, cap - *offset, format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= cap - *offset)
        return false;
    *offset += (size_t)n;
    return true;
}
typedef struct {
    q2_pc_web_t *web;
    const char *name;
    size_t index;
} report_context_t;
static q2_result_t report_action(void *context, q2_key_action_t action) {
    report_context_t *r = context;
    q2_pc_web_t *w = r->web;
    /* Consume the complete waking gesture, including its delayed single/double/long recognition. */
    q2_system_ui_state_t ui;
    q2_lvgl_system_state(w->display, &ui);
    if (w->wake_gesture[r->index] && !ui.locked)
        return Q2_OK;
    q2_result_t e = q2_keys_report(w->keys, r->name, action);
    size_t slot = (size_t)(w->reports % EVENT_HISTORY);
    strcpy(w->events[slot].name, r->name);
    w->events[slot].action = action;
    w->events[slot].result = e;
    w->reports++;
    strcpy(w->last_key, r->name);
    w->last_value = action;
    if (e != Q2_OK)
        Q2_LOGW(w->kernel, "web", "key report failed name=%s action=%u result=%d", r->name, action, e);
    return e;
}
static q2_result_t advance_inputs(q2_pc_web_t *w) {
    size_t count = 0;
    q2_keys_count(w->keys, &count);
    uint64_t now = q2_pc_now_ms();
    for (size_t i = 0; i < count; i++) {
        /* A lost browser must not leave a held key stuck. Pending released clicks remain valid. */
        if (w->input[i].down && now - w->input_seen >= INPUT_LEASE_MS) {
            q2_pc_key_cancel(&w->input[i]);
            w->wake_gesture[i] = false;
            continue;
        }
        q2_key_state_t key;
        q2_keys_at(w->keys, i, &key);
        report_context_t context = {w, key.name, i};
        if (w->input[i].down) {
            bool consumed = false;
            q2_result_t e = q2_lvgl_activity(w->display, &consumed);
            if (e != Q2_OK)
                return e;
            w->wake_gesture[i] |= consumed;
        }
        q2_pc_key_poll(&w->input[i], now, report_action, &context);
        if (!w->input[i].down && !w->input[i].pending)
            w->wake_gesture[i] = false;
    }
    return Q2_OK;
}
static void state(q2_pc_web_t *w, client_t *c) {
    char json[8192];
    size_t at = 0, count = 0;
    q2_keys_count(w->keys, &count);
    q2_system_ui_state_t ui;
    q2_lvgl_system_state(w->display, &ui);
    bool ok = append(
        json, sizeof(json), &at,
        "{\"locked\":%s,\"lifecycle\":%s,\"screensaver\":%s,\"content_height\":%u,\"status_bar_height\":%u,",
        ui.locked ? "true" : "false", ui.lifecycle ? "true" : "false",
        ui.screensaver_active ? "true" : "false", ui.content_height, ui.content_top);
    ok =
        ok && append(json, sizeof(json), &at,
                     "\"token\":\"%s\",\"reports\":%llu,\"last_key\":\"%s\",\"last_action\":%u,"
                     "\"timing\":{\"single_min_ms\":%u,\"double_gap_ms\":%u,\"long_press_ms\":%u},\"keys\":[",
                     w->token, (unsigned long long)w->reports, w->last_key, (unsigned)w->last_value,
                     Q2_KEY_SINGLE_MIN_MS, Q2_KEY_DOUBLE_GAP_MS, Q2_KEY_LONG_PRESS_MS);
    for (size_t i = 0; i < count && ok; i++) {
        q2_key_state_t key;
        q2_keys_at(w->keys, i, &key);
        ok = append(json, sizeof(json), &at, "%s{\"name\":\"%s\",\"last_action\":%u,\"pressed\":%s}",
                    i ? "," : "", key.name, (unsigned)key.last_action, w->input[i].down ? "true" : "false");
    }
    ok = ok && append(json, sizeof(json), &at, "],\"events\":[");
    uint64_t start = w->reports > EVENT_HISTORY ? w->reports - EVENT_HISTORY : 0;
    for (uint64_t n = start; n < w->reports && ok; n++) {
        size_t slot = (size_t)(n % EVENT_HISTORY);
        ok = append(json, sizeof(json), &at, "%s{\"id\":%llu,\"name\":\"%s\",\"action\":%u,\"result\":%d}",
                    n == start ? "" : ",", (unsigned long long)(n + 1), w->events[slot].name,
                    (unsigned)w->events[slot].action, w->events[slot].result);
    }
    ok = ok && append(json, sizeof(json), &at, "],\"simulation\":{\"enabled\":%s",
                      w->device_data ? "true" : "false");
    if (w->device_data) {
        const char *modes[] = {"off", "value", "error", "timeout"};
        const q2_pc_device_data_t *d = w->device_data;
        ok = ok && append(json, sizeof(json), &at,
                          ",\"battery\":{\"mode\":\"%s\",\"percent\":%d,\"voltage_mv\":%d,\"charging\":%s}",
                          modes[d->battery.mode], d->battery.percent, d->battery.voltage_mv,
                          d->battery.charging ? "true" : "false");
        const q2_pc_radio_sim_t *radios[] = {&d->wifi, &d->ble};
        for (unsigned i = 0; i < 2 && ok; i++)
            ok = append(json, sizeof(json), &at, ",\"%s\":{\"mode\":\"%s\",\"connected\":%s,\"rssi_dbm\":%d}",
                        i ? "ble" : "wifi", modes[radios[i]->mode], radios[i]->connected ? "true" : "false",
                        radios[i]->rssi_dbm);
    }
    ok = ok && append(json, sizeof(json), &at, "}}");
    if (ok)
        response(c, 200, "application/json; charset=utf-8", json, at);
    else
        result(c, Q2_ERR_LIMIT);
}
/* Strict, bounded query parsing. Unknown/duplicate fields and non-integer values never partially apply. */
typedef struct {
    char *key, *value;
} field_t;
static const char *field(field_t *fields, unsigned count, const char *key) {
    for (unsigned i = 0; i < count; i++)
        if (!strcmp(fields[i].key, key))
            return fields[i].value;
    return NULL;
}
static bool integer_field(field_t *fields, unsigned count, const char *key, int low, int high, int *out) {
    const char *s = field(fields, count, key);
    if (!s || !*s)
        return false;
    for (unsigned i = (*s == '-'); s[i]; i++)
        if (s[i] < '0' || s[i] > '9')
            return false;
    char *end;
    errno = 0;
    long n = strtol(s, &end, 10);
    if (errno || end == s || *end || n < low || n > high)
        return false;
    *out = (int)n;
    return true;
}
static q2_result_t device_request(q2_pc_web_t *w, char *query) {
    if (!w->device_data)
        return Q2_ERR_UNSUPPORTED;
    field_t fields[5];
    unsigned count = 0;
    char *next = query;
    while (next && *next) {
        if (count == 5)
            return Q2_ERR_ARGUMENT;
        char *end = strchr(next, '&');
        if (end)
            *end++ = 0;
        char *equals = strchr(next, '=');
        if (!equals || equals == next || !equals[1])
            return Q2_ERR_ARGUMENT;
        *equals++ = 0;
        if (field(fields, count, next))
            return Q2_ERR_ARGUMENT;
        fields[count++] = (field_t){next, equals};
        if (end && !*end)
            return Q2_ERR_ARGUMENT;
        next = end;
    }
    const char *group = field(fields, count, "group"), *mode = field(fields, count, "mode");
    if (!group || !mode || (strcmp(group, "battery") && strcmp(group, "wifi") && strcmp(group, "ble")))
        return Q2_ERR_ARGUMENT;
    q2_pc_sim_mode_t selected;
    if (!strcmp(mode, "off"))
        selected = Q2_PC_SIM_OFF;
    else if (!strcmp(mode, "value"))
        selected = Q2_PC_SIM_VALUE;
    else if (!strcmp(mode, "error"))
        selected = Q2_PC_SIM_ERROR;
    else if (!strcmp(mode, "timeout"))
        selected = Q2_PC_SIM_TIMEOUT;
    else
        return Q2_ERR_ARGUMENT;
    if (!strcmp(group, "battery")) {
        q2_pc_battery_sim_t sample = w->device_data->battery;
        sample.mode = selected;
        if (selected == Q2_PC_SIM_VALUE) {
            int charging;
            if (count != 5 || !integer_field(fields, count, "percent", 0, 100, &sample.percent) ||
                !integer_field(fields, count, "voltage_mv", 0, 20000, &sample.voltage_mv) ||
                !integer_field(fields, count, "charging", 0, 1, &charging))
                return Q2_ERR_ARGUMENT;
            sample.charging = charging != 0;
        } else if (count != 2)
            return Q2_ERR_ARGUMENT;
        return q2_pc_device_data_battery(w->device_data, &sample);
    }
    q2_pc_radio_sim_t sample = !strcmp(group, "ble") ? w->device_data->ble : w->device_data->wifi;
    sample.mode = selected;
    if (selected == Q2_PC_SIM_VALUE) {
        int connected;
        if (count != 4 || !integer_field(fields, count, "connected", 0, 1, &connected) ||
            !integer_field(fields, count, "rssi_dbm", -127, 0, &sample.rssi_dbm))
            return Q2_ERR_ARGUMENT;
        sample.connected = connected != 0;
    } else if (count != 2)
        return Q2_ERR_ARGUMENT;
    return q2_pc_device_data_radio(w->device_data, !strcmp(group, "ble"), &sample);
}
static void dispatch(q2_pc_web_t *w, client_t *c) {
    char method[8], path[256], version[16], host[80], origin[128], length[32], transfer[80], token[80];
    int end = 0;
    if (sscanf(c->request, "%7s %255s %15s%n", method, path, version, &end) != 3 ||
        strncmp(c->request + end, "\r\n", 2) || strcmp(version, "HTTP/1.1") ||
        !header(c->request, "Host", host, sizeof(host)) || strcmp(host, w->authority) ||
        !header(c->request, "Origin", origin, sizeof(origin)) ||
        !header(c->request, "Content-Length", length, sizeof(length)) ||
        !header(c->request, "Transfer-Encoding", transfer, sizeof(transfer)) || *transfer ||
        (*length && strcmp(length, "0"))) {
        result(c, Q2_ERR_ARGUMENT);
        return;
    }
    char expected[96];
    snprintf(expected, sizeof(expected), "http://%s", w->authority);
    if (*origin && strcmp(origin, expected)) {
        result(c, Q2_ERR_PERMISSION);
        return;
    }
    if (!strcmp(method, "GET")) {
        if (!strcmp(path, "/"))
            response(c, 200, "text/html; charset=utf-8", page, sizeof(page));
        else if (!strcmp(path, "/api/state"))
            state(w, c);
        else
            result(c, Q2_ERR_NOT_FOUND);
        return;
    }
    if (strcmp(method, "POST") || !header(c->request, "X-Q2-Token", token, sizeof(token)) ||
        strcmp(token, w->token)) {
        result(c, Q2_ERR_PERMISSION);
        return;
    }
    if (!strncmp(path, "/api/device?", 12)) {
        result(c, device_request(w, path + 12));
        return;
    }
    if (!strcmp(path, "/api/heartbeat")) {
        w->input_seen = q2_pc_now_ms();
        result(c, Q2_OK);
        return;
    }
    char name[32], edge[16];
    int used = 0;
    q2_result_t e = Q2_ERR_ARGUMENT;
    if (sscanf(path, "/api/key?name=%31[A-Za-z0-9_.-]&state=%15[a-z]%n", name, edge, &used) == 2 && used &&
        path[used] == 0 && (!strcmp(edge, "down") || !strcmp(edge, "up") || !strcmp(edge, "cancel"))) {
        size_t count = 0;
        q2_keys_count(w->keys, &count);
        e = Q2_ERR_NOT_FOUND;
        for (size_t i = 0; i < count; i++) {
            q2_key_state_t key;
            q2_keys_at(w->keys, i, &key);
            if (strcmp(key.name, name))
                continue;
            w->input_seen = q2_pc_now_ms();
            report_context_t context = {w, name, i};
            if (!strcmp(edge, "down")) {
                bool consumed = false;
                e = q2_lvgl_activity(w->display, &consumed);
                if (e != Q2_OK)
                    break;
                w->wake_gesture[i] |= consumed;
            } else if (!strcmp(edge, "cancel"))
                w->wake_gesture[i] = false;
            e = !strcmp(edge, "cancel") ? q2_pc_key_cancel(&w->input[i])
                                        : q2_pc_key_edge(&w->input[i], !strcmp(edge, "down"), w->input_seen,
                                                         report_action, &context);
            break;
        }
    }
    result(c, e);
}
q2_result_t q2_pc_web_create(q2_kernel_t *k, q2_keys_t *keys, q2_lvgl_t *display,
                             q2_pc_device_data_t *device_data, unsigned port, q2_pc_web_t **out) {
    if (!k || !keys || !display || !out || port > 65535)
        return Q2_ERR_ARGUMENT;
    *out = calloc(1, sizeof(q2_pc_web_t));
    if (!*out)
        return Q2_ERR_MEMORY;
    q2_pc_web_t *w = *out;
    w->listener = INVALID_SOCKET;
    for (unsigned i = 0; i < CLIENTS; i++)
        w->clients[i].socket = INVALID_SOCKET;
    w->kernel = k;
    w->keys = keys;
    w->display = display;
    w->device_data = device_data;
    unsigned char random[32];
#ifdef _WIN32
    WSADATA data;
    if (BCryptGenRandom(NULL, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0 ||
        WSAStartup(MAKEWORD(2, 2), &data))
        goto failure;
    w->wsa = true;
#else
    /* Kernel entropy, never a predictable PRNG for the HTTP control token. */
    size_t used = 0;
    while (used < sizeof(random)) {
        ssize_t count = getrandom(random + used, sizeof(random) - used, 0);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            goto failure;
        used += (size_t)count;
    }
#endif
    for (unsigned i = 0; i < sizeof(random); i++)
        snprintf(w->token + 2 * i, 3, "%02x", random[i]);
    w->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (w->listener == INVALID_SOCKET)
        goto failure;
#ifdef _WIN32
    BOOL exclusive = TRUE;
    if (setsockopt(w->listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&exclusive, sizeof(exclusive)))
        goto failure;
#endif
    /* POSIX intentionally does not set SO_REUSEADDR/PORT: one simulator owns the port. */
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons((uint16_t)port);
    if (bind(w->listener, (struct sockaddr *)&address, sizeof(address)) || listen(w->listener, CLIENTS) ||
        !nonblocking(w->listener))
        goto failure;
#ifdef _WIN32
    int size = sizeof(address);
#else
    socklen_t size = sizeof(address);
#endif
    if (getsockname(w->listener, (struct sockaddr *)&address, &size))
        goto failure;
    w->port = ntohs(address.sin_port);
    snprintf(w->authority, sizeof(w->authority), "127.0.0.1:%u", w->port);
    Q2_LOGI(k, "web", "hardware simulator http://%s/", w->authority);
    return Q2_OK;
failure:
    q2_pc_web_destroy(out);
    return Q2_ERR_IO;
}
q2_result_t q2_pc_web_poll(q2_pc_web_t *w) {
    if (!w)
        return Q2_ERR_ARGUMENT;
    for (unsigned i = 0; i < CLIENTS; i++)
        if (w->clients[i].socket == INVALID_SOCKET) {
            SOCKET socket = accept(w->listener, NULL, NULL);
            if (socket != INVALID_SOCKET) {
                if (!nonblocking(socket))
                    closesocket(socket);
                else {
                    w->clients[i].socket = socket;
                    w->clients[i].started = q2_pc_now_ms();
                }
            } else if (!socket_retry())
                return Q2_ERR_IO;
            break;
        }
    for (unsigned i = 0; i < CLIENTS; i++) {
        client_t *c = &w->clients[i];
        if (c->socket == INVALID_SOCKET)
            continue;
        if (q2_pc_now_ms() - c->started > 3000) {
            close_client(c);
            continue;
        }
        if (!c->response) {
            int n = recv(c->socket, c->request + c->used, (int)(REQUEST_MAX - c->used), 0);
            if (n == 0 || (n == SOCKET_ERROR && !socket_retry())) {
                close_client(c);
                continue;
            }
            if (n > 0) {
                if (memchr(c->request + c->used, 0, (size_t)n)) {
                    result(c, Q2_ERR_ARGUMENT);
                } else {
                    c->used += (size_t)n;
                    c->request[c->used] = 0;
                    char *end = strstr(c->request, "\r\n\r\n");
                    if (end) {
                        if ((size_t)(end + 4 - c->request) != c->used)
                            result(c, Q2_ERR_ARGUMENT);
                        else
                            dispatch(w, c);
                    } else if (c->used == REQUEST_MAX)
                        result(c, Q2_ERR_LIMIT);
                }
            }
        }
        if (c->response) {
            size_t remaining = c->length - c->sent;
            if (remaining > 65536)
                remaining = 65536;
            int n = send(c->socket, c->response + c->sent, (int)remaining, Q2_SEND_FLAGS);
            if (n > 0)
                c->sent += (size_t)n;
            if (c->sent == c->length || n == 0 || (n == SOCKET_ERROR && !socket_retry()))
                close_client(c);
        }
    }
    return advance_inputs(w);
}
q2_result_t q2_pc_web_destroy(q2_pc_web_t **ref) {
    if (!ref)
        return Q2_ERR_ARGUMENT;
    if (!*ref)
        return Q2_OK;
    q2_pc_web_t *w = *ref;
    for (unsigned i = 0; i < CLIENTS; i++)
        close_client(&w->clients[i]);
    if (w->listener != INVALID_SOCKET)
        closesocket(w->listener);
#ifdef _WIN32
    if (w->wsa)
        WSACleanup();
#endif
    free(w);
    *ref = NULL;
    return Q2_OK;
}
