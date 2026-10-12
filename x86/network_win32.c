#define WIN32_LEAN_AND_MEAN
#include "network.h"
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <winhttp.h>
typedef struct {
    HANDLE thread;
    volatile LONG cancelled;
    bool detached;
    char url[Q2_TEXT_MAX], body[Q2_NETWORK_BODY_MAX + 1];
    char upload[Q2_NETWORK_UPLOAD_MAX + 1];
    size_t upload_size;
    bool post;
    unsigned timeout, code;
    size_t size;
    q2_result_t result;
    ULONGLONG deadline;
} job_t;
struct q2_pc_network {
    job_t *jobs[Q2_NETWORK_MAX];
};
static q2_result_t failure(void) {
    DWORD error = GetLastError();
    if (error == ERROR_WINHTTP_TIMEOUT)
        return Q2_ERR_TIMEOUT;
    if (error == ERROR_WINHTTP_NAME_NOT_RESOLVED)
        return Q2_ERR_NOT_FOUND;
    if (error == ERROR_WINHTTP_SECURE_FAILURE)
        return Q2_ERR_PERMISSION;
    if (error == ERROR_WINHTTP_INVALID_URL || error == ERROR_WINHTTP_UNRECOGNIZED_SCHEME)
        return Q2_ERR_ARGUMENT;
    return Q2_ERR_IO;
}
static bool advance(job_t *j, HINTERNET session) {
    if (InterlockedCompareExchange(&j->cancelled, 0, 0)) {
        j->result = Q2_ERR_CANCELLED;
        return false;
    }
    ULONGLONG now = GetTickCount64();
    if (now >= j->deadline) {
        j->result = Q2_ERR_TIMEOUT;
        return false;
    }
    int remaining = (int)(j->deadline - now);
    if (!WinHttpSetTimeouts(session, remaining, remaining, remaining, remaining)) {
        j->result = failure();
        return false;
    }
    return true;
}
static DWORD WINAPI worker(void *context) {
    job_t *j = context;
    HINTERNET session = NULL, connection = NULL, request = NULL;
    WCHAR url[Q2_TEXT_MAX], host[Q2_TEXT_MAX], path[Q2_TEXT_MAX], extra[Q2_TEXT_MAX], target[Q2_TEXT_MAX * 2];
    URL_COMPONENTS parts = {.dwStructSize = sizeof(parts),
                            .lpszHostName = host,
                            .dwHostNameLength = Q2_TEXT_MAX,
                            .lpszUrlPath = path,
                            .dwUrlPathLength = Q2_TEXT_MAX,
                            .lpszExtraInfo = extra,
                            .dwExtraInfoLength = Q2_TEXT_MAX};
    j->result = Q2_ERR_IO;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, j->url, -1, url, Q2_TEXT_MAX) ||
        !WinHttpCrackUrl(url, 0, 0, &parts)) {
        j->result = Q2_ERR_ARGUMENT;
        goto done;
    }
    if (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS) {
        j->result = Q2_ERR_ARGUMENT;
        goto done;
    }
    wcscpy(target, parts.dwUrlPathLength ? path : L"/");
    wcscat(target, extra);
    // Explicit direct network. No browser cookies, implicit login, proxy discovery or redirect side effects.
    session = WinHttpOpen(L"Q2-Network/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        j->result = failure();
        goto done;
    }
    if (!advance(j, session))
        goto done;
    connection = WinHttpConnect(session, host, parts.nPort, 0);
    if (!connection) {
        j->result = failure();
        goto done;
    }
    request = WinHttpOpenRequest(connection, j->post ? L"POST" : L"GET", target, NULL, WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES,
                                 parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!request) {
        j->result = failure();
        goto done;
    }
    DWORD disable = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
    DWORD retries = 0;
    if (!WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable)) ||
        !WinHttpSetOption(request, WINHTTP_OPTION_CONNECT_RETRIES, &retries, sizeof(retries))) {
        j->result = failure();
        goto done;
    }
    if (!advance(j, request))
        goto done;
    if (!WinHttpSendRequest(request,
                            j->post ? L"Content-Type: application/json\r\n" : WINHTTP_NO_ADDITIONAL_HEADERS,
                            j->post ? (DWORD)-1 : 0, j->post ? j->upload : WINHTTP_NO_REQUEST_DATA,
                            (DWORD)j->upload_size, (DWORD)j->upload_size, 0)) {
        j->result = failure();
        goto done;
    }
    if (!advance(j, request))
        goto done;
    if (!WinHttpReceiveResponse(request, NULL)) {
        j->result = failure();
        goto done;
    }
    DWORD status = 0, length = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX)) {
        j->result = failure();
        goto done;
    }
    j->code = status;
    for (;;) {
        if (!advance(j, request))
            goto done;
        char chunk[1024];
        DWORD got = 0;
        if (!WinHttpReadData(request, chunk, sizeof(chunk), &got)) {
            j->result = failure();
            goto done;
        }
        if (!got)
            break;
        if (j->size + got > Q2_NETWORK_BODY_MAX) {
            j->result = Q2_ERR_LIMIT;
            goto done;
        }
        // Transport carries opaque bytes; Kernel enforces text-only responses for text requests.
        memcpy(j->body + j->size, chunk, got);
        j->size += got;
    }
    j->body[j->size] = 0;
    j->result = Q2_OK;
