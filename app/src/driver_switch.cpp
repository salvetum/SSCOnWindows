/*
 * Dongle driver automation — implementation (see driver_switch.h).
 *
 * SPDX-License-Identifier: MIT
 */

#include "driver_switch.h"
#include "driver_mode.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
    const char *kSignerSubject = "A2DPWB Driver Signer";
    const char *kSignerPw      = "a2dpwb-pw-2026!";
    const char *kCatName       = "a2dpwb_winusb.cat";
    const char *kInfName       = "a2dpwb_winusb.inf";
    const char *kSubDir        = "A2DPWB\\a2dpwb.pfx";
    const char *kTmpDdfName    = "a2dpwb_winusb.ddf";

    /* pnputil argument quoting: INF paths contain only [A-Za-z0-9_\\] on the
       typical path, but %TEMP% can be anything — always double-quote. */
    std::string quote(const std::string &s)
    {
        return "\"" + s + "\"";
    }

    /* Convert a path/argstring obtained from Win32 ANSI APIs to UTF-16 so it
       can be handed to ShellExecuteExW. */
    std::wstring to_wide(const std::string &s)
    {
        if (s.empty()) return std::wstring();
        int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), nullptr, 0);
        if (n <= 0) return std::wstring(s.begin(), s.end());
        std::wstring w((size_t)n, L'\0');
        MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), &w[0], n);
        return w;
    }

    /* Run any tool. elevated=true -> UAC prompt (ShellExecuteEx "runas").
       Returns false + message on launch/failure. */
    bool run_program(const wchar_t *file, const std::wstring &args,
                     bool elevated, std::string &err)
    {
        SHELLEXECUTEINFOW sei = {};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        if (elevated) sei.lpVerb = L"runas";
        sei.lpFile = file;
        sei.lpParameters = args.c_str();
        sei.nShow = SW_HIDE;

        if (!ShellExecuteExW(&sei)) {
            DWORD le = GetLastError();
            if (elevated && le == ERROR_CANCELLED) {
                err = "Elevation cancelled by user.";
            } else {
                char buf[192];
                snprintf(buf, sizeof(buf), "Failed to launch %ls (error %lu).",
                         file, (unsigned long)le);
                err = buf;
            }
            return false;
        }

        WaitForSingleObject(sei.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(sei.hProcess, &code);
        CloseHandle(sei.hProcess);

        if (code != 0) {
            char buf[192];
            snprintf(buf, sizeof(buf), "%ls exited with code %lu.",
                     file, (unsigned long)code);
            err = buf;
            return false;
        }
        return true;
    }

    bool run_elevated(const std::wstring &args, std::string &err)
    {
        return run_program(L"pnputil.exe", args, true, err);
    }

    bool run_ok(const std::wstring &args, DongleSwitchReport &rep)
    {
        std::string err;
        if (!run_elevated(args, err)) {
            rep.result = err.rfind("Elevation cancelled", 0) == 0
                             ? DongleSwitchResult::ElevationCancelled
                             : DongleSwitchResult::PnPError;
            rep.message = err;
            return false;
        }
        return true;
    }

    /* Resolve the current user's LocalAppData for a persistent copy of the
       signing PFX (kept across %TEMP% wipes). */
    bool local_app_data(std::string &out)
    {
        char buf[MAX_PATH] = {};
        if (FAILED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
                                    nullptr, 0, buf)))
            return false;
        if (buf[0] == '\0') return false;
        std::string dir = buf;
        std::replace(dir.begin(), dir.end(), '/', '\\');
        out = dir + "\\";
        return true;
    }

    /* Find a tool (makecat.exe / signtool.exe) under the Windows SDK bin
       tree. Prefers an x64 build. */
    std::string find_sdk_tool(const char *name)
    {
        const char *roots[] = {
            "C:\\Program Files (x86)\\Windows Kits\\10\\bin",
            "C:\\Program Files\\Windows Kits\\10\\bin",
        };
        for (const char *root : roots) {
            std::vector<std::string> frontier{ std::string(root) };
            std::vector<std::string> next;
            std::string fallback;
            for (int depth = 0; depth < 5 && !frontier.empty(); ++depth) {
                for (const auto &d : frontier) {
                    WIN32_FIND_DATAA fd = {};
                    HANDLE h = FindFirstFileA((d + "\\*").c_str(), &fd);
                    if (h == INVALID_HANDLE_VALUE) continue;
                    do {
                        std::string fn = fd.cFileName;
                        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                            if (!fn.empty() && fn[0] != '.')
                                next.push_back(d + "\\" + fn);
                        } else if (_stricmp(fn.c_str(), name) == 0) {
                            std::string full = d + "\\" + fn;
                            if (full.find("\\x64\\") != std::string::npos)
                                { FindClose(h); return full; }
                            if (fallback.empty()) fallback = full;
                        }
                    } while (FindNextFileA(h, &fd));
                    FindClose(h);
                }
                frontier.swap(next);
                next.clear();
            }
            if (!fallback.empty()) return fallback;
        }
        return std::string();
    }

    /* Scan C:\Windows\INF\oem*.inf entries for a package that (a) references
       our VID:PID and (b) is a WinUSB-flavored package (mentions winusb.inf).
       Vendor BTHUSB packages (Realtek rtkfilter etc.) never reference
       winusb.inf, so they are left alone. */
    std::vector<std::string> find_winusb_infs(uint16_t vid, uint16_t pid)
    {
        std::vector<std::string> out;

        char win_dir[MAX_PATH] = {};
        if (!GetWindowsDirectoryA(win_dir, sizeof(win_dir))) return out;
        std::string inf_dir = std::string(win_dir) + "\\INF\\";

        WIN32_FIND_DATAA fd = {};
        HANDLE h = FindFirstFileA((inf_dir + "oem*.inf").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return out;
        do {
            std::string full = inf_dir + fd.cFileName;

            FILE *f = nullptr;
            fopen_s(&f, full.c_str(), "rb");
            if (!f) continue;
            std::string text;
            text.resize(64 * 1024);
            size_t got = fread(text.data(), 1, text.size(), f);
            fclose(f);
            if (got == 0) continue;
            text.resize(got);
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c) { return (char)tolower(c); });

            char needle[40];
            snprintf(needle, sizeof(needle), "vid_%04x&pid_%04x", (unsigned)vid, (unsigned)pid);
            if (text.find(needle) == std::string::npos) continue;
            if (text.find("winusb.inf") == std::string::npos) continue;

            out.push_back(fd.cFileName); /* e.g. oem117.inf */
        } while (FindNextFileA(h, &fd));
        FindClose(h);

        return out;
    }

    /* Write a file (binary-safe). Returns false + message on failure. */
    bool write_file(const std::string &path, const std::string &body,
                    std::string &err)
    {
        FILE *f = nullptr;
        if (fopen_s(&f, path.c_str(), "wb") != 0 || !f) {
            err = "Could not write " + path;
            return false;
        }
        fwrite(body.data(), 1, body.size(), f);
        fclose(f);
        return true;
    }

    /* One-time elevated self-signed certificate setup. Creates the signer if
       missing, trusts it (Root + TrustedPublisher) and exports the PFX to a
       persistent location under %LOCALAPPDATA%. Skipped when the PFX already
       exists. Returns the PFX path on success. */
    bool ensure_signing_pfx(std::string &pfx_out, std::string &err)
    {
        std::string appdata;
        if (!local_app_data(appdata)) {
            err = "Could not resolve %LOCALAPPDATA%.";
            return false;
        }
        std::string dir = appdata + "A2DPWB";
        std::string pfx = dir + "\\a2dpwb.pfx";

        FILE *f = nullptr;
        fopen_s(&f, pfx.c_str(), "rb");
        if (f) { fclose(f); pfx_out = pfx; return true; } /* already set up */

        CreateDirectoryA(dir.c_str(), nullptr);

        /* Elevated one-shot PowerShell: create/reuse cert, trust, export. */
        char tmp_path[MAX_PATH];
        if (GetTempPathA(sizeof(tmp_path), tmp_path) == 0) {
            err = "Could not resolve the temp directory.";
            return false;
        }
        std::string ps_path = std::string(tmp_path) + "a2dpwb_cert_setup.ps1";
        std::string ps =
            "$ErrorActionPreference = \"Stop\"\r\n"
            "$certDir = \"" + dir + "\"\r\n"
            "$pfxPath = \"" + pfx + "\"\r\n"
            "$cerPath = \"$certDir\\a2dpwb.cer\"\r\n"
            "$password = \"" + std::string(kSignerPw) + "\"\r\n"
            "$cert = Get-ChildItem Cert:\\LocalMachine\\My | Where-Object { $_.Subject -like \"*" + kSignerSubject + "*\" } | Select-Object -First 1\r\n"
            "if (-not $cert) {\r\n"
            "  $cert = New-SelfSignedCertificate -Subject \"CN=" + kSignerSubject + ", O=A2DPWB\" -Type CodeSigningCert -CertStoreLocation Cert:\\LocalMachine\\My -KeyExportPolicy Exportable -KeySpec Signature\r\n"
            "}\r\n"
            "Export-Certificate -Cert $cert -FilePath $cerPath -Force | Out-Null\r\n"
            "$secure = ConvertTo-SecureString $password -AsPlainText -Force\r\n"
            "Export-PfxCertificate -Cert $cert -FilePath $pfxPath -Password $secure -Force | Out-Null\r\n"
            "Import-Certificate -FilePath $cerPath -CertStoreLocation Cert:\\LocalMachine\\Root | Out-Null\r\n"
            "Import-Certificate -FilePath $cerPath -CertStoreLocation Cert:\\LocalMachine\\TrustedPublisher | Out-Null\r\n"
            "Write-Output \"OK\"\r\n";

        if (!write_file(ps_path, ps, err)) return false;
        {
            std::string arg = "-NoProfile -ExecutionPolicy Bypass -File " + quote(ps_path);
            std::wstring warg = to_wide(arg);
            if (!run_program(L"powershell.exe", warg, true, err)) return false;
        }
        DeleteFileA(ps_path.c_str());

        fopen_s(&f, pfx.c_str(), "rb");
        if (!f) { err = "Signing certificate setup produced no PFX."; return false; }
        fclose(f);
        pfx_out = pfx;
        return true;
    }

    /* Generate the WinUSB INF (mirrors what Zadig installs for the UB500,
       reusing its proven device-interface GUID). No binaries of its own —
       it only pulls the Microsoft-signed winusb.inf system driver. The
       catalog hash is what makes /add-driver accept the package. */
    bool write_winusb_inf(const std::string &path, uint16_t vid, uint16_t pid,
                          std::string &err)
    {
        char hwid[32];
        snprintf(hwid, sizeof(hwid), "USB\\VID_%04X&PID_%04X", (unsigned)vid, (unsigned)pid);

        char body[4096];
        snprintf(body, sizeof(body),
            "[Version]\r\n"
            "Signature=\"$Windows NT$\"\r\n"
            "Class=USBDevice\r\n"
            "ClassGuid={88bae032-5a81-49f0-bc3d-a4ff138216d6}\r\n"
            "Provider=\"A2DPWB\"\r\n"
            "DriverVer=09/15/2026,1.0.0.0\r\n"
            "CatalogFile=%s\r\n"
            "\r\n"
            "[Manufacturer]\r\n"
            "%%Mfg%%=DeviceInstall,NTamd64\r\n"
            "\r\n"
            "[DeviceInstall.NTamd64]\r\n"
            "%%DeviceName%%=USB_Install, %s\r\n"
            "\r\n"
            "[USB_Install]\r\n"
            "Include = winusb.inf\r\n"
            "Needs   = WINUSB.NT\r\n"
            "\r\n"
            "[USB_Install.Services]\r\n"
            "Include    = winusb.inf\r\n"
            "Needs      = WINUSB.NT.Services\r\n"
            "\r\n"
            "[USB_Install.HW]\r\n"
            "AddReg = Dev_AddReg\r\n"
            "\r\n"
            "[Dev_AddReg]\r\n"
            "HKR,,DeviceInterfaceGUIDs,0x10000,\"{3fe6abe9-5d35-4cc6-9b10-78e6f199c952}\"\r\n"
            "\r\n"
            "[Strings]\r\n"
            "Mfg=\"A2DPWB\"\r\n"
            "DeviceName=\"A2DPWB WinUSB Streaming Dongle\"\r\n"
            "\r\n", kCatName, hwid);

        return write_file(path, std::string(body), err);
    }

    /* Build the SHA-256 catalog for the given INF (makecat). ResultDir must
       be the directory that also holds the INF (pnputil looks both up).
       Returns the .cat path. */
    bool make_catalog(const std::string &ddf_path, const std::string &inf_path,
                      const std::string &result_dir, std::string &err)
    {
        std::string makecat = find_sdk_tool("makecat.exe");
        if (makecat.empty()) {
            err = "Windows SDK makecat.exe not found (need the Windows 10+ SDK tools).";
            return false;
        }

        std::string ddf =
            "[CatalogHeader]\r\n"
            "Name=" + std::string(kCatName) + "\r\n"
            "ResultDir=" + result_dir + "\r\n"
            "CatalogVersion=2\r\n"
            "HashAlgorithms=SHA256\r\n"
            "PageHashes=false\r\n"
            "EncodingType=0x00010001\r\n"
            "\r\n"
            "[CatalogFiles]\r\n"
            "<HASH>" + std::string(kInfName) + "=" + inf_path + "\r\n";
        if (!write_file(ddf_path, ddf, err)) return false;

        {
            std::wstring makecat_w = to_wide(makecat);
            std::wstring warg = to_wide(quote(ddf_path));
            if (!run_program(makecat_w.c_str(), warg, false, err)) return false;
        }

        std::string cat = result_dir + "\\" + kCatName;
        FILE *f = nullptr;
        fopen_s(&f, cat.c_str(), "rb");
        if (!f) {
            err = "makecat produced no catalog.";
            return false;
        }
        fclose(f);
        return true;
    }

    /* Sign the catalog with the persistent self-signed cert (signtool). */
    bool sign_catalog(const std::string &cat_path, const std::string &pfx,
                      std::string &err)
    {
        std::string signtool = find_sdk_tool("signtool.exe");
        if (signtool.empty()) {
            err = "Windows SDK signtool.exe not found (need the Windows 10+ SDK tools).";
            return false;
        }

        std::string arg = "sign /f " + quote(pfx) + " /p " + quote(kSignerPw) +
                          " /fd SHA256 /td SHA256 " + quote(cat_path);
        std::wstring signtool_w = to_wide(signtool);
        std::wstring warg = to_wide(arg);
        return run_program(signtool_w.c_str(), warg, false, err);
    }

    /* Write the temps dir + prepare INF, catalog and signature, then hand
       the signed package to pnputil. */
    DongleSwitchReport install_winusb_package(uint16_t vid, uint16_t pid)
    {
        DongleSwitchReport rep;
        rep.result = DongleSwitchResult::Ok;

        std::string err;
        std::string pfx;
        if (!ensure_signing_pfx(pfx, err)) {
            rep.result = DongleSwitchResult::PnPError;
            rep.message = "Signing certificate setup failed: " + err;
            return rep;
        }

        char tmp_path[MAX_PATH];
        if (GetTempPathA(sizeof(tmp_path), tmp_path) == 0) {
            rep.result = DongleSwitchResult::InfWriteError;
            rep.message = "Could not resolve the temp directory.";
            return rep;
        }
        std::string tmp = tmp_path;
        std::string inf_path = tmp + kInfName;
        std::string ddf_path = tmp + kTmpDdfName;

        if (!write_winusb_inf(inf_path, vid, pid, err)) {
            rep.result = DongleSwitchResult::InfWriteError;
            rep.message = err;
            return rep;
        }
        /* Parse trailing backslash off the temp dir for ResultDir. */
        std::string result_dir = tmp;
        while (!result_dir.empty() &&
               (result_dir.back() == '\\' || result_dir.back() == '/'))
            result_dir.pop_back();

        if (!make_catalog(ddf_path, inf_path, result_dir, err)) {
            rep.result = DongleSwitchResult::PnPError;
            rep.message = err;
            return rep;
        }
        std::string cat_path = result_dir + "\\" + kCatName;
        if (!sign_catalog(cat_path, pfx, err)) {
            rep.result = DongleSwitchResult::PnPError;
            rep.message = err;
            return rep;
        }

        DeleteFileA(ddf_path.c_str());

        std::wstring add_args = L"/add-driver " + to_wide(quote(inf_path)) +
            L" /install /force";
        if (!run_ok(add_args, rep)) return rep;
        return rep;
    }
} // namespace

