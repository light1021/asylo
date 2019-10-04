/*
 *
 * Copyright 2019 Asylo authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "asylo/platform/host_call/untrusted/host_call_handlers.h"

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include "asylo/platform/primitives/util/message.h"
#include "asylo/platform/primitives/util/status_conversions.h"
#include "asylo/platform/system_call/untrusted_invoke.h"
#include "asylo/util/status_macros.h"

namespace asylo {
namespace host_call {
namespace {

using primitives::Extent;

size_t GetNumAddrinfos(struct addrinfo *addrs) {
  size_t ret = 0;
  for (struct addrinfo *addr = addrs; addr != nullptr; addr = addr->ai_next) {
    ret++;
  }
  return ret;
}

// Returns true if the sa_family is AF_INET or AF_INET6, false otherwise.
bool IpCompliant(const struct sockaddr *addr) {
  return (addr->sa_family == AF_INET) || (addr->sa_family == AF_INET6);
}

bool IsIfAddrSupported(const struct ifaddrs *entry) {
  if (entry->ifa_addr && !IpCompliant(entry->ifa_addr)) return false;
  if (entry->ifa_netmask && !IpCompliant(entry->ifa_netmask)) return false;
  if (entry->ifa_ifu.ifu_dstaddr && !IpCompliant(entry->ifa_ifu.ifu_dstaddr)) {
    return false;
  }
  return true;
}

// Gets the number of ifaddr nodes in a linked list of ifaddrs. Skips nodes that
// have unsupported AF families.
size_t GetNumIfAddrs(struct ifaddrs *addrs) {
  size_t ret = 0;
  for (struct ifaddrs *addr = addrs; addr != nullptr; addr = addr->ifa_next) {
    if (IsIfAddrSupported(addr)) ret++;
  }
  return ret;
}

Status SerializeIfAddrs(primitives::MessageWriter *writer,
                        struct ifaddrs *ifaddr_list) {
  if (!writer || !ifaddr_list) {
    return {error::GoogleError::INVALID_ARGUMENT,
            "SerializeIfAddrs: NULL MessageWriter or ifaddrs provided."};
  }

  for (struct ifaddrs *addr = ifaddr_list; addr != nullptr;
       addr = addr->ifa_next) {
    // If the entry is of a format we don't support, don't include it.
    if (!IsIfAddrSupported(addr)) continue;

    // We push 5 entries per ifaddr to output.
    writer->PushString(addr->ifa_name);
    writer->Push<uint32_t>(addr->ifa_flags);
    ASYLO_RETURN_IF_ERROR(MakeStatus(writer->PushSockAddr(addr->ifa_addr)));
    ASYLO_RETURN_IF_ERROR(MakeStatus(writer->PushSockAddr(addr->ifa_netmask)));
    ASYLO_RETURN_IF_ERROR(
        MakeStatus(writer->PushSockAddr(addr->ifa_ifu.ifu_dstaddr)));
  }

  return Status::OkStatus();
}

Status SerializeAddrInfo(primitives::MessageWriter *writer,
                         struct addrinfo *addrs) {
  for (struct addrinfo *addr = addrs; addr != nullptr; addr = addr->ai_next) {
    // We push 6 entries per addrinfo to output.
    writer->Push<int>(addr->ai_flags);
    writer->Push<int>(addr->ai_family);
    writer->Push<int>(addr->ai_socktype);
    writer->Push<int>(addr->ai_protocol);
    writer->PushByCopy(
        Extent{reinterpret_cast<char *>(addr->ai_addr), addr->ai_addrlen});
    writer->PushByCopy(Extent{
        addr->ai_canonname,
        addr->ai_canonname == nullptr ? 0 : strlen(addr->ai_canonname) + 1});
  }

  return Status::OkStatus();
}

}  // namespace

Status SystemCallHandler(const std::shared_ptr<primitives::Client> &client,
                         void *context, primitives::MessageReader *input,
                         primitives::MessageWriter *output) {
  ASYLO_RETURN_IF_INCORRECT_READER_ARGUMENTS(*input, 1);
  auto request = input->next();

  Extent response;  // To be owned by untrusted call parameters.
  primitives::PrimitiveStatus status =
      system_call::UntrustedInvoke(request, &response);
  if (!status.ok()) {
    return primitives::MakeStatus(status);
  }
  output->PushByCopy(response);
  free(response.data());

  return Status::OkStatus();
}

Status IsAttyHandler(const std::shared_ptr<primitives::Client> &client,
                     void *context, primitives::MessageReader *input,
                     primitives::MessageWriter *output) {
  ASYLO_RETURN_IF_INCORRECT_READER_ARGUMENTS(*input, 1);
  int fd = input->next<int>();
  output->Push<int>(isatty(fd));  // Push return value first.
  output->Push<int>(
      errno);  // Push errno next. We always push the errno on the MessageWriter
               // regardless of the return value of the host call. The caller is
               // responsible for evaluating the return value and setting the
               // errno appropriately in its local environment.
  return Status::OkStatus();
}

Status USleepHandler(const std::shared_ptr<primitives::Client> &client,
                     void *context, primitives::MessageReader *input,
                     primitives::MessageWriter *output) {
  ASYLO_RETURN_IF_INCORRECT_READER_ARGUMENTS(*input, 1);
  auto usec = input->next<useconds_t>();
  output->Push<int>(usleep(usec));  // Push return value first.
  output->Push<int>(errno);         // Push errno next.
  return Status::OkStatus();
}

Status SysconfHandler(const std::shared_ptr<primitives::Client> &client,
                      void *context, primitives::MessageReader *input,
                      primitives::MessageWriter *output) {
  ASYLO_RETURN_IF_INCORRECT_READER_ARGUMENTS(*input, 1);
  int kLinux_name = input->next<int>();
  output->Push<int64_t>(sysconf(kLinux_name));  // Push return value first.
  output->Push<int>(errno);                     // Push errno next.
  return Status::OkStatus();
}

Status ReallocHandler(const std::shared_ptr<primitives::Client> &client,
                      void *context, primitives::MessageReader *input,
                      primitives::MessageWriter *output) {
  ASYLO_RETURN_IF_INCORRECT_READER_ARGUMENTS(*input, 2);
  void *in_ptr = input->next<void *>();
  size_t size = input->next<size_t>();
  void *out_ptr = realloc(in_ptr, size);
  output->Push(reinterpret_cast<uint64_t>(out_ptr));
  output->Push<int>(errno);
  return Status::OkStatus();
}

Status SleepHandler(const std::shared_ptr<primitives::Client> &client,
                    void *context, primitives::MessageReader *input,
                    primitives::MessageWriter *output) {
  ASYLO_RETURN_IF_INCORRECT_READER_ARGUMENTS(*input, 1);
  auto seconds = input->next<uint32_t>();
  output->Push<uint32_t>(sleep(seconds));  // Push return value first.
  output->Push<int>(errno);                // Push errno next.
  return Status::OkStatus();
}

Status SendMsgHandler(const std::shared_ptr<primitives::Client> &client,
                      void *context, primitives::MessageReader *input,
                      primitives::MessageWriter *output) {
  ASYLO_RETURN_IF_INCORRECT_READER_ARGUMENTS(*input, 6);
  struct msghdr msg;
  int sockfd = input->next<int>();

  auto msg_name_extent = input->next();
  msg.msg_name = msg_name_extent.As<char>();
  msg.msg_namelen = msg_name_extent.size();

  auto msg_iov_extent = input->next();
  // The message is serialized into a single buffer on the trusted side.
  struct iovec msg_iov[1];
  memset(msg_iov, 0, sizeof(*msg_iov));
  msg_iov[0].iov_base = msg_iov_extent.As<char>();
  msg_iov[0].iov_len = msg_iov_extent.size();
  msg.msg_iov = msg_iov;
  msg.msg_iovlen = 1;

  auto msg_control_extent = input->next();
  msg.msg_control = msg_control_extent.As<char>();
  msg.msg_controllen = msg_control_extent.size();

  msg.msg_flags = input->next<int>();

  int flags = input->next<int>();
  output->Push<int64_t>(sendmsg(sockfd, &msg, flags));  // Push return value.
  output->Push<int>(errno);                             // Push errno.
  return Status::OkStatus();
}

Status RecvMsgHandler(const std::shared_ptr<primitives::Client> &client,
                      void *context, primitives::MessageReader *input,
                      primitives::MessageWriter *output) {
  ASYLO_RETURN_IF_INCORRECT_READER_ARGUMENTS(*input, 6);
  int sockfd = input->next<int>();

  // An upper bound of buffer size for name/control to avoid allocating memory
  // for a non-initialized random size.
  constexpr size_t kMaxBufferSize = 1024;

  struct msghdr msg;
  msg.msg_namelen = input->next<uint64_t>();
  std::unique_ptr<char[]> msg_name_buffer(nullptr);
  if (msg.msg_namelen > 0 && msg.msg_namelen < kMaxBufferSize) {
    msg_name_buffer.reset(new char[msg.msg_namelen]);
  } else {
    msg.msg_namelen = 0;
  }
  msg.msg_name = msg_name_buffer.get();

  // Receive message in a single buffer, which will be copied into the scattered
  // buffers once back inside the enclave.
  msg.msg_iovlen = 1;
  struct iovec msg_iov[1];
  memset(msg_iov, 0, sizeof(*msg_iov));
  msg_iov[0].iov_len = input->next<uint64_t>();
  std::unique_ptr<char[]> msg_iov_buffer(nullptr);
  if (msg_iov[0].iov_len > 0) {
    msg_iov_buffer.reset(new char[msg_iov[0].iov_len]);
  }
  msg_iov[0].iov_base = msg_iov_buffer.get();
  msg.msg_iov = msg_iov;

  msg.msg_controllen = input->next<uint64_t>();
  std::unique_ptr<char[]> msg_control_buffer(nullptr);
  if (msg.msg_controllen > 0 && msg.msg_controllen < kMaxBufferSize) {
    msg_control_buffer.reset(new char[msg.msg_controllen]);
  } else {
    msg.msg_controllen = 0;
  }
  msg.msg_control = msg_control_buffer.get();

  msg.msg_flags = input->next<int>();
  int flags = input->next<int>();

  output->Push<int64_t>(recvmsg(sockfd, &msg, flags));  // Push return value.
  output->Push<int>(errno);                             // Push errno.
  output->PushByCopy(Extent{msg.msg_name, msg.msg_namelen});  // Push msg name.
  output->PushByCopy(Extent{msg.msg_iov[0].iov_base,
                            msg.msg_iov[0].iov_len});  // Push received msg.
  output->PushByCopy(
      Extent{msg.msg_control, msg.msg_controllen});  // Push control msg.

  return Status::OkStatus();
}

Status GetSocknameHandler(const std::shared_ptr<primitives::Client> &client,
                          void *context, primitives::MessageReader *input,
                          primitives::MessageWriter *output) {
  ASYLO_RETURN_IF_INCORRECT_READER_ARGUMENTS(*input, 1);
  int sockfd = input->next<int>();
  struct sockaddr_storage sock_addr;
  socklen_t sock_len = sizeof(sock_addr);

  int ret = getsockname(sockfd, reinterpret_cast<struct sockaddr *>(&sock_addr),
                        &sock_len);

  LOG_IF(FATAL, sock_len > sizeof(sock_addr))
      << "Insufficient sockaddr buf space encountered for getsockname host "
         "call";

  output->Push<int>(ret);
  output->Push<int>(errno);
  output->Push<struct sockaddr_storage>(sock_addr);

  return Status::OkStatus();
}

Status AcceptHandler(const std::shared_ptr<primitives::Client> &client,
                     void *context, primitives::MessageReader *input,
                     primitives::MessageWriter *output) {
  ASYLO_RETURN_IF_INCORRECT_READER_ARGUMENTS(*input, 1);
  int sockfd = input->next<int>();
  struct sockaddr_storage sock_addr;
  socklen_t sock_len = sizeof(sock_addr);

  int ret = accept(sockfd, reinterpret_cast<struct sockaddr *>(&sock_addr),
                   &sock_len);

  LOG_IF(FATAL, sock_len > sizeof(sock_addr))
      << "Insufficient sockaddr buf space encountered for accept host call.";

  output->Push<int>(ret);
  output->Push<int>(errno);
  output->Push<struct sockaddr_storage>(sock_addr);

  return Status::OkStatus();
}

Status GetPeernameHandler(const std::shared_ptr<primitives::Client> &client,
                          void *context, primitives::MessageReader *input,
                          primitives::MessageWriter *output) {
  ASYLO_RETURN_IF_INCORRECT_READER_ARGUMENTS(*input, 1);
  int sockfd = input->next<int>();
  struct sockaddr_storage sock_addr;
  socklen_t sock_len = sizeof(sock_addr);

  int ret = getpeername(sockfd, reinterpret_cast<struct sockaddr *>(&sock_addr),
                        &sock_len);

  LOG_IF(FATAL, sock_len > sizeof(sock_addr))
      << "Insufficient sockaddr buf space encountered for getpeername host "
         "call.";

  output->Push<int>(ret);
  output->Push<int>(errno);
  output->Push<struct sockaddr_storage>(sock_addr);

  return Status::OkStatus();
}

Status RecvFromHandler(const std::shared_ptr<primitives::Client> &client,
                       void *context, primitives::MessageReader *input,
                       primitives::MessageWriter *output) {
  ASYLO_RETURN_IF_INCORRECT_READER_ARGUMENTS(*input, 3);
  int sockfd = input->next<int>();
  size_t len = input->next<size_t>();
  int klinux_flags = input->next<int>();
  struct sockaddr_storage sock_addr;
  socklen_t sock_len = sizeof(sock_addr);

  auto buffer = absl::make_unique<char[]>(len);
  ssize_t ret = recvfrom(
      sockfd, reinterpret_cast<void *>(buffer.get()), len, klinux_flags,
      reinterpret_cast<struct sockaddr *>(&sock_addr), &sock_len);

  LOG_IF(FATAL, sock_len > sizeof(sock_addr))
      << "Insufficient sockaddr buf space encountered for recvfrom host call.";

  output->Push<int>(ret);
  output->Push<int>(errno);
  output->PushByCopy(Extent{buffer.get(), len});
  output->Push<struct sockaddr_storage>(sock_addr);

  return Status::OkStatus();
}

Status RaiseHandler(const std::shared_ptr<primitives::Client> &client,
                    void *context, primitives::MessageReader *input,
                    primitives::MessageWriter *output) {
  ASYLO_RETURN_IF_INCORRECT_READER_ARGUMENTS(*input, 1);
  int klinux_sig = input->next<int>();
  output->Push<int>(raise(klinux_sig));
  output->Push<int>(errno);
  return Status::OkStatus();
}

Status GetSockOptHandler(const std::shared_ptr<primitives::Client> &client,
                         void *context, primitives::MessageReader *input,
                         primitives::MessageWriter *output) {
  ASYLO_RETURN_IF_INCORRECT_READER_ARGUMENTS(*input, 4);

  int sockfd = input->next<int>();
  int level = input->next<int>();
  int klinux_optname = input->next<int>();
  Extent optval = input->next();
  socklen_t optlen = optval.size();

  int ret = getsockopt(sockfd, level, klinux_optname, optval.data(), &optlen);

  output->Push<int>(ret);
  output->Push<int>(errno);
  output->PushByCopy(Extent{reinterpret_cast<char *>(optval.data()), optlen});
  return Status::OkStatus();
}

Status GetAddrInfoHandler(const std::shared_ptr<primitives::Client> &client,
                          void *context, primitives::MessageReader *input,
                          primitives::MessageWriter *output) {
  bool hints_provided = input->size() == 6;
  if (!hints_provided) {
    ASYLO_RETURN_IF_INCORRECT_READER_ARGUMENTS(*input, 2);
  }

  Extent node_buffer = input->next();
  Extent service_buffer = input->next();
  const char *node = node_buffer.empty() ? nullptr : node_buffer.As<char>();
  const char *service =
      service_buffer.empty() ? nullptr : service_buffer.As<char>();

  struct addrinfo hints {};
  if (hints_provided) {
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_flags = input->next<int>();
    hints.ai_family = input->next<int>();
    hints.ai_socktype = input->next<int>();
    hints.ai_protocol = input->next<int>();
    hints.ai_canonname = nullptr;
    hints.ai_addr = nullptr;
    hints.ai_next = nullptr;
  }
  struct addrinfo *result;
  output->Push<int>(
      getaddrinfo(node, service, hints_provided ? &hints : nullptr, &result));
  output->Push<int>(errno);
  output->Push<uint64_t>(GetNumAddrinfos(result));
  ASYLO_RETURN_IF_ERROR(SerializeAddrInfo(output, result));

  // result has been copied to MessageWriter, so free it up.
  freeaddrinfo(result);
  return Status::OkStatus();
}

Status InetPtonHandler(const std::shared_ptr<primitives::Client> &client,
                       void *context, primitives::MessageReader *input,
                       primitives::MessageWriter *output) {
  ASYLO_RETURN_IF_INCORRECT_READER_ARGUMENTS(*input, 2);
  int klinux_af = input->next<int>();
  auto src_buffer = input->next();

  size_t dst_len = 0;
  if (klinux_af == AF_INET) {
    dst_len = sizeof(struct in_addr);
  } else if (klinux_af == AF_INET6) {
    dst_len = sizeof(struct in6_addr);
  } else {
    return {error::GoogleError::INVALID_ARGUMENT,
            "InetPtonHandler: Unrecognized af_family encountered."};
  }
  auto dst = absl::make_unique<char[]>(dst_len);
  output->Push<int>(inet_pton(klinux_af, src_buffer.As<char>(),
                              reinterpret_cast<void *>(dst.get())));
  output->Push<int>(errno);
  output->PushByCopy(Extent{dst.get(), dst_len});

  return Status::OkStatus();
}

Status InetNtopHandler(const std::shared_ptr<primitives::Client> &client,
                       void *context, primitives::MessageReader *input,
                       primitives::MessageWriter *output) {
  ASYLO_RETURN_IF_INCORRECT_READER_ARGUMENTS(*input, 3);
  int klinux_af = input->next<int>();
  auto src_buffer = input->next();
  auto size = input->next<socklen_t>();

  auto dst = absl::make_unique<char[]>(size);
  inet_ntop(klinux_af, src_buffer.data(), dst.get(), size);

  output->PushByCopy(Extent{dst.get(), size});
  output->Push<int>(errno);

  return Status::OkStatus();
}

Status SigprocmaskHandler(const std::shared_ptr<primitives::Client> &client,
                          void *context, primitives::MessageReader *input,
                          primitives::MessageWriter *output) {
  ASYLO_RETURN_IF_INCORRECT_READER_ARGUMENTS(*input, 2);

  int klinux_how = input->next<int>();
  sigset_t klinux_set = input->next<sigset_t>();
  sigset_t klinux_oldset;

  output->Push<int>(sigprocmask(klinux_how, &klinux_set, &klinux_oldset));
  output->Push<int>(errno);
  output->Push<sigset_t>(klinux_oldset);
  return Status::OkStatus();
}

Status IfNameToIndexHandler(const std::shared_ptr<primitives::Client> &client,
                            void *context, primitives::MessageReader *input,
                            primitives::MessageWriter *output) {
  ASYLO_RETURN_IF_INCORRECT_READER_ARGUMENTS(*input, 1);

  Extent ifname_buffer = input->next();
  output->Push<unsigned int>(if_nametoindex(ifname_buffer.As<char>()));
  output->Push<int>(errno);

  return Status::OkStatus();
}

Status IfIndexToNameHandler(const std::shared_ptr<primitives::Client> &client,
                            void *context, primitives::MessageReader *input,
                            primitives::MessageWriter *output) {
  ASYLO_RETURN_IF_INCORRECT_READER_ARGUMENTS(*input, 1);

  unsigned int ifindex = input->next<unsigned int>();
  char ifname[IF_NAMESIZE];
  output->PushString(if_indextoname(ifindex, ifname));
  output->Push<int>(errno);

  return Status::OkStatus();
}

Status GetIfAddrsHandler(const std::shared_ptr<primitives::Client> &client,
                         void *context, primitives::MessageReader *input,
                         primitives::MessageWriter *output) {
  ASYLO_RETURN_IF_READER_NOT_EMPTY(*input);
  struct ifaddrs *ifaddr_list = nullptr;
  output->Push<int>(getifaddrs(&ifaddr_list));
  output->Push<int>(errno);
  output->Push<uint64_t>(GetNumIfAddrs(ifaddr_list));
  ASYLO_RETURN_IF_ERROR(SerializeIfAddrs(output, ifaddr_list));

  freeifaddrs(ifaddr_list);
  return Status::OkStatus();
}

}  // namespace host_call
}  // namespace asylo
