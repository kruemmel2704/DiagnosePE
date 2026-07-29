#pragma once
#include <thread>
#include <mutex>
#include <string>

struct RamTestProgress {
    float percentDone = 0.0f;
    size_t bytesTested = 0;
    size_t bytesTotal = 0;
    int errorsFound = 0;
    int currentPass = 0;
    bool isCompleted = false;
    bool isAllocationPhase = true;
    std::wstring statusMsg = L"Bereit";
};

class RamTest {
private:
    std::thread workerThread;
    bool isRunning = false;
    RamTestProgress progress;
    size_t testSizeInBytes = 0;
    mutable std::mutex stateMutex;

    void TestLoop();

public:
    RamTest();
    ~RamTest();

    void Start(size_t sizeInBytes);
    void Stop();
    bool IsRunning() const;
    
    RamTestProgress GetProgress() const;
};
