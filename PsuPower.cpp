// TrafficMonitor plugin: estimated PSU wall draw from HWiNFO64 gadget values.
// Reads HKCU\Software\HWiNFO64\VSB (HWiNFO "Report value in Gadget"), once per
// TrafficMonitor refresh. No threads, no timers, no extra process.
//
// Estimate: dc   = CPU Package Power * (1 + CpuVrmLoss) + GPU Power + OtherWatts
//           wall = dc / efficiency(dc), interpolated from EfficiencyCurve
// Tune the values in PsuPower.ini next to the DLL.
//
// Defaults are for: i7-9700K, ROG STRIX Z390-F, 2x16 GB DDR4 RGB, 970 Evo Plus,
// SU650, SkyHawk 2 TB, H115i PRO, P300A, USB keyboard/mouse/headset dongle and
// a be quiet! Pure Power 11 600 W on 230 V (be quiet! spec: 10/20/50/100% load).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cwchar>
#include <cstdlib>
#include "third_party/TrafficMonitor/PluginInterface.h"

static const wchar_t* kVsbKey = L"Software\\HWiNFO64\\VSB";

class PsuItem : public IPluginItem
{
public:
    wchar_t value[32] = L"-- W";
    const wchar_t* GetItemName() const override { return L"PSU Power"; }
    const wchar_t* GetItemId() const override { return L"PsuPowerEst"; }
    const wchar_t* GetItemLableText() const override { return L"PSU"; }
    const wchar_t* GetItemValueText() const override { return value; }
    const wchar_t* GetItemValueSampleText() const override { return L"888 W"; }
};

class PsuPlugin : public ITMPlugin
{
public:
    PsuPlugin() { LoadConfig(); }

    IPluginItem* GetItem(int index) override { return index == 0 ? &m_item : nullptr; }

    void DataRequired() override
    {
        double cpu = -1, gpu = -1;
        ReadGadgetValues(cpu, gpu);

        // HWiNFO leaves the last values in the registry when it exits, so treat
        // readings that stop changing as stale.
        if (cpu == m_lastCpu && gpu == m_lastGpu)
            ++m_unchanged;
        else
            m_unchanged = 0;
        m_lastCpu = cpu;
        m_lastGpu = gpu;

        if (cpu < 0 || gpu < 0 || m_unchanged >= 10)
        {
            wcscpy_s(m_item.value, L"-- W");
            wcscpy_s(m_tooltip, L"PSU: no data (is HWiNFO64 running with gadget reporting on?)");
            return;
        }

        double cpuIn = cpu * (1 + m_cpuVrmLoss);
        double dc = cpuIn + gpu + m_otherWatts;
        double eff = Efficiency(dc);
        double wall = dc / eff;
        swprintf_s(m_item.value, L"%.0f W", wall);
        swprintf_s(m_tooltip,
            L"PSU (estimated wall draw): %.0f W\r\nPSU output: %.0f W at %.1f%% efficiency\r\nCPU: %.0f W (package %.0f W + VRM loss)\r\nGPU: %.0f W\r\nRest of system: %.0f W",
            wall, dc, eff * 100, cpuIn, cpu, gpu, m_otherWatts);
    }

    const wchar_t* GetInfo(PluginInfoIndex index) override
    {
        switch (index)
        {
        case TMI_NAME: return L"PSU Power";
        case TMI_DESCRIPTION: return L"Estimated PSU wall draw from HWiNFO64 CPU and GPU power.";
        case TMI_AUTHOR: return L"jozuern";
        case TMI_COPYRIGHT: return L"Copyright (c) 2026 jozuern, MIT License";
        case TMI_VERSION: return L"1.1";
        case TMI_URL: return L"https://github.com/jozuern/TrafficMonitor-PsuPower";
        default: return L"";
        }
    }

    const wchar_t* GetTooltipInfo() override { return m_tooltip; }

private:
    void ReadGadgetValues(double& cpu, double& gpu)
    {
        HKEY key;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kVsbKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
            return;
        for (int i = 0; i < 64 && (cpu < 0 || gpu < 0); ++i)
        {
            wchar_t name[24], label[128], raw[64];
            swprintf_s(name, L"Label%d", i);
            if (!ReadString(key, name, label, _countof(label)))
                break;
            double* target = nullptr;
            if (cpu < 0 && wcscmp(label, m_cpuLabel) == 0) target = &cpu;
            else if (gpu < 0 && wcscmp(label, m_gpuLabel) == 0) target = &gpu;
            if (!target)
                continue;
            swprintf_s(name, L"ValueRaw%d", i);
            if (ReadString(key, name, raw, _countof(raw)))
                *target = _wtof(raw);
        }
        RegCloseKey(key);
    }

