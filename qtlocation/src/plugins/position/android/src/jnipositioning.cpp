/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtPositioning module of the Qt Toolkit.
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

#include <QDateTime>
#include <QDebug>
#include <QMap>
#include <QRandomGenerator>
#include <QtGlobal>
#include <QtCore/private/qjnihelpers_p.h>
#include <android/log.h>
#include <QGeoPositionInfo>
#include "qgeopositioninfosource_android_p.h"
#include "qgeosatelliteinfosource_android_p.h"

#include "jnipositioning.h"

static JavaVM *javaVM = nullptr;
static jclass positioningClass;

static jmethodID providerListMethodId;
static jmethodID lastKnownPositionMethodId;
static jmethodID startUpdatesMethodId;
static jmethodID stopUpdatesMethodId;
static jmethodID requestUpdateMethodId;
static jmethodID startSatelliteUpdatesMethodId;

static const char logTag[] = "QtPositioning";
static const char classErrorMsg[] = "Can't find class \"%s\"";
static const char methodErrorMsg[] = "Can't find method \"%s%s\"";

namespace {

/*!
    \internal
    This class encapsulates satellite system types, as defined by Android
    GnssStatus API. Initialize during JNI_OnLoad() by the init() method, from
    the Java side, rather than hard-coding.
*/
class ConstellationMapper
{
public:
    static bool init(JNIEnv *jniEnv)
    {
        if (QtAndroidPrivate::androidSdkVersion() > 23) {
            jclass gnssStatusObject = jniEnv->FindClass("android/location/GnssStatus");
            if (!gnssStatusObject)
                return false;

            jfieldID gpsFieldId = jniEnv->GetStaticFieldID(gnssStatusObject,
                                                           "CONSTELLATION_GPS", "I");
            jfieldID glonassFieldId = jniEnv->GetStaticFieldID(gnssStatusObject,
                                                               "CONSTELLATION_GLONASS", "I");
            if (!gpsFieldId || !glonassFieldId)
                return false;

            m_gpsId = jniEnv->GetStaticIntField(gnssStatusObject, gpsFieldId);
            m_glonassId = jniEnv->GetStaticIntField(gnssStatusObject, glonassFieldId);
        }
        // no need to query it for API level <= 23
        return true;
    }

    static QGeoSatelliteInfo::SatelliteSystem toSatelliteSystem(int constellationType)
    {
        if (constellationType == m_gpsId)
            return QGeoSatelliteInfo::GPS;
        else if (constellationType == m_glonassId)
            return QGeoSatelliteInfo::GLONASS;

        return QGeoSatelliteInfo::Undefined;
    }

private:
    static int m_gpsId;
    static int m_glonassId;
};

int ConstellationMapper::m_gpsId = -1;
int ConstellationMapper::m_glonassId = -1;

} // anonymous namespace

namespace AndroidPositioning {
    typedef QMap<int, QGeoPositionInfoSourceAndroid * > PositionSourceMap;
    typedef QMap<int, QGeoSatelliteInfoSourceAndroid * > SatelliteSourceMap;

    Q_GLOBAL_STATIC(PositionSourceMap, idToPosSource)

    Q_GLOBAL_STATIC(SatelliteSourceMap, idToSatSource)

    struct AttachedJNIEnv
    {
        AttachedJNIEnv()
        {
            attached = false;
            if (javaVM && javaVM->GetEnv(reinterpret_cast<void**>(&jniEnv), JNI_VERSION_1_6) < 0) {
                if (javaVM->AttachCurrentThread(&jniEnv, nullptr) < 0) {
                    __android_log_print(ANDROID_LOG_ERROR, logTag, "AttachCurrentThread failed");
                    jniEnv = nullptr;
                    return;
                }
                attached = true;
            }
        }

        ~AttachedJNIEnv()
        {
            if (attached)
                javaVM->DetachCurrentThread();
        }
        bool attached;
        JNIEnv *jniEnv;
    };

