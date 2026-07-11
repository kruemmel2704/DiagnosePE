#include "pdf_writer.h"
#include <windows.h>
#include <cmath>
#include <iomanip>
#include <algorithm>

PdfWriter::PdfWriter() {
    // Clear stream
    stream.str("");
    stream.clear();
}

std::string PdfWriter::EscapeAndEncode(const std::wstring& text) {
    std::string result;
    result.reserve(text.length());
    for (wchar_t w : text) {
        if (w == L'(' || w == L')' || w == L'\\') {
            result.push_back('\\');
            result.push_back(static_cast<char>(w));
        } else if (w >= 32 && w <= 126) {
            result.push_back(static_cast<char>(w));
        } else {
            // Map common German and other characters to ISO-8859-1 (WinAnsiEncoding)
            switch (w) {
                case L'ä': result.push_back(static_cast<char>(228)); break;
                case L'ö': result.push_back(static_cast<char>(246)); break;
                case L'ü': result.push_back(static_cast<char>(252)); break;
                case L'Ä': result.push_back(static_cast<char>(196)); break;
                case L'Ö': result.push_back(static_cast<char>(214)); break;
                case L'Ü': result.push_back(static_cast<char>(220)); break;
                case L'ß': result.push_back(static_cast<char>(223)); break;
                case L'°': result.push_back(static_cast<char>(176)); break; // degree symbol
                case L'Ø': result.push_back(static_cast<char>(216)); break;
                case L'ø': result.push_back(static_cast<char>(248)); break;
                default: result.push_back('?'); break;
            }
        }
    }
    return result;
}

void PdfWriter::DrawPdfText(float x, float y, const std::wstring& text, int fontSize, bool bold, float r, float g, float b) {
    float pdfY = 842.0f - y; // map y (top-left) to PDF y (bottom-left)
    stream << "BT\n";
    stream << r << " " << g << " " << b << " rg\n"; // text fill color
    if (bold) {
        stream << "/F2 " << fontSize << " Tf\n";
    } else {
        stream << "/F1 " << fontSize << " Tf\n";
    }
    stream << x << " " << pdfY << " Td\n";
    stream << "(" << EscapeAndEncode(text) << ") Tj\n";
    stream << "ET\n";
}

void PdfWriter::DrawLine(float x1, float y1, float x2, float y2, float width, float r, float g, float b) {
    float pdfY1 = 842.0f - y1;
    float pdfY2 = 842.0f - y2;
    stream << r << " " << g << " " << b << " RG\n"; // stroke color
    stream << width << " w\n";
    stream << x1 << " " << pdfY1 << " m\n";
    stream << x2 << " " << pdfY2 << " l\n";
    stream << "S\n";
}

void PdfWriter::DrawRect(float x, float y, float w, float h, float r, float g, float b, bool fill, float strokeWidth, float sr, float sg, float sb) {
    float pdfY = 842.0f - y - h;
    if (fill) {
        stream << r << " " << g << " " << b << " rg\n"; // fill color
        stream << x << " " << pdfY << " " << w << " " << h << " re\n";
        stream << "f\n";
    }
    if (strokeWidth > 0.0f) {
        stream << sr << " " << sg << " " << sb << " RG\n"; // stroke color
        stream << strokeWidth << " w\n";
        stream << x << " " << pdfY << " " << w << " " << h << " re\n";
        stream << "S\n";
    }
}