DongleSwitchReport set_dongle_winusb(uint16_t vid, uint16_t pid, bool enable_winusb)
{
    DongleSwitchReport rep;
    rep.result = DongleSwitchResult::Ok;

    DongleDriverStatus st = detect_dongle_driver(vid, pid);
    if (st.mode == DongleDriverMode::NotPresent) {
        char msg[96];
        snprintf(msg, sizeof(msg), "No UB500 (VID 0x%04X/PID 0x%04X) device node found.",
                 (unsigned)vid, (unsigned)pid);
        rep.result = DongleSwitchResult::NotPresent;
        rep.message = msg;
        return rep;
    }

    bool already = (enable_winusb && st.mode == DongleDriverMode::WinUsbStream) ||
                   (!enable_winusb && st.mode == DongleDriverMode::BthUsb);
    if (already) {
        rep.result = DongleSwitchResult::NoChange;
        rep.message = enable_winusb
                          ? "Dongle is already in WinUSB (Streaming) mode."
                          : "Dongle is already in BTHUSB (Windows Bluetooth) mode.";
        return rep;
    }

    /* 1) Clean the store of any WinUSB-flavored package matching this VID:PID
       (ours after an earlier toggle, or one installed via Zadig). */
    auto infs = find_winusb_infs(vid, pid);
    for (const auto &inf : infs) {
        std::wstring del = L"/delete-driver " + to_wide(inf) + L" /uninstall";
        if (!run_ok(del, rep)) return rep;
    }

    /* 2) Enable: generate + sign a catalog for our own WinUSB package. */
    if (enable_winusb) {
        rep = install_winusb_package(vid, pid);
        if (rep.result != DongleSwitchResult::Ok) return rep;
    }

    /* 3) Force re-evaluation: drop the devnode and let Setup re-pick the best
       driver (our fresh WinUSB package, or the vendor BTHUSB package). */
    if (!st.instance_id.empty()) {
        std::wstring rem_args = L"/remove-device " + to_wide(quote(st.instance_id));
        if (!run_ok(rem_args, rep)) return rep;
    }
    if (!run_ok(L"/scan-devices", rep)) return rep;

    rep.message = enable_winusb
                      ? "WinUSB driver installed. Streaming mode should now be active (re-detecting...)."
                      : "WinUSB packages removed, device restored for BTHUSB (re-detecting...).";
    return rep;
}