#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "audio_win32.h"
#include <mmsystem.h>
#include <objbase.h>
#include <stdlib.h>
#include <string.h>
#include <winhttp.h>
typedef struct {
    HANDLE thread;
    volatile LONG cancelled, paused, state, position, duration, error;
    q2_result_t result;
    ULONGLONG deadline;
    char url[Q2_AUDIO_URL_MAX + 1], magic[12];
    WCHAR path[MAX_PATH];
    size_t size;
} job_t;
struct q2_pc_audio {
    job_t *job;
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
    if (remaining > 5000)
        remaining = 5000;
    if (!WinHttpSetTimeouts(session, remaining, remaining, remaining, remaining)) {
        j->result = failure();
        return false;
    }
    return true;
}
static q2_result_t download(job_t *j) {
    HANDLE file = INVALID_HANDLE_VALUE;
    HINTERNET session = NULL, connection = NULL, request = NULL;
    WCHAR url[(Q2_AUDIO_URL_MAX + 1)], host[(Q2_AUDIO_URL_MAX + 1)], path[(Q2_AUDIO_URL_MAX + 1)],
        extra[(Q2_AUDIO_URL_MAX + 1)], target[(Q2_AUDIO_URL_MAX + 1) * 2];
    URL_COMPONENTS parts = {.dwStructSize = sizeof(parts),
                            .lpszHostName = host,
                            .dwHostNameLength = (Q2_AUDIO_URL_MAX + 1),
                            .lpszUrlPath = path,
                            .dwUrlPathLength = (Q2_AUDIO_URL_MAX + 1),
                            .lpszExtraInfo = extra,
                            .dwExtraInfoLength = (Q2_AUDIO_URL_MAX + 1)};
    j->result = Q2_ERR_IO;
    WCHAR folder[MAX_PATH];
    DWORD folder_length = GetTempPathW(MAX_PATH, folder);
    if (!folder_length || folder_length >= MAX_PATH || !GetTempFileNameW(folder, L"q2a", 0, j->path))
        return Q2_ERR_IO;
    file = CreateFileW(j->path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (file == INVALID_HANDLE_VALUE)
        goto done;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, j->url, -1, url, (Q2_AUDIO_URL_MAX + 1)) ||
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
    request =
        WinHttpOpenRequest(connection, L"GET", target, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
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
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
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
    if (status != 200) {
        j->result = status == 401 || status == 403 ? Q2_ERR_PERMISSION : Q2_ERR_IO;
        goto done;
    }
    for (;;) {
        if (!advance(j, request))
            goto done;
        char chunk[16384];
        DWORD got = 0;
        if (!WinHttpReadData(request, chunk, sizeof(chunk), &got)) {
            j->result = failure();
            goto done;
        }
        if (!got)
            break;
        if (j->size + got > (32u * 1024u * 1024u)) {
            j->result = Q2_ERR_LIMIT;
            goto done;
        }
        if (j->size < 12) {
            size_t n = 12 - j->size;
            if (n > got)
                n = got;
            memcpy(j->magic + j->size, chunk, n);
        }
        DWORD written = 0;
        if (!WriteFile(file, chunk, got, &written, NULL) || written != got) {
            j->result = Q2_ERR_IO;
            goto done;
        }
        j->size += got;
    }
    j->result = Q2_OK;
done:
    // Synchronous WinHTTP handles are exclusively worker-owned, including cancellation cleanup.
    if (request)
        WinHttpCloseHandle(request);
    if (connection)
        WinHttpCloseHandle(connection);
    if (session)
        WinHttpCloseHandle(session);
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    return j->result;
}
static DWORD WINAPI worker(void *context) {
    job_t *j = context;
    MCIDEVICEID device = 0;
    // MCI MPEG decoder requires an STA; keep its COM/message lifetime on this worker.
    HRESULT com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    q2_result_t result = download(j);
    if (result != Q2_OK || InterlockedCompareExchange(&j->cancelled, 0, 0))
        goto done;
    bool wave = !memcmp(j->magic, "RIFF", 4) && !memcmp(j->magic + 8, "WAVE", 4);
    bool mp3 = !memcmp(j->magic, "ID3", 3) ||
               ((unsigned char)j->magic[0] == 255 && ((unsigned char)j->magic[1] & 224) == 224);
    if (!wave && !mp3) {
        result = Q2_ERR_FORMAT;
        goto done;
    }
    MCI_OPEN_PARMSW open = {0};
    open.lpstrDeviceType = wave ? L"waveaudio" : L"mpegvideo";
    open.lpstrElementName = j->path;
    result = Q2_ERR_IO;
    if (mciSendCommandW(0, MCI_OPEN, MCI_OPEN_TYPE | MCI_OPEN_ELEMENT, (DWORD_PTR)&open))
        goto done;
    device = open.wDeviceID;
    MCI_SET_PARMS format = {.dwTimeFormat = MCI_FORMAT_MILLISECONDS};
    if (mciSendCommandW(device, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&format))
        goto done;
    MCI_STATUS_PARMS status = {.dwItem = MCI_STATUS_LENGTH};
    if (mciSendCommandW(device, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&status))
        goto done;
    InterlockedExchange(&j->duration, (LONG)status.dwReturn);
    MCI_PLAY_PARMS play = {0};
    if (mciSendCommandW(device, MCI_PLAY, 0, (DWORD_PTR)&play))
        goto done;
    bool paused = false;
    InterlockedExchange(&j->state, Q2_AUDIO_PLAYING);
    for (;;) {
        if (InterlockedCompareExchange(&j->cancelled, 0, 0)) {
            result = Q2_ERR_CANCELLED;
            break;
        }
        bool want_pause = InterlockedCompareExchange(&j->paused, 0, 0) != 0;
        if (want_pause != paused) {
            if (mciSendCommandW(device, want_pause ? MCI_PAUSE : MCI_PLAY, 0, (DWORD_PTR)&play))
                break;
            paused = want_pause;
            InterlockedExchange(&j->state, paused ? Q2_AUDIO_PAUSED : Q2_AUDIO_PLAYING);
        }
        status.dwItem = MCI_STATUS_POSITION;
        if (mciSendCommandW(device, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&status))
            break;
        InterlockedExchange(&j->position, (LONG)status.dwReturn);
        status.dwItem = MCI_STATUS_MODE;
        if (mciSendCommandW(device, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&status))
            break;
        if (!paused && status.dwReturn == MCI_MODE_STOP) {
            result = Q2_OK;
            InterlockedExchange(&j->state, Q2_AUDIO_ENDED);
            break;
        }
        MSG message;
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(40);
    }
done:
    if (device)
        mciSendCommandW(device, MCI_CLOSE, 0, 0);
    if (j->path[0])
        DeleteFileW(j->path);
    if (SUCCEEDED(com))
        CoUninitialize();
    if (result != Q2_OK && result != Q2_ERR_CANCELLED) {
        InterlockedExchange(&j->error, result);
        InterlockedExchange(&j->state, Q2_AUDIO_ERROR);
    }
    return 0;
}
static bool reap(q2_pc_audio_t *s) {
    if (!s->job)
        return true;
    if (WaitForSingleObject(s->job->thread, 0) != WAIT_OBJECT_0)
        return false;
    CloseHandle(s->job->thread);
    free(s->job);
    s->job = NULL;
    return true;
}
static q2_result_t play_url(void *ctx, const char *url) {
    q2_pc_audio_t *s = ctx;
    if (!reap(s))
        return Q2_ERR_BUSY;
    job_t *j = calloc(1, sizeof(*j));
    if (!j)
        return Q2_ERR_MEMORY;
    strcpy(j->url, url);
    j->deadline = GetTickCount64() + 30000;
    j->state = Q2_AUDIO_BUFFERING;
    j->thread = CreateThread(NULL, 0, worker, j, 0, NULL);
    if (!j->thread) {
        free(j);
        return Q2_ERR_IO;
    }
    s->job = j;
    return Q2_OK;
}
static q2_result_t pause_job(void *ctx, bool paused) {
    q2_pc_audio_t *s = ctx;
    if (!s->job)
        return Q2_ERR_STATE;
    InterlockedExchange(&s->job->paused, paused);
    return Q2_OK;
}
static q2_result_t stop_job(void *ctx) {
    q2_pc_audio_t *s = ctx;
    if (s->job)
        InterlockedExchange(&s->job->cancelled, 1);
    return Q2_OK;
}
static q2_result_t snapshot(void *ctx, q2_audio_snapshot_t *out) {
    q2_pc_audio_t *s = ctx;
    *out = (q2_audio_snapshot_t){.state = Q2_AUDIO_IDLE};
    if (s->job) {
        out->state = (q2_audio_state_t)InterlockedCompareExchange(&s->job->state, 0, 0);
        out->position_ms = (uint32_t)InterlockedCompareExchange(&s->job->position, 0, 0);
        out->duration_ms = (uint32_t)InterlockedCompareExchange(&s->job->duration, 0, 0);
        out->error = (q2_result_t)InterlockedCompareExchange(&s->job->error, 0, 0);
    }
    return Q2_OK;
}
q2_result_t q2_pc_audio_create(q2_pc_audio_t **out, q2_audio_backend_t *backend) {
    if (!out || !backend)
        return Q2_ERR_ARGUMENT;
    *out = calloc(1, sizeof(**out));
    if (!*out)
        return Q2_ERR_MEMORY;
    *backend = (q2_audio_backend_t){1, sizeof(*backend), *out, play_url, pause_job, stop_job, snapshot};
    return Q2_OK;
}
q2_result_t q2_pc_audio_destroy(q2_pc_audio_t **s) {
    if (!s)
        return Q2_ERR_ARGUMENT;
    if (!*s)
        return Q2_OK;
    stop_job(*s);
    if ((*s)->job && WaitForSingleObject((*s)->job->thread, 15000) != WAIT_OBJECT_0)
        return Q2_ERR_BUSY;
    reap(*s);
    free(*s);
    *s = NULL;
    return Q2_OK;
}
