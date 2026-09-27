// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include <QCoreApplication>
#include <QStringList>
#include <QTextStream>

#include "oic/core/endpoints.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    const QStringList args = QCoreApplication::arguments();
    if (args.size() < 2) {
        err << "usage: image-client-mcp (--version|--smoke)\n"
            << "the MCP stdio protocol server is not implemented yet\n";
        return 2;
    }

    if (args.at(1) == QLatin1String("--version")) {
        out << "image-client-mcp 0.1.0\n";
        return 0;
    }

    if (args.at(1) == QLatin1String("--smoke")) {
        // Confirms the core library is linked and usable from this binary.
        const QStringList candidates =
            oic::core::candidateEndpoints(QStringLiteral("https://api.openai.com"),
                                          oic::core::Route::Generations);
        out << candidates.join(QLatin1Char('\n')) << '\n';
        return 0;
    }

    err << "unknown argument: " << args.at(1) << '\n';
    return 2;
}
