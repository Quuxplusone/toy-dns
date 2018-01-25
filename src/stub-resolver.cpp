
#include "bus.h"
#include "exception.h"
#include "message.h"
#include "nonstd.h"
#include "question.h"
#include "stub-resolver.h"

#include <assert.h>
#include <arpa/inet.h>
#include <future>
#include <iostream>
#include <errno.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace dns;

StubResolver::StubResolver(bus::Bus& bus, Upstream upstream) : m_bus(bus)
{
    m_upstreams.push_back(std::move(upstream));
}

Message recv_message_from_socket(int sockfd)
{
    char buffer[1024];
    Message response;

    while (true) {
        struct sockaddr_in serverAddress;
        socklen_t addrLen = sizeof serverAddress;
        int nbytes = recvfrom(
            sockfd,
            buffer, sizeof buffer,
            0,
            reinterpret_cast<struct sockaddr *>(&serverAddress), &addrLen
        );
        std::cout << "Received " << nbytes << " bytes!" << std::endl;
        if (nbytes <= 0) {
            continue;
        }
        const char *parsed = nullptr;
        try {
            parsed = response.decode(buffer, buffer + nbytes);
        } catch (const dns::Exception& e) {
            std::cout << "During packet decode: " << e.what() << std::endl;
            std::cout << "During packet decode: " << e.what() << std::endl;
            continue;
        }
        if (parsed == nullptr) {
            std::cout << "Failed to parse packet of length " << nbytes << std::endl;
            continue;
        }
        if (parsed != buffer + nbytes) {
            std::cout << "Packet of length " << nbytes
                << " parsed as message of length " << (parsed - buffer)
                << " with some trailing bytes" << std::endl;
        }
        break;
    }

    return response;
}

void send_message_to_socket(const Message& query, int sockfd, const Upstream& upstream)
{
    char buffer[512];
    const char *end = query.encode(buffer, buffer + sizeof buffer);
    if (end == nullptr) {
        throw dns::Exception("Buffer wasn't long enough to encode query");
    }
    for (const char *src = buffer; src != end; ) {
        int sent = sendto(
            sockfd,
            src, (end - src),
            0,
            upstream.sockaddr(), upstream.sockaddr_length()
        );
        if (sent < 0) {
            throw dns::Exception("sendto() failed: ", strerror(errno));
        }
        std::cout << "Sent " << sent << " bytes!" << std::endl;
        src += sent;
    }
}

nonstd::future<Message> StubResolver::async_resolve(const Message& query, nonstd::milliseconds timeout) const
{
    const Upstream& upstream = this->m_upstreams.at(0);

    Upstream ephemeral_port("127.0.0.1", 0);
    int sockfd = ephemeral_port.bind_udp_socket(timeout);
    assert(sockfd > 0);

    // Send the query to the upstream server.
    return m_bus.when_ready_to_send(sockfd).on_value_f([this, query, sockfd, upstream]() {
        send_message_to_socket(query, sockfd, upstream);

        // Now listen until we hear a response.
        return m_bus.when_ready_to_recv(sockfd).on_value([sockfd]() -> Message {
            return recv_message_from_socket(sockfd);
        }).finally([sockfd]() {
            close(sockfd);
        });
    });
}
