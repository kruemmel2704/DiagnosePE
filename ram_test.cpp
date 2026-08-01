#include "ram_test.h"
#include "platform_compat.h"
#include <chrono>
#include <vector>

RamTest::RamTest() {
    isRunning = false;
}

RamTest::~RamTest() {
    Stop();
}

void RamTest::Start(size_t sizeInBytes) {
    Stop();

    std::lock_guard<std::mutex> lock(stateMutex);
    testSizeInBytes = sizeInBytes;
    progress.percentDone = 0.0f;
    progress.bytesTested = 0;
    progress.bytesTotal = sizeInBytes;
    progress.errorsFound = 0;
    progress.currentPass = 1;
    progress.isCompleted = false;
    progress.isAllocationPhase = true;
    progress.statusMsg = L"Allokiere Arbeitsspeicher...";

    isRunning = true;
    workerThread = std::thread(&RamTest::TestLoop, this);
}

void RamTest::Stop() {
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        if (!isRunning) {
            return;
        }
        isRunning = false;
    }

    if (workerThread.joinable()) {
        workerThread.join();
    }
}

bool RamTest::IsRunning() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return isRunning;
}

RamTestProgress RamTest::GetProgress() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return progress;
}

void RamTest::TestLoop() {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif

    struct RamChunk {
        uint32_t* ptr;
        size_t numElements;
    };

    std::vector<RamChunk> chunks;
    size_t numElements = testSizeInBytes / sizeof(uint32_t);
    size_t tempSize = testSizeInBytes;
    const size_t maxChunkSize = 256 * 1024 * 1024; // 256 MB per chunk

    try {
        while (tempSize > 0) {
            size_t currentChunkSize = std::min(tempSize, maxChunkSize);
            size_t numEl = currentChunkSize / sizeof(uint32_t);
            if (numEl > 0) {
                uint32_t* p = new uint32_t[numEl];
                chunks.push_back({p, numEl});
            }
            tempSize -= currentChunkSize;
        }
    } catch (const std::bad_alloc&) {
        for (auto& c : chunks) {
            delete[] c.ptr;
        }
        std::lock_guard<std::mutex> lock(stateMutex);
        progress.statusMsg = L"Fehler: Nicht gen\u00FCgend freier Arbeitsspeicher!";
        progress.isCompleted = true;
        isRunning = false;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(stateMutex);
        progress.isAllocationPhase = false;
    }

    const uint32_t patterns[] = {
        0x55555555,
        0xAAAAAAAA,
        0x00000000,
        0xFFFFFFFF,
        0x12345678
    };
    const int numPatterns = sizeof(patterns) / sizeof(patterns[0]);

    int pass = 1;
    while (true) {
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            if (!isRunning || pass > 3) {
                break;
            }
            progress.currentPass = pass;
        }

        for (int pIdx = 0; pIdx < numPatterns; ++pIdx) {
            uint32_t pattern = patterns[pIdx];

            {
                std::lock_guard<std::mutex> lock(stateMutex);
                if (!isRunning) break;
                wchar_t statusBuf[128];
                swprintf_s(statusBuf, L"Durchgang %d - Muster 0x%08X schreiben...", pass, pattern);
                progress.statusMsg = statusBuf;
            }

            // 1. Write Phase
            size_t elementsTested = 0;
            for (size_t cIdx = 0; cIdx < chunks.size(); ++cIdx) {
                uint32_t* chunkPtr = chunks[cIdx].ptr;
                size_t chunkElements = chunks[cIdx].numElements;
                for (size_t i = 0; i < chunkElements; ++i) {
                    if (elementsTested % 65536 == 0) {
                        std::lock_guard<std::mutex> lock(stateMutex);
                        if (!isRunning) break;
                        float progressFraction = (float)elementsTested / (float)numElements * 0.5f;
                        progress.percentDone = progressFraction;
                        progress.bytesTested = elementsTested * sizeof(uint32_t);
                    }
                    chunkPtr[i] = pattern;
                    elementsTested++;
                }
                if (!isRunning) break;
            }

            {
                std::lock_guard<std::mutex> lock(stateMutex);
                if (!isRunning) break;
                wchar_t statusBuf[128];
                swprintf_s(statusBuf, L"Durchgang %d - Muster 0x%08X verifizieren...", pass, pattern);
                progress.statusMsg = statusBuf;
            }

            // 2. Read and Verify Phase
            elementsTested = 0;
            for (size_t cIdx = 0; cIdx < chunks.size(); ++cIdx) {
                uint32_t* chunkPtr = chunks[cIdx].ptr;
                size_t chunkElements = chunks[cIdx].numElements;
                for (size_t i = 0; i < chunkElements; ++i) {
                    if (elementsTested % 65536 == 0) {
                        std::lock_guard<std::mutex> lock(stateMutex);
                        if (!isRunning) break;
                        float progressFraction = 0.5f + (float)elementsTested / (float)numElements * 0.5f;
                        progress.percentDone = progressFraction;
                        progress.bytesTested = elementsTested * sizeof(uint32_t);
                    }

                    uint32_t val = chunkPtr[i];
                    if (val != pattern) {
                        std::lock_guard<std::mutex> lock(stateMutex);
                        progress.errorsFound++;
                        chunkPtr[i] = pattern; // Reset to expected to avoid cascading
                    }
                    elementsTested++;
                }
                if (!isRunning) break;
            }
        }

        pass++;
    }

    // Clean up allocated chunks
    for (auto& c : chunks) {
        delete[] c.ptr;
    }

    std::lock_guard<std::mutex> lock(stateMutex);
    if (isRunning) {
        progress.statusMsg = progress.errorsFound == 0 ? L"Erfolgreich beendet (Keine Fehler)" : L"Abgeschlossen mit Fehlern!";
        progress.isCompleted = true;
        progress.percentDone = 1.0f;
        isRunning = false;
    }
}
