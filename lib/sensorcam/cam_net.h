// BSD sockets on both lwIP (ESP32) and Winsock (PC test build).
#pragma once
#include <stdint.h>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
inline void netInit() {
  WSADATA w;
  WSAStartup(MAKEWORD(2, 2), &w);
}
inline void netClose(int fd) { closesocket((SOCKET)fd); }
inline void netNonblock(int fd) {
  u_long on = 1;
  ioctlsocket((SOCKET)fd, FIONBIO, &on);
}
inline bool netWouldBlock() {
  int e = WSAGetLastError();
  return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEALREADY;
}
#else
#include <errno.h>
#include <fcntl.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>
inline void netInit() {}
inline void netClose(int fd) { closesocket(fd); }
inline void netNonblock(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }
inline bool netWouldBlock() { return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS; }
#endif

// Adds a socket to a set for select(), keeping maxFd the highest.
inline void netWatch(int fd, fd_set& set, int& maxFd) {
  FD_SET(fd, &set);
  if (fd > maxFd) maxFd = fd;
}

// An IPv4 address as the network holds it, as text: 192.168.1.20.
inline std::string netAddressText(uint32_t address) {
  const uint8_t* b = (const uint8_t*)&address;
  return std::to_string(b[0]) + "." + std::to_string(b[1]) + "." + std::to_string(b[2]) + "." + std::to_string(b[3]);
}