bool PdfWriter::Save(const std::wstring& filePath) {
    std::string catalog = "<< /Type /Catalog /Pages 2 0 R >>";
    std::string pages = "<< /Type /Pages /Kids [3 0 R] /Count 1 >>";
    std::string page = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << /Font << /F1 4 0 R /F2 5 0 R >> >> /Contents 6 0 R >>";
    std::string font1 = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>";
    std::string font2 = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>";

    std::string contentStream = stream.str();
    std::stringstream contentsObj;
    contentsObj << "<< /Length " << contentStream.length() << " >>\nstream\n" << contentStream << "\nendstream";
    std::string contents = contentsObj.str();

    FILE* f = nullptr;
    if (_wfopen_s(&f, filePath.c_str(), L"wb") != 0 || !f) {
        return false;
    }

    auto write_str = [](FILE* file, const std::string& str) {
        fwrite(str.c_str(), 1, str.length(), file);
    };

    write_str(f, "%PDF-1.4\n");

    std::vector<size_t> fileOffsets(7);

    fileOffsets[1] = ftell(f);
    write_str(f, "1 0 obj\n" + catalog + "\nendobj\n");

    fileOffsets[2] = ftell(f);
    write_str(f, "2 0 obj\n" + pages + "\nendobj\n");

    fileOffsets[3] = ftell(f);
    write_str(f, "3 0 obj\n" + page + "\nendobj\n");

    fileOffsets[4] = ftell(f);
    write_str(f, "4 0 obj\n" + font1 + "\nendobj\n");

    fileOffsets[5] = ftell(f);
    write_str(f, "5 0 obj\n" + font2 + "\nendobj\n");

    fileOffsets[6] = ftell(f);
    write_str(f, "6 0 obj\n" + contents + "\nendobj\n");

    size_t xref_pos = ftell(f);
    write_str(f, "xref\n0 7\n0000000000 65535 f \r\n");
    for (int i = 1; i <= 6; ++i) {
        char buf[64];
        sprintf_s(buf, "%010zu 00000 n \r\n", fileOffsets[i]);
        write_str(f, buf);
    }

    write_str(f, "trailer\n<< /Size 7 /Root 1 0 R >>\nstartxref\n");
    write_str(f, std::to_string(xref_pos) + "\n%%EOF\n");

    fclose(f);
    return true;
}

