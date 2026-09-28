// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#pragma once

#include <QObject>
#include <QStringList>

namespace oic::app {

// The lightbox state machine: which image is open, at what zoom, and whether a second
// image is shown alongside for comparison. Pure view-state logic with no store/network
// dependency, so it is unit-testable directly (tst_app) while LightboxOverlay.qml binds to
// it and stays a thin view. One `changed()` signal covers every property -- the state is
// tiny and always changes together.
class LightboxController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool open READ isOpen NOTIFY changed)
    Q_PROPERTY(QStringList model READ model WRITE setModel NOTIFY changed)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY changed)
    Q_PROPERTY(qreal zoom READ zoom NOTIFY changed)
    Q_PROPERTY(bool compare READ isCompare NOTIFY changed)
    Q_PROPERTY(int compareIndex READ compareIndex NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(bool canCompare READ canCompare NOTIFY changed)

public:
    explicit LightboxController(QObject *parent = nullptr);

    bool isOpen() const { return m_open; }
    QStringList model() const { return m_model; }
    void setModel(const QStringList &urls);
    int currentIndex() const { return m_currentIndex; }
    qreal zoom() const { return m_zoom; }
    bool isCompare() const { return m_compare; }
    int compareIndex() const { return m_compareIndex; }
    int count() const { return m_model.size(); }
    bool canCompare() const { return m_model.size() >= 2; }

    Q_INVOKABLE void openAt(int index);
    Q_INVOKABLE void close();
    Q_INVOKABLE void next();
    Q_INVOKABLE void prev();
    Q_INVOKABLE void step(int delta);
    Q_INVOKABLE void zoomIn();
    Q_INVOKABLE void zoomOut();
    Q_INVOKABLE void resetZoom();
    Q_INVOKABLE void toggleCompare();

Q_SIGNALS:
    void changed();

private:
    QStringList m_model;
    int m_currentIndex = -1;
    qreal m_zoom = 1.0;
    bool m_compare = false;
    int m_compareIndex = -1;
    bool m_open = false;

    static constexpr qreal kMinZoom = 1.0;
    static constexpr qreal kMaxZoom = 8.0;
    static constexpr qreal kZoomStep = 1.25;
};

}  // namespace oic::app
