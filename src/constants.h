#pragma once

#include <guiddef.h>

namespace cb {

inline constexpr wchar_t kProxyName[] = L"ASIO Stadium (Proxy)";
inline constexpr char kProxyNameA[] = "ASIO Stadium (Proxy)";
inline constexpr wchar_t kLogDirName[] = L"CrashBarrier";

// The Line 6 driver, resolved through the normal DLL search (system32). Override for testing.
inline constexpr wchar_t kRealDllName[] = L"StadiumAsio_OnInterposer3_64.dll";
inline constexpr char kRealDriverNameA[] = "Helix Stadium";  // what getDriverName returns, observed
inline constexpr wchar_t kDriverPathEnv[] = L"CRASHBARRIER_DRIVER_PATH";

// {FD7D8485-C3B6-417B-B1E8-255DB2E131B1} - generated 2026-10-27. Never change: hosts persist it.
inline constexpr CLSID CLSID_CrashBarrier = {
    0xfd7d8485, 0xc3b6, 0x417b, {0xb1, 0xe8, 0x25, 0x5d, 0xb2, 0xe1, 0x31, 0xb1}};

// {BF6AD246-4CB9-426C-B41D-520E50F0D983} - HKLM\SOFTWARE\ASIO\ASIO Stadium
inline constexpr CLSID CLSID_Stadium = {
    0xbf6ad246, 0x4cb9, 0x426c, {0xb4, 0x1d, 0x52, 0x0e, 0x50, 0xf0, 0xd9, 0x83}};

}  // namespace cb