    int registerPositionInfoSource(QObject *obj)
    {
        static bool firstInit = true;
        if (firstInit) {
            firstInit = false;
        }

        int key = -1;
        if (obj->inherits("QGeoPositionInfoSource")) {
            QGeoPositionInfoSourceAndroid *src = qobject_cast<QGeoPositionInfoSourceAndroid *>(obj);
            Q_ASSERT(src);
            do {
                key = qAbs(int(QRandomGenerator::global()->generate()));
            } while (idToPosSource()->contains(key));

            idToPosSource()->insert(key, src);
        } else if (obj->inherits("QGeoSatelliteInfoSource")) {
            QGeoSatelliteInfoSourceAndroid *src = qobject_cast<QGeoSatelliteInfoSourceAndroid *>(obj);
            Q_ASSERT(src);
            do {
                key = qAbs(int(QRandomGenerator::global()->generate()));
            } while (idToSatSource()->contains(key));

            idToSatSource()->insert(key, src);
        }

        return key;
    }

    void unregisterPositionInfoSource(int key)
    {
        if (idToPosSource.exists())
            idToPosSource->remove(key);

        if (idToSatSource.exists())
            idToSatSource->remove(key);
    }

    enum PositionProvider
    {
        PROVIDER_GPS = 0,
        PROVIDER_NETWORK = 1,
        PROVIDER_PASSIVE = 2
    };


    QGeoPositionInfoSource::PositioningMethods availableProviders()
    {
        QGeoPositionInfoSource::PositioningMethods ret = QGeoPositionInfoSource::NoPositioningMethods;
        AttachedJNIEnv env;
        if (!env.jniEnv)
            return ret;
        jintArray jProviders = static_cast<jintArray>(env.jniEnv->CallStaticObjectMethod(
                                                          positioningClass, providerListMethodId));
        if (!jProviders) {
            // Work-around for QTBUG-116645
            __android_log_print(ANDROID_LOG_INFO, logTag, "Got null providers array!");
            return ret;
        }
        jint *providers = env.jniEnv->GetIntArrayElements(jProviders, nullptr);
        const int size = env.jniEnv->GetArrayLength(jProviders);
        for (int i = 0; i < size; i++) {
            switch (providers[i]) {
            case PROVIDER_GPS:
                ret |= QGeoPositionInfoSource::SatellitePositioningMethods;
                break;
            case PROVIDER_NETWORK:
                ret |= QGeoPositionInfoSource::NonSatellitePositioningMethods;
                break;
            case PROVIDER_PASSIVE:
                //we ignore as Qt doesn't have interface for it right now
                break;
            default:
                __android_log_print(ANDROID_LOG_INFO, logTag, "Unknown positioningMethod");
            }
        }

        env.jniEnv->ReleaseIntArrayElements(jProviders, providers, 0);
        env.jniEnv->DeleteLocalRef(jProviders);

        return ret;
    }

    //caching originally taken from corelib/kernel/qjni.cpp
    typedef QHash<QByteArray, jmethodID> JMethodIDHash;
    Q_GLOBAL_STATIC(JMethodIDHash, cachedMethodID)

    static jmethodID getCachedMethodID(JNIEnv *env,
                                       jclass clazz,
                                       const char *name,
                                       const char *sig)
    {
        jmethodID id = nullptr;
        uint offset_name = qstrlen(name);
        uint offset_signal = qstrlen(sig);
        QByteArray key(int(offset_name + offset_signal), Qt::Uninitialized);
        memcpy(key.data(), name, offset_name);
        memcpy(key.data()+offset_name, sig, offset_signal);
        QHash<QByteArray, jmethodID>::iterator it = cachedMethodID->find(key);
        if (it == cachedMethodID->end()) {
            id = env->GetMethodID(clazz, name, sig);
            if (env->ExceptionCheck()) {
                id = nullptr;
    #ifdef QT_DEBUG
                env->ExceptionDescribe();
    #endif // QT_DEBUG
                env->ExceptionClear();
            }

            cachedMethodID->insert(key, id);
        } else {
            id = it.value();
        }
        return id;
    }

