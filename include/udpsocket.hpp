#ifndef SOCC_HPP
#define SOCC_HPP

#include <bits/iterator_concepts.h>
#include <cstddef>
#include <cstdint>
#include <array>
#include <iostream>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <type_traits>
#include <unistd.h>
#include <vector>

namespace Socc
{

template<
  std::size_t BufferSize = 1024UL,
  typename PortT = std::uint16_t,
  bool DebugMode = true
>
class UDPServ
{
private:

  // TODO(markovejnovic): Incorrect array size, should use the template.
  using OnDataCallback = void(std::array<std::byte, 1024>::iterator begin,
                              std::array<std::byte, 1024>::iterator end);

public:
  UDPServ(const PortT port, OnDataCallback onDataReceived): m_onReceivedCallback(onDataReceived) {
    m_servAddr = {
      .sin_family = AF_INET,
      .sin_port = htons(port),
      .sin_addr = {
        .s_addr = INADDR_ANY,
      },
    };

    m_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (m_fd < 0) {
      throw std::runtime_error("Could not initialize the socket");
    }

    if (bind(m_fd, reinterpret_cast<struct sockaddr*>(&m_servAddr), sizeof(m_servAddr)) < 0) {
      throw std::runtime_error("Could not bind to the socket.");
    }

    if (DebugMode) {
      std::cout << "Server is setup on " << port << std::endl;
    }
  }

  void Begin() {
    if (DebugMode) {
      std::cout << "Opening Server" << std::endl;
    }

    while (true) {
      struct sockaddr_in clientAddr;
      socklen_t clientAddrLen;

      const size_t nBytes = recvfrom(m_fd, m_inBuf.begin(), BufferSize, MSG_WAITALL,
                                     reinterpret_cast<struct sockaddr*>(&clientAddr),
                                     &clientAddrLen);

      if (DebugMode) {
        char clientAddrStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientAddrStr, INET_ADDRSTRLEN);
        std::cout << "Received " << nBytes << " UDP packets from " << clientAddrStr << std::endl;
      }

      if (m_onReceivedCallback != nullptr) {
        m_onReceivedCallback(m_inBuf.begin(), m_inBuf.begin() + nBytes);
      }
    }
  }
private:
  using Container = std::array<std::byte, BufferSize>;
  int m_fd;
  struct sockaddr_in m_servAddr;
  Container m_inBuf;
  OnDataCallback* m_onReceivedCallback;
};

}

#endif // SOCC_HPP
