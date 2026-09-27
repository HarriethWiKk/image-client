// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/core/models.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

namespace oic::core {
namespace {

struct Ratio {
  const char *name;
  double value;
};

QString normalize(QStringView value) {
  return QString(value).trimmed().toLower();
}

// Ordered, unlike the Python set this was ported from: on an exact tie the
// first entry now always wins instead of depending on hash iteration order.
// "auto" is an accepted user value but never a derivation target, so it is
// absent from this table.
constexpr std::array<Ratio, 13> kGrokRatios{{
    {"1:1", 1.0},
    {"16:9", 16.0 / 9.0},
    {"9:16", 9.0 / 16.0},
    {"4:3", 4.0 / 3.0},
    {"3:4", 3.0 / 4.0},
    {"3:2", 3.0 / 2.0},
    {"2:3", 2.0 / 3.0},
    {"2:1", 2.0},
    {"1:2", 0.5},
    {"19.5:9", 19.5 / 9.0},
    {"9:19.5", 9.0 / 19.5},
    {"20:9", 20.0 / 9.0},
    {"9:20", 9.0 / 20.0},
}};

constexpr std::array<const char *, 5> kGrokImageModels{{
    "grok-imagine-edit",
    "grok-imagine-image",
    "grok-imagine-image-lite",
    "grok-imagine-image-pro",
    "grok-imagine-image-quality",
}};

QString exactRatio(int width, int height) {
  const int divisor = std::gcd(width, height);
  return QStringLiteral("%1:%2").arg(width / divisor).arg(height / divisor);
}

template <std::size_t N>
QString closestRatio(int width, int height, const std::array<Ratio, N> &candidates, const char *fallback) {
  const QString exact = exactRatio(width, height);
  for (const Ratio &candidate : candidates) {
    if (QString::fromLatin1(candidate.name) == exact) {
      return QString::fromLatin1(candidate.name);
    }
  }

  const double current = static_cast<double>(width) / static_cast<double>(height);
  const Ratio *best = nullptr;
  for (const Ratio &candidate : candidates) {
    if (best == nullptr || std::abs(current - candidate.value) < std::abs(current - best->value)) {
      best = &candidate;
    }
  }
  return QString::fromLatin1(best != nullptr ? best->name : fallback);
}

}  // namespace

bool isGptImage2(QStringView model) {
  const QString value = normalize(model);
  if (value == QLatin1String("gpt-image-2") || value.startsWith(QLatin1String("gpt-image-2-"))) {
    return true;
  }
  // gpt-image-2.5-* shares the Images API path: image[] on edit, and
  // background=transparent must still be rejected.
  return value.startsWith(QLatin1String("gpt-image-2.5"));
}

bool isGrokImageModel(QStringView model) {
  const QString value = normalize(model);
  if (value == QLatin1String("grok-imagine")) {
    return true;
  }
  return std::any_of(kGrokImageModels.begin(), kGrokImageModels.end(), [&value](const char *name) {
    const QLatin1String literal(name);
    return value == literal || value.startsWith(QString(literal) + QLatin1Char('-'));
  });
}

bool parseSize(QStringView size, int *width, int *height) {
  const QString value = normalize(size);
  const int separator = value.indexOf(QLatin1Char('x'));
  if (separator < 0 || width == nullptr || height == nullptr) {
    return false;
  }

  bool widthOk = false;
  bool heightOk = false;
  const int parsedWidth = value.left(separator).trimmed().toInt(&widthOk);
  const int parsedHeight = value.mid(separator + 1).trimmed().toInt(&heightOk);
  if (!widthOk || !heightOk || parsedWidth <= 0 || parsedHeight <= 0) {
    return false;
  }

  *width = parsedWidth;
  *height = parsedHeight;
  return true;
}

QString grokResolutionFromSize(QStringView size) {
  int width = 0;
  int height = 0;
  if (!parseSize(size, &width, &height)) {
    return QStringLiteral("1k");
  }
  return std::max(width, height) > 1536 ? QStringLiteral("2k") : QStringLiteral("1k");
}

QString closestGrokRatio(int width, int height) {
  return closestRatio(width, height, kGrokRatios, "1:1");
}

QStringList grokSupportedRatios() {
  QStringList ratios;
  ratios.reserve(static_cast<qsizetype>(kGrokRatios.size()) + 1);
  for (const Ratio &ratio : kGrokRatios) {
    ratios.append(QLatin1String(ratio.name));
  }
  ratios.append(QLatin1String("auto"));
  return ratios;
}

QString grokAspectRatioFromSize(QStringView size) {
  int width = 0;
  int height = 0;
  if (!parseSize(size, &width, &height)) {
    return QStringLiteral("1:1");
  }
  return closestGrokRatio(width, height);
}

}  // namespace oic::core