    QGeoPositionInfo positionInfoFromJavaLocation(JNIEnv * jniEnv, const jobject &location)
    {
        QGeoPositionInfo info;
        jclass thisClass = jniEnv->GetObjectClass(location);
        if (!thisClass)
            return QGeoPositionInfo();

        jmethodID mid = getCachedMethodID(jniEnv, thisClass, "getLatitude", "()D");
        jdouble latitude = jniEnv->CallDoubleMethod(location, mid);
        mid = getCachedMethodID(jniEnv, thisClass, "getLongitude", "()D");
        jdouble longitude = jniEnv->CallDoubleMethod(location, mid);
        QGeoCoordinate coordinate(latitude, longitude);

        //altitude
        mid = getCachedMethodID(jniEnv, thisClass, "hasAltitude", "()Z");
        jboolean attributeExists = jniEnv->CallBooleanMethod(location, mid);
        if (attributeExists) {
            mid = getCachedMethodID(jniEnv, thisClass, "getAltitude", "()D");
            jdouble value = jniEnv->CallDoubleMethod(location, mid);
            if (value != 0.0)
            {
                coordinate.setAltitude(value);
            }
        }

        info.setCoordinate(coordinate);

        //time stamp
        mid = getCachedMethodID(jniEnv, thisClass, "getTime", "()J");
        jlong timestamp = jniEnv->CallLongMethod(location, mid);
        info.setTimestamp(QDateTime::fromMSecsSinceEpoch(timestamp, Qt::UTC));

        //horizontal accuracy
        mid = getCachedMethodID(jniEnv, thisClass, "hasAccuracy", "()Z");
        attributeExists = jniEnv->CallBooleanMethod(location, mid);
        if (attributeExists) {
            mid = getCachedMethodID(jniEnv, thisClass, "getAccuracy", "()F");
            jfloat accuracy = jniEnv->CallFloatMethod(location, mid);
            if (accuracy != 0.0)
            {
                info.setAttribute(QGeoPositionInfo::HorizontalAccuracy, qreal(accuracy));
            }
        }

        //vertical accuracy
        mid = getCachedMethodID(jniEnv, thisClass, "hasVerticalAccuracy", "()Z");
        if (mid) {
            attributeExists = jniEnv->CallBooleanMethod(location, mid);
            if (attributeExists) {
                mid = getCachedMethodID(jniEnv, thisClass, "getVerticalAccuracyMeters", "()F");
                if (mid) {
                    jfloat accuracy = jniEnv->CallFloatMethod(location, mid);
                    if (accuracy != 0.0)
                    {
                        info.setAttribute(QGeoPositionInfo::VerticalAccuracy, qreal(accuracy));
                    }
                }
            }
        }

        if (!mid)
            jniEnv->ExceptionClear();

        //ground speed
        mid = getCachedMethodID(jniEnv, thisClass, "hasSpeed", "()Z");
        attributeExists = jniEnv->CallBooleanMethod(location, mid);
        if (attributeExists) {
            mid = getCachedMethodID(jniEnv, thisClass, "getSpeed", "()F");
            jfloat speed = jniEnv->CallFloatMethod(location, mid);
            if (speed != 0)
            {
                info.setAttribute(QGeoPositionInfo::GroundSpeed, qreal(speed));
            }
        }

        //bearing
        mid = getCachedMethodID(jniEnv, thisClass, "hasBearing", "()Z");
        attributeExists = jniEnv->CallBooleanMethod(location, mid);
        if (attributeExists) {
            mid = getCachedMethodID(jniEnv, thisClass, "getBearing", "()F");
            jfloat bearing = jniEnv->CallFloatMethod(location, mid);
            if (bearing != 0.0)
            {
                info.setAttribute(QGeoPositionInfo::Direction, qreal(bearing));
            }
        }

        jniEnv->DeleteLocalRef(thisClass);
        return info;
    }

    QList<QGeoSatelliteInfo> satelliteInfoFromJavaLocation(JNIEnv *jniEnv,
                                                           jobjectArray satellites,
                                                           QList<QGeoSatelliteInfo>* usedInFix)
    {
        QList<QGeoSatelliteInfo> sats;
        jsize length = jniEnv->GetArrayLength(satellites);
        for (int i = 0; i<length; i++) {
            jobject element = jniEnv->GetObjectArrayElement(satellites, i);
            if (jniEnv->ExceptionOccurred()) {
                qWarning() << "Cannot process all satellite data due to exception.";
                break;
            }

            jclass thisClass = jniEnv->GetObjectClass(element);
            if (!thisClass)
                continue;

            QGeoSatelliteInfo info;

            //signal strength
            jmethodID mid = getCachedMethodID(jniEnv, thisClass, "getSnr", "()F");
            jfloat snr = jniEnv->CallFloatMethod(element, mid);
            info.setSignalStrength(int(snr));

            //ignore any satellite with no signal whatsoever
            if (qFuzzyIsNull(snr))
                continue;

            //prn
            mid = getCachedMethodID(jniEnv, thisClass, "getPrn", "()I");
            jint prn = jniEnv->CallIntMethod(element, mid);
            info.setSatelliteIdentifier(prn);

            if (prn >= 1 && prn <= 32)
                info.setSatelliteSystem(QGeoSatelliteInfo::GPS);
            else if (prn >= 65 && prn <= 96)
                info.setSatelliteSystem(QGeoSatelliteInfo::GLONASS);

            //azimuth
            mid = getCachedMethodID(jniEnv, thisClass, "getAzimuth", "()F");
            jfloat azimuth = jniEnv->CallFloatMethod(element, mid);
            info.setAttribute(QGeoSatelliteInfo::Azimuth, qreal(azimuth));

            //elevation
            mid = getCachedMethodID(jniEnv, thisClass, "getElevation", "()F");
            jfloat elevation = jniEnv->CallFloatMethod(element, mid);
            info.setAttribute(QGeoSatelliteInfo::Elevation, qreal(elevation));

            //used in a fix
            mid = getCachedMethodID(jniEnv, thisClass, "usedInFix", "()Z");
            jboolean inFix = jniEnv->CallBooleanMethod(element, mid);

            sats.append(info);

            if (inFix)
                usedInFix->append(info);

            jniEnv->DeleteLocalRef(thisClass);
            jniEnv->DeleteLocalRef(element);
        }

        return sats;
    }

