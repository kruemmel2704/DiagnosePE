#pragma once
#include <vector>
#include <thread>
#include <atomic>

class StressTest {
private:
    std::vector<std::thread> workers;
    std::atomic<bool> isRunning;
    std::atomic<float> dummyResult; // Volatile-like target to prevent compiler optimization

    void WorkerLoop();

public:
    StressTest();
    ~StressTest();

    void Start(int threadCount);
    void Stop();
    bool IsRunning() const;
};