done:
    // Synchronous WinHTTP handles are exclusively worker-owned, including cancellation cleanup.
    if (request)
        WinHttpCloseHandle(request);
    if (connection)
        WinHttpCloseHandle(connection);
    if (session)
        WinHttpCloseHandle(session);
    return 0;
}
static void reap(q2_pc_network_t *s) {
    for (unsigned i = 0; i < Q2_NETWORK_MAX; i++) {
        job_t *j = s->jobs[i];
        if (j && j->detached && WaitForSingleObject(j->thread, 0) == WAIT_OBJECT_0) {
            CloseHandle(j->thread);
            free(j);
            s->jobs[i] = NULL;
        }
    }
}
static q2_result_t begin_request(void *ctx, const char *url, const char *body, size_t size, unsigned timeout,
                                 void **out) {
    q2_pc_network_t *s = ctx;
    *out = NULL;
    reap(s);
    unsigned slot = 0;
    while (slot < Q2_NETWORK_MAX && s->jobs[slot])
        slot++;
    if (slot == Q2_NETWORK_MAX)
        return Q2_ERR_BUSY;
    job_t *j = calloc(1, sizeof(*j));
    if (!j)
        return Q2_ERR_MEMORY;
    strcpy(j->url, url);
    j->post = body != NULL;
    if (size)
        memcpy(j->upload, body, size);
    j->upload_size = size;
    j->timeout = timeout;
    j->deadline = GetTickCount64() + timeout;
    j->thread = CreateThread(NULL, 0, worker, j, 0, NULL);
    if (!j->thread) {
        free(j);
        return Q2_ERR_IO;
    }
    s->jobs[slot] = j;
    *out = j;
    return Q2_OK;
}
static q2_result_t begin(void *ctx, const char *url, unsigned timeout, void **out) {
    return begin_request(ctx, url, NULL, 0, timeout, out);
}
static q2_result_t post_request(void *ctx, const char *url, const char *body, size_t size, unsigned timeout,
                                void **out) {
    return begin_request(ctx, url, body, size, timeout, out);
}
static q2_result_t poll_job(void *ctx, void *job, q2_network_response_t *out) {
    (void)ctx;
    job_t *j = job;
    DWORD state = WaitForSingleObject(j->thread, 0);
    if (state == WAIT_TIMEOUT)
        return Q2_ERR_BUSY;
    if (state != WAIT_OBJECT_0)
        return Q2_ERR_IO;
    // Joining by signalled handle makes the worker's response immutable and visible to owner.
    if (j->result == Q2_OK)
        *out = (q2_network_response_t){j->code, j->body, j->size};
    return j->result;
}
static q2_result_t release_job(void *ctx, void **job) {
    if (*job) {
        job_t *j = *job;
        j->detached = true;
        InterlockedExchange(&j->cancelled, 1);
        *job = NULL;
    }
    reap(ctx);
    return Q2_OK;
}
q2_result_t q2_pc_network_create(q2_pc_network_t **out, q2_network_backend_t *backend) {
    if (!out || !backend)
        return Q2_ERR_ARGUMENT;
    *out = calloc(1, sizeof(**out));
    if (!*out)
        return Q2_ERR_MEMORY;
    *backend = (q2_network_backend_t){2, sizeof(*backend), *out, begin, poll_job, release_job, post_request};
    return Q2_OK;
}
q2_result_t q2_pc_network_destroy(q2_pc_network_t **s) {
    if (!s)
        return Q2_ERR_ARGUMENT;
    if (!*s)
        return Q2_OK;
    for (unsigned i = 0; i < Q2_NETWORK_MAX; i++)
        if ((*s)->jobs[i]) {
            (*s)->jobs[i]->detached = true;
            InterlockedExchange(&(*s)->jobs[i]->cancelled, 1);
        }
    ULONGLONG end = GetTickCount64() + 11000;
    for (unsigned i = 0; i < Q2_NETWORK_MAX; i++)
        if ((*s)->jobs[i]) {
            ULONGLONG now = GetTickCount64();
            if (now >= end || WaitForSingleObject((*s)->jobs[i]->thread, (DWORD)(end - now)) != WAIT_OBJECT_0)
                return Q2_ERR_BUSY;
        }
    reap(*s);
    free(*s);
    *s = NULL;
    return Q2_OK;
}
