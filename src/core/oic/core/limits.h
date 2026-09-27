// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QtGlobal>

// Frozen limits. Each value traces to a real failure mode; see SPEC §3.
// Changing one requires recording the reason next to it.
namespace oic::limits {

inline constexpr const char *kDefaultImageModel = "gpt-image-2";

inline constexpr int kDefaultTimeoutSeconds = 500;
inline constexpr int kMinTimeoutSeconds = 5;
inline constexpr int kMaxTimeoutSeconds = 600;

// GUI allows 8 images per request; the MCP surface is deliberately stricter.
inline constexpr int kMaxImageCount = 8;
inline constexpr int kMaxMcpImageCount = 4;
inline constexpr int kCliMaxImageCount = 10;

inline constexpr int kMaxUpstreamMediaItems = 16;
inline constexpr int kMaxPartialImages = 3;
inline constexpr int kDefaultPartialImages = 2;

inline constexpr int kMaxPromptChars = 16000;
inline constexpr int kMaxModelChars = 200;
inline constexpr int kMaxBaseUrlChars = 2048;
inline constexpr int kMaxApiKeyChars = 8192;
inline constexpr int kMaxSizeChars = 64;

inline constexpr qint64 kMaxReferenceImageBytes = 10LL * 1024 * 1024;
inline constexpr qint64 kMaxReferenceImagesBytes = 30LL * 1024 * 1024;
inline constexpr int kMaxEditReferenceImages = 16;
inline constexpr int kMaxGrokEditReferenceImages = 3;

inline constexpr qint64 kMaxRemoteMediaBytes = 64LL * 1024 * 1024;
inline constexpr qint64 kMaxGeneratedMediaBytes = 96LL * 1024 * 1024;
inline constexpr qint64 kMaxUpstreamJsonBytes = 128LL * 1024 * 1024;
inline constexpr qint64 kDiagnosticBodyBytes = 64LL * 1024;
inline constexpr qint64 kMaxRetainedRawBytes = 64LL * 1024;

inline constexpr int kMaxRedirects = 3;

inline constexpr int kJobTtlSeconds = 60 * 60;
inline constexpr int kMaxJobRuntimeSeconds = 15 * 60;
inline constexpr int kMaxConcurrentJobs = 2;
inline constexpr int kMaxPendingJobs = 8;
inline constexpr int kMaxLiveJobs = 64;

}  // namespace oic::limits
