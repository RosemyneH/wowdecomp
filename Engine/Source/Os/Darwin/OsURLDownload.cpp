#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include "Engine/Source/Os/OsURLDownload.h"
#include "Engine/Source/Storm/H/storm.h"

static int ConnectHost(const std::string& host, const std::string& port, int32_t timeout) {
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* addresses;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &addresses)) {
        return -1;
    }

    timeval tv;
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;

    int sock = -1;

    for (addrinfo* address = addresses; address; address = address->ai_next) {
        sock = socket(address->ai_family, address->ai_socktype, address->ai_protocol);

        if (sock < 0) {
            continue;
        }

        int noSigPipe = 1;
        setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof(noSigPipe));

        if (timeout > 0) {
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        }

        if (!connect(sock, address->ai_addr, address->ai_addrlen)) {
            break;
        }

        int error = errno;
        close(sock);
        errno = error;
        sock = -1;
    }

    freeaddrinfo(addresses);

    return sock;
}

static void OsUrlDownloadThread(OsUrlDownload* download, std::string host, std::string port, std::string path, int32_t timeout) {
    int sock = ConnectHost(host, port, timeout);

    if (sock < 0) {
        download->m_errorCode = errno == EAGAIN || errno == ETIMEDOUT ? HTTPError::Timeout : HTTPError::Failed;
    } else {
        std::string request = "GET " + path + " HTTP/1.0\r\nHost: " + host + "\r\nUser-Agent: Blizzard Web Client\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n";
        ssize_t sent = send(sock, request.data(), request.size(), 0);

        if (sent < 0 || static_cast<size_t>(sent) != request.size()) {
            download->m_errorCode = HTTPError::Failed;
        } else {
            char data[16384];
            std::string header;
            size_t bodyOffset = std::string::npos;

            while (!download->m_done) {
                ssize_t received;
                do {
                    received = recv(sock, data, sizeof(data), 0);
                } while (received < 0 && errno == EINTR);

                if (received < 0) {
                    download->m_errorCode = errno == EAGAIN || errno == EWOULDBLOCK ? HTTPError::Timeout : HTTPError::Failed;
                    break;
                }

                if (!received) {
                    if (bodyOffset == std::string::npos) {
                        download->m_errorCode = HTTPError::Failed;
                    }

                    break;
                }

                const char* body = data;
                size_t bodySize = static_cast<size_t>(received);

                if (bodyOffset == std::string::npos) {
                    header.append(data, bodySize);
                    bodyOffset = header.find("\r\n\r\n");

                    if (bodyOffset == std::string::npos) {
                        if (header.size() > 65536) {
                            download->m_errorCode = HTTPError::Failed;
                            break;
                        }

                        continue;
                    }

                    size_t space = header.find(' ');
                    download->m_statusCode = space != std::string::npos ? static_cast<uint32_t>(atoi(header.c_str() + space + 1)) : 0;

                    if (download->m_statusCode < 200 || download->m_statusCode >= 300) {
                        download->m_errorCode = download->m_statusCode == 404 ? HTTPError::NotFound : HTTPError::Failed;
                        break;
                    }

                    body = header.data() + bodyOffset + 4;
                    bodySize = header.size() - bodyOffset - 4;
                }

                if (bodySize && download->m_callback && !download->m_callback(download->m_param, body, static_cast<uint32_t>(bodySize), false, HTTPError::NoError)) {
                    download->m_done = true;
                }
            }
        }

        close(sock);
    }

    OsURLDownloadCallback callback = download->m_callback;
    void* param = download->m_param;
    HTTPError::ErrorCode errorCode = download->m_errorCode;

    delete download;

    if (callback) {
        callback(param, nullptr, 0, true, errorCode);
    }
}

// INLINED
OsUrlDownload::OsUrlDownload() {
    this->m_internet = nullptr;
    this->m_connect = nullptr;
    this->m_request = nullptr;
    this->m_connectEvent = nullptr;
    this->m_requestEvent = nullptr;
    this->m_completeEvent = nullptr;
    this->m_dataEvent = nullptr;
    this->m_state = 0;
    this->m_done = false;
    this->m_statusCode = 0;
    this->m_callback = nullptr;
    this->m_error = 0;
    this->m_errorCode = HTTPError::NoError;
}

// ʕ •ᴥ•ʔ✿ Plain HTTP/1.0 only: no TLS and no chunked transfer decoding ✿ ʕ •ᴥ•ʔ
bool OsUrlDownload::DownloadString(const char* url, OsURLDownloadCallback callback, void* param, int32_t timeout) {
    if (!url || SStrCmpI(url, "http://", 7)) {
        return false;
    }

    std::string remainder = url + 7;
    size_t slash = remainder.find('/');
    std::string authority = remainder.substr(0, slash);
    std::string path = slash != std::string::npos ? remainder.substr(slash) : "/";

    size_t colon = authority.rfind(':');
    std::string host = authority.substr(0, colon);
    std::string port = colon != std::string::npos ? authority.substr(colon + 1) : "80";

    if (host.empty()) {
        return false;
    }

    OsUrlDownload* download = new OsUrlDownload();
    download->m_callback = callback;
    download->m_param = param;
    download->m_state = 1;

    std::thread(OsUrlDownloadThread, download, host, port, path, timeout).detach();

    return true;
}

// OFFSET: 0x00870040
bool OsURLDownload(const char* url, OsURLDownloadCallback callback, void* param) {
    return OsUrlDownload::DownloadString(url, callback, param, 5000);
}