    QList<QGeoSatelliteInfo> satelliteInfoFromJavaGnssStatus(JNIEnv *jniEnv, jobject gnssStatus,
                                                             QList<QGeoSatelliteInfo>* usedInFix)
    {
        QList<QGeoSatelliteInfo> sats;

        jclass statusClass = jniEnv->GetObjectClass(gnssStatus);
        if (!statusClass)
            return sats;

        jmethodID satCountMethod = getCachedMethodID(jniEnv, statusClass,
                                                     "getSatelliteCount", "()I");
        jmethodID sigStrengthMethod = getCachedMethodID(jniEnv, statusClass, "getCn0DbHz", "(I)F");
        jmethodID constTypeMethod = getCachedMethodID(jniEnv, statusClass,
                                                      "getConstellationType", "(I)I");
        jmethodID svIdMethod = getCachedMethodID(jniEnv, statusClass, "getSvid", "(I)I");
        jmethodID azimuthMethod = getCachedMethodID(jniEnv, statusClass,
                                                    "getAzimuthDegrees", "(I)F");
        jmethodID elevationMethod = getCachedMethodID(jniEnv, statusClass,
                                                      "getElevationDegrees", "(I)F");
        jmethodID usedInFixMethod = getCachedMethodID(jniEnv, statusClass,
                                                      "usedInFix", "(I)Z");

        if (!satCountMethod || !sigStrengthMethod || !constTypeMethod || !svIdMethod
                || !azimuthMethod || !elevationMethod || !usedInFixMethod) {
            jniEnv->DeleteLocalRef(statusClass);
            return sats;
        }

        const int satellitesCount = jniEnv->CallIntMethod(gnssStatus, satCountMethod);
        for (int i = 0; i < satellitesCount; ++i) {
            QGeoSatelliteInfo info;

            // signal strength - this is actually a carrier-to-noise density,
            // but the values are very close to what was previously returned by
            // getSnr() method of the GpsSatellite API.
            const jfloat cn0 = jniEnv->CallFloatMethod(gnssStatus, sigStrengthMethod, i);
            info.setSignalStrength(static_cast<int>(cn0));

            // satellite system
            const jint constellationType = jniEnv->CallIntMethod(gnssStatus, constTypeMethod, i);
            info.setSatelliteSystem(ConstellationMapper::toSatelliteSystem(constellationType));

            // satellite identifier
            const jint svId = jniEnv->CallIntMethod(gnssStatus, svIdMethod, i);
            info.setSatelliteIdentifier(svId);

            // azimuth
            const jfloat azimuth = jniEnv->CallFloatMethod(gnssStatus, azimuthMethod, i);
            info.setAttribute(QGeoSatelliteInfo::Azimuth, static_cast<qreal>(azimuth));

            // elevation
            const jfloat elevation = jniEnv->CallFloatMethod(gnssStatus, elevationMethod, i);
            info.setAttribute(QGeoSatelliteInfo::Elevation, static_cast<qreal>(elevation));

            // Used in fix - true if this satellite is actually used in
            // determining the position.
            const jboolean inFix = jniEnv->CallBooleanMethod(gnssStatus, usedInFixMethod, i);

            sats.append(info);

            if (inFix)
                usedInFix->append(info);
        }

        jniEnv->DeleteLocalRef(statusClass);
        return sats;
    }

