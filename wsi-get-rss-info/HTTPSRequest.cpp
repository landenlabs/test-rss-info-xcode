
#include "HTTPSRequest.hpp"

#include <iostream>

#ifndef SOCK_NONBLOCK
#include <fcntl.h>
# define SOCK_NONBLOCK O_NONBLOCK
#endif
#include <chrono>
#include <thread>

namespace https
{

SSL* ssl;
int sslFd;

int RecvPacket()
{
    int len=100;
    char buf[1024];

    do {
        len = SSL_read(ssl, buf, (int)sizeof(buf));
        buf[len]=0;
        // std::cerr << buf;       // #### DEBUG
    } while (len > 0);

    if (len < 0) {
        int err = SSL_get_error(ssl, len);

        if (err == SSL_ERROR_WANT_READ)
            return 0;
        if (err == SSL_ERROR_WANT_WRITE)
            return 0;
        if (err == SSL_ERROR_ZERO_RETURN || err == SSL_ERROR_SYSCALL || err == SSL_ERROR_SSL)
            return -1;
    }
    return len;
}

int SendPacket(const char *buf)
{
    int len = SSL_write(ssl, buf, strlen(buf));
    if (len < 0) {
        int err = SSL_get_error(ssl, len);
        switch (err) {
        case SSL_ERROR_WANT_WRITE:
            return 0;
        case SSL_ERROR_WANT_READ:
            return 0;
        case SSL_ERROR_ZERO_RETURN:
        case SSL_ERROR_SYSCALL:
        case SSL_ERROR_SSL:
        default:
            return -1;
        }
    }
    return len;
}

void log_ssl()
{
    int err;
    while ((err = ERR_get_error()) != 0) {
        char *str = ERR_error_string(err, 0);
        if (!str)
            return;
        std::cerr << "HTTPS SSL Error " << str << std::endl;  // #### DEBUG
    }
}


    inline namespace detail
    {
#ifdef _WIN32
        WinSock::WinSock()
        {
            WSADATA wsaData;
            const auto error = WSAStartup(MAKEWORD(2, 2), &wsaData);
            if (error != 0)
                throw std::system_error(error, std::system_category(), "WSAStartup failed");

            if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2)
            {
                WSACleanup();
                throw std::runtime_error("Invalid WinSock version");
            }

            started = true;
        }

        WinSock::~WinSock()
        {
            if (started) WSACleanup();
        }

        WinSock:: WinSock(WinSock&& other) noexcept:
            started(other.started)
        {
            other.started = false;
        }

        WinSock::WinSock& operator=(WinSock&& other) noexcept
        {
            if (&other == this) return *this;
            if (started) WSACleanup();
            started = other.started;
            other.started = false;
            return *this;
        }
#endif

        int getLastError() noexcept
        {
#ifdef _WIN32
            return WSAGetLastError();
#else
            return errno;
#endif
        }

        constexpr int getAddressFamily(InternetProtocol internetProtocol)
        {
            return (internetProtocol == InternetProtocol::V4) ? AF_INET :
                (internetProtocol == InternetProtocol::V6) ? AF_INET6 :
                throw RequestError("Unsupported protocol");
        }

        // class Socket 
            /* explicit */ Socket::Socket(InternetProtocol internetProtocol):
                endpoint(socket(getAddressFamily(internetProtocol), SOCK_STREAM, IPPROTO_TCP))
            {
                if (endpoint == invalid)
                    throw std::system_error(getLastError(), std::system_category(), "Failed to create socket");

#if defined(__APPLE__)
                const int value = 1;
                if (setsockopt(endpoint, SOL_SOCKET, SO_NOSIGPIPE, &value, sizeof(value)) == -1)
                    throw std::system_error(getLastError(), std::system_category(), "Failed to set socket option");
#endif
            }

            Socket::~Socket()
            {
                if (endpoint != invalid) closeSocket(endpoint);
            }

            Socket::Socket(Socket&& other) noexcept:
                endpoint(other.endpoint)
            {
                other.endpoint = invalid;
            }

