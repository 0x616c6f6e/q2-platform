#define _GNU_SOURCE
#include "network.h"
#include "pc_time.h"
#include <curl/curl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    pthread_t thread;
    atomic_bool cancelled, complete;
    bool detached, post;
    char url[Q2_TEXT_MAX], body[Q2_NETWORK_BODY_MAX + 1], upload[Q2_NETWORK_UPLOAD_MAX + 1];
    size_t size, upload_size;
    unsigned code;
    uint64_t deadline;
    q2_result_t result;
} job_t;
struct q2_pc_network {
    job_t *jobs[Q2_NETWORK_MAX];
};
static q2_result_t failure(CURLcode code) {
    switch (code) {
    case CURLE_OK:
        return Q2_OK;
    case CURLE_OPERATION_TIMEDOUT:
        return Q2_ERR_TIMEOUT;
    case CURLE_COULDNT_RESOLVE_HOST:
        return Q2_ERR_NOT_FOUND;
    case CURLE_PEER_FAILED_VERIFICATION:
    case CURLE_SSL_CACERT_BADFILE:
    case CURLE_SSL_CERTPROBLEM:
        return Q2_ERR_PERMISSION;
    case CURLE_URL_MALFORMAT:
    case CURLE_UNSUPPORTED_PROTOCOL:
        return Q2_ERR_ARGUMENT;
    case CURLE_OUT_OF_MEMORY:
        return Q2_ERR_MEMORY;
    case CURLE_FILESIZE_EXCEEDED:
        return Q2_ERR_LIMIT;
    case CURLE_ABORTED_BY_CALLBACK:
        return Q2_ERR_CANCELLED;
    default:
        return Q2_ERR_IO;
    }
}
static int progress(void *ctx, curl_off_t total, curl_off_t got, curl_off_t upload, curl_off_t sent) {
    (void)total;
    (void)got;
    (void)upload;
    (void)sent;
    job_t *j = ctx;
    if (atomic_load_explicit(&j->cancelled, memory_order_relaxed)) {
        j->result = Q2_ERR_CANCELLED;
        return 1;
    }
    if (q2_pc_now_ms() >= j->deadline) {
        j->result = Q2_ERR_TIMEOUT;
        return 1;
    }
    return 0;
}
static size_t receive(char *data, size_t unit, size_t count, void *ctx) {
    job_t *j = ctx;
    if (unit && count > SIZE_MAX / unit) {
        j->result = Q2_ERR_LIMIT;
        return 0;
    }
    size_t size = unit * count;
    if (progress(j, 0, 0, 0, 0))
        return 0;
    if (size > Q2_NETWORK_BODY_MAX - j->size) {
        j->result = Q2_ERR_LIMIT;
        return 0;
    }
    /* Opaque bytes serve both text requests and native cover downloads; Kernel validates Lua text. */
    memcpy(j->body + j->size, data, size);
    j->size += size;
    j->body[j->size] = 0;
    return size;
}
static void *worker(void *ctx) {
    job_t *j = ctx;
    CURL *curl = NULL;
    struct curl_slist *headers = NULL;
    /* NOSIGNAL avoids process-wide signal handlers. Block SIGPIPE only in this disposable worker. */
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGPIPE);
    if (pthread_sigmask(SIG_BLOCK, &signals, NULL) != 0) {
        j->result = Q2_ERR_IO;
        goto done;
    }
    if (progress(j, 0, 0, 0, 0))
        goto done;
    curl = curl_easy_init();
    if (!curl) {
        j->result = Q2_ERR_MEMORY;
        goto done;
    }
    uint64_t now = q2_pc_now_ms();
    if (now >= j->deadline) {
        j->result = Q2_ERR_TIMEOUT;
        goto done;
    }
    /* Every handle is private to one worker, including all configuration and cleanup. */
