#if defined(__linux__) || defined(__APPLE__)

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <strings.h>
#include <curl/curl.h>
#include "Engine/Source/BlizzardCore/Source/System/URL/URL.h"
#include "Engine/Source/BlizzardCore/Source/System/String/String.h"
#include "Engine/Source/BlizzardCore/Source/System/Thread/Thread.h"

struct CurlSession {
    CURL* m_curl;
    curl_slist* m_headers;
    std::string m_url;
    std::string m_postData;
    std::string m_lastModified;
    int64_t m_contentLength;
    int32_t m_statusCode;
    bool m_started;
    bool m_wantData;
};

static CurlSession* Session(Blizzard::URL::HttpRequest* request) {
    return static_cast<CurlSession*>(request->m_session);
}

static bool BeginResponse(Blizzard::URL::HttpRequest* request) {
    CurlSession* session = Session(request);
    session->m_started = true;

    long statusCode = 0;
    curl_easy_getinfo(session->m_curl, CURLINFO_RESPONSE_CODE, &statusCode);
    session->m_statusCode = static_cast<int32_t>(statusCode);
    request->m_callback->GetHTTPStatusCode(session->m_statusCode);

    if (session->m_statusCode < 200 || session->m_statusCode >= 300) {
        return true;
    }

    if (!session->m_lastModified.empty()) {
        request->m_callback->GetLastModified(session->m_lastModified.c_str());
    }

    if (session->m_contentLength >= 0) {
        request->m_callback->GetContentLength(session->m_contentLength);
    }

    session->m_wantData = request->m_callback->WantData();

    if (!session->m_wantData) {
        request->m_done = true;
    }

    return session->m_wantData;
}

static size_t HeaderCallback(char* data, size_t size, size_t count, void* param) {
    Blizzard::URL::HttpRequest* request = static_cast<Blizzard::URL::HttpRequest*>(param);
    CurlSession* session = Session(request);
    size_t length = size * count;

    if (!request->m_connected) {
        request->m_callback->ConnectedOK();
        request->m_connected = true;
    }

    std::string line(data, length);

    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
        line.pop_back();
    }

    if (!strncasecmp(line.c_str(), "HTTP/", 5)) {
        session->m_lastModified.clear();
        session->m_contentLength = -1;
    } else if (!strncasecmp(line.c_str(), "Last-Modified:", 14)) {
        session->m_lastModified = line.substr(line.find_first_not_of(' ', 14));
    } else if (!strncasecmp(line.c_str(), "Content-Length:", 15)) {
        session->m_contentLength = strtoll(line.c_str() + 15, nullptr, 10);
    }

    return length;
}

static size_t WriteCallback(char* data, size_t size, size_t count, void* param) {
    Blizzard::URL::HttpRequest* request = static_cast<Blizzard::URL::HttpRequest*>(param);
    CurlSession* session = Session(request);
    size_t length = size * count;

    if (!session->m_started && !BeginResponse(request)) {
        return 0;
    }

    if (session->m_statusCode < 200 || session->m_statusCode >= 300) {
        return length;
    }

    if (!request->m_callback->ReceivedData(data, static_cast<int32_t>(length))) {
        request->m_status = Blizzard::URL::DownloadCallback::DOWNLOAD_ABORTED;
        request->m_done = true;
        return 0;
    }

    if (request->m_callback->Stop()) {
        request->m_status = Blizzard::URL::DownloadCallback::DOWNLOAD_STOPPED;
        request->m_done = true;
        return 0;
    }

    return length;
}

static uint32_t ReadResponseData(void* param) {
    Blizzard::URL::HttpRequest* request = static_cast<Blizzard::URL::HttpRequest*>(param);
    CurlSession* session = Session(request);
    CURLcode result = curl_easy_perform(session->m_curl);

    if (result == CURLE_OK && !session->m_started) {
        BeginResponse(request);
    }

    if (result != CURLE_OK && !request->m_done) {
        switch (result) {
            case CURLE_OPERATION_TIMEDOUT:
                request->m_status = Blizzard::URL::DownloadCallback::DOWNLOAD_TIMEOUT;
                break;

            case CURLE_COULDNT_RESOLVE_HOST:
            case CURLE_COULDNT_RESOLVE_PROXY:
                request->m_status = Blizzard::URL::DownloadCallback::DOWNLOAD_NAME_NOT_RESOLVED;
                break;

            case CURLE_COULDNT_CONNECT:
                request->m_status = Blizzard::URL::DownloadCallback::DOWNLOAD_CANNOT_CONNECT;
                break;

            default:
                request->m_status = Blizzard::URL::DownloadCallback::DOWNLOAD_FAILED;
                break;
        }
    } else if (session->m_statusCode < 200 || session->m_statusCode >= 300) {
        switch (session->m_statusCode) {
            case 304:
                request->m_status = Blizzard::URL::DownloadCallback::DOWNLOAD_NOT_MODIFIED;
                break;

            case 400:
                request->m_status = Blizzard::URL::DownloadCallback::DOWNLOAD_BAD_REQUEST;
                break;

            case 403:
                request->m_status = Blizzard::URL::DownloadCallback::DOWNLOAD_FORBIDDEN;
                break;

            case 404:
                request->m_status = Blizzard::URL::DownloadCallback::DOWNLOAD_NOT_FOUND;
                break;

            default:
                request->m_status = Blizzard::URL::DownloadCallback::DOWNLOAD_FAILED;
                break;
        }
    }

    request->m_done = true;
    request->m_callback->Completed(request->m_status);
    delete request;

    return 0;
}

