#if defined(__linux__) || defined(__APPLE__)

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <strings.h>
#include "Engine/Source/BlizzardCore/Source/System/Lock/Lock.h"
#include "Engine/Source/BlizzardCore/Source/System/Process/Process.h"
#include "Engine/Source/BlizzardCore/Include/Blizzard/Util/Digest.h"
#include "Engine/Source/BlizzardCore/Include/Blizzard/Util/MD5.h"
#include "Engine/Source/BlizzardCore/Source/Packages/Streaming/Manifests.h"
#include "Engine/Source/BlizzardCore/Source/Packages/Streaming/Streaming.h"
#include "Engine/Source/BlizzardCore/Source/System/Debug/ErrorStack.h"
#include "Engine/Source/BlizzardCore/Source/System/File/File.h"
#include "Engine/Source/BlizzardCore/Source/System/Log/Log.h"
#include "Engine/Source/BlizzardCore/Source/System/String/String.h"
#include "Engine/Source/BlizzardCore/Source/System/Time/Time.h"
#include "Engine/Source/BlizzardCore/Source/System/URL/URL.h"
#include "Engine/Source/BlizzardCore/Source/System/Using/Using.h"

static Blizzard::Using::Use s_useFile("Blizzard::File");

static bool s_transportShouldBlock = true;

static double s_throttleScale = 0.0085;

static uint32_t s_transportTimeout = 10000;

static std::string s_cookieName;

static std::string s_cookieValue;

static int64_t s_throttleBias;

static int64_t s_throttleDebt;

int32_t Blizzard::Streaming::Transport::AdjustRange(int64_t* start, int64_t* end, int32_t md5Size, int64_t fileSize) {
    int64_t alignedStart = *start - *start % md5Size;
    int64_t alignedEnd = md5Size + *end - (md5Size + *end) % md5Size - 1;
    int64_t firstChunk = alignedStart / md5Size;
    int64_t lastChunk = (alignedEnd + 1) / md5Size - 1;

    if (alignedEnd > fileSize) {
        alignedEnd = fileSize - 1;
    }

    int32_t skip = static_cast<int32_t>(32 * firstChunk);
    int64_t original = *start;
    *start = skip + alignedStart;
    *end = alignedEnd + 32 * (lastChunk + 1);

    return static_cast<int32_t>(skip + original - *start);
}

void Blizzard::Streaming::TransportHttpCallback::Completed(Blizzard::URL::DownloadCallback::Status status) {
    this->m_status = status;

    switch (status) {
        case Blizzard::URL::DownloadCallback::DOWNLOAD_SUCCESS:
        case Blizzard::URL::DownloadCallback::DOWNLOAD_STOPPED:
            this->m_success = true;
            break;

        case Blizzard::URL::DownloadCallback::DOWNLOAD_FAILED:
        case Blizzard::URL::DownloadCallback::DOWNLOAD_TIMEOUT:
        case Blizzard::URL::DownloadCallback::DOWNLOAD_NOT_FOUND:
        case Blizzard::URL::DownloadCallback::DOWNLOAD_NOT_MODIFIED:
        case Blizzard::URL::DownloadCallback::DOWNLOAD_ABORTED:
        case Blizzard::URL::DownloadCallback::DOWNLOAD_FORBIDDEN:
        case Blizzard::URL::DownloadCallback::DOWNLOAD_NAME_NOT_RESOLVED:
        case Blizzard::URL::DownloadCallback::DOWNLOAD_CANNOT_CONNECT:
        case Blizzard::URL::DownloadCallback::DOWNLOAD_BAD_REQUEST:
        case Blizzard::URL::DownloadCallback::DOWNLOAD_UNKNOWN_ERROR:
            this->m_success = false;
            break;
    }

    Blizzard::Lock::EventSet(this->m_event);
}

void Blizzard::Streaming::Transport::THROTTLE::Wait() {
    int64_t delta = s_throttleBias + this->m_start + static_cast<int64_t>(this->m_bytes * s_throttleScale) - Blizzard::Time::GetTimestamp();
    s_throttleDebt += delta;

    if (s_throttleDebt < 0 || s_throttleDebt <= 1000000) {
        return;
    }

    do {
        Blizzard::Time::Timestamp start = Blizzard::Time::GetTimestamp();
        int64_t milliseconds = s_throttleDebt / 1000000;
        int32_t sleep = milliseconds <= 100 ? static_cast<int32_t>(milliseconds) : 100;

        if (sleep > 0) {
            Blizzard::Process::Sleep(sleep);
        }

        s_throttleDebt += start - Blizzard::Time::GetTimestamp();
    } while (s_throttleDebt > 1000000);
}

