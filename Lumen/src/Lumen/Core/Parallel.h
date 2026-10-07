#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <functional>
#include <thread>
#include <vector>

namespace Lumen {

	// Runs fn(i) for every i in [0, count) across the available hardware threads and waits for completion.
	// Iterations must be independent. Falls back to the calling thread for small counts or single-core machines.
	inline void ParallelFor(size_t count, const std::function<void(size_t)>& fn)
	{
		const size_t threadCount = std::min<size_t>(std::max(1u, std::thread::hardware_concurrency()), count);
		if (threadCount <= 1)
		{
			for (size_t i = 0; i < count; i++)
				fn(i);
			return;
		}

		std::atomic<size_t> next{ 0 };
		auto worker = [&]
		{
			for (size_t i = next++; i < count; i = next++)
				fn(i);
		};

		std::vector<std::thread> threads;
		threads.reserve(threadCount - 1);
		for (size_t t = 1; t < threadCount; t++)
			threads.emplace_back(worker);
		worker();
		for (std::thread& thread : threads)
			thread.join();
	}

}
