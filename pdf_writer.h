#pragma once
#include <string>
#include <vector>
#include <sstream>

struct PdfDataPoint {
    std::wstring timeStr;
    float cpuLoad = 0.0f;
    float cpuTemp = 0.0f;
    float gpuLoad = 0.0f;
    float gpuTemp = 0.0f;
    float ramPercent = 0.0f;
};

class PdfWriter {
private:
    std::stringstream stream;
    std::vector<size_t> offsets;
    std::vector<std::string> objects;

    size_t AddObject(const std::string& content);
    std::string EscapeAndEncode(const std::wstring& text);

public:
    PdfWriter();
    
    void DrawPdfText(float x, float y, const std::wstring& text, int fontSize, bool bold = false, float r = 0.0f, float g = 0.0f, float b = 0.0f);
    void DrawLine(float x1, float y1, float x2, float y2, float width = 1.0f, float r = 0.0f, float g = 0.0f, float b = 0.0f);
    void DrawRect(float x, float y, float w, float h, float r = 0.95f, float g = 0.95f, float b = 0.95f, bool fill = true, float strokeWidth = 0.0f, float sr = 0.0f, float sg = 0.0f, float sb = 0.0f);
    
    bool Save(const std::wstring& filePath);

    static bool GenerateSystemReport(
        const std::wstring& filePath,
        const std::wstring& cpuName,
        const std::wstring& gpuName,
        const std::wstring& ramTotalDisplay,
        const std::wstring& sysModel,
        const std::wstring& sysSerial,
        const std::wstring& mbModel,
        const std::wstring& startTimeStr,
        const std::wstring& endTimeStr,
        const std::wstring& durationStr,
        const std::vector<PdfDataPoint>& dataPoints
    );

    static bool GenerateBatteryReport(
        const std::wstring& filePath,
        const std::wstring& cpuName,
        const std::wstring& gpuName,
        const std::wstring& ramTotalDisplay,
        const std::wstring& sysModel,
        const std::wstring& sysSerial,
        const std::wstring& mbModel,
        const std::wstring& batHealthDisplay,
        const std::wstring& batManufacturer,
        const std::wstring& batDeviceName,
        const std::wstring& batChemistry,
        const std::wstring& batDesignCapDisplay,
        const std::wstring& batFullChargeCapDisplay,
        const std::wstring& startTimeStr,
        const std::wstring& endTimeStr,
        const std::wstring& durationStr,
        double startPercent,
        double endPercent,
        const std::vector<std::wstring>& logEntries
    );
};
