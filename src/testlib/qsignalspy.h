// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QSIGNALSPY_H
#define QSIGNALSPY_H

#include <QtCore/qbytearray.h>
#include <QtCore/qlist.h>
#include <QtCore/qobject.h>
#include <QtCore/qmetaobject.h>
#include <QtTest/qtesteventloop.h>
#include <QtCore/qvariant.h>
#include <QtCore/qmutex.h>

QT_BEGIN_NAMESPACE


class QVariant;

class QSignalSpy: public QObject, public QList<QList<QVariant> >
{
    struct ObjectSignal {
        const QObject *obj;
        QMetaMethod sig;
    };

public:
    explicit QSignalSpy(const QObject *obj, const char *aSignal)
        : QSignalSpy(verify(obj, aSignal)) {}

private:
    static ObjectSignal verify(const QObject *obj, const char *aSignal)
    {
        if (!isObjectValid(obj))
            return {};

        if (!aSignal) {
            qWarning("QSignalSpy: Null signal name is not valid");
            return {};
        }

        if (((aSignal[0] - '0') & 0x03) != QSIGNAL_CODE) {
            qWarning("QSignalSpy: Not a valid signal, use the SIGNAL macro");
            return {};
        }

        const QByteArray ba = QMetaObject::normalizedSignature(aSignal + 1);
        const QMetaObject * const mo = obj->metaObject();
        const int sigIndex = mo->indexOfMethod(ba.constData());
        if (sigIndex < 0) {
            qWarning("QSignalSpy: No such signal: '%s'", ba.constData());
            return {};
        }

        return verify(obj, mo->method(sigIndex));
    }

public:
#ifdef Q_QDOC
    template <typename PointerToMemberFunction>
    QSignalSpy(const QObject *object, PointerToMemberFunction signal);
#else
    template <typename Func>
    QSignalSpy(const typename QtPrivate::FunctionPointer<Func>::Object *obj, Func signal0)
        : QSignalSpy(verify(obj, QMetaMethod::fromSignal(signal0))) {}
#endif // Q_QDOC

    QSignalSpy(const QObject *obj, QMetaMethod signal)
        : QSignalSpy(verify(obj, signal)) {}

private:
    static ObjectSignal verify(const QObject *obj, QMetaMethod signal)
    {
        if (isObjectValid(obj) && isSignalMetaMethodValid(signal))
            return {obj, signal};
        else
            return {};
    }

public:
    inline bool isValid() const { return !sig.isEmpty(); }
    inline QByteArray signal() const { return sig; }

    bool wait(int timeout)
    { return wait(std::chrono::milliseconds{timeout}); }

    bool wait(std::chrono::milliseconds timeout = std::chrono::seconds{5})
    {
        QMutexLocker locker(&m_mutex);
        Q_ASSERT(!m_waiting);
        const qsizetype origCount = size();
        m_waiting = true;
        locker.unlock();

        m_loop.enterLoop(timeout);

        locker.relock();
        m_waiting = false;
        return size() > origCount;
    }

    int qt_metacall(QMetaObject::Call call, int methodId, void **a) override
    {
        methodId = QObject::qt_metacall(call, methodId, a);
        if (methodId < 0)
            return methodId;

        if (call == QMetaObject::InvokeMetaMethod) {
            if (methodId == 0) {
                appendArgs(a);
            }
            --methodId;
        }
        return methodId;
    }

private:
    explicit QSignalSpy(ObjectSignal os)
    {
        if (!os.obj)
            return;
        initArgs(os.sig, os.obj);
        if (!connectToSignal(os.obj, os.sig.methodIndex()))
            return;

        sig = os.sig.methodSignature();
    }

    bool connectToSignal(const QObject *sender, int sigIndex)
    {
        static const int memberOffset = QObject::staticMetaObject.methodCount();
        const bool connected = QMetaObject::connect(
            sender, sigIndex, this, memberOffset, Qt::DirectConnection, nullptr);

        if (!connected)
            qWarning("QSignalSpy: QMetaObject::connect returned false. Unable to connect.");

        return connected;
    }

    static bool isSignalMetaMethodValid(const QMetaMethod &signal)
    {
        if (!signal.isValid()) {
            qWarning("QSignalSpy: Null signal is not valid");
            return false;
        }

        if (signal.methodType() != QMetaMethod::Signal) {
            qWarning("QSignalSpy: Not a signal: '%s'", signal.methodSignature().constData());
            return false;
        }

        return true;
    }

    static bool isObjectValid(const QObject *object)
    {
        const bool valid = !!object;

        if (!valid)
            qWarning("QSignalSpy: Cannot spy on a null object");

        return valid;
    }

    void initArgs(const QMetaMethod &member, const QObject *obj)
    {
        QMutexLocker locker(&m_mutex);
        args.reserve(member.parameterCount());
        for (int i = 0; i < member.parameterCount(); ++i) {
            QMetaType tp = member.parameterMetaType(i);
            if (!tp.isValid() && obj) {
                locker.unlock();
                void *argv[] = { &tp, &i };
                QMetaObject::metacall(const_cast<QObject*>(obj),
                                      QMetaObject::RegisterMethodArgumentMetaType,
                                      member.methodIndex(), argv);
                locker.relock();
            }
            if (!tp.isValid()) {
                qWarning("QSignalSpy: Unable to handle parameter '%s' of type '%s' of method '%s',"
                         " use qRegisterMetaType to register it.",
                         member.parameterNames().at(i).constData(),
                         member.parameterTypes().at(i).constData(),
                         member.name().constData());
            }
            args << tp.id();
        }
    }

    void appendArgs(void **a)
    {
        QMutexLocker locker(&m_mutex);
        QList<QVariant> list;
        list.reserve(args.size());
        for (qsizetype i = 0; i < args.size(); ++i) {
            const QMetaType::Type type = static_cast<QMetaType::Type>(args.at(i));
            if (type == QMetaType::QVariant)
                list << *reinterpret_cast<QVariant *>(a[i + 1]);
            else
                list << QVariant(QMetaType(type), a[i + 1]);
        }
        append(std::move(list));

        if (m_waiting) {
            locker.unlock();
            m_loop.exitLoop();
        }
    }

    // the full, normalized signal name
    QByteArray sig;
    // holds the QMetaType types for the argument list of the signal
    QList<int> args;

    QTestEventLoop m_loop;
    bool m_waiting = false;
    QMutex m_mutex; // protects m_waiting, args and the QList base class, between appendArgs() and wait()
};

QT_END_NAMESPACE

#endif
