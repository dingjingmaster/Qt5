/****************************************************************************
**
** Copyright (C) 2015 Klaralvdalens Datakonsult AB (KDAB).
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Qt3D module of the Qt Toolkit.
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

#ifndef SCENE3DITEM_H
#define SCENE3DITEM_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include <QtCore/qpointer.h>
#include <QtQuick/QQuickItem>

QT_BEGIN_NAMESPACE

class QOffscreenSurface;

namespace Qt3DCore {
class QAspectEngine;
class QEntity;
}

namespace Qt3DRender {

class QCamera;
class QRenderAspect;
class Scene3DRenderer;
class Scene3DCleaner;
class Scene3DView;
class QFrameGraphNode;
class QRenderSurfaceSelector;
class AspectEngineDestroyer;

class Scene3DItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(Qt3DCore::QEntity* entity READ entity WRITE setEntity NOTIFY entityChanged)
    Q_PROPERTY(QStringList aspects READ aspects WRITE setAspects NOTIFY aspectsChanged)
    Q_PROPERTY(bool multisample READ multisample WRITE setMultisample NOTIFY multisampleChanged)
    Q_PROPERTY(CameraAspectRatioMode cameraAspectRatioMode READ cameraAspectRatioMode WRITE setCameraAspectRatioMode NOTIFY cameraAspectRatioModeChanged)
    Q_PROPERTY(bool hoverEnabled READ isHoverEnabled WRITE setHoverEnabled NOTIFY hoverEnabledChanged)
    Q_PROPERTY(CompositingMode compositingMode READ compositingMode WRITE setCompositingMode NOTIFY compositingModeChanged REVISION 14)
    Q_CLASSINFO("DefaultProperty", "entity")
public:
    explicit Scene3DItem(QQuickItem *parent = 0);
    ~Scene3DItem();

    QStringList aspects() const;
    Qt3DCore::QEntity *entity() const;

    bool multisample() const;
    void setMultisample(bool enable);
    Q_INVOKABLE void setItemAreaAndDevicePixelRatio(QSize area, qreal devicePixelRatio);
    bool isHoverEnabled() const;

    enum CameraAspectRatioMode {
        AutomaticAspectRatio,
        UserAspectRatio
    };
    Q_ENUM(CameraAspectRatioMode); // LCOV_EXCL_LINE
    CameraAspectRatioMode cameraAspectRatioMode() const;

    enum CompositingMode {
        FBO,
        Underlay
    };
    Q_ENUM(CompositingMode) // LCOV_EXCL_LINE
    CompositingMode compositingMode() const;

    void addView(Scene3DView *view);
    void removeView(Scene3DView *view);

public Q_SLOTS:
    void setAspects(const QStringList &aspects);
    void setEntity(Qt3DCore::QEntity *entity);
    void setCameraAspectRatioMode(CameraAspectRatioMode mode);
    void setHoverEnabled(bool enabled);
    void setCompositingMode(CompositingMode mode);

Q_SIGNALS:
    void aspectsChanged();
    void entityChanged();
    void multisampleChanged();
    void cameraAspectRatioModeChanged(CameraAspectRatioMode mode);
    void hoverEnabledChanged();
    void compositingModeChanged();

private Q_SLOTS:
    void applyRootEntityChange();
    void requestUpdate();

private:
    void synchronize();
    bool prepareQt3DFrame();
    QSGNode *updatePaintNode(QSGNode *node, UpdatePaintNodeData *nodeData) override;
    void setWindowSurface(QObject *rootObject);
    void setCameraAspectModeHelper();
    void updateCameraAspectRatio();
    void mousePressEvent(QMouseEvent *event) override;
    bool needsRender(QRenderAspect *renderAspect);
    void updateWindowSurface();
    void createDummySurface(QWindow *window, QRenderSurfaceSelector *surfaceSelector);
    void applyAspects();

    QStringList m_aspects;
    Qt3DCore::QEntity *m_entity;
    Qt3DCore::QEntity *m_viewHolderEntity;
    Qt3DRender::QFrameGraphNode *m_viewHolderFG;

    Qt3DCore::QAspectEngine *m_aspectEngine;
    Qt3DCore::QAspectEngine *m_aspectToDelete;
    QSGNode *m_lastManagerNode;
    AspectEngineDestroyer *m_aspectEngineDestroyer;

    bool m_multisample;
    bool m_dirty;
    bool m_dirtyViews;
    bool m_clearsWindowByDefault;
    bool m_disableClearWindow;
    bool m_wasFrameProcessed;
    bool m_wasSGUpdated;

    QPointer<Qt3DRender::QCamera> m_camera;
    CameraAspectRatioMode m_cameraAspectRatioMode;
    CompositingMode m_compositingMode;
    QOffscreenSurface *m_dummySurface;
    QVector<Scene3DView *> m_views;
    QMetaObject::Connection m_windowConnection;
    qint8 m_framesToRender;

    static qint8 ms_framesNeededToFlushPipeline;
};

} // Qt3DRender

QT_END_NAMESPACE

#endif
