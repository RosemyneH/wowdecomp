#if defined(__APPLE__) && !defined(MSG_NOSIGNAL)
#define MSG_NOSIGNAL 0
#endif
#include <string.h>

#include "fmod_net.h"
#include "fmod_os_misc.h"
#include "fmod_time.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

static bool                     FMOD_OS_Net_Initialised  = false;
FMOD_OS_CRITICALSECTION         *gResolveCrit            = 0;

#define FMOD_OS_NET_SOCKET(_handle) ((int)(intptr_t)(_handle))

// OFFSET: 0x009266E0
FMOD_RESULT FMOD_OS_Net_Init()
{
    if (!FMOD_OS_Net_Initialised)
    {
        FMOD_RESULT result = FMOD_OS_CriticalSection_Create(&gResolveCrit);
        if (result != FMOD_OK)
        {
            return result;
        }

        FMOD_OS_Net_Initialised = true;
    }

    return FMOD_OK;
}

// OFFSET: 0x00926730
FMOD_RESULT FMOD_OS_Net_Shutdown()
{
    if (FMOD_OS_Net_Initialised)
    {
        FMOD_OS_CriticalSection_Free(gResolveCrit);
        gResolveCrit = 0;
        FMOD_OS_Net_Initialised = false;
    }

    return FMOD_OK;
}

// OFFSET: 0x00926760
FMOD_RESULT FMOD_OS_Net_Connect(const char *host, const unsigned short port, void **handle)
{
    struct sockaddr_in  server;
    struct timeval      tv;
    int                 nonblocking = 1;
    fd_set              writefd;
    unsigned int        timeout = FMOD_Net_Timeout;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        return FMOD_ERR_MEMORY;
    }

    *handle = (void *)(intptr_t)sock;

    FMOD_memset(&server, 0, sizeof(struct sockaddr_in));
    server.sin_addr.s_addr = inet_addr(host);

    if (server.sin_addr.s_addr == INADDR_NONE)
    {
        FMOD_RESULT result = FMOD_OS_CriticalSection_Enter(gResolveCrit);
        if (result != FMOD_OK)
        {
            close(sock);
            return result;
        }

        struct hostent *h = gethostbyname(host);
        if (!h)
        {
            FMOD_OS_CriticalSection_Leave(gResolveCrit);
            close(sock);
            return FMOD_ERR_NET_URL;
        }

        FMOD_memcpy(&server.sin_addr, (struct in_addr *)h->h_addr, sizeof(struct in_addr));
        FMOD_OS_CriticalSection_Leave(gResolveCrit);
    }

    server.sin_family = AF_INET;
    server.sin_port = htons(port);

    FD_ZERO(&writefd);
    FD_SET(sock, &writefd);
    tv.tv_sec  = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;

    ioctl(sock, FIONBIO, &nonblocking);

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0 && errno != EINPROGRESS)
    {
        close(sock);
        return FMOD_ERR_NET_CONNECT;
    }

    if (select(sock + 1, 0, &writefd, 0, &tv) <= 0)
    {
        close(sock);
        return FMOD_ERR_NET_CONNECT;
    }

    nonblocking = 0;
    ioctl(sock, FIONBIO, &nonblocking);

    return FMOD_OK;
}

// OFFSET: 0x00926960
FMOD_RESULT FMOD_OS_Net_Listen(const unsigned short port, void **listenhandle)
{
    struct sockaddr_in  address;
    int                 nonblocking = 1;

    int listensocket = socket(AF_INET, SOCK_STREAM, 0);
    if (listensocket < 0)
    {
        return FMOD_ERR_MEMORY;
    }

    if (ioctl(listensocket, FIONBIO, &nonblocking) < 0)
    {
        return FMOD_ERR_NET_SOCKET_ERROR;
    }

    FMOD_memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listensocket, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        return FMOD_ERR_NET_SOCKET_ERROR;
    }

    if (listen(listensocket, 3) < 0)
    {
        return FMOD_ERR_NET_SOCKET_ERROR;
    }

    *listenhandle = (void *)(intptr_t)listensocket;

    return FMOD_OK;
}

