#pragma once

inline bool g_RestartRequested = false;
inline int g_RestartAdapterIndex = -1;
inline bool g_StartupSettingsSnapshotFailed = false;
#if defined(UVSR_BUILD_TESTING)
inline bool g_VerifySettingsContractRequested = false;
inline int g_VerifySettingsContractResult = 1;
inline bool g_VerifyRetainedRuntimeRequested = false;
inline int g_VerifyRetainedRuntimeResult = 1;
inline bool g_RuntimeDebugValidationRequested = false;
#endif


bool RestartCurrentProcess();
