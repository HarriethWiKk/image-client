// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QString>
#include <QStringList>
#include <QStringView>

namespace oic::core {

// Model identity must always be compared case- and whitespace-normalized.
// The old CLI compared `model == "gpt-image-2"` verbatim, so `GPT-Image-2`
// silently took the edit path with the `image` field instead of `image[]` and
// skipped the transparent-background rejection, surfacing as an unexplainable
// upstream 400.
bool isGptImage2(QStringView model);

bool isGrokImageModel(QStringView model);

// Derives "1k"/"2k" from a "WxH" size string; falls back to "1k" when unparsable.
QString grokResolutionFromSize(QStringView size);

// Nearest supported Grok aspect ratio for a "WxH" size string; "1:1" when unparsable.
QString grokAspectRatioFromSize(QStringView size);

// Same derivation on already-parsed edges. Exposed so callers that hold a
// width/height pair need no second parse.
QString closestGrokRatio(int width, int height);

// Ratios a user may request. Includes "auto", which is accepted as input but is
// never produced by closestGrokRatio.
QStringList grokSupportedRatios();

// Parses "1024x1024" (case-insensitive, surrounding whitespace allowed).
// Returns false when either edge is missing, non-numeric, or non-positive.
bool parseSize(QStringView size, int *width, int *height);

}  // namespace oic::core