#define SET(option, value)                                                                                   \
    do {                                                                                                     \
        CURLcode e = curl_easy_setopt(curl, option, value);                                                  \
        if (e != CURLE_OK) {                                                                                 \
            j->result = failure(e);                                                                          \
            goto done;                                                                                       \
        }                                                                                                    \
    } while (0)
    SET(CURLOPT_URL, j->url);
    SET(CURLOPT_NOSIGNAL, 1L);
    SET(CURLOPT_TIMEOUT_MS, (long)(j->deadline - now));
    SET(CURLOPT_CONNECTTIMEOUT_MS, (long)(j->deadline - now));
    SET(CURLOPT_NOPROGRESS, 0L);
    SET(CURLOPT_XFERINFOFUNCTION, progress);
    SET(CURLOPT_XFERINFODATA, j);
    SET(CURLOPT_WRITEFUNCTION, receive);
    SET(CURLOPT_WRITEDATA, j);
#if LIBCURL_VERSION_NUM >= 0x075500
    SET(CURLOPT_PROTOCOLS_STR, "http,https");
#else
    SET(CURLOPT_PROTOCOLS, (long)(CURLPROTO_HTTP | CURLPROTO_HTTPS));
#endif
    SET(CURLOPT_FOLLOWLOCATION, 0L);
    SET(CURLOPT_DISALLOW_USERNAME_IN_URL, 1L);
    SET(CURLOPT_NETRC, (long)CURL_NETRC_IGNORED);
    SET(CURLOPT_HTTPAUTH, (long)CURLAUTH_NONE);
    SET(CURLOPT_PROXY, "");
    SET(CURLOPT_SSL_VERIFYPEER, 1L);
    SET(CURLOPT_SSL_VERIFYHOST, 2L);
    SET(CURLOPT_USERAGENT, "Q2-Network/1.0");
    SET(CURLOPT_ACCEPT_ENCODING, "");
    SET(CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)Q2_NETWORK_BODY_MAX);
    /* No cookie engine, browser auth, environment proxy or redirect side effects. */
    if (j->post) {
        headers = curl_slist_append(NULL, "Content-Type: application/json");
        if (!headers) {
            j->result = Q2_ERR_MEMORY;
            goto done;
        }
        struct curl_slist *more = curl_slist_append(headers, "Expect:");
        if (!more) {
            j->result = Q2_ERR_MEMORY;
            goto done;
        }
        headers = more;
        SET(CURLOPT_HTTPHEADER, headers);
        SET(CURLOPT_POST, 1L);
        SET(CURLOPT_POSTFIELDS, j->upload);
        SET(CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)j->upload_size);
    }
    CURLcode code = curl_easy_perform(curl);
    if (j->result == Q2_OK)
        j->result = failure(code);
    if (j->result == Q2_OK) {
        long status = 0;
        code = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        j->result = failure(code);
        if (j->result == Q2_OK && (status < 100 || status > 599))
            j->result = Q2_ERR_FORMAT;
        j->code = (unsigned)status;
    }