Blizzard::Streaming::TransportHttpCallback::~TransportHttpCallback() {
    Blizzard::Lock::EventDestroy(this->m_event);
}

int32_t Blizzard::Streaming::TransportHttpCallback::GetTimeout() {
    return this->m_timeout;
}

Blizzard::URL::DownloadCallback::ByteRange Blizzard::Streaming::TransportHttpCallback::GetByteRange() {
    ByteRange range;
    range.m_start = this->m_rangeStart;
    range.m_end = this->m_rangeEnd;
    return range;
}

bool Blizzard::Streaming::TransportHttpCallback::ShouldBlock() {
    return s_transportShouldBlock;
}

void Blizzard::Streaming::TransportHttpCallback::SetByteRange(int64_t start, int64_t end) {
    this->m_rangeStart = start;
    this->m_rangeEnd = end;
}

void Blizzard::Streaming::TransportHttpCallback::SetTimeOutValue(uint32_t timeout) {
    this->m_timeout = timeout;
}

bool Blizzard::Streaming::TransportHttpCallback::Stop() {
    return this->m_stop;
}

Blizzard::URL::DownloadCallback::Cookie Blizzard::Streaming::TransportHttpCallback::GetCookie() {
    Cookie cookie;
    cookie.m_name = this->m_cookieName.c_str();
    cookie.m_value = this->m_cookieValue.c_str();
    return cookie;
}

bool Blizzard::Streaming::Transport::ReadFile(Manifests::TransportItem* transport, std::string url, int32_t maxRetry, void* data, int64_t fileSize, int64_t offset, int32_t size) {
    Blizzard::File::StreamRecord* file;

    if (!Blizzard::File::Open(url.c_str() + 7, 4101, file)) {
        return false;
    }

    int32_t count = size;

    if (Blizzard::File::Read(file, data, offset, &count) && count == size) {
        Blizzard::File::Close(file);
        return true;
    }

    Blizzard::File::Close(file);
    return false;
}

Blizzard::Streaming::TransportHttpCallback::TransportHttpCallback(uint32_t size) {
    this->m_cookieName.erase(this->m_cookieName.begin(), this->m_cookieName.end());
    this->m_cookieValue.erase(this->m_cookieValue.begin(), this->m_cookieValue.end());
    this->m_data.reserve(size);
    this->m_data.erase(this->m_data.begin(), this->m_data.end());
    this->m_received = 0;
    this->m_rangeStart = 0;
    this->m_rangeEnd = 0;
    this->m_timeout = s_transportTimeout;
    this->m_stop = false;
    this->m_success = false;
    Blizzard::Lock::EventCreate(this->m_event);
}

void Blizzard::Streaming::TransportHttpCallback::SetCookie(const char* name, const char* value) {
    this->m_cookieName.assign(name, strlen(name));
    this->m_cookieValue.assign(value, strlen(value));
}

bool Blizzard::Streaming::TransportHttpCallback::ReceivedData(const void* data, int32_t size) {
    if (this->m_received >= this->m_rangeEnd - this->m_rangeStart + 1) {
        this->m_stop = true;
    }

    if (size <= 0) {
        return true;
    }

    int64_t left = this->m_rangeEnd - this->m_rangeStart - this->m_received + 1;
    int32_t take = size >= left ? static_cast<int32_t>(left) : size;

    this->m_data.append(static_cast<const char*>(data), take);
    this->m_received += take;

    if (take != size) {
        this->m_stop = true;
    }

    return true;
}

Blizzard::Streaming::URL_INFO::URL_INFO(std::string url, int64_t start, int64_t end, int32_t size) {
    this->m_start = start;
    this->m_end = end;
    this->m_size = size;
    this->m_url = url;
}

