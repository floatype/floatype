#include "floatype/zenz/scorer_client.h"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "base/clock.h"
#include "floatype/scorer/protocol.h"

extern char** environ;

namespace mozc::floatype {
namespace {

using ::floatype::scorer::FormatRequest;
using ::floatype::scorer::ParseResponse;
using ::floatype::scorer::Request;
using ::floatype::scorer::Response;
using ::floatype::scorer::TakeLatestLine;

// Spawns `command` with `child_fd` as its stdin and stdout. Every other file
// descriptor except stderr is closed in the child, and signals are reset.
pid_t Spawn(const std::vector<std::string>& command, int child_fd) {
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, child_fd, STDIN_FILENO);
  posix_spawn_file_actions_adddup2(&actions, child_fd, STDOUT_FILENO);
  posix_spawn_file_actions_addinherit_np(&actions, STDERR_FILENO);

  posix_spawnattr_t attributes;
  posix_spawnattr_init(&attributes);
  sigset_t all_signals;
  sigfillset(&all_signals);
  posix_spawnattr_setsigdefault(&attributes, &all_signals);
  sigset_t no_signals;
  sigemptyset(&no_signals);
  posix_spawnattr_setsigmask(&attributes, &no_signals);
  posix_spawnattr_setflags(&attributes, POSIX_SPAWN_CLOEXEC_DEFAULT |
                                            POSIX_SPAWN_SETSIGDEF |
                                            POSIX_SPAWN_SETSIGMASK);

  std::vector<char*> argv;
  argv.reserve(command.size() + 1);
  for (const std::string& arg : command) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  argv.push_back(nullptr);

  pid_t pid = -1;
  const int error =
      posix_spawn(&pid, argv[0], &actions, &attributes, argv.data(), environ);
  posix_spawnattr_destroy(&attributes);
  posix_spawn_file_actions_destroy(&actions);
  if (error != 0) {
    LOG(WARNING) << "Cannot start the scorer " << command[0] << ": errno "
                 << error;
    return -1;
  }
  return pid;
}

}  // namespace

ScorerClient::ScorerClient(std::vector<std::string> command)
    : command_(std::move(command)) {}

ScorerClient::~ScorerClient() { Stop(); }

bool ScorerClient::Start() {
  const absl::Time now = Clock::GetAbslTime();
  if (command_.empty() || now - last_start_ < kRestartInterval) {
    return false;
  }
  last_start_ = now;

  int fds[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
    LOG(WARNING) << "socketpair failed: errno " << errno;
    return false;
  }
  const int parent_fd = fds[0];
  const int child_fd = fds[1];
  // Writing to a dead scorer must fail with EPIPE instead of killing Mozc.
  const int on = 1;
  setsockopt(parent_fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
  fcntl(parent_fd, F_SETFL, fcntl(parent_fd, F_GETFL) | O_NONBLOCK);
  fcntl(parent_fd, F_SETFD, FD_CLOEXEC);

  pid_ = Spawn(command_, child_fd);
  close(child_fd);
  if (pid_ < 0) {
    close(parent_fd);
    return false;
  }
  fd_ = parent_fd;
  buffer_.clear();
  return true;
}

void ScorerClient::Stop() {
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
  if (pid_ > 0) {
    // Closing the socket already makes the scorer exit; the signal also stops
    // one that is in the middle of a conversion.
    kill(pid_, SIGTERM);
    while (waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {
    }
    pid_ = -1;
  }
  buffer_.clear();
}

bool ScorerClient::Send(const Request& request) {
  if (fd_ < 0 && !Start()) {
    return false;
  }
  const std::string line = FormatRequest(request);
  const ssize_t written = send(fd_, line.data(), line.size(), 0);
  if (written == static_cast<ssize_t>(line.size())) {
    return true;
  }
  if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    return false;  // The scorer is far behind; skip this request.
  }
  // A dead scorer, or a partial line that would corrupt the next request.
  Stop();
  return false;
}

std::optional<Response> ScorerClient::Poll() {
  char chunk[4096];
  while (fd_ >= 0) {
    const ssize_t n = recv(fd_, chunk, sizeof(chunk), 0);
    if (n > 0) {
      buffer_.append(chunk, n);
    } else if (n < 0 && errno == EINTR) {
      continue;
    } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      break;
    } else {
      Stop();  // The scorer exited or crashed.
    }
  }
  std::string line;
  if (TakeLatestLine(&buffer_, &line) > 0) {
    if (std::optional<Response> response = ParseResponse(line)) {
      latest_ = std::move(response);
    }
  }
  return latest_;
}

}  // namespace mozc::floatype