            Socket& Socket::operator=(Socket&& other) noexcept
            {
                if (&other == this) return *this;
                if (endpoint != invalid) closeSocket(endpoint);
                endpoint = other.endpoint;
                other.endpoint = invalid;
                return *this;
            }


            void Socket::setupSSL(bool initSSL) {
                if (initSSL) {
                    SSL_library_init();
                    SSLeay_add_ssl_algorithms();
                    SSL_load_error_strings();
                }
                const SSL_METHOD *meth =
                        // SSLv23_client_method();
                        // DTLS_client_method();
                        TLS_client_method();
                        // TLSv1_2_client_method();
                ctx = SSL_CTX_new (meth);
                ssl = SSL_new (ctx);
                if (!ssl) {
                    log_ssl();
                    throw std::system_error(getLastError(), std::system_category(),"Error creating SSL");
                }
                sslFd = SSL_get_fd(ssl);
                SSL_set_fd(ssl, endpoint);
                int err = SSL_connect(ssl);
                if (err <= 0) {
                    log_ssl();
                    throw std::system_error(getLastError(), std::system_category(),"Error creating SSL connection, err=" + std::to_string(err));
                }

                int status = fcntl(endpoint, F_SETFL, fcntl(endpoint, F_GETFL, 0) | O_NONBLOCK);
                if (status == -1){
                  throw std::system_error(getLastError(), std::system_category(), "Failed to set socket nonBlocking");
                }
                // std::cerr << "SSL connection using %" << SSL_get_cipher(ssl) << std::endl;  // #### DEBUG
            }

            void Socket::connect(const struct sockaddr* address, socklen_t addressSize)
            {
                auto result = ::connect(endpoint, address, addressSize);

#ifdef _WIN32
                while (result == -1 && WSAGetLastError() == WSAEINTR)
                    result = ::connect(endpoint, address, addressSize);
#else
                while (result == -1 && errno == EINTR)
                    result = ::connect(endpoint, address, addressSize);
#endif

                if (result == -1)
                    throw std::system_error(getLastError(), std::system_category(), "Failed to connect");
                
                setupSSL();
            }

            size_t Socket::send(const void* buffer, size_t length, int flags)
            {
#if 1
                int len = SSL_write(ssl, buffer, (int)length);
                if (len < 0) {
                    int err = SSL_get_error(ssl, len);
                    switch (err) {
                    case SSL_ERROR_WANT_WRITE:
                        return 0;
                    case SSL_ERROR_WANT_READ:
                        return 0;
                    case SSL_ERROR_ZERO_RETURN:
                    case SSL_ERROR_SYSCALL:
                    case SSL_ERROR_SSL:
                    default:
                        return -1;
                    }
                }
                return len; // length written
#else
#ifdef _WIN32
                auto result = ::send(endpoint, reinterpret_cast<const char*>(buffer),
                                     static_cast<int>(length), flags);

                while (result == -1 && WSAGetLastError() == WSAEINTR)
                    result = ::send(endpoint, reinterpret_cast<const char*>(buffer),
                                    static_cast<int>(length), flags);

#else
                auto result = ::send(endpoint, reinterpret_cast<const char*>(buffer),  length, flags);

                while (result == -1 && errno == EINTR)
                    result = ::send(endpoint, reinterpret_cast<const char*>(buffer),   length, flags);
#endif
                if (result == -1)
                    throw std::system_error(getLastError(), std::system_category(), "Failed to send data");

                return static_cast<size_t>(result);
#endif
            }