bool PdfWriter::GenerateSystemReport(
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
) {
    PdfWriter pdf;

    // Colors
    float primaryR = 0.0f, primaryG = 0.505f, primaryB = 0.737f; // CPM Blue
    float darkR = 0.117f, darkG = 0.16f, darkB = 0.231f;      // Dark slate text
    float grayR = 0.392f, grayG = 0.454f, grayB = 0.545f;      // Slate gray text

    // 1. Header
    pdf.DrawPdfText(40.0f, 60.0f, L"SYSTEM-PR\u00DCFBERICHT", 22, true, primaryR, primaryG, primaryB);
    pdf.DrawPdfText(40.0f, 78.0f, L"Hardware-Diagnose & Stresstest Protokoll", 10, false, grayR, grayG, grayB);
    pdf.DrawPdfText(490.0f, 60.0f, L"CPM", 20, true, primaryR, primaryG, primaryB);

    // Divider Line
    pdf.DrawLine(40.0f, 95.0f, 555.0f, 95.0f, 1.5f, primaryR, primaryG, primaryB);

    // 2. Hardware configuration card
    pdf.DrawRect(40.0f, 110.0f, 515.0f, 130.0f, 0.96f, 0.97f, 0.98f, true, 1.0f, 0.88f, 0.91f, 0.94f);
    
    pdf.DrawPdfText(52.0f, 128.0f, L"HARDWARE-KONFIGURATION", 11, true, primaryR, primaryG, primaryB);
    pdf.DrawPdfText(52.0f, 144.0f, L"System-Modell:    " + sysModel, 9, false, darkR, darkG, darkB);
    pdf.DrawPdfText(52.0f, 158.0f, L"Seriennummer:     " + sysSerial, 9, false, darkR, darkG, darkB);
    pdf.DrawPdfText(52.0f, 172.0f, L"Prozessor (CPU):  " + cpuName, 9, false, darkR, darkG, darkB);
    pdf.DrawPdfText(52.0f, 186.0f, L"Grafikkarte (GPU): " + gpuName, 9, false, darkR, darkG, darkB);
    pdf.DrawPdfText(52.0f, 200.0f, L"Arbeitsspeicher:  " + ramTotalDisplay, 9, false, darkR, darkG, darkB);
    pdf.DrawPdfText(52.0f, 214.0f, L"Mainboard:        " + mbModel, 9, false, darkR, darkG, darkB);

    pdf.DrawPdfText(320.0f, 128.0f, L"AUFZEICHNUNGSDATEN", 11, true, primaryR, primaryG, primaryB);
    pdf.DrawPdfText(320.0f, 144.0f, L"Startzeit:    " + startTimeStr, 9, false, darkR, darkG, darkB);
    pdf.DrawPdfText(320.0f, 158.0f, L"Endzeit:      " + endTimeStr, 9, false, darkR, darkG, darkB);
    
    std::wstringstream ptCountStr;
    ptCountStr << L"Messdauer:    " << durationStr << L" (" << dataPoints.size() << L" Punkte)";
    pdf.DrawPdfText(320.0f, 172.0f, ptCountStr.str(), 9, false, darkR, darkG, darkB);

    // 3. Section: Zusammenfassung der Messwerte
    pdf.DrawPdfText(40.0f, 265.0f, L"ZUSAMMENFASSUNG DER MESSWERTE", 13, true, darkR, darkG, darkB);

    // Table Header
    float tableY = 280.0f;
    float rowHeight = 22.0f;
    pdf.DrawRect(40.0f, tableY, 515.0f, rowHeight, 0.94f, 0.96f, 0.98f, true, 1.0f, 0.88f, 0.91f, 0.94f);
    pdf.DrawPdfText(50.0f, tableY + 15.0f, L"Komponente", 9, true, darkR, darkG, darkB);
    pdf.DrawPdfText(180.0f, tableY + 15.0f, L"\u00D8 Auslastung", 9, true, darkR, darkG, darkB);
    pdf.DrawPdfText(280.0f, tableY + 15.0f, L"Max. Auslastung", 9, true, darkR, darkG, darkB);
    pdf.DrawPdfText(380.0f, tableY + 15.0f, L"\u00D8 Temp.", 9, true, darkR, darkG, darkB);
    pdf.DrawPdfText(470.0f, tableY + 15.0f, L"Max. Temp.", 9, true, darkR, darkG, darkB);

    // Calculate metrics
    float cpuLoadSum = 0, cpuLoadMax = 0, cpuTempSum = 0, cpuTempMax = 0;
    int cpuTempCount = 0, cpuLoadCount = 0;
    float gpuLoadSum = 0, gpuLoadMax = 0, gpuTempSum = 0, gpuTempMax = 0;
    int gpuTempCount = 0, gpuLoadCount = 0;
    float ramPercentSum = 0, ramPercentMax = 0;
    int ramCount = 0;

    for (const auto& pt : dataPoints) {
        if (pt.cpuLoad >= 0) {
            cpuLoadSum += pt.cpuLoad;
            cpuLoadMax = (std::max)(cpuLoadMax, pt.cpuLoad);
            cpuLoadCount++;
        }
        if (pt.cpuTemp > 0) {
            cpuTempSum += pt.cpuTemp;
            cpuTempMax = (std::max)(cpuTempMax, pt.cpuTemp);
            cpuTempCount++;
        }
        if (pt.gpuLoad >= 0) {
            gpuLoadSum += pt.gpuLoad;
            gpuLoadMax = (std::max)(gpuLoadMax, pt.gpuLoad);
            gpuLoadCount++;
        }
        if (pt.gpuTemp > 0) {
            gpuTempSum += pt.gpuTemp;
            gpuTempMax = (std::max)(gpuTempMax, pt.gpuTemp);
            gpuTempCount++;
        }
        if (pt.ramPercent >= 0) {
            ramPercentSum += pt.ramPercent;
            ramPercentMax = (std::max)(ramPercentMax, pt.ramPercent);
            ramCount++;
        }
    }

    wchar_t rowBuf[64];
    
    // CPU Row
    std::wstring cpuAvgLoad = (cpuLoadCount > 0) ? (swprintf_s(rowBuf, L"%.1f %%", cpuLoadSum / cpuLoadCount), rowBuf) : L"--";
    std::wstring cpuMaxLoad = (cpuLoadCount > 0) ? (swprintf_s(rowBuf, L"%.1f %%", cpuLoadMax), rowBuf) : L"--";
    std::wstring cpuAvgTemp = (cpuTempCount > 0) ? (swprintf_s(rowBuf, L"%.1f \u00B0C", cpuTempSum / cpuTempCount), rowBuf) : L"--";
    std::wstring cpuMaxTemp = (cpuTempCount > 0) ? (swprintf_s(rowBuf, L"%.1f \u00B0C", cpuTempMax), rowBuf) : L"--";

    float row1Y = tableY + rowHeight;
    pdf.DrawRect(40.0f, row1Y, 515.0f, rowHeight, 1.0f, 1.0f, 1.0f, true, 1.0f, 0.88f, 0.91f, 0.94f);
    pdf.DrawPdfText(50.0f, row1Y + 15.0f, L"Prozessor (CPU)", 9, true, darkR, darkG, darkB);
    pdf.DrawPdfText(180.0f, row1Y + 15.0f, cpuAvgLoad, 9, false, darkR, darkG, darkB);
    pdf.DrawPdfText(280.0f, row1Y + 15.0f, cpuMaxLoad, 9, false, darkR, darkG, darkB);
    pdf.DrawPdfText(380.0f, row1Y + 15.0f, cpuAvgTemp, 9, false, darkR, darkG, darkB);
    pdf.DrawPdfText(470.0f, row1Y + 15.0f, cpuMaxTemp, 9, false, darkR, darkG, darkB);

    // GPU Row
    bool hasGpu = (gpuName.find(L"Keine") == std::wstring::npos);
    std::wstring gpuAvgLoad = (hasGpu && gpuLoadCount > 0) ? (swprintf_s(rowBuf, L"%.1f %%", gpuLoadSum / gpuLoadCount), rowBuf) : L"--";
    std::wstring gpuMaxLoad = (hasGpu && gpuLoadCount > 0) ? (swprintf_s(rowBuf, L"%.1f %%", gpuLoadMax), rowBuf) : L"--";
    std::wstring gpuAvgTemp = (hasGpu && gpuTempCount > 0) ? (swprintf_s(rowBuf, L"%.1f \u00B0C", gpuTempSum / gpuTempCount), rowBuf) : L"--";
    std::wstring gpuMaxTemp = (hasGpu && gpuTempCount > 0) ? (swprintf_s(rowBuf, L"%.1f \u00B0C", gpuTempMax), rowBuf) : L"--";

    float row2Y = tableY + rowHeight * 2;
    pdf.DrawRect(40.0f, row2Y, 515.0f, rowHeight, 1.0f, 1.0f, 1.0f, true, 1.0f, 0.88f, 0.91f, 0.94f);
    pdf.DrawPdfText(50.0f, row2Y + 15.0f, L"Grafikkarte (GPU)", 9, true, darkR, darkG, darkB);
    pdf.DrawPdfText(180.0f, row2Y + 15.0f, gpuAvgLoad, 9, false, darkR, darkG, darkB);
    pdf.DrawPdfText(280.0f, row2Y + 15.0f, gpuMaxLoad, 9, false, darkR, darkG, darkB);
    pdf.DrawPdfText(380.0f, row2Y + 15.0f, gpuAvgTemp, 9, false, darkR, darkG, darkB);
    pdf.DrawPdfText(470.0f, row2Y + 15.0f, gpuMaxTemp, 9, false, darkR, darkG, darkB);

    // RAM Row
    std::wstring ramAvgPercent = (ramCount > 0) ? (swprintf_s(rowBuf, L"%.1f %%", ramPercentSum / ramCount), rowBuf) : L"--";
    std::wstring ramMaxPercent = (ramCount > 0) ? (swprintf_s(rowBuf, L"%.1f %%", ramPercentMax), rowBuf) : L"--";

    float row3Y = tableY + rowHeight * 3;
    pdf.DrawRect(40.0f, row3Y, 515.0f, rowHeight, 1.0f, 1.0f, 1.0f, true, 1.0f, 0.88f, 0.91f, 0.94f);
    pdf.DrawPdfText(50.0f, row3Y + 15.0f, L"Arbeitsspeicher (RAM)", 9, true, darkR, darkG, darkB);
    pdf.DrawPdfText(180.0f, row3Y + 15.0f, ramAvgPercent, 9, false, darkR, darkG, darkB);
    pdf.DrawPdfText(280.0f, row3Y + 15.0f, ramMaxPercent, 9, false, darkR, darkG, darkB);
    pdf.DrawPdfText(380.0f, row3Y + 15.0f, L"--", 9, false, darkR, darkG, darkB);
    pdf.DrawPdfText(470.0f, row3Y + 15.0f, L"--", 9, false, darkR, darkG, darkB);

    // 4. Timeline
    pdf.DrawPdfText(40.0f, 395.0f, L"ZEITLICHER VERLAUF DER MESSWERTE", 13, true, darkR, darkG, darkB);

    float histY = 410.0f;
    pdf.DrawRect(40.0f, histY, 515.0f, rowHeight, 0.94f, 0.96f, 0.98f, true, 1.0f, 0.88f, 0.91f, 0.94f);
    pdf.DrawPdfText(50.0f, histY + 15.0f, L"Zeitstempel", 9, true, darkR, darkG, darkB);
    pdf.DrawPdfText(150.0f, histY + 15.0f, L"CPU Last", 9, true, darkR, darkG, darkB);
    pdf.DrawPdfText(230.0f, histY + 15.0f, L"CPU Temp.", 9, true, darkR, darkG, darkB);
    pdf.DrawPdfText(320.0f, histY + 15.0f, L"GPU Last", 9, true, darkR, darkG, darkB);
    pdf.DrawPdfText(400.0f, histY + 15.0f, L"GPU Temp.", 9, true, darkR, darkG, darkB);
    pdf.DrawPdfText(480.0f, histY + 15.0f, L"RAM Last", 9, true, darkR, darkG, darkB);

    // Downsample values log to max 15 entries
    std::vector<PdfDataPoint> displayList;
    if (dataPoints.size() <= 15) {
        displayList = dataPoints;
    } else {
        displayList.push_back(dataPoints.front());
        double step = (double)(dataPoints.size() - 1) / 14.0;
        for (int idx = 1; idx < 14; ++idx) {
            int targetIdx = (int)std::round(idx * step);
            if (targetIdx > 0 && targetIdx < (int)dataPoints.size() - 1) {
                displayList.push_back(dataPoints[targetIdx]);
            }
        }
        displayList.push_back(dataPoints.back());
    }

    float currentY = histY + rowHeight;
    bool isAltRow = false;
    for (const auto& pt : displayList) {
        float r = isAltRow ? 0.98f : 1.0f;
        float g = isAltRow ? 0.98f : 1.0f;
        float b = isAltRow ? 0.99f : 1.0f;
        pdf.DrawRect(40.0f, currentY, 515.0f, rowHeight, r, g, b, true, 1.0f, 0.88f, 0.91f, 0.94f);

        pdf.DrawPdfText(50.0f, currentY + 15.0f, pt.timeStr, 9, false, darkR, darkG, darkB);
        
        std::wstring cpuL = (pt.cpuLoad >= 0) ? (swprintf_s(rowBuf, L"%.1f %%", pt.cpuLoad), rowBuf) : L"--";
        pdf.DrawPdfText(150.0f, currentY + 15.0f, cpuL, 9, false, darkR, darkG, darkB);

        // CPU temp color alert
        float cR = darkR, cG = darkG, cB = darkB;
        if (pt.cpuTemp > 80.0f) { cR = 0.937f; cG = 0.266f; cB = 0.266f; } // Red
        else if (pt.cpuTemp > 65.0f) { cR = 0.96f; cG = 0.62f; cB = 0.043f; } // Amber
        std::wstring cpuT = (pt.cpuTemp > 0) ? (swprintf_s(rowBuf, L"%.1f \u00B0C", pt.cpuTemp), rowBuf) : L"--";
        pdf.DrawPdfText(230.0f, currentY + 15.0f, cpuT, 9, false, cR, cG, cB);

        std::wstring gpuL = (hasGpu && pt.gpuLoad >= 0) ? (swprintf_s(rowBuf, L"%.1f %%", pt.gpuLoad), rowBuf) : L"--";
        pdf.DrawPdfText(320.0f, currentY + 15.0f, gpuL, 9, false, darkR, darkG, darkB);

        // GPU temp color alert
        float gR = darkR, gG = darkG, gB = darkB;
        if (pt.gpuTemp > 83.0f) { gR = 0.937f; gG = 0.266f; gB = 0.266f; }
        else if (pt.gpuTemp > 75.0f) { gR = 0.96f; gG = 0.62f; gB = 0.043f; }
        std::wstring gpuT = (hasGpu && pt.gpuTemp > 0) ? (swprintf_s(rowBuf, L"%.1f \u00B0C", pt.gpuTemp), rowBuf) : L"--";
        pdf.DrawPdfText(400.0f, currentY + 15.0f, gpuT, 9, false, gR, gG, gB);

        std::wstring ramP = (pt.ramPercent >= 0) ? (swprintf_s(rowBuf, L"%.1f %%", pt.ramPercent), rowBuf) : L"--";
        pdf.DrawPdfText(480.0f, currentY + 15.0f, ramP, 9, false, darkR, darkG, darkB);

        currentY += rowHeight;
        isAltRow = !isAltRow;
    }

    // 5. Footer
    pdf.DrawLine(40.0f, 800.0f, 555.0f, 800.0f, 1.0f, 0.88f, 0.91f, 0.94f);
    pdf.DrawPdfText(40.0f, 814.0f, L"Generiert mit CPM Hardware-Diagnose Tool (Native C++)", 8, false, grayR, grayG, grayB);
    pdf.DrawPdfText(510.0f, 814.0f, L"Seite 1 von 1", 8, false, grayR, grayG, grayB);

    return pdf.Save(filePath);
}

