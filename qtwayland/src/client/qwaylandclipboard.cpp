/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qwaylandclipboard_p.h"
#include "qwaylanddisplay_p.h"
#include "qwaylandinputdevice_p.h"
#include "qwaylanddataoffer_p.h"
#include "qwaylanddatasource_p.h"
#include "qwaylanddatadevice_p.h"
#if QT_CONFIG(wayland_client_primary_selection)
#include "qwaylandprimaryselectionv1_p.h"
#endif

QT_BEGIN_NAMESPACE

namespace QtWaylandClient {

QWaylandClipboard::QWaylandClipboard(QWaylandDisplay *display)
    : mDisplay(display)
{
    m_clientClipboard[QClipboard::Clipboard] = nullptr;
    m_clientClipboard[QClipboard::Selection] = nullptr;
}

QWaylandClipboard::~QWaylandClipboard()
{
    if (m_clientClipboard[QClipboard::Clipboard] != m_clientClipboard[QClipboard::Selection])
        delete m_clientClipboard[QClipboard::Clipboard];
    delete m_clientClipboard[QClipboard::Selection];
}

QMimeData *QWaylandClipboard::mimeData(QClipboard::Mode mode)
{
    auto *seat = mDisplay->currentInputDevice();
    if (!seat)
        return &m_emptyData;

    switch (mode) {
    case QClipboard::Clipboard:
        if (auto *dataDevice = seat->dataDevice()) {
            if (dataDevice->selectionSource())
                return m_clientClipboard[QClipboard::Clipboard];
            if (auto *offer = dataDevice->selectionOffer())
                return offer->mimeData();
        }
        return &m_emptyData;
    case QClipboard::Selection:
#if QT_CONFIG(wayland_client_primary_selection)
        if (auto *selectionDevice = seat->primarySelectionDevice()) {
            if (selectionDevice->selectionSource())
                return m_clientClipboard[QClipboard::Selection];
            if (auto *offer = selectionDevice->selectionOffer())
                return offer->mimeData();
        }
#endif
        return &m_emptyData;
    default:
        return &m_emptyData;
    }
}

void QWaylandClipboard::setMimeData(QMimeData *data, QClipboard::Mode mode)
{
    auto *seat = mDisplay->currentInputDevice();
    if (!seat) {
        qCWarning(lcQpaWayland) << "Can't set clipboard contents with no wl_seats available";
        return;
    }

    static const QString plain = QStringLiteral("text/plain");
    static const QString utf8 = QStringLiteral("text/plain;charset=utf-8");

    if (data && data->hasFormat(plain) && !data->hasFormat(utf8))
        data->setData(utf8, data->data(plain));

    if (m_clientClipboard[mode]) {
        if (m_clientClipboard[QClipboard::Clipboard] != m_clientClipboard[QClipboard::Selection])
            delete m_clientClipboard[mode];
        m_clientClipboard[mode] = nullptr;
    }

    m_clientClipboard[mode] = data;

    switch (mode) {
    case QClipboard::Clipboard:
        if (auto *dataDevice = seat->dataDevice()) {
            dataDevice->setSelectionSource(data ? new QWaylandDataSource(mDisplay->dndSelectionHandler(),
                                                                         m_clientClipboard[QClipboard::Clipboard]) : nullptr);
            emitChanged(mode);
        }
        break;
    case QClipboard::Selection:
#if QT_CONFIG(wayland_client_primary_selection)
        if (auto *selectionDevice = seat->primarySelectionDevice()) {
            selectionDevice->setSelectionSource(data ? new QWaylandPrimarySelectionSourceV1(mDisplay->primarySelectionManager(),
                                                                                            m_clientClipboard[QClipboard::Selection]) : nullptr);
            emitChanged(mode);
        }
#endif
        break;
    default:
        break;
    }
}

bool QWaylandClipboard::supportsMode(QClipboard::Mode mode) const
{
#if QT_CONFIG(wayland_client_primary_selection)
    if (mode == QClipboard::Selection) {
        auto *seat = mDisplay->currentInputDevice();
        return seat && seat->primarySelectionDevice();
    }
#endif
    return mode == QClipboard::Clipboard;
}

bool QWaylandClipboard::ownsMode(QClipboard::Mode mode) const
{
    QWaylandInputDevice *seat = mDisplay->currentInputDevice();
    if (!seat)
        return false;

    switch (mode) {
    case QClipboard::Clipboard:
        return seat->dataDevice() && seat->dataDevice()->selectionSource() != nullptr;
#if QT_CONFIG(wayland_client_primary_selection)
    case QClipboard::Selection:
        return seat->primarySelectionDevice() && seat->primarySelectionDevice()->selectionSource() != nullptr;
#endif
    default:
        return false;
    }
}

}

QT_END_NAMESPACE