            size_t Socket::recv(void* buffer, size_t length, int flags)
            {
#if 1
                char* cbuffer = (char*)buffer;
                size_t off = 0;
                int len = 0;
                do {
                    off += len;
                    errno = 0;
                    unsigned tryCnt = 0;
                    do {
                        if (errno == EWOULDBLOCK) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(300));
                        }
                        errno = 0;
                        len = SSL_read(ssl, cbuffer+off, length-off);
                        // std::cerr << "rlen=" << len << " got=" << off << " more=" << (length-off) << std::endl;
                    } while (errno == EWOULDBLOCK && tryCnt++ < 1);
                } while (len > 0 && off+len < length);

                if (len < 0) {
                    int err = SSL_get_error(ssl, len);

                    if (err == SSL_ERROR_WANT_READ)
                        len = 0; // return 0;
                    if (err == SSL_ERROR_WANT_WRITE)
                        return 0;
                    if (err == SSL_ERROR_ZERO_RETURN || err == SSL_ERROR_SYSCALL || err == SSL_ERROR_SSL)
                        return -1;
                }
                // std::cerr << "ssl rlen=" << (off+len) << std::endl;
                return off+len;
#else
#ifdef _WIN32
                auto result = ::recv(endpoint, reinterpret_cast<char*>(buffer),
                                     static_cast<int>(length), flags);

                while (result == -1 && WSAGetLastError() == WSAEINTR)
                    result = ::recv(endpoint, reinterpret_cast<char*>(buffer),
                                    static_cast<int>(length), flags);
#else
                auto result = ::recv(endpoint, reinterpret_cast<char*>(buffer),
                                     length, flags);

                while (result == -1 && errno == EINTR)
                    result = ::recv(endpoint, reinterpret_cast<char*>(buffer),
                                    length, flags);
#endif
                if (result == -1)
                    throw std::system_error(getLastError(), std::system_category(), "Failed to read data");

                return static_cast<size_t>(result);
