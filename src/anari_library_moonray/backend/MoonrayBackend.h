// Copyright 2026 openmoonray-anari contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace moonray {
namespace rndr {
class RenderOptions;
} // namespace rndr
} // namespace moonray

namespace moonray_anari {

// Process-global MoonRay renderer driver (docs/adr/0005).
//
// MoonRay is one renderer per process: a single global RenderDriver whose
// creation asserts it is the only one, and `initGlobalDriver()`,
// `RenderContext` construction, `startFrame()` and `stopFrame()` must all run
// on the *same* thread. ANARI calls arrive on arbitrary application threads,
// so this class owns one dedicated backend thread and every call into MoonRay
// is dispatched to it through execute().
//
// The instance is refcounted and shared by all devices in the process: a
// second ANARI device does not get a second renderer, it shares this one.
struct MoonrayBackend
{
  // Returns the process-global backend, creating (and initializing MoonRay's
  // global driver) on first use. Returns nullptr and fills `error` when the
  // driver could not be initialized.
  static std::shared_ptr<MoonrayBackend> acquire(std::string &error);

  ~MoonrayBackend();

  MoonrayBackend(const MoonrayBackend &) = delete;
  MoonrayBackend &operator=(const MoonrayBackend &) = delete;

  // Run `fn` on the backend thread and block until it has completed. Calls are
  // serialized: one MoonRay operation at a time (docs/adr/0005). An exception
  // thrown by `fn` is transported to the caller and rethrown here.
  void execute(const std::function<void()> &fn);

  // The RenderOptions the global driver was initialized with. RenderContext
  // instances must be constructed from the same options (thread count is fixed
  // at first init), so this is the object callers pass to it.
  moonray::rndr::RenderOptions &options();

  // Where the RDL2 shader DSOs were looked for, for error messages.
  const std::string &dsoPath() const;

 private:
  MoonrayBackend();

  bool start(std::string &error);
  void threadMain();

  std::thread m_thread;

  // Serializes callers of execute() against each other.
  std::mutex m_callMutex;

  // Handoff of one task to the backend thread.
  std::mutex m_mutex;
  std::condition_variable m_taskPosted;
  std::condition_variable m_taskDone;
  const std::function<void()> *m_task{nullptr};
  bool m_taskComplete{false};
  bool m_quit{false};
  std::exception_ptr m_taskError;

  // Set by the backend thread while starting up.
  bool m_started{false};
  std::string m_startError;

  std::unique_ptr<moonray::rndr::RenderOptions> m_options;
  std::string m_dsoPath;
};

} // namespace moonray_anari
