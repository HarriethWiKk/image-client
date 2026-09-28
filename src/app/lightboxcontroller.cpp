// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 HarriethWiKk
#include "oic/app/lightboxcontroller.h"

namespace oic::app {

LightboxController::LightboxController(QObject *parent) : QObject(parent) {}

void LightboxController::setModel(const QStringList &urls)
{
    m_model = urls;
    if (m_model.isEmpty()) {
        m_currentIndex = -1;
        m_compareIndex = -1;
    } else {
        m_currentIndex = qBound(0, m_currentIndex < 0 ? 0 : m_currentIndex, m_model.size() - 1);
        if (m_compare)
            m_compareIndex = qBound(0, m_compareIndex < 0 ? 0 : m_compareIndex, m_model.size() - 1);
    }
    emit changed();
}

void LightboxController::openAt(int index)
{
    if (m_model.isEmpty()) {  // nothing to show: stay closed rather than index -1
        m_open = false;
        emit changed();
        return;
    }
    m_currentIndex = qBound(0, index, m_model.size() - 1);
    m_zoom = kMinZoom;
    m_compare = false;
    m_compareIndex = -1;
    m_open = true;
    emit changed();
}

void LightboxController::close()
{
    m_open = false;
    m_compare = false;
    m_compareIndex = -1;
    m_zoom = kMinZoom;
    emit changed();
}

void LightboxController::step(int delta)
{
    const int n = m_model.size();
    if (n == 0)
        return;
    m_currentIndex = ((m_currentIndex + delta) % n + n) % n;  // wrap in both directions
    if (m_compare && m_compareIndex >= 0)
        m_compareIndex = ((m_compareIndex + delta) % n + n) % n;
    m_zoom = kMinZoom;  // a new image is shown; drop the zoom so it is never off-screen
    emit changed();
}

void LightboxController::next()
{
    step(1);
}

void LightboxController::prev()
{
    step(-1);
}

void LightboxController::zoomIn()
{
    m_zoom = qMin(kMaxZoom, m_zoom * kZoomStep);
    emit changed();
}

void LightboxController::zoomOut()
{
    m_zoom = qMax(kMinZoom, m_zoom / kZoomStep);
    emit changed();
}

void LightboxController::resetZoom()
{
    m_zoom = kMinZoom;
    emit changed();
}

void LightboxController::toggleCompare()
{
    if (m_model.size() < 2) {  // cannot pair a single image; stay in single view
        m_compare = false;
        m_compareIndex = -1;
        emit changed();
        return;
    }
    m_compare = !m_compare;
    if (m_compare) {
        if (m_compareIndex < 0)
            m_compareIndex = (m_currentIndex + 1) % m_model.size();
    } else {
        m_compareIndex = -1;  // leaving compare mode: the partner index is no longer meaningful
    }
    emit changed();
}

}  // namespace oic::app