    QGeoPositionInfo lastKnownPosition(bool fromSatellitePositioningMethodsOnly)
    {
        AttachedJNIEnv env;
        if (!env.jniEnv)
            return QGeoPositionInfo();

        if (!requestionPositioningPermissions(env.jniEnv))
            return {};

        jobject location = env.jniEnv->CallStaticObjectMethod(positioningClass,
                                                              lastKnownPositionMethodId,
                                                              fromSatellitePositioningMethodsOnly);
        if (location == nullptr)
            return QGeoPositionInfo();

        const QGeoPositionInfo info = positionInfoFromJavaLocation(env.jniEnv, location);
        env.jniEnv->DeleteLocalRef(location);

        return info;
    }

    inline int positioningMethodToInt(QGeoPositionInfoSource::PositioningMethods m)
    {
        int providerSelection = 0;
        if (m & QGeoPositionInfoSource::SatellitePositioningMethods)
            providerSelection |= 1;
        if (m & QGeoPositionInfoSource::NonSatellitePositioningMethods)
            providerSelection |= 2;

        return providerSelection;
    }

    QGeoPositionInfoSource::Error startUpdates(int androidClassKey)
    {
        AttachedJNIEnv env;
        if (!env.jniEnv)
            return QGeoPositionInfoSource::UnknownSourceError;

        QGeoPositionInfoSourceAndroid *source = AndroidPositioning::idToPosSource()->value(androidClassKey);

        if (source) {
            if (!requestionPositioningPermissions(env.jniEnv))
                return QGeoPositionInfoSource::AccessError;

            int errorCode = env.jniEnv->CallStaticIntMethod(positioningClass, startUpdatesMethodId,
                                             androidClassKey,
                                             positioningMethodToInt(source->preferredPositioningMethods()),
                                             source->updateInterval());
            switch (errorCode) {
            case 0:
            case 1:
            case 2:
            case 3:
                return static_cast<QGeoPositionInfoSource::Error>(errorCode);
            default:
                break;
            }
        }

        return QGeoPositionInfoSource::UnknownSourceError;
    }

    //used for stopping regular and single updates
    void stopUpdates(int androidClassKey)
    {
        AttachedJNIEnv env;
        if (!env.jniEnv)
            return;

        env.jniEnv->CallStaticVoidMethod(positioningClass, stopUpdatesMethodId, androidClassKey);
    }

    QGeoPositionInfoSource::Error requestUpdate(int androidClassKey)
    {
        AttachedJNIEnv env;
        if (!env.jniEnv)
            return QGeoPositionInfoSource::UnknownSourceError;

        QGeoPositionInfoSourceAndroid *source = AndroidPositioning::idToPosSource()->value(androidClassKey);

        if (source) {
            if (!requestionPositioningPermissions(env.jniEnv))
                return QGeoPositionInfoSource::AccessError;

            int errorCode = env.jniEnv->CallStaticIntMethod(positioningClass, requestUpdateMethodId,
                                             androidClassKey,
                                             positioningMethodToInt(source->preferredPositioningMethods()));
            switch (errorCode) {
            case 0:
            case 1:
            case 2:
            case 3:
                return static_cast<QGeoPositionInfoSource::Error>(errorCode);
            default:
                break;
            }
        }
        return QGeoPositionInfoSource::UnknownSourceError;
    }

    QGeoSatelliteInfoSource::Error startSatelliteUpdates(int androidClassKey, bool isSingleRequest, int requestTimeout)
    {
        AttachedJNIEnv env;
        if (!env.jniEnv)
            return QGeoSatelliteInfoSource::UnknownSourceError;

        QGeoSatelliteInfoSourceAndroid *source = AndroidPositioning::idToSatSource()->value(androidClassKey);

        if (source) {
            if (!requestionPositioningPermissions(env.jniEnv))
                return QGeoSatelliteInfoSource::AccessError;

            int interval = source->updateInterval();
            if (isSingleRequest)
                interval = requestTimeout;
            int errorCode = env.jniEnv->CallStaticIntMethod(positioningClass, startSatelliteUpdatesMethodId,
                                             androidClassKey,
                                             interval, isSingleRequest);
            switch (errorCode) {
            case -1:
            case 0:
            case 1:
            case 2:
                return static_cast<QGeoSatelliteInfoSource::Error>(errorCode);
            default:
                qWarning() << "startSatelliteUpdates: Unknown error code " << errorCode;
                break;
            }
        }
        return QGeoSatelliteInfoSource::UnknownSourceError;
    }

