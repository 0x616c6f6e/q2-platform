#include "network.h"
#include "pc_time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                                  \
            exit(1);                                                                                         \
        }                                                                                                    \
    } while (0)
static q2_network_response_t wait_response(q2_network_backend_t *b, void *job) {
    uint64_t end = q2_pc_now_ms() + 5000;
    q2_network_response_t response = {0};
    q2_result_t e;
    do {
        e = b->poll(b->context, job, &response);
        if (e == Q2_ERR_BUSY)
            q2_pc_sleep_ms(2);
        CHECK(q2_pc_now_ms() < end);
    } while (e == Q2_ERR_BUSY);
    CHECK(e == Q2_OK && response.code == 200);
    return response;
}
int main(int argc, char **argv) {
    CHECK(argc == 2);
    q2_pc_network_t *network = NULL;
    q2_network_backend_t b;
    CHECK(q2_pc_network_create(&network, &b) == Q2_OK);
    char url[Q2_TEXT_MAX];
    void *jobs[Q2_NETWORK_MAX] = {0}, *extra = NULL;
    for (unsigned i = 0; i < Q2_NETWORK_MAX; i++) {
        snprintf(url, sizeof(url), "%s/health", argv[1]);
        CHECK(b.start(b.context, url, 3000, &jobs[i]) == Q2_OK);
        memset(url, 'X', strlen(url)); /* A worker must use its copy, not this caller buffer. */
    }
    snprintf(url, sizeof(url), "%s/health", argv[1]);
    CHECK(b.start(b.context, url, 3000, &extra) == Q2_ERR_BUSY && !extra);
    for (unsigned i = 0; i < Q2_NETWORK_MAX; i++) {
        q2_network_response_t response = wait_response(&b, jobs[i]);
        CHECK(response.size == 10 && !memcmp(response.body, "diagnostic", 10));
    }
    for (unsigned i = 0; i < Q2_NETWORK_MAX; i++)
        CHECK(b.release(b.context, &jobs[i]) == Q2_OK && !jobs[i]);
    snprintf(url, sizeof(url), "%s/echo", argv[1]);
    char upload[] = "{\"message\":\"copy\"}";
    uint64_t end = q2_pc_now_ms() + 3000;
    q2_result_t e;
    do {
        e = b.post(b.context, url, upload, strlen(upload), 3000, &extra);
        if (e == Q2_ERR_BUSY)
            q2_pc_sleep_ms(2);
        CHECK(q2_pc_now_ms() < end);
    } while (e == Q2_ERR_BUSY);
    CHECK(e == Q2_OK);
    memset(upload, 'X', strlen(upload));
    q2_network_response_t response = wait_response(&b, extra);
    CHECK(response.size == sizeof(upload) - 1 &&
          !memcmp(response.body, "{\"message\":\"copy\"}", response.size));
    CHECK(b.release(b.context, &extra) == Q2_OK && !extra);
    CHECK(b.release(b.context, &extra) == Q2_OK); /* NULL release is idempotent. */
    q2_pc_sleep_ms(5);
    snprintf(url, sizeof(url), "%s/binary", argv[1]);
    CHECK(b.start(b.context, url, 3000, &extra) == Q2_OK);
    response = wait_response(&b, extra);
    CHECK(response.size == 3 && !memcmp(response.body, "a\0b", 3));
    CHECK(b.release(b.context, &extra) == Q2_OK);
    q2_pc_sleep_ms(5);
    snprintf(url, sizeof(url), "%s/slow", argv[1]);
    CHECK(b.start(b.context, url, 3000, &extra) == Q2_OK);
    q2_pc_sleep_ms(40);
    uint64_t started = q2_pc_now_ms();
    CHECK(b.release(b.context, &extra) == Q2_OK && !extra);
    CHECK(q2_pc_now_ms() - started < 200); /* Release cannot wait for the 800-ms server response. */
    CHECK(q2_pc_network_destroy(&network) == Q2_OK && !network);
    CHECK(q2_pc_network_destroy(&network) == Q2_OK);
    puts("PASS: curl parallel quota, input ownership, slot reuse, opaque bytes, nonblocking cancel and "
         "teardown");
    return 0;
}