bool PdfWriter::GenerateBatteryReport(
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
) {
    PdfWriter pdf;

    // Colors
    float primaryR = 0.062f, primaryG = 0.725f, primaryB = 0.505f; // CPM Battery Green (#10B981)
    float darkR = 0.117f, darkG = 0.16f, darkB = 0.231f;       // Dark slate text
    float grayR = 0.392f, grayG = 0.454f, grayB = 0.545f;       // Slate gray text

    // 1. Header
    pdf.DrawPdfText(40.0f, 60.0f, L"BATTERIE-PR\u00DCFBERICHT", 20, true, primaryR, primaryG, primaryB);
    pdf.DrawPdfText(40.0f, 76.0f, L"Batterie-Gesundheitsdiagnose & Stresstest Protokoll", 10, false, grayR, grayG, grayB);
    pdf.DrawPdfText(490.0f, 60.0f, L"CPM", 18, true, primaryR, primaryG, primaryB);

    // Divider Line
    pdf.DrawLine(40.0f, 95.0f, 555.0f, 95.0f, 1.5f, primaryR, primaryG, primaryB);

    // 2. Specifications card
    pdf.DrawRect(40.0f, 110.0f, 515.0f, 135.0f, 0.96f, 0.97f, 0.98f, true, 1.0f, 0.88f, 0.91f, 0.94f);
    
    pdf.DrawPdfText(52.0f, 128.0f, L"BATTERIE-SPEZIFIKATIONEN", 10.5f, true, primaryR, primaryG, primaryB);
    pdf.DrawPdfText(52.0f, 142.0f, L"Gesundheitszustand (Health): " + batHealthDisplay, 9, true, darkR, darkG, darkB);
    pdf.DrawPdfText(52.0f, 154.0f, L"Hersteller / Modell:         " + batManufacturer + L" / " + batDeviceName, 9, false, darkR, darkG, darkB);
    pdf.DrawPdfText(52.0f, 166.0f, L"Design-Kapazit\u00E4t:            " + batDesignCapDisplay, 9, false, darkR, darkG, darkB);
    pdf.DrawPdfText(52.0f, 178.0f, L"Volle Ladekapazit\u00E4t:         " + batFullChargeCapDisplay, 9, false, darkR, darkG, darkB);
    pdf.DrawPdfText(52.0f, 190.0f, L"Batterie-Chemie:             " + batChemistry, 9, false, darkR, darkG, darkB);

    pdf.DrawPdfText(320.0f, 128.0f, L"TEST-INFO & RUNTIME", 10.5f, true, primaryR, primaryG, primaryB);
    pdf.DrawPdfText(320.0f, 142.0f, L"Startzeit:    " + startTimeStr, 9, false, darkR, darkG, darkB);
    pdf.DrawPdfText(320.0f, 154.0f, L"Endzeit:      " + endTimeStr, 9, false, darkR, darkG, darkB);
    pdf.DrawPdfText(320.0f, 166.0f, L"Testdauer:    " + durationStr, 9, false, darkR, darkG, darkB);
    pdf.DrawPdfText(320.0f, 178.0f, L"CPU:          " + cpuName, 9, false, darkR, darkG, darkB);
    pdf.DrawPdfText(320.0f, 190.0f, L"Grafikkarte:  " + gpuName, 9, false, darkR, darkG, darkB);
    pdf.DrawPdfText(320.0f, 202.0f, L"PC Modell:    " + sysModel, 9, false, darkR, darkG, darkB);
    pdf.DrawPdfText(320.0f, 214.0f, L"Seriennummer: " + sysSerial, 9, false, darkR, darkG, darkB);

    // 3. Section: Summary
    pdf.DrawPdfText(40.0f, 260.0f, L"ZUSAMMENFASSUNG DES STRESSTESTS", 12, true, darkR, darkG, darkB);

    // Summary box
    pdf.DrawRect(40.0f, 272.0f, 515.0f, 60.0f, 0.94f, 0.99f, 0.98f, true, 1.0f, 0.73f, 0.90f, 0.85f); // Light green bg

    double totalDrain = startPercent - endPercent;
    if (totalDrain < 0) totalDrain = 0;

    wchar_t sumBuf[128];
    swprintf_s(sumBuf, L"Anfangsladung: %.0f%%", startPercent);
    pdf.DrawPdfText(52.0f, 280.0f, sumBuf, 9, true, darkR, darkG, darkB);
    swprintf_s(sumBuf, L"Endladung:     %.0f%%", endPercent);
    pdf.DrawPdfText(52.0f, 294.0f, sumBuf, 9, true, darkR, darkG, darkB);
    
    // Parse duration seconds from durationStr (assuming hh:mm:ss format)
    int hours = 0, mins = 0, secs = 0;
    swscanf_s(durationStr.c_str(), L"%d:%d:%d", &hours, &mins, &secs);
    double totalHours = hours + (mins / 60.0) + (secs / 3600.0);
    double dischargeRate = totalHours > 0 ? totalDrain / totalHours : 0.0;
    double estLife = (dischargeRate > 0 && endPercent > 0) ? endPercent / dischargeRate : 0.0;

    swprintf_s(sumBuf, L"Gesamt-Verlust: %.0f%% in %d Min.", totalDrain, mins + hours * 60);
    pdf.DrawPdfText(52.0f, 308.0f, sumBuf, 9, true, 0.86f, 0.15f, 0.15f); // Red alert

    swprintf_s(sumBuf, L"Entladungsrate:         ~%.1f%% / Std.", dischargeRate);
    pdf.DrawPdfText(320.0f, 280.0f, sumBuf, 9, true, darkR, darkG, darkB);
    pdf.DrawPdfText(320.0f, 294.0f, L"Belastungstyp:          YouTube 4K HDR Stress-Test", 9, false, darkR, darkG, darkB);

    if (dischargeRate > 0) {
        swprintf_s(sumBuf, L"Proj. Restlaufzeit:     ~%.1f Std. (unter Last)", estLife);
    } else {
        swprintf_s(sumBuf, L"Proj. Restlaufzeit:     ~--");
    }
    pdf.DrawPdfText(320.0f, 308.0f, sumBuf, 9, true, darkR, darkG, darkB);

    // 4. Chronological test logs
    pdf.DrawPdfText(40.0f, 355.0f, L"PROTOKOLL DES TESTVERLAUFS", 12, true, darkR, darkG, darkB);

    float logTableY = 370.0f;
    float rowHeight = 18.0f;
    pdf.DrawRect(40.0f, logTableY, 515.0f, rowHeight, 0.94f, 0.96f, 0.98f, true, 1.0f, 0.88f, 0.91f, 0.94f);
    pdf.DrawPdfText(50.0f, logTableY + 12.0f, L"Eintrag", 9, true, darkR, darkG, darkB);

    // Downsample log list to fit up to 20 entries
    std::vector<std::wstring> displayList;
    if (logEntries.size() <= 20) {
        displayList = logEntries;
    } else {
        displayList.push_back(logEntries.front());
        double step = (double)(logEntries.size() - 1) / 19.0;
        for (int idx = 1; idx < 19; ++idx) {
            int targetIdx = (int)std::round(idx * step);
            if (targetIdx > 0 && targetIdx < (int)logEntries.size() - 1) {
                displayList.push_back(logEntries[targetIdx]);
            }
        }
        displayList.push_back(logEntries.back());
    }

    float currentY = logTableY + rowHeight;
    bool isAltRow = false;
    for (const auto& logLine : displayList) {
        float r = isAltRow ? 0.98f : 1.0f;
        float g = isAltRow ? 0.98f : 1.0f;
        float b = isAltRow ? 0.99f : 1.0f;
        pdf.DrawRect(40.0f, currentY, 515.0f, rowHeight, r, g, b, true, 1.0f, 0.88f, 0.91f, 0.94f);
        pdf.DrawPdfText(50.0f, currentY + 12.0f, logLine, 9, false, darkR, darkG, darkB);

        currentY += rowHeight;
        isAltRow = !isAltRow;
    }

    // 5. Footer
    pdf.DrawLine(40.0f, 800.0f, 555.0f, 800.0f, 1.0f, 0.88f, 0.91f, 0.94f);
    pdf.DrawPdfText(40.0f, 814.0f, L"Generiert mit CPM Hardware-Diagnose Tool (Native C++)", 8, false, grayR, grayG, grayB);
    pdf.DrawPdfText(510.0f, 814.0f, L"Seite 1 von 1", 8, false, grayR, grayG, grayB);

    return pdf.Save(filePath);
}