    bool requestionPositioningPermissions(JNIEnv *env)
    {
        using namespace QtAndroidPrivate;

        if (androidSdkVersion() < 23)
            return true;

        // Android v23+ requires runtime permission check and requests
        QString permission(QLatin1String("android.permission.ACCESS_FINE_LOCATION"));

        if (checkPermission(permission) == PermissionsResult::Denied) {
            const QHash<QString, PermissionsResult> results =
                    requestPermissionsSync(env, QStringList() << permission);
            if (!results.contains(permission) || results[permission] == PermissionsResult::Denied) {
                qWarning() << "Position data not available due to missing permission " << permission;
                return false;
            }
        }

        return true;
    }
}

static void positionUpdated(JNIEnv *env, jobject /*thiz*/, jobject location, jint androidClassKey, jboolean isSingleUpdate)
{
    QGeoPositionInfo info = AndroidPositioning::positionInfoFromJavaLocation(env, location);

    QGeoPositionInfoSourceAndroid *source = AndroidPositioning::idToPosSource()->value(androidClassKey);
    if (!source) {
        qWarning("positionUpdated: source == 0");
        return;
    }

    //we need to invoke indirectly as the Looper thread is likely to be not the same thread
    if (!isSingleUpdate)
        QMetaObject::invokeMethod(source, "processPositionUpdate", Qt::AutoConnection,
                              Q_ARG(QGeoPositionInfo, info));
    else
        QMetaObject::invokeMethod(source, "processSinglePositionUpdate", Qt::AutoConnection,
                              Q_ARG(QGeoPositionInfo, info));
}

static void locationProvidersDisabled(JNIEnv *env, jobject /*thiz*/, jint androidClassKey)
{
    Q_UNUSED(env);
    QObject *source = AndroidPositioning::idToPosSource()->value(androidClassKey);
    if (!source)
        source = AndroidPositioning::idToSatSource()->value(androidClassKey);
    if (!source) {
        qWarning("locationProvidersDisabled: source == 0");
        return;
    }

    QMetaObject::invokeMethod(source, "locationProviderDisabled", Qt::AutoConnection);
}

static void locationProvidersChanged(JNIEnv *env, jobject /*thiz*/, jint androidClassKey)
{
    Q_UNUSED(env);
    QObject *source = AndroidPositioning::idToPosSource()->value(androidClassKey);
    if (!source) {
        qWarning("locationProvidersChanged: source == 0");
        return;
    }

    QMetaObject::invokeMethod(source, "locationProvidersChanged", Qt::AutoConnection);
}

static void notifySatelliteInfoUpdated(const QList<QGeoSatelliteInfo> &inView,
                                       const QList<QGeoSatelliteInfo> &inUse,
                                       jint androidClassKey, jboolean isSingleUpdate)
{
    QGeoSatelliteInfoSourceAndroid *source = AndroidPositioning::idToSatSource()->value(androidClassKey);
    if (!source) {
        qWarning("satelliteUpdated: source == 0");
        return;
    }

    QMetaObject::invokeMethod(source, "processSatelliteUpdateInView", Qt::AutoConnection,
                              Q_ARG(QList<QGeoSatelliteInfo>, inView), Q_ARG(bool, isSingleUpdate));

    QMetaObject::invokeMethod(source, "processSatelliteUpdateInUse", Qt::AutoConnection,
                              Q_ARG(QList<QGeoSatelliteInfo>, inUse), Q_ARG(bool, isSingleUpdate));
}

static void satelliteGpsUpdated(JNIEnv *env, jobject thiz, jobjectArray satellites,
                                jint androidClassKey, jboolean isSingleUpdate)
{
    Q_UNUSED(thiz);
    QList<QGeoSatelliteInfo> inUse;
    QList<QGeoSatelliteInfo> sats =
            AndroidPositioning::satelliteInfoFromJavaLocation(env, satellites, &inUse);

    notifySatelliteInfoUpdated(sats, inUse, androidClassKey, isSingleUpdate);
}

