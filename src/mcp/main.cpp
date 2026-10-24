// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include <QCoreApplication>
#include <QStringList>
#include <QTextStream>

#ifdef Q_OS_WIN
#  include <fcntl.h>
#  include <io.h>
#endif

#include "oic/core/endpoints.h"
#include "oic/mcp/server.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QStringList args = QCoreApplication::arguments();
    if (args.size() > 1) {
        QTextStream out(stdout);
        QTextStream err(stderr);
        const QString arg = args.at(1);
        if (arg == QLatin1String("--version")) {
            out << oic::mcp::kServerName << " " << oic::mcp::kServerVersion << '\n';
            out.flush();
            return 0;
        }
        if (arg == QLatin1String("--smoke")) {
            // Confirms the core library is linked and usable from this binary.
            const QStringList candidates = oic::core::candidateEndpoints(
                QStringLiteral("https://api.openai.com"), oic::core::Route::Generations);
            out << candidates.join(QLatin1Char('\n')) << '\n';
            out.flush();
            return 0;
        }
        err << "unknown argument: " << arg << '\n';
        err.flush();
        return 2;
    }

#ifdef Q_OS_WIN
    // stdio is the protocol channel: text mode would turn every LF frame into CRLF.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    oic::mcp::Server server;
    return server.run();
}