bool Blizzard::Streaming::Transport::MD5_Data(std::string* out, std::string* data, int32_t md5Size, int64_t start, int64_t end) {
    bool result = false;

    std::string chunk;
    chunk.reserve(static_cast<uint32_t>(end - start + 1));

    out->reserve(data->size());
    out->erase(out->begin(), out->end());

    std::string hash;
    hash.reserve(32);

    uint32_t position = 0;
    uint32_t consumed = 0;

    if (data->size()) {
        uint32_t hashOffset = 0;
        uint32_t blockEnd = md5Size;

        for (;;) {
            chunk.erase(chunk.begin(), chunk.end());

            uint32_t length = md5Size;
            uint32_t next = position + md5Size + 32;

            if (next > data->size()) {
                length = data->size() - position - 32;
            }

            chunk = data->substr(position, length);
            consumed += length + 32;
            out->append(chunk);

            hash.erase(hash.begin(), hash.end());

            uint32_t hashPosition = blockEnd + hashOffset;

            if (blockEnd + hashOffset + 32 > data->size()) {
                hashPosition = data->size() - 32;
            }

            Blizzard::Util::Digest<16> expected;
            expected.FromHexString(data->substr(hashPosition, 32).c_str());

            Blizzard::Util::MD5 md5;
            md5.Process(chunk.c_str(), chunk.size());

            Blizzard::Util::Digest<16> actual;
            md5.Finish(actual.m_data);

            result = actual == expected;

            if (!result) {
                break;
            }

            blockEnd += md5Size;
            hashOffset += 32;
            position = next;

            if (consumed >= data->size()) {
                break;
            }
        }
    }

    return result;
}

void Blizzard::Streaming::Transport::GetSplitFileURL(const std::string& url, int64_t start, int64_t end, int32_t skip, int32_t splitSize, int32_t md5Size, int32_t size, std::vector<URL_INFO>* urls) {
    if (md5Size) {
        splitSize += 32 * (splitSize / md5Size);
    }

    int64_t first = start / splitSize;
    int64_t firstOffset = start % splitSize;
    int64_t last = (end - 1) / splitSize;
    int32_t previous = 0;

    if (first != last) {
        Blizzard::Log::Printf(3, "Logs/Profiling_MPQSplit.log", "Got split %s, %d-d\n", url.c_str(), start, end);
    }

    if (first > last) {
        return;
    }

    int64_t lastOffset = end % splitSize;

    for (int64_t chunk = first; chunk <= last; chunk++) {
        int64_t chunkStart = chunk == first ? firstOffset : 0;
        int64_t chunkEnd = lastOffset;

        if (lastOffset < chunkStart) {
            chunkEnd = splitSize - 1;
        }

        int64_t length = chunkEnd - chunkStart + 1;
        int32_t blocks = static_cast<int32_t>(length / (md5Size + 32));

        if (length % (md5Size + 32)) {
            blocks++;
        }

        int32_t chunkSize;

        if (chunk == first) {
            chunkSize = static_cast<int32_t>(chunkEnd - 32 * blocks - chunkStart - skip + 1);
        } else {
            chunkSize = size - previous;
        }

        previous = chunkSize;

        char number[260];
        sprintf(number, "%d", static_cast<int32_t>(chunk));

        std::string chunkUrl = url;
        chunkUrl.append(".", 1);
        chunkUrl.append(number, strlen(number));

        urls->push_back(URL_INFO(std::string(chunkUrl), chunkStart, chunkEnd, chunkSize));
    }
}