static void satelliteGnssUpdated(JNIEnv *env, jobject thiz, jobject gnssStatus,
                                 jint androidClassKey, jboolean isSingleUpdate)
{
    Q_UNUSED(thiz);
    QList<QGeoSatelliteInfo> inUse;
    QList<QGeoSatelliteInfo> sats =
            AndroidPositioning::satelliteInfoFromJavaGnssStatus(env, gnssStatus, &inUse);

    notifySatelliteInfoUpdated(sats, inUse, androidClassKey, isSingleUpdate);
}


#define FIND_AND_CHECK_CLASS(CLASS_NAME) \
clazz = env->FindClass(CLASS_NAME); \
if (!clazz) { \
    __android_log_print(ANDROID_LOG_FATAL, logTag, classErrorMsg, CLASS_NAME); \
    return JNI_FALSE; \
}

#define GET_AND_CHECK_STATIC_METHOD(VAR, CLASS, METHOD_NAME, METHOD_SIGNATURE) \
VAR = env->GetStaticMethodID(CLASS, METHOD_NAME, METHOD_SIGNATURE); \
if (!VAR) { \
    __android_log_print(ANDROID_LOG_FATAL, logTag, methodErrorMsg, METHOD_NAME, METHOD_SIGNATURE); \
    return JNI_FALSE; \
}

static JNINativeMethod methods[] = {
    {"positionUpdated", "(Landroid/location/Location;IZ)V", (void *)positionUpdated},
    {"locationProvidersDisabled", "(I)V", (void *) locationProvidersDisabled},
    {"satelliteGpsUpdated", "([Landroid/location/GpsSatellite;IZ)V", (void *)satelliteGpsUpdated},
    {"locationProvidersChanged", "(I)V", (void *) locationProvidersChanged},
    {"satelliteGnssUpdated", "(Landroid/location/GnssStatus;IZ)V", (void *)satelliteGnssUpdated}
};

static bool registerNatives(JNIEnv *env)
{
    jclass clazz;
    FIND_AND_CHECK_CLASS("org/qtproject/qt5/android/positioning/QtPositioning");
    positioningClass = static_cast<jclass>(env->NewGlobalRef(clazz));

    if (env->RegisterNatives(positioningClass, methods, sizeof(methods) / sizeof(methods[0])) < 0) {
        __android_log_print(ANDROID_LOG_FATAL, logTag, "RegisterNatives failed");
        return JNI_FALSE;
    }

    GET_AND_CHECK_STATIC_METHOD(providerListMethodId, positioningClass, "providerList", "()[I");
    GET_AND_CHECK_STATIC_METHOD(lastKnownPositionMethodId, positioningClass, "lastKnownPosition", "(Z)Landroid/location/Location;");
    GET_AND_CHECK_STATIC_METHOD(startUpdatesMethodId, positioningClass, "startUpdates", "(III)I");
    GET_AND_CHECK_STATIC_METHOD(stopUpdatesMethodId, positioningClass, "stopUpdates", "(I)V");
    GET_AND_CHECK_STATIC_METHOD(requestUpdateMethodId, positioningClass, "requestUpdate", "(II)I");
    GET_AND_CHECK_STATIC_METHOD(startSatelliteUpdatesMethodId, positioningClass, "startSatelliteUpdates", "(IIZ)I");

    return true;
}

Q_DECL_EXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void * /*reserved*/)
{
    static bool initialized = false;
    if (initialized)
        return JNI_VERSION_1_6;
    initialized = true;

    typedef union {
        JNIEnv *nativeEnvironment;
        void *venv;
    } UnionJNIEnvToVoid;

    __android_log_print(ANDROID_LOG_INFO, logTag, "Positioning start");
    UnionJNIEnvToVoid uenv;
    uenv.venv = nullptr;
    javaVM = nullptr;

    if (vm->GetEnv(&uenv.venv, JNI_VERSION_1_6) != JNI_OK) {
        __android_log_print(ANDROID_LOG_FATAL, logTag, "GetEnv failed");
        return -1;
    }
    JNIEnv *env = uenv.nativeEnvironment;
    if (!registerNatives(env)) {
        __android_log_print(ANDROID_LOG_FATAL, logTag, "registerNatives failed");
        return -1;
    }

    if (!ConstellationMapper::init(env)) {
        __android_log_print(ANDROID_LOG_ERROR, logTag,
                            "Failed to extract constellation type constants. "
                            "Satellite system will be undefined!");
    }

    javaVM = vm;
    return JNI_VERSION_1_6;
}

