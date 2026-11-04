extern "C" {
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
}

#include <auss.hpp>
#include <cstring>
#include <ios>

#include "i3ipc++/ipc-util.hpp"

namespace i3ipc {

static_assert(sizeof(header_t) == 14, "i3 IPC header must be exactly 14 bytes (6 magic + 2 * uint32_t)");

/**
 * Upper bound for a payload size we are willing to allocate for a single reply. The size comes straight from the
 * header, so without a limit a corrupted or malicious peer could make us allocate up to 4 GiB.
 */
static const uint32_t g_max_payload_size = 256u * 1024u * 1024u;

static const char g_i3_ipc_magic[sizeof(header_t::magic)] = {'i', '3', '-', 'i', 'p', 'c'};

static std::string format_errno(const std::string& msg = std::string()) {
  const int saved_errno = errno; // Streaming below must not be able to clobber it
  auss_t a;
  if (msg.size() > 0)
    a << msg << ": ";
  a << "errno " << saved_errno << " (" << strerror(saved_errno) << ')';
  return a;
}

errno_error::errno_error() : ipc_error(format_errno()) {}
errno_error::errno_error(const std::string& msg) : ipc_error(format_errno(msg)) {}

buf_t::buf_t(uint32_t payload_size) : size(0), data(nullptr), header(nullptr), payload(nullptr) {
  if (payload_size > UINT32_MAX - sizeof(header_t)) {
    throw ipc_error("Payload is too large");
  }
  size = sizeof(header_t) + payload_size;
  data = new uint8_t[size];
  header = reinterpret_cast<header_t*>(data);
  payload = reinterpret_cast<char*>(data + sizeof(header_t));
  memcpy(header->magic, g_i3_ipc_magic, sizeof(header->magic));
  header->size = payload_size;
  header->type = 0x0;
}
buf_t::~buf_t() {
  delete[] data;
}

void buf_t::realloc_payload_to_header() {
  const uint32_t payload_size = header->size;
  if (payload_size > UINT32_MAX - sizeof(header_t)) {
    throw ipc_error("Payload is too large");
  }
  const uint32_t new_size = sizeof(header_t) + payload_size;
  uint8_t* new_data = new uint8_t[new_size];
  memcpy(new_data, header, sizeof(header_t));
  delete[] data;
  data = new_data;
  size = new_size;
  header = reinterpret_cast<header_t*>(data);
  payload = reinterpret_cast<char*>(data + sizeof(header_t));
}

int32_t i3_connect(const std::string& socket_path) {
  struct sockaddr_un addr;
  if (socket_path.size() >= sizeof(addr.sun_path)) {
    throw ipc_error("Socket path is too long (" + std::to_string(socket_path.size()) + " bytes, at most " +
                    std::to_string(sizeof(addr.sun_path) - 1) + " are supported): '" + socket_path + "'");
  }

#ifdef SOCK_CLOEXEC
  int32_t sockfd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
#else
  int32_t sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
#endif
  if (sockfd == -1) {
    throw errno_error("Could not create a socket");
  }

#ifndef SOCK_CLOEXEC
  // Do not leak the connection to processes spawned by the application
  (void)fcntl(sockfd, F_SETFD, FD_CLOEXEC);
#endif

  memset(&addr, 0, sizeof(struct sockaddr_un));
  addr.sun_family = AF_UNIX;
  memcpy(addr.sun_path, socket_path.c_str(), socket_path.size());
  if (connect(sockfd, (const struct sockaddr*)&addr, sizeof(struct sockaddr_un)) < 0) {
    const int saved_errno = errno;
    close(sockfd); // Do not leak the descriptor if the connection is failed
    errno = saved_errno;
    throw errno_error("Failed to connect to i3 at '" + (socket_path.empty() ? "<empty>" : socket_path) + "'");
  }

  return sockfd;
}

void i3_disconnect(const int32_t sockfd) {
  close(sockfd);
}

std::shared_ptr<buf_t> i3_pack(const ClientMessageType type, const std::string& payload) {
  if (payload.length() > g_max_payload_size) {
    throw ipc_error("Payload is too large");
  }
  // The payload is binary-safe: it is copied as is and could contain NUL bytes
  auto buff = std::make_shared<buf_t>(static_cast<uint32_t>(payload.length()));
  buff->header->type = static_cast<uint32_t>(type);
  if (!payload.empty()) {
    memcpy(buff->payload, payload.data(), payload.length());
  }
  return buff;
}

/**
 * Wait until the descriptor is ready for the given poll events. Used when a non-blocking socket would block
 */
static void wait_for_fd(int fd, short events) {
  struct pollfd pfd;
  pfd.fd = fd;
  pfd.events = events;
  pfd.revents = 0;
  while (poll(&pfd, 1, -1) == -1) {
    if (errno != EINTR)
      throw errno_error(auss_t() << "Failed to poll socket 0x" << std::hex << fd);
  }
}

/**
 * Write the whole buffer, restarting after interruptions and waiting if the socket is non-blocking
 * @return count on success, -1 on error (errno is set)
 */
static ssize_t writeall(int fd, const uint8_t* buf, size_t count) {
  size_t written = 0;

  while (written < count) {
    // MSG_NOSIGNAL: a vanished i3 must be reported as an error (EPIPE), not kill the whole application with SIGPIPE
#ifdef MSG_NOSIGNAL
    ssize_t n = send(fd, buf + written, count - written, MSG_NOSIGNAL);
#else
    ssize_t n = write(fd, buf + written, count - written);
#endif
    if (n == -1) {
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        wait_for_fd(fd, POLLOUT);
        continue;
      }
      return -1;
    }
    written += (size_t)n;
  }

