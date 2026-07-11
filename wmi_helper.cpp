#define _WIN32_DCOM
#include "wmi_helper.h"
#include <iostream>
#include <comdef.h>
#include <WbemIdl.h>
#include <algorithm>
#include <cwctype>

#pragma comment(lib, "wbemuuid.lib")

static bool g_ComInitialized = false;

bool WmiHelper::Initialize() {
    if (g_ComInitialized) return true;

    HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        if (hr == RPC_E_CHANGED_MODE) {
            // Already initialized in another mode, that's fine
            return true;
        }
        return false;
    }

    hr = CoInitializeSecurity(
        NULL,
        -1,                          // COM authentication
        NULL,                        // Authentication services
        NULL,                        // Reserved
        RPC_C_AUTHN_LEVEL_DEFAULT,   // Default authentication
        RPC_C_IMP_LEVEL_IMPERSONATE, // Default Impersonation  
        NULL,                        // Authentication info
        EOAC_NONE,                   // Additional capabilities 
        NULL                         // Reserved
    );

    if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
        CoUninitialize();
        return false;
    }

    g_ComInitialized = true;
    return true;
}

void WmiHelper::Uninitialize() {
    if (g_ComInitialized) {
        CoUninitialize();
        g_ComInitialized = false;
    }
}

std::vector<WmiHelper::WmiRow> WmiHelper::Query(
    const std::wstring& namespacePath,
    const std::wstring& query,
    const std::vector<std::wstring>& properties
) {
    std::vector<WmiRow> results;

    if (!Initialize()) {
        return results;
    }

    IWbemLocator* pLoc = NULL;
    HRESULT hr = CoCreateInstance(
        CLSID_WbemLocator,
        0,
        CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, (LPVOID*)&pLoc
    );

    if (FAILED(hr)) {
        return results;
    }

    IWbemServices* pSvc = NULL;
    hr = pLoc->ConnectServer(
        _bstr_t(namespacePath.c_str()), // Object path of WMI namespace
        NULL,                    // User name. NULL = current user
        NULL,                    // User password. NULL = current
        0,                       // Locale. NULL indicates current
        NULL,                    // Security flags.
        0,                       // Authority (e.g. Kerberos)
        0,                       // Context object
        &pSvc                    // pointer to IWbemServices proxy
    );

    if (FAILED(hr)) {
        pLoc->Release();
        return results;
    }

    hr = CoSetProxyBlanket(
        pSvc,                        // Indicates the proxy to set
        RPC_C_AUTHN_WINNT,           // RPC_C_AUTHN_xxx
        RPC_C_AUTHZ_NONE,            // RPC_C_AUTHZ_xxx
        NULL,                        // Server principal name 
        RPC_C_AUTHN_LEVEL_CALL,      // RPC_C_AUTHN_LEVEL_xxx 
        RPC_C_IMP_LEVEL_IMPERSONATE, // RPC_C_IMP_LEVEL_xxx
        NULL,                        // client identity
        EOAC_NONE                    // proxy capabilities 
    );

    if (FAILED(hr)) {
        pSvc->Release();
        pLoc->Release();
        return results;
    }

    IEnumWbemClassObject* pEnumerator = NULL;
    hr = pSvc->ExecQuery(
        bstr_t("WQL"),
        bstr_t(query.c_str()),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        NULL,
        &pEnumerator
    );

    if (FAILED(hr)) {
        pSvc->Release();
        pLoc->Release();
        return results;
    }

    IWbemClassObject* pclsObj = NULL;
    ULONG uReturn = 0;

    while (pEnumerator) {
        hr = pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn);
        if (0 == uReturn) {
            break;
        }

        WmiRow row;
        for (const auto& prop : properties) {
            VARIANT vtProp;
            VariantInit(&vtProp);

            hr = pclsObj->Get(prop.c_str(), 0, &vtProp, 0, 0);
            if (SUCCEEDED(hr)) {
                if (vtProp.vt == VT_BSTR && vtProp.bstrVal != NULL) {
                    row.values[prop] = vtProp.bstrVal;
                } else if (vtProp.vt == VT_I4) {
                    row.values[prop] = std::to_wstring(vtProp.lVal);
                } else if (vtProp.vt == VT_UI4) {
                    row.values[prop] = std::to_wstring(vtProp.ulVal);
                } else if (vtProp.vt == VT_BOOL) {
                    row.values[prop] = vtProp.boolVal ? L"True" : L"False";
                } else if (vtProp.vt == VT_NULL || vtProp.vt == VT_EMPTY) {
                    row.values[prop] = L"Unbekannt";
                } else {
                    // Try to convert to string anyway or fallback
                    _bstr_t bstrVal(vtProp);
                    if (bstrVal.length() > 0) {
                        row.values[prop] = (wchar_t*)bstrVal;
                    } else {
                        row.values[prop] = L"Unbekannt";
                    }
                }
            } else {
                row.values[prop] = L"Unbekannt";
            }
            VariantClear(&vtProp);
        }

        results.push_back(row);
        pclsObj->Release();
        pclsObj = NULL;
    }

    pEnumerator->Release();
    pSvc->Release();
    pLoc->Release();

    return results;
}

std::wstring WmiHelper::SanitizeValue(const std::wstring& val) {
    if (val.empty()) return L"Unbekannt";

    // Trim whitespace
    std::wstring trimmed = val;
    trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](wchar_t ch) {
        return !std::iswspace(ch);
    }));
    trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(), [](wchar_t ch) {
        return !std::iswspace(ch);
    }).base(), trimmed.end());

    if (trimmed.empty()) return L"Unbekannt";

    // Placeholders list in lowercase for matching
    std::vector<std::wstring> placeholders = {
        L"to be filled by o.e.m.",
        L"to be filled by oem",
        L"system product name",
        L"system manufacturer",
        L"base board product name",
        L"base board manufacturer",
        L"default string",
        L"none",
        L"unknown",
        L"o.e.m.",
        L"fill by o.e.m."
    };

    std::wstring lowerVal = trimmed;
    std::transform(lowerVal.begin(), lowerVal.end(), lowerVal.begin(), ::towlower);

    for (const auto& placeholder : placeholders) {
        if (lowerVal == placeholder) {
            return L"Unbekannt";
        }
    }

    return trimmed;
}