    static bool ReadString(HKEY key, const wchar_t* name, wchar_t* buf, DWORD count)
    {
        DWORD size = (count - 1) * sizeof(wchar_t);
        DWORD type = 0;
        if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(buf), &size) != ERROR_SUCCESS || type != REG_SZ)
            return false;
        buf[size / sizeof(wchar_t)] = L'\0';
        return true;
    }

    // Linear interpolation over the curve points; below the first point the first
    // segment's slope is extended (efficiency keeps falling at very low load).
    double Efficiency(double dc) const
    {
        if (m_curveCount == 0)
            return 1.0;
        if (m_curveCount == 1)
            return m_curve[0][1];
        int i = 0;
        while (i < m_curveCount - 2 && dc > m_curve[i + 1][0])
            ++i;
        if (dc >= m_curve[m_curveCount - 1][0])
            return m_curve[m_curveCount - 1][1];
        double t = (dc - m_curve[i][0]) / (m_curve[i + 1][0] - m_curve[i][0]);
        double eff = m_curve[i][1] + t * (m_curve[i + 1][1] - m_curve[i][1]);
        return eff < 0.5 ? 0.5 : (eff > 1.0 ? 1.0 : eff);
    }

    // "60:0.811,120:0.879" -> sorted-as-given (load W, efficiency) pairs
    void ParseCurve(const wchar_t* text)
    {
        m_curveCount = 0;
        const wchar_t* p = text;
        while (*p && m_curveCount < kMaxCurve)
        {
            wchar_t* end;
            double load = wcstod(p, &end);
            if (end == p || *end != L':')
                break;
            p = end + 1;
            double eff = wcstod(p, &end);
            if (end == p)
                break;
            if (load > 0 && eff > 0.1 && eff <= 1.0)
            {
                m_curve[m_curveCount][0] = load;
                m_curve[m_curveCount][1] = eff;
                ++m_curveCount;
            }
            p = end;
            while (*p == L',' || *p == L' ')
                ++p;
        }
    }

    void LoadConfig()
    {
        wchar_t path[MAX_PATH];
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&kVsbKey), &self);
        DWORD len = GetModuleFileNameW(self, path, MAX_PATH);
        if (len == 0 || len >= MAX_PATH)
            return;
        wchar_t* dot = wcsrchr(path, L'.');
        if (!dot)
            return;
        wcscpy_s(dot, MAX_PATH - (dot - path), L".ini");

        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        {
            WritePrivateProfileStringW(L"PsuPower", L"OtherWatts", kDefaultOther, path);
            WritePrivateProfileStringW(L"PsuPower", L"CpuVrmLoss", kDefaultVrm, path);
            WritePrivateProfileStringW(L"PsuPower", L"EfficiencyCurve", kDefaultCurve, path);
            WritePrivateProfileStringW(L"PsuPower", L"CpuLabel", kDefaultCpuLabel, path);
            WritePrivateProfileStringW(L"PsuPower", L"GpuLabel", kDefaultGpuLabel, path);
        }
        GetPrivateProfileStringW(L"PsuPower", L"CpuLabel", kDefaultCpuLabel, m_cpuLabel, _countof(m_cpuLabel), path);
        GetPrivateProfileStringW(L"PsuPower", L"GpuLabel", kDefaultGpuLabel, m_gpuLabel, _countof(m_gpuLabel), path);
        wchar_t buf[256];
        GetPrivateProfileStringW(L"PsuPower", L"OtherWatts", kDefaultOther, buf, _countof(buf), path);
        m_otherWatts = _wtof(buf);
        GetPrivateProfileStringW(L"PsuPower", L"CpuVrmLoss", kDefaultVrm, buf, _countof(buf), path);
        m_cpuVrmLoss = _wtof(buf);
        GetPrivateProfileStringW(L"PsuPower", L"EfficiencyCurve", kDefaultCurve, buf, _countof(buf), path);
        ParseCurve(buf);
        if (m_curveCount == 0)
            ParseCurve(kDefaultCurve);
    }

    static constexpr const wchar_t* kDefaultOther = L"35";
    static constexpr const wchar_t* kDefaultVrm = L"0.07";
    static constexpr const wchar_t* kDefaultCurve = L"60:0.811,120:0.879,300:0.912,600:0.899";
    static constexpr const wchar_t* kDefaultCpuLabel = L"CPU Package Power";
    static constexpr const wchar_t* kDefaultGpuLabel = L"GPU Power";
    static constexpr int kMaxCurve = 8;

    PsuItem m_item;
    wchar_t m_cpuLabel[128] = L"CPU Package Power";
    wchar_t m_gpuLabel[128] = L"GPU Power";
    wchar_t m_tooltip[320] = L"";
    double m_otherWatts = 35;
    double m_cpuVrmLoss = 0.07;
    double m_curve[kMaxCurve][2] = {};
    int m_curveCount = 0;
    double m_lastCpu = -2, m_lastGpu = -2;
    int m_unchanged = 0;
};

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance()
{
    static PsuPlugin instance;
    return &instance;
}
