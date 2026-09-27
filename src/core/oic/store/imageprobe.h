// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QByteArray>
#include <QString>

namespace oic::store {

// Image geometry read straight from the container header. Deliberately not
// QImageReader: that would drag Qt6Gui into image-client-mcp.exe, and the only
// thing the store needs is dimensions plus "is this really an image".
//
// SPEC 6.4 still requires QImageReader with setAllocationLimit(0) and
// setImageCountLimit(1) wherever pixels are actually decoded -- that is the GUI's
// job, not this code's.
struct ImageInfo {
    bool recognized = false;
    QString mime;
    int width = 0;
    int height = 0;
};

ImageInfo probeImage(const QByteArray &bytes);

}  // namespace oic::store