bool Blizzard::Streaming::Transport::ReadHttp(Manifests::TransportItem* transport, std::string url, int32_t maxRetry, void* data, int64_t fileSize, int64_t offset, int32_t size) {
    int32_t error = 0;

    TransportHttpCallback* callback = new TransportHttpCallback(size);

    std::string baseUrl = url;
    int64_t start = offset;
    int64_t end = offset + size - 1;
    int32_t skip = 0;
    std::vector<URL_INFO> urls;

    if (transport) {
        skip = AdjustRange(&start, &end, transport->m_md5Size, fileSize);

        if (skip) {
            printf("offset is %d\n", skip);
        }

        if (transport->m_splitSize > 0) {
            GetSplitFileURL(baseUrl, start, end, skip, transport->m_splitSize, transport->m_md5Size, size, &urls);
        }
    } else {
        urls.push_back(URL_INFO(std::string(baseUrl), offset, offset + size - 1, size));
    }

    bool result = false;
    int32_t copied = 0;

    for (uint32_t i = 0; i < urls.size(); i++) {
        result = false;

        std::string current = urls[i].m_url;
        int64_t rangeStart = urls[i].m_start;
        int64_t rangeEnd = urls[i].m_end;
        int32_t rangeSize = urls[i].m_size;

        callback->m_data.erase(callback->m_data.begin(), callback->m_data.end());
        callback->m_received = 0;
        callback->m_stop = false;
        callback->m_success = false;
        callback->SetByteRange(rangeStart, rangeEnd);

        if (s_cookieName.size() && s_cookieValue.size()) {
            callback->SetCookie(s_cookieName.c_str(), s_cookieValue.c_str());
        }

        std::string checked;

        for (int32_t attempt = 0; attempt < maxRetry; attempt++) {
            if (attempt) {
                s_statRetries++;
            }

            result = Blizzard::URL::DownloadURL(current.c_str(), callback);

            if (!result) {
                error = 1;
                Blizzard::Process::Sleep(10);
            } else {
                Blizzard::Lock::EventWaitForever(callback->m_event);
                result = callback->m_success;

                if (!result) {
                    error = 3;
                    Blizzard::Process::Sleep(10);
                } else if (rangeEnd - rangeStart + 1 != callback->m_data.size()) {
                    result = false;
                    error = 2;
                } else {
                    result = true;

                    if (transport) {
                        result = MD5_Data(&checked, &callback->m_data, transport->m_md5Size, rangeStart, rangeEnd);

                        if (!result) {
                            error = 4;
                        }
                    }
                }
            }

            if (result) {
                break;
            }

            callback->m_data.erase(callback->m_data.begin(), callback->m_data.end());
            callback->m_received = 0;
            callback->m_stop = false;
            callback->m_success = false;

            if (callback->m_status == Blizzard::URL::DownloadCallback::DOWNLOAD_TIMEOUT) {
                callback->SetTimeOutValue(2 * callback->GetTimeout());
            }
        }

        if (!result) {
            continue;
        }

        if (transport) {
            memmove(static_cast<uint8_t*>(data) + copied, checked.c_str() + skip, rangeSize);
        } else {
            memmove(data, callback->m_data.c_str(), size);
        }

        copied += rangeSize;
    }

    if (result && copied != size) {
        error = 5;
        result = false;
    }

    if (!result) {
        const char* message;

        switch (error) {
            case 1:
                message = "DownloadURL failed with no error";
                break;

            case 2:
                message = "Downloaded smaller than expected";
                break;

            case 3:
                if (callback->m_status == Blizzard::URL::DownloadCallback::DOWNLOAD_TIMEOUT) {
                    message = "DownloadURL failed - Timed out";
                } else if (callback->m_status == Blizzard::URL::DownloadCallback::DOWNLOAD_NOT_FOUND) {
                    message = "DownloadURL failed - File not found";
                } else {
                    message = "DownloadURL failed";
                }

                break;

            case 4:
                message = "MD5 check failed";
                break;

            case 5:
                message = "Copied smaller than expected";
                break;

            default:
                message = "Unknown error";
                break;
        }

        Blizzard::Log::Printf(3, "Logs/Streaming_Error.log", "%d - %s - %s\n", callback->m_status, message, url.c_str());

        Blizzard::String::QuickFormat<1024> text("%s - %s", message, url.c_str());
        System_File::FileError& fileError = System_File::s_error;
        Blizzard::Debug::ErrorStackRecord* stack = Blizzard::Debug::AddErrorToStack(fileError.m_errorStack, callback->m_status, text.m_buffer, 21);

        if (stack) {
            System_File::FileError& current = System_File::s_error;

            if (current.m_errorStack != stack) {
                Blizzard::Debug::FreeErrorStack(current.m_errorStack);
            }

            current.m_errorStack = stack;
        }
    }

    if (callback) {
        delete callback;
    }

    return result;
}

bool Blizzard::Streaming::Transport::Read(Manifests::TransportItem* transport, std::string url, int32_t maxRetry, void* data, int64_t fileSize, int64_t offset, int32_t size) {
    struct Handler {
        const char* m_prefix;
        bool (*m_read)(Manifests::TransportItem*, std::string, int32_t, void*, int64_t, int64_t, int32_t);
    };

    Handler handlers[3] = {
        { "file://", ReadFile },
        { "http://", ReadHttp },
        { nullptr, nullptr }
    };

    bool result = false;

    for (Handler* handler = handlers; handler->m_prefix; handler++) {
        if (url.c_str() && !strncasecmp(url.c_str(), handler->m_prefix, strlen(handler->m_prefix))) {
            THROTTLE throttle;
            throttle.m_bytes = 0;
            throttle.m_start = Blizzard::Time::GetTimestamp();

            result = handler->m_read(transport, std::string(url), maxRetry, data, fileSize, offset, size);

            if (result) {
                throttle.m_bytes = size;
            }

            throttle.Wait();
            break;
        }
    }

    return result;
}

#endif
