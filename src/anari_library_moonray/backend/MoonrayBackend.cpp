// Copyright 2026 openmoonray-anari contributors
// SPDX-License-Identifier: Apache-2.0

#include "MoonrayBackend.h"

// moonray
#include <moonray/rendering/rndr/RenderContext.h>
#include <moonray/rendering/rndr/RenderOptions.h>
// std
#include <cstdio>
#include <cstdlib>

namespace moonray_anari {

namespace {

// The process-global instance. weak_ptr so the driver is torn down again once
// the last device is gone, which mirrors what moonray.exe does at exit
// (~RenderContext, then cleanUpGlobalDriver, all on the same thread).
std::mutex g_instanceMutex;
std::weak_ptr<MoonrayBackend> g_instance;

} // namespace

std::shared_ptr<MoonrayBackend> MoonrayBackend::acquire(std::string &error)
{
  std::lock_guard<std::mutex> guard(g_instanceMutex);

  if (auto existing = g_instance.lock())
    return existing;

  std::shared_ptr<MoonrayBackend> backend(new MoonrayBackend);
  if (!backend->start(error))
    return {};

  g_instance = backend;
  return backend;
}

MoonrayBackend::MoonrayBackend() = default;

MoonrayBackend::~MoonrayBackend()
{
  if (!m_thread.joinable())
    return;

  {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_quit = true;
  }
  m_taskPosted.notify_one();
  m_thread.join();
}

bool MoonrayBackend::start(std::string &error)
{
  m_thread = std::thread([this]() { threadMain(); });

  std::unique_lock<std::mutex> lock(m_mutex);
  m_taskDone.wait(lock, [this]() { return m_started || !m_startError.empty(); });

  if (!m_startError.empty()) {
    error = m_startError;
    lock.unlock();
    // Let the thread finish; it never entered the task loop.
    if (m_thread.joinable())
      m_thread.join();
    return false;
  }

  return true;
}

void MoonrayBackend::threadMain()
{
  // --- global driver init, on this thread and only here -------------------
  try {
    m_options = std::make_unique<moonray::rndr::RenderOptions>();

    // RenderOptions::getDsoPath() already falls back to scene_rdl2's
    // DsoFinder, which honors RDL2_DSO_PATH and the <exe>/../rdl2dso layout;
    // recording the result is what lets a failure name the directory that was
    // actually searched.
    m_dsoPath = m_options->getDsoPath();

    moonray::rndr::initGlobalDriver(*m_options);
  } catch (const std::exception &e) {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_startError = std::string("MoonRay global driver init failed: ") + e.what();
    m_taskDone.notify_all();
    return;
  } catch (...) {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_startError = "MoonRay global driver init failed (unknown exception)";
    m_taskDone.notify_all();
    return;
  }

  {
    std::lock_guard<std::mutex> guard(m_mutex);
    m_started = true;
  }
  m_taskDone.notify_all();

  // --- task loop ----------------------------------------------------------
  for (;;) {
    const std::function<void()> *task = nullptr;
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_taskPosted.wait(lock, [this]() { return m_task != nullptr || m_quit; });
      if (m_quit && m_task == nullptr)
        break;
      task = m_task;
    }

    std::exception_ptr err;
    try {
      (*task)();
    } catch (...) {
      err = std::current_exception();
    }

    {
      std::lock_guard<std::mutex> guard(m_mutex);
      m_task = nullptr;
      m_taskError = err;
      m_taskComplete = true;
    }
    m_taskDone.notify_one();
  }

  // Everything MoonRay-owned must be gone before this point: the device
  // destroys its RenderContext through execute() before releasing its
  // reference to this backend.
  //
  // An exception here would leave this thread function, which is
  // std::terminate() - and this runs while the last device is being
  // destroyed, so there is nobody left to tell anyway.
  try {
    moonray::rndr::cleanUpGlobalDriver();
  } catch (const std::exception &e) {
    std::fprintf(stderr,
        "[moonray] releasing the global driver failed: %s\n",
        e.what());
  } catch (...) {
    std::fprintf(stderr,
        "[moonray] releasing the global driver failed with an unknown "
        "exception\n");
  }
}

void MoonrayBackend::execute(const std::function<void()> &fn)
{
  // One MoonRay operation at a time (docs/adr/0005).
  std::lock_guard<std::mutex> callGuard(m_callMutex);

  std::unique_lock<std::mutex> lock(m_mutex);
  m_task = &fn;
  m_taskComplete = false;
  m_taskError = nullptr;
  m_taskPosted.notify_one();
  m_taskDone.wait(lock, [this]() { return m_taskComplete; });

  auto err = m_taskError;
  m_taskError = nullptr;
  lock.unlock();

  if (err)
    std::rethrow_exception(err);
}

moonray::rndr::RenderOptions &MoonrayBackend::options()
{
  return *m_options;
}

const std::string &MoonrayBackend::dsoPath() const
{
  return m_dsoPath;
}

} // namespace moonray_anari
