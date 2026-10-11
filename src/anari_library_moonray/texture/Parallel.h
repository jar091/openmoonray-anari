// Copyright 2026 openmoonray-anari contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

// std
#include <algorithm>
#include <cstddef>
#include <thread>
#include <vector>

namespace moonray_anari {
namespace texture {

// Calls fn(begin, end) for consecutive parts of [0, count), on a few threads
// when there are at least `grain` items per thread to make that worthwhile.
// Preparing a texture - converting, transforming and filtering every texel of
// a 4K image - is what an edit in an interactive session waits for; it runs
// on the application's thread or the scene sync, where MoonRay's own thread
// pool is not at work.
template <typename Function>
void parallelRanges(size_t count, size_t grain, Function &&fn)
{
  const size_t hardware = std::max(1u, std::thread::hardware_concurrency());
  const size_t threads = std::min<size_t>(
      {size_t(8), hardware, grain > 0 ? count / grain : count});
  if (threads <= 1) {
    fn(size_t(0), count);
    return;
  }

  std::vector<std::thread> workers;
  workers.reserve(threads - 1);
  const size_t step = (count + threads - 1) / threads;
  for (size_t t = 1; t < threads; ++t) {
    const size_t begin = std::min(count, t * step);
    const size_t end = std::min(count, begin + step);
    if (begin < end)
      workers.emplace_back([&fn, begin, end] { fn(begin, end); });
  }
  fn(size_t(0), std::min(count, step));
  for (auto &worker : workers)
    worker.join();
}

} // namespace texture
} // namespace moonray_anari
