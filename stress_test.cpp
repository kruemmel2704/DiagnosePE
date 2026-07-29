#include "stress_test.h"
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#endif

StressTest::StressTest() {
    isRunning.store(false);
    dummyResult.store(0.0f);
}

StressTest::~StressTest() {
    Stop();
}

void StressTest::WorkerLoop() {
    // Set priority to below normal to keep UI responsive
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif

    double val = 12345.67;
    int counter = 0;

    while (isRunning.load(std::memory_order_relaxed)) {
        for (int j = 0; j < 1000; ++j) {
            val = std::sqrt(val * 1.0000001 + j);
            val = std::sin(val) * std::cos(val);
        }

        if (val > 1000000.0 || std::isnan(val) || std::isinf(val)) {
            val = 12345.67;
        }

        dummyResult.store(static_cast<float>(val), std::memory_order_relaxed);

        counter++;
        if (counter % 500 == 0) {
            // Briefly yield execution
            std::this_thread::yield();
        }
    }
}

void StressTest::Start(int threadCount) {
    if (isRunning.load()) {
        return;
    }

    isRunning.store(true);
    workers.clear();
    workers.reserve(threadCount);

    for (int i = 0; i < threadCount; ++i) {
        workers.emplace_back(&StressTest::WorkerLoop, this);
    }
}

void StressTest::Stop() {
    if (!isRunning.load()) {
        return;
    }

    isRunning.store(false);

    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    workers.clear();
}

bool StressTest::IsRunning() const {
    return isRunning.load();
}