  return (ssize_t)written;
}

/**
 * Read exactly count bytes, restarting after interruptions and waiting if the socket is non-blocking
 * @param what a description of the data (for error messages)
 * @throws eof_error if the peer closed the connection before count bytes were received
 */
static void read_exact(int fd, uint8_t* out, size_t count, const char* what) {
  size_t readed = 0;

  while (readed < count) {
    ssize_t n = read(fd, out + readed, count - readed);
    if (n == -1) {
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        wait_for_fd(fd, POLLIN);
        continue;
      }
      throw errno_error(auss_t() << "Failed to read " << what << " from socket 0x" << std::hex << fd);
    }
    if (n == 0) {
      throw eof_error(auss_t() << "Unexpected EOF while reading " << what);
    }

    readed += (size_t)n;
  }
}

void i3_send(const int32_t sockfd, const buf_t& buff) {
  if (writeall(sockfd, buff.data, buff.size) == -1) {
    throw errno_error(auss_t() << "Failed to write to socket 0x" << std::hex << sockfd);
  }
}

std::shared_ptr<buf_t> i3_recv(const int32_t sockfd) {
  auto buff = std::make_shared<buf_t>(0);

  read_exact(sockfd, buff->data, sizeof(header_t), "header");

  if (memcmp(buff->header->magic, g_i3_ipc_magic, sizeof(g_i3_ipc_magic)) != 0) {
    throw invalid_header_error("Invalid magic in reply");
  }

  const uint32_t payload_size = buff->header->size; // A copy: a reference to a packed field is not allowed
  if (payload_size > g_max_payload_size) {
    throw invalid_header_error(auss_t() << "Payload of reply is too large: " << payload_size << " bytes");
  }

  buff->realloc_payload_to_header();

  read_exact(sockfd, reinterpret_cast<uint8_t*>(buff->payload), payload_size, "payload");

  return buff;
}

std::shared_ptr<buf_t> i3_msg(const int32_t sockfd, const ClientMessageType type, const std::string& payload) {
  auto send_buff = i3_pack(type, payload);
  i3_send(sockfd, *send_buff);
  auto recv_buff = i3_recv(sockfd);
  const uint32_t expected_type = send_buff->header->type;
  const uint32_t got_type = recv_buff->header->type;
  if (expected_type != got_type) {
    throw invalid_header_error(auss_t() << "Invalid reply type: Expected 0x" << std::hex << expected_type << ", got 0x"
                                        << got_type);
  }
  return recv_buff;
}

} // namespace i3ipc
