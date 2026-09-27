# Third-party notices

This project is licensed under the Apache License, Version 2.0 (see `LICENSE`).
It dynamically links against Qt and carries design and test lineage from an
upstream project. Each is listed below with its own license terms.

## Qt 6 (LGPLv3)

This program uses the Qt Toolkit, Copyright (C) The Qt Company Ltd. and other
contributors, which is offered under the GNU Lesser General Public License
version 3 (LGPLv3) or, at your option, the GNU General Public License version 2
or version 3.

Components used at runtime:

| Module | License |
|---|---|
| Qt Core (`Qt6Core.dll`) | LGPLv3 / GPL v2 / GPL v3 |
| Qt Gui (`Qt6Gui.dll`) | LGPLv3 / GPL v2 / GPL v3 |
| Qt QML (`Qt6Qml.dll`, `Qt6QmlModels.dll`) | LGPLv3 / GPL v2 / GPL v3 |
| Qt Quick (`Qt6Quick.dll`, `Qt6QuickTemplates2.dll`) | LGPLv3 / GPL v2 / GPL v3 |
| Qt Quick Controls 2 (`Qt6QuickControls2*.dll`, `qml/QtQuick/Controls/Basic`) | LGPLv3 / GPL v2 / GPL v3 |
| Qt Network (`Qt6Network.dll`) | LGPLv3 / GPL v2 / GPL v3 |
| Qt SQL (`Qt6Sql.dll`, SQLite driver) | LGPLv3 / GPL v2 / GPL v3 |
| Qt Test (`Qt6Test.dll`) — test binaries only, not distributed | LGPLv3 / GPL v2 / GPL v3 |

The full license texts are distributed with Qt and are available at
<https://www.gnu.org/licenses/lgpl-3.0.html>, <https://www.gnu.org/licenses/gpl-2.0.html>
and <https://doc.qt.io/qt-6/licenses.html>.

### How the LGPLv3 conditions are met

- **Relinking / replacement.** Qt is used exclusively through the shared
  libraries that ship in the official binary distribution; this project does not
  build or link a static Qt. Every Qt DLL in the released directory may be
  replaced with a rebuilt copy of the corresponding version, which is the
  recombination mechanism LGPLv3 requires.
- **Corresponding source for the library.** Qt source for the exact version used
  is obtainable from the official archive at
  <https://download.qt.io/archive/qt/> and from <https://code.qt.io>. The
  precise version this release was built against is recorded in `CMakeLists.txt`
  (`find_package(Qt6 6.8 ...)`); the release notes and the build log identify the
  point release.
- **No additional restrictions.** Nothing in `LICENSE` restricts modification or
  replacement of the Qt libraries.
- **Build-time tools.** `moc`, `rcc` and `qmlcachegen` are used at build time.
  <!-- VERIFY BEFORE FIRST PUBLIC RELEASE: Qt states generated code from moc/rcc/
       uic is unrestricted. Equivalent wording for qmlcachegen output was not
       located while this file was written, so confirm it (or drop AOT QML
       compilation, which is a one-line CMake change) before relying on this
       bullet. -->
- **TLS.** HTTPS is provided by the Qt TLS backend `qschannelbackend.dll`, which
  delegates to the Windows Secure Channel API already present in the operating
  system. No OpenSSL build is shipped or linked.

## SQLite

The bundled SQLite library is Public Domain, provided by
<https://www.sqlite.org/copyright.html>.

## Upstream project lineage

The protocol behaviour of this client — endpoint discovery, model name
classification, Grok aspect-ratio and resolution derivation, response parsing,
and the bounded-read and SSRF rules reproduced in `src/core` and `tests` — was
specified against, and in places ported as assertions from:

> `openai-image-client` Copyright (c) 2026 akihitohyh
> <https://github.com/HarriethWiKk/openai-image-client> (branch `main`)

That project is MIT licensed. Its license text is reproduced in full below
because portions of its logic survive in this repository in ported form.

```
MIT License

Copyright (c) 2026 akihitohyh

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
