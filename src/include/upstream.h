#pragma once

#include <arpa/inet.h>

namespace dns {

class Upstream {
public:
    explicit Upstream(const char *ipv4, int port);

    int bind_udp_socket() const;

    const sockaddr *sockaddr() const noexcept { return reinterpret_cast<const struct sockaddr *>(&m_sockaddr); }
    int sockaddr_length() const noexcept { return sizeof m_sockaddr; }

private:
    sockaddr_in m_sockaddr;
};

} // namespace dns