// OFFSET: 0x00926A00
FMOD_RESULT FMOD_OS_Net_Accept(const void *listenhandle, void **clienthandle)
{
    if (listenhandle == (void *)-1)
    {
        return FMOD_ERR_NET_SOCKET_ERROR;
    }

    int clientsocket = accept(FMOD_OS_NET_SOCKET(listenhandle), NULL, NULL);
    if (clientsocket < 0)
    {
        if (errno == EWOULDBLOCK)
        {
            return FMOD_ERR_NET_WOULD_BLOCK;
        }

        return FMOD_ERR_NET_SOCKET_ERROR;
    }

    *clienthandle = (void *)(intptr_t)clientsocket;

    return FMOD_OK;
}

// OFFSET: 0x00926A50
FMOD_RESULT FMOD_OS_Net_Close(const void *handle)
{
    if (handle != (void *)-1)
    {
        close(FMOD_OS_NET_SOCKET(handle));
    }

    return FMOD_OK;
}

// OFFSET: 0x00926A70
FMOD_RESULT FMOD_OS_Net_Write(const void *handle, const char *buf, const unsigned int len, unsigned int *byteswritten)
{
    int bytestowrite = len;

    if (handle == (void *)-1)
    {
        return FMOD_ERR_NET_SOCKET_ERROR;
    }

    *byteswritten = 0;

    while (bytestowrite)
    {
        int written = send(FMOD_OS_NET_SOCKET(handle), buf, bytestowrite, MSG_NOSIGNAL);
        if (written < 0)
        {
            if (errno == EWOULDBLOCK)
            {
                return FMOD_ERR_NET_WOULD_BLOCK;
            }

            return FMOD_ERR_NET_SOCKET_ERROR;
        }

        *byteswritten += written;
        bytestowrite -= written;
        buf += written;
    }

    return FMOD_OK;
}

// OFFSET: 0x00926AE0
FMOD_RESULT FMOD_OS_Net_Read(const void *handle, char *buf, const unsigned int len, unsigned int *bytesread)
{
    int bytestoread = len;

    if (handle == (void *)-1)
    {
        return FMOD_ERR_NET_SOCKET_ERROR;
    }

    if (!buf || (len <= 0))
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    *bytesread = 0;

    while (bytestoread)
    {
        int read = recv(FMOD_OS_NET_SOCKET(handle), buf, bytestoread, 0);
        if (read < 0)
        {
            if (errno == EWOULDBLOCK)
            {
                return FMOD_ERR_NET_WOULD_BLOCK;
            }

            return FMOD_ERR_NET_SOCKET_ERROR;
        }
        else if (read == 0)
        {
            return FMOD_ERR_FILE_EOF;
        }

        *bytesread  += read;
        bytestoread -= read;
        buf += read;
    }

    return FMOD_OK;
}

// OFFSET: 0x00926B70
FMOD_RESULT FMOD_OS_Net_ReadLine(const void *handle, char *buf, const unsigned int len)
{
    unsigned int pos, read;
    char c = 0;

    buf[0] = 0;

    if (handle == (void *)-1)
    {
        return FMOD_ERR_NET_SOCKET_ERROR;
    }

    if (!buf || (len <= 0))
    {
        return FMOD_ERR_INVALID_PARAM;
    }

    pos = 0;

    while (pos < len)
    {
        FMOD_RESULT result = FMOD_OS_Net_Read(handle, &c, 1, &read);
        if (result == FMOD_ERR_NET_WOULD_BLOCK)
        {
            return FMOD_ERR_NET_WOULD_BLOCK;
        }

        if (read == 1)
        {
            if (c == '\n')
            {
                break;
            }
            else if (c != '\r')
            {
                buf[pos++] = c;
            }
        }
        else
        {
            break;
        }
    }

    if (pos >= len)
    {
        pos = len - 1;
    }

    buf[pos] = 0;

    return FMOD_OK;
}