#undef SET
done:
    if (curl)
        curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    /* Publish immutable response bytes only after transport cleanup; owner reads with acquire. */
    atomic_store_explicit(&j->complete, true, memory_order_release);
    return NULL;
}
static void reap(q2_pc_network_t *s) {
    for (unsigned i = 0; i < Q2_NETWORK_MAX; i++) {
        job_t *j = s->jobs[i];
        if (j && j->detached && atomic_load_explicit(&j->complete, memory_order_acquire) &&
            pthread_tryjoin_np(j->thread, NULL) == 0) {
            free(j);
            s->jobs[i] = NULL;
        }
    }
}
static bool owned(q2_pc_network_t *s, void *job) {
    if (!s || !job)
        return false;
    for (unsigned i = 0; i < Q2_NETWORK_MAX; i++)
        if (s->jobs[i] == job)
            return true;
    return false;
}
static q2_result_t begin_request(void *ctx, const char *url, const char *body, size_t size, unsigned timeout,
                                 void **out) {
    if (!ctx || !out)
        return Q2_ERR_ARGUMENT;
    *out = NULL;
    if (!url || !*url || strlen(url) >= Q2_TEXT_MAX || size > Q2_NETWORK_UPLOAD_MAX || (!body && size) ||
        (body && memchr(body, 0, size)) || timeout < 100 || timeout > 10000)
        return Q2_ERR_ARGUMENT;
    q2_pc_network_t *s = ctx;
    reap(s);
    unsigned slot = 0;
    while (slot < Q2_NETWORK_MAX && s->jobs[slot])
        slot++;
    if (slot == Q2_NETWORK_MAX)
        return Q2_ERR_BUSY;
    job_t *j = calloc(1, sizeof(*j));
    if (!j)
        return Q2_ERR_MEMORY;
    atomic_init(&j->cancelled, false);
    atomic_init(&j->complete, false);
    strcpy(j->url, url);
    j->post = body != NULL;
    if (size)
        memcpy(j->upload, body, size);
    j->upload_size = size;
    j->deadline = q2_pc_now_ms() + timeout;
    if (pthread_create(&j->thread, NULL, worker, j) != 0) {
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
    if (!body)
        return Q2_ERR_ARGUMENT;
    return begin_request(ctx, url, body, size, timeout, out);
}
static q2_result_t poll_job(void *ctx, void *job, q2_network_response_t *out) {
    if (!out || !owned(ctx, job))
        return Q2_ERR_ARGUMENT;
    job_t *j = job;
    if (!atomic_load_explicit(&j->complete, memory_order_acquire))
        return Q2_ERR_BUSY;
    if (j->result == Q2_OK)
        *out = (q2_network_response_t){j->code, j->body, j->size};
    return j->result;
}
static q2_result_t release_job(void *ctx, void **job) {
    if (!ctx || !job || (*job && !owned(ctx, *job)))
        return Q2_ERR_ARGUMENT;
    if (*job) {
        job_t *j = *job;
        j->detached = true;
        atomic_store_explicit(&j->cancelled, true, memory_order_relaxed);
        *job = NULL;
    }
    reap(ctx);
    return Q2_OK;
}
q2_result_t q2_pc_network_create(q2_pc_network_t **out, q2_network_backend_t *backend) {
    if (!out || !backend)
        return Q2_ERR_ARGUMENT;
    *out = NULL;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        return Q2_ERR_IO;
    /* A synchronous resolver with NOSIGNAL cannot honor DNS deadlines. Fail explicitly in that build. */
    const curl_version_info_data *version = curl_version_info(CURLVERSION_NOW);
    if (!(version->features & CURL_VERSION_ASYNCHDNS) || !(version->features & CURL_VERSION_SSL)) {
        curl_global_cleanup();
        return Q2_ERR_UNSUPPORTED;
    }
    q2_pc_network_t *s = calloc(1, sizeof(*s));
    if (!s) {
        curl_global_cleanup();
        return Q2_ERR_MEMORY;
    }
    *out = s;
    *backend = (q2_network_backend_t){2, sizeof(*backend), s, begin, poll_job, release_job, post_request};
    return Q2_OK;
}
q2_result_t q2_pc_network_destroy(q2_pc_network_t **ref) {
    if (!ref)
        return Q2_ERR_ARGUMENT;
    if (!*ref)
        return Q2_OK;
    q2_pc_network_t *s = *ref;
    for (unsigned i = 0; i < Q2_NETWORK_MAX; i++) {
        if (!s->jobs[i])
            continue;
        s->jobs[i]->detached = true;
        atomic_store_explicit(&s->jobs[i]->cancelled, true, memory_order_relaxed);
    }
    uint64_t deadline = q2_pc_now_ms() + 11000;
    for (;;) {
        reap(s);
        bool pending = false;
        for (unsigned i = 0; i < Q2_NETWORK_MAX; i++)
            pending |= s->jobs[i] != NULL;
        if (!pending)
            break;
        if (q2_pc_now_ms() >= deadline)
            return Q2_ERR_BUSY;
        q2_pc_sleep_ms(5);
    }
    free(s);
    *ref = NULL;
    curl_global_cleanup();
    return Q2_OK;
}
