// Runs floatype_scorer as a child process and talks to it without blocking.

#ifndef MOZC_FLOATYPE_ZENZ_SCORER_CLIENT_H_
#define MOZC_FLOATYPE_ZENZ_SCORER_CLIENT_H_

#include <sys/types.h>

#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/time.h"
#include "floatype/scorer/protocol.h"

namespace mozc::floatype {

// The scorer is started on the first request or Warm(). When it dies, it is
// started again on a later request, at most once per kRestartInterval, so a
// scorer that cannot run does not cost a process launch on every conversion.
// One that ended itself after idling (its --idle_exit_seconds) started long
// enough ago to start again at once.
class ScorerClient {
 public:
  static constexpr absl::Duration kRestartInterval = absl::Seconds(10);

  // `command` is the scorer's argv, starting with the executable's path.
  explicit ScorerClient(std::vector<std::string> command);
  ScorerClient(const ScorerClient&) = delete;
  ScorerClient& operator=(const ScorerClient&) = delete;
  ~ScorerClient();

  // Arguments after `command`, used from the next start; a change stops a
  // running scorer.
  void SetArguments(std::vector<std::string> arguments);

  // Starts the scorer if it is not running, so that it has loaded the model
  // by the time a request comes. The start runs on another thread: a key
  // never waits for it (about 1.3ms).
  void Warm();

  // Sends a request. Returns false when the scorer cannot take it; then no
  // response will come for it.
  bool Send(const ::floatype::scorer::Request& request);

  // Reads what has arrived and returns the latest response so far, if any.
  // The response stays until a newer one arrives, so that every caller sharing
  // the scorer can look for its own request id.
  std::optional<::floatype::scorer::Response> Poll();

 private:
  bool Start() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void Stop() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  // Whether the scorer is running; cleans up after one that has ended.
  bool Running() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

  const std::vector<std::string> command_;
  // Warm() starts the scorer on warm_thread_ while the session goes on.
  absl::Mutex mutex_;
  std::thread warm_thread_;
  bool warming_ ABSL_GUARDED_BY(mutex_) = false;
  std::vector<std::string> arguments_ ABSL_GUARDED_BY(mutex_);
  int fd_ ABSL_GUARDED_BY(mutex_) = -1;
  pid_t pid_ ABSL_GUARDED_BY(mutex_) = -1;
  std::string buffer_ ABSL_GUARDED_BY(mutex_);
  std::optional<::floatype::scorer::Response> latest_ ABSL_GUARDED_BY(mutex_);
  absl::Time last_start_ ABSL_GUARDED_BY(mutex_) = absl::InfinitePast();
};

}  // namespace mozc::floatype

#endif  // MOZC_FLOATYPE_ZENZ_SCORER_CLIENT_H_