#endif
            }

            // operator Type() const noexcept { return endpoint; }


            void Socket::close() {
                SSL_free(ssl);
                // closeSocket(socket);
                // X509_free(cert);
                SSL_CTX_free(ctx);
            }
      
        //};
    }

    std::string urlEncode(const std::string& str)
    {
        constexpr char hexChars[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

        std::string result;

        for (auto i = str.begin(); i != str.end(); ++i)
        {
            const std::uint8_t cp = *i & 0xFF;

            if ((cp >= 0x30 && cp <= 0x39) || // 0-9
                (cp >= 0x41 && cp <= 0x5A) || // A-Z
                (cp >= 0x61 && cp <= 0x7A) || // a-z
                cp == 0x2D || cp == 0x2E || cp == 0x5F) // - . _
                result += static_cast<char>(cp);
            else if (cp <= 0x7F) // length = 1
                result += std::string("%") + hexChars[(*i & 0xF0) >> 4] + hexChars[*i & 0x0F];
            else if ((cp >> 5) == 0x06) // length = 2
            {
                result += std::string("%") + hexChars[(*i & 0xF0) >> 4] + hexChars[*i & 0x0F];
                if (++i == str.end()) break;
                result += std::string("%") + hexChars[(*i & 0xF0) >> 4] + hexChars[*i & 0x0F];
            }
            else if ((cp >> 4) == 0x0E) // length = 3
            {
                result += std::string("%") + hexChars[(*i & 0xF0) >> 4] + hexChars[*i & 0x0F];
                if (++i == str.end()) break;
                result += std::string("%") + hexChars[(*i & 0xF0) >> 4] + hexChars[*i & 0x0F];
                if (++i == str.end()) break;
                result += std::string("%") + hexChars[(*i & 0xF0) >> 4] + hexChars[*i & 0x0F];
            }
            else if ((cp >> 3) == 0x1E) // length = 4
            {
                result += std::string("%") + hexChars[(*i & 0xF0) >> 4] + hexChars[*i & 0x0F];
                if (++i == str.end()) break;
                result += std::string("%") + hexChars[(*i & 0xF0) >> 4] + hexChars[*i & 0x0F];
                if (++i == str.end()) break;
                result += std::string("%") + hexChars[(*i & 0xF0) >> 4] + hexChars[*i & 0x0F];
                if (++i == str.end()) break;
                result += std::string("%") + hexChars[(*i & 0xF0) >> 4] + hexChars[*i & 0x0F];
            }
        }

        return result;
    }

    // class Request final
        /* explicit */ Request::Request(const std::string& url, InternetProtocol protocol) : internetProtocol(protocol)
        {
            const auto schemeEndPosition = url.find("://");

            if (schemeEndPosition != std::string::npos)
            {
                scheme = url.substr(0, schemeEndPosition);
                path = url.substr(schemeEndPosition + 3);
            }
            else
            {
                scheme = "https";
                path = url;
            }

            const auto fragmentPosition = path.find('#');

            // remove the fragment part
            if (fragmentPosition != std::string::npos)
                path.resize(fragmentPosition);

            const auto pathPosition = path.find('/');

            if (pathPosition == std::string::npos)
            {
                domain = path;
                path = "/";
            }
            else
            {
                domain = path.substr(0, pathPosition);
                path = path.substr(pathPosition);
            }

            const auto portPosition = domain.find(':');

            if (portPosition != std::string::npos)
            {
                port = domain.substr(portPosition + 1);
                domain.resize(portPosition);
            }
            else
                port = "443";   // not 80
        }

        Request::~Request() {
            
        }

        Response Request::send(const std::string& method,
                      const std::map<std::string, std::string>& parameters,
                      const std::vector<std::string>& headers)
        {
            std::string body;
            bool first = true;

            for (const auto& parameter : parameters)
            {
                if (!first) body += "&";
                first = false;

                body += urlEncode(parameter.first) + "=" + urlEncode(parameter.second);
            }

            return send(method, body, headers);
        }

        Response Request::send(const std::string& method,
                      const std::vector<uint8_t>& body,
                      const std::vector<std::string>& headers)
        {
            Response response;

            if (strcasecmp(scheme.c_str(), "https") != 0)
                throw RequestError("Only HTTPS scheme is supported");

            addrinfo hints = {};
            hints.ai_family = getAddressFamily(internetProtocol);
            hints.ai_socktype = SOCK_STREAM;

            addrinfo* info;
            if (getaddrinfo(domain.c_str(), port.c_str(), &hints, &info) != 0)
                throw std::system_error(getLastError(), std::system_category(), "Failed to get address info of " + domain);

            std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addressInfo(info, freeaddrinfo);

            std::string headerData = method + " " + path + " HTTP/1.1\r\n";

            for (const std::string& header : headers)
                headerData += header + "\r\n";

            headerData += "Host: " + domain + "\r\n"
                "Content-Length: " + std::to_string(body.size()) + "\r\n"
                "\r\n";

            std::vector<uint8_t> requestData(headerData.begin(), headerData.end());
            requestData.insert(requestData.end(), body.begin(), body.end());

            Socket socket(internetProtocol);

            // take the first address from the list
            socket.connect(addressInfo->ai_addr, static_cast<socklen_t>(addressInfo->ai_addrlen));

            auto remaining = requestData.size();
            auto sendData = requestData.data();

            // send the request
            while (remaining > 0)
            {
                const auto size = socket.send(sendData, remaining, noSignal);
                remaining -= size;
                sendData += size;
            }

            std::uint8_t tempBuffer[4096];
            constexpr std::uint8_t crlf[] = {'\r', '\n'};
            std::vector<std::uint8_t> responseData;
            bool firstLine = true;
            bool parsedHeaders = false;
            bool contentLengthReceived = false;
            unsigned long contentLength = 0;
            bool chunkedResponse = false;
            std::size_t expectedChunkSize = 0;
            bool removeCrlfAfterChunk = false;

            // read the response
            for (;;)
            {
                const auto size = socket.recv(tempBuffer, sizeof(tempBuffer), noSignal);

                if (size == 0)
                    break; // disconnected

                responseData.insert(responseData.end(), tempBuffer, tempBuffer + size);

                if (!parsedHeaders)
                {
                    for (;;)
                    {
                        const auto i = std::search(responseData.begin(), responseData.end(), std::begin(crlf), std::end(crlf));

                        // didn't find a newline
                        if (i == responseData.end())
                            break;

                        const std::string line(responseData.begin(), i);
                        responseData.erase(responseData.begin(), i + 2);

                        // empty line indicates the end of the header section
                        if (line.empty())
                        {
                            parsedHeaders = true;
                            break;
                        }
                        else if (firstLine) // first line
                        {
                            firstLine = false;

                            std::string::size_type lastPos = 0;
                            const auto length = line.length();
                            std::vector<std::string> parts;

                            // tokenize first line
                            while (lastPos < length + 1)
                            {
                                auto pos = line.find(' ', lastPos);
                                if (pos == std::string::npos) pos = length;

                                if (pos != lastPos)
                                    parts.emplace_back(line.data() + lastPos,
                                                       static_cast<std::vector<std::string>::size_type>(pos) - lastPos);

                                lastPos = pos + 1;
                            }

                            if (parts.size() >= 2)
                                response.status = std::stoi(parts[1]);
                        }
                        else // headers
                        {
                            response.headers.push_back(line);

                            const auto pos = line.find(':');

                            if (pos != std::string::npos)
                            {
                                std::string headerName = line.substr(0, pos);
                                std::string headerValue = line.substr(pos + 1);

                                // ltrim
                                headerValue.erase(headerValue.begin(),
                                                  std::find_if(headerValue.begin(), headerValue.end(),
                                                               [](int c) {return !std::isspace(c);}));

                                // rtrim
                                headerValue.erase(std::find_if(headerValue.rbegin(), headerValue.rend(),
                                                               [](int c) {return !std::isspace(c);}).base(),
                                                  headerValue.end());

                                if (headerName == "Content-Length")
                                {
                                    contentLength = std::stoul(headerValue);
                                    contentLengthReceived = true;
                                    response.body.reserve(contentLength);
                                    // std::cerr << "  Content-Length=" << contentLength << std::endl;
                                }
                                else if (headerName == "Transfer-Encoding")
                                {
                                    if (headerValue == "chunked") {
                                        chunkedResponse = true;
                                        // std::cerr << "  chunked=true\n";
                                    } else
                                        throw ResponseError("Unsupported transfer encoding: " + headerValue);
                                }
                            }
                        }
                    }
                }

                if (parsedHeaders)
                {
                    // Content-Length must be ignored if Transfer-Encoding is received
                    if (chunkedResponse)
                    {
                        bool dataReceived = false;
                        for (;;)
                        {
                            if (expectedChunkSize > 0)
                            {
                                const auto toWrite = std::min(expectedChunkSize, responseData.size());
                                response.body.insert(response.body.end(), responseData.begin(), responseData.begin() + static_cast<ptrdiff_t>(toWrite));
                                responseData.erase(responseData.begin(), responseData.begin() + static_cast<ptrdiff_t>(toWrite));
                                expectedChunkSize -= toWrite;

                                if (expectedChunkSize == 0) removeCrlfAfterChunk = true;
                                if (responseData.empty()) break;
                            }
                            else
                            {
                                if (removeCrlfAfterChunk)
                                {
                                    if (responseData.size() >= 2)
                                    {
                                        removeCrlfAfterChunk = false;
                                        responseData.erase(responseData.begin(), responseData.begin() + 2);
                                    }
                                    else break;
                                }

                                const auto i = std::search(responseData.begin(), responseData.end(), std::begin(crlf), std::end(crlf));

                                if (i == responseData.end()) 
                                    break;

                                const std::string line(responseData.begin(), i);
                                responseData.erase(responseData.begin(), i + 2);

                                expectedChunkSize = std::stoul(line, nullptr, 16);

                                if (expectedChunkSize == 0)
                                {
                                    dataReceived = true;
                                    break;
                                }
                            }
                        }

                        if (dataReceived)
                            break;
                    }
                    else
                    {
                        response.body.insert(response.body.end(), responseData.begin(), responseData.end());
                        responseData.clear();

                        // got the whole content
                        if (contentLengthReceived && response.body.size() >= contentLength)
                            break;
                    }
                }
            }

            socket.close();
            
            return response;
        }


    // };
}

 