Blizzard::URL::HttpRequest::~HttpRequest() {
    CurlSession* session = Session(this);

    if (session) {
        curl_slist_free_all(session->m_headers);
        curl_easy_cleanup(session->m_curl);
        delete session;
    }
}

Blizzard::URL::HttpRequest::HttpRequest() {
    this->m_connectEvent = nullptr;
    this->m_requestEvent = nullptr;
    this->m_completeEvent = nullptr;
    this->m_dataEvent = nullptr;
    this->m_session = nullptr;
    this->m_connect = nullptr;
    this->m_request = nullptr;
    this->m_state = 0;
    this->m_done = false;
    this->m_connected = false;
    this->m_callback = nullptr;
    this->m_error = 0;
    this->m_status = Blizzard::URL::DownloadCallback::DOWNLOAD_SUCCESS;
}

bool Blizzard::URL::DownloadURL(const char* url, DownloadCallback* callback) {
    static const CURLcode s_globalInit = curl_global_init(CURL_GLOBAL_DEFAULT);

    if (s_globalInit != CURLE_OK) {
        return false;
    }

    CURL* curl = curl_easy_init();

    if (!curl) {
        return false;
    }

    HttpRequest* request = new HttpRequest();
    CurlSession* session = new CurlSession();
    session->m_curl = curl;
    session->m_headers = nullptr;
    session->m_url = url;
    session->m_contentLength = -1;
    session->m_statusCode = 0;
    session->m_started = false;
    session->m_wantData = false;
    request->m_session = session;
    request->m_callback = callback;
    request->m_state = 1;

    curl_easy_setopt(curl, CURLOPT_URL, session->m_url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Blizzard Web Client");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, request);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, request);

    int32_t timeout = callback->GetTimeout();

    if (timeout > 0) {
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(timeout));
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, static_cast<long>((timeout + 999) / 1000));
    }

    const char* postData = callback->GetPostData();
    char header[1024];

    if (postData) {
        session->m_postData = postData;
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, session->m_postData.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(session->m_postData.size()));
        Blizzard::String::Format(header, sizeof(header), "Content-Type: %s", callback->GetPostContentType() ? callback->GetPostContentType() : "text/html");
        session->m_headers = curl_slist_append(session->m_headers, header);
    } else if (!callback->WantData()) {
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    }

    const char* ifModifiedSince = callback->WantIfModifiedSince();

    if (ifModifiedSince) {
        Blizzard::String::Format(header, sizeof(header), "If-Modified-Since: %s", ifModifiedSince);
        session->m_headers = curl_slist_append(session->m_headers, header);
    }

    DownloadCallback::ByteRange range = callback->GetByteRange();

    if (range.m_start != -1 || range.m_end != -1) {
        Blizzard::String::Format(header, sizeof(header), "Range: bytes=%lld-%lld", static_cast<long long>(range.m_start), static_cast<long long>(range.m_end));
        session->m_headers = curl_slist_append(session->m_headers, header);
    }

    if (session->m_headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, session->m_headers);
    }

    DownloadCallback::Cookie cookie = callback->GetCookie();

    if (cookie.m_name && *cookie.m_name && cookie.m_value) {
        std::string value = std::string(cookie.m_name) + "=" + cookie.m_value;
        curl_easy_setopt(curl, CURLOPT_COOKIE, value.c_str());
    }

    if (!Blizzard::Thread::CreateDetachedFromPool(nullptr, ReadResponseData, request, 0, 0)) {
        delete request;
        return false;
    }

    return true;
}

#endif
