// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#include <QTest>
#include <QTestEventLoop>
#include <QSignalSpy>
#include <QSemaphore>
#include <QAbstractEventDispatcher>
#if defined(Q_OS_WIN32)
#include <QWinEventNotifier>
#endif

#include <qcoreapplication.h>
#include <qelapsedtimer.h>
#include <qmutex.h>
#include <qthread.h>
#include <qtimer.h>
#include <qwaitcondition.h>
#include <qdebug.h>
#include <qmetaobject.h>
#include <qscopeguard.h>
#include <private/qobject_p.h>
#include <private/qthread_p.h>

#ifdef Q_OS_UNIX
#include <pthread.h>
#endif
#if defined(Q_OS_WIN)
#include <qt_windows.h>
#if defined(Q_OS_WIN32)
#include <process.h>
#endif
#endif

#ifndef QT_NO_EXCEPTIONS
#include <exception>
#endif

#include <QtTest/private/qemulationdetector_p.h>

using namespace std::chrono_literals;

class tst_QThread : public QObject
{
    Q_OBJECT
private slots:
    void currentThreadId();
    void currentThread();
    void idealThreadCount();
    void isFinished();
    void isRunning();
    void setPriority();
    void setStackSize();
    void exit();
    void start();
    void terminate();
    void quit();
    void started();
    void finished();
    void terminated(); // Named after a signal that was removed in Qt 5.0
    void exec();
    void sleep();
    void msleep();
    void usleep();

    void nativeThreadAdoption();
    void adoptedThreadAffinity();
    void adoptedThreadSetPriority();
    void adoptedThreadExit();
    void adoptedThreadExec();
    void adoptedThreadFinished();
    void adoptedThreadExecFinished();
    void adoptMultipleThreads();
    void adoptMultipleThreadsOverlap();
    void adoptedThreadBindingStatus();

    void exitAndStart();
    void exitAndExec();

    void connectThreadFinishedSignalToObjectDeleteLaterSlot();
    void wait2();
    void wait3_slowDestructor();
    void destroyFinishRace();
    void startFinishRace();
    void startAndQuitCustomEventLoop();
    void isRunningInFinished();

    void customEventDispatcher();

    void requestTermination();

    void stressTest();

    void quitLock();

    void create();
    void createDestruction();
    void threadIdReuse();

    void terminateAndPrematureDestruction();
    void terminateAndDoubleDestruction();

    void bindingListCleanupAfterDelete();
};

enum { one_minute = 60 * 1000, five_minutes = 5 * one_minute };

template <class Int>
static QString msgElapsed(Int elapsed)
{
    return QString::fromLatin1("elapsed: %1").arg(elapsed);
}

class SignalRecorder : public QObject
{
    Q_OBJECT
public:
    QAtomicInt activationCount;

    inline SignalRecorder()
        : activationCount(0)
    { }

    bool wasActivated()
    { return activationCount.loadRelaxed() > 0; }

public slots:
    void slot();
};

void SignalRecorder::slot()
{ activationCount.ref(); }

class Current_Thread : public QThread
{
public:
    Qt::HANDLE id;
    QThread *thread;

    void run() override
    {
        id = QThread::currentThreadId();
        thread = QThread::currentThread();
    }
};

class Simple_Thread : public QThread
{
public:
    QMutex mutex;
    QWaitCondition cond;

    void run() override
    {
        QMutexLocker locker(&mutex);
        cond.wakeOne();
    }
};

class Exit_Object : public QObject
{
    Q_OBJECT
public:
    QThread *thread;
    int code;
public slots:
    void slot()
    { thread->exit(code); }
};

class Exit_Thread : public Simple_Thread
{
public:
    Exit_Object *object;
    int code;
    int result;

    void run() override
    {
        Simple_Thread::run();
        if (object) {
            object->thread = this;
            object->code = code;
            QTimer::singleShot(100, object, SLOT(slot()));
        }
        result = exec();
    }
};

class Terminate_Thread : public Simple_Thread
{
public:
    void run() override
    {
        setTerminationEnabled(false);
        {
            QMutexLocker locker(&mutex);
            cond.wakeOne();
            cond.wait(&mutex, five_minutes);
        }
        setTerminationEnabled(true);
        qFatal("tst_QThread: test case hung");
    }
};

class Quit_Object : public QObject
{
    Q_OBJECT
public:
    QThread *thread;
public slots:
    void slot()
    { thread->quit(); }
};

class Quit_Thread : public Simple_Thread
{
public:
    Quit_Object *object;
    int result;

    void run() override
    {
        Simple_Thread::run();
        if (object) {
            object->thread = this;
            QTimer::singleShot(100, object, SLOT(slot()));
        }
        result = exec();
    }
};

class Sleep_Thread : public Simple_Thread
{
public:
    enum SleepType { Second, Millisecond, Microsecond };

    SleepType sleepType;
    ulong interval;

    qint64 elapsed; // result, in *MILLISECONDS*

    void run() override
    {
        QMutexLocker locker(&mutex);

        elapsed = 0;
        QElapsedTimer timer;
        timer.start();
        std::chrono::nanoseconds dur{0};
        switch (sleepType) {
        case Second:
            dur = std::chrono::seconds{interval};
            break;
        case Millisecond:
            dur = std::chrono::milliseconds{interval};
            break;
        case Microsecond:
            dur = std::chrono::microseconds{interval};
            break;
        }
        sleep(dur);
        elapsed = timer.elapsed();

        cond.wakeOne();
    }
};

void tst_QThread::currentThreadId()
{
    Current_Thread thread;
    thread.id = nullptr;
    thread.thread = nullptr;
    thread.start();
    QVERIFY(thread.wait(five_minutes));
    QVERIFY(thread.id != nullptr);
    QVERIFY(thread.id != QThread::currentThreadId());
}

void tst_QThread::currentThread()
{
    QVERIFY(QThread::currentThread() != nullptr);
    QCOMPARE(QThread::currentThread(), thread());

    Current_Thread thread;
    thread.id = nullptr;
    thread.thread = nullptr;
    thread.start();
    QVERIFY(thread.wait(five_minutes));
    QCOMPARE(thread.thread, static_cast<QThread *>(&thread));
}

void tst_QThread::idealThreadCount()
{
    QVERIFY(QThread::idealThreadCount() > 0);
    qDebug() << "Ideal thread count:" << QThread::idealThreadCount();
}

void tst_QThread::isFinished()
{
    Simple_Thread thread;
    QVERIFY(!thread.isFinished());
    QMutexLocker locker(&thread.mutex);
    thread.start();
    QVERIFY(!thread.isFinished());
    thread.cond.wait(locker.mutex());
    QVERIFY(thread.wait(five_minutes));
    QVERIFY(thread.isFinished());
}

void tst_QThread::isRunning()
{
    Simple_Thread thread;
    QVERIFY(!thread.isRunning());
    QMutexLocker locker(&thread.mutex);
    thread.start();
    QVERIFY(thread.isRunning());
    thread.cond.wait(locker.mutex());
    QVERIFY(thread.wait(five_minutes));
    QVERIFY(!thread.isRunning());
}

void tst_QThread::setPriority()
{
    Simple_Thread thread;

    // cannot change the priority, since the thread is not running
    QCOMPARE(thread.priority(), QThread::InheritPriority);
    QTest::ignoreMessage(QtWarningMsg, "QThread::setPriority: Cannot set priority, thread is not running");
    thread.setPriority(QThread::IdlePriority);
    QCOMPARE(thread.priority(), QThread::InheritPriority);
    QTest::ignoreMessage(QtWarningMsg, "QThread::setPriority: Cannot set priority, thread is not running");
    thread.setPriority(QThread::LowestPriority);
    QCOMPARE(thread.priority(), QThread::InheritPriority);
    QTest::ignoreMessage(QtWarningMsg, "QThread::setPriority: Cannot set priority, thread is not running");
    thread.setPriority(QThread::LowPriority);
    QCOMPARE(thread.priority(), QThread::InheritPriority);
    QTest::ignoreMessage(QtWarningMsg, "QThread::setPriority: Cannot set priority, thread is not running");
    thread.setPriority(QThread::NormalPriority);
    QCOMPARE(thread.priority(), QThread::InheritPriority);
    QTest::ignoreMessage(QtWarningMsg, "QThread::setPriority: Cannot set priority, thread is not running");
    thread.setPriority(QThread::HighPriority);
    QCOMPARE(thread.priority(), QThread::InheritPriority);
    QTest::ignoreMessage(QtWarningMsg, "QThread::setPriority: Cannot set priority, thread is not running");
    thread.setPriority(QThread::HighestPriority);
    QCOMPARE(thread.priority(), QThread::InheritPriority);
    QTest::ignoreMessage(QtWarningMsg, "QThread::setPriority: Cannot set priority, thread is not running");
    thread.setPriority(QThread::TimeCriticalPriority);
    QCOMPARE(thread.priority(), QThread::InheritPriority);

    QCOMPARE(thread.priority(), QThread::InheritPriority);
    QMutexLocker locker(&thread.mutex);
    thread.start();

    // change the priority of a running thread
    QCOMPARE(thread.priority(), QThread::InheritPriority);
    thread.setPriority(QThread::IdlePriority);
    QCOMPARE(thread.priority(), QThread::IdlePriority);
    thread.setPriority(QThread::LowestPriority);
    QCOMPARE(thread.priority(), QThread::LowestPriority);
    thread.setPriority(QThread::LowPriority);
    QCOMPARE(thread.priority(), QThread::LowPriority);
    thread.setPriority(QThread::NormalPriority);
    QCOMPARE(thread.priority(), QThread::NormalPriority);
    thread.setPriority(QThread::HighPriority);
    QCOMPARE(thread.priority(), QThread::HighPriority);
    thread.setPriority(QThread::HighestPriority);
    QCOMPARE(thread.priority(), QThread::HighestPriority);
    thread.setPriority(QThread::TimeCriticalPriority);
    QCOMPARE(thread.priority(), QThread::TimeCriticalPriority);
    thread.cond.wait(locker.mutex());
    QVERIFY(thread.wait(five_minutes));

    QCOMPARE(thread.priority(), QThread::InheritPriority);
    QTest::ignoreMessage(QtWarningMsg, "QThread::setPriority: Cannot set priority, thread is not running");
    thread.setPriority(QThread::IdlePriority);
    QCOMPARE(thread.priority(), QThread::InheritPriority);
    QTest::ignoreMessage(QtWarningMsg, "QThread::setPriority: Cannot set priority, thread is not running");
    thread.setPriority(QThread::LowestPriority);
    QCOMPARE(thread.priority(), QThread::InheritPriority);
    QTest::ignoreMessage(QtWarningMsg, "QThread::setPriority: Cannot set priority, thread is not running");
    thread.setPriority(QThread::LowPriority);
    QCOMPARE(thread.priority(), QThread::InheritPriority);
    QTest::ignoreMessage(QtWarningMsg, "QThread::setPriority: Cannot set priority, thread is not running");
    thread.setPriority(QThread::NormalPriority);
    QCOMPARE(thread.priority(), QThread::InheritPriority);
    QTest::ignoreMessage(QtWarningMsg, "QThread::setPriority: Cannot set priority, thread is not running");
    thread.setPriority(QThread::HighPriority);
    QCOMPARE(thread.priority(), QThread::InheritPriority);
    QTest::ignoreMessage(QtWarningMsg, "QThread::setPriority: Cannot set priority, thread is not running");
    thread.setPriority(QThread::HighestPriority);
    QCOMPARE(thread.priority(), QThread::InheritPriority);
    QTest::ignoreMessage(QtWarningMsg, "QThread::setPriority: Cannot set priority, thread is not running");
    thread.setPriority(QThread::TimeCriticalPriority);
    QCOMPARE(thread.priority(), QThread::InheritPriority);
}

void tst_QThread::setStackSize()
{
    Simple_Thread thread;
    QCOMPARE(thread.stackSize(), 0u);
    thread.setStackSize(8192u);
    QCOMPARE(thread.stackSize(), 8192u);
    thread.setStackSize(0u);
    QCOMPARE(thread.stackSize(), 0u);
}

void tst_QThread::exit()
{
    Exit_Thread thread;
    thread.object = new Exit_Object;
    thread.object->moveToThread(&thread);
    thread.code = 42;
    thread.result = 0;
    QVERIFY(!thread.isFinished());
    QVERIFY(!thread.isRunning());
    QMutexLocker locker(&thread.mutex);
    thread.start();
    QVERIFY(thread.isRunning());
    QVERIFY(!thread.isFinished());
    thread.cond.wait(locker.mutex());
    QVERIFY(thread.wait(five_minutes));
    QVERIFY(thread.isFinished());
    QVERIFY(!thread.isRunning());
    QCOMPARE(thread.result, thread.code);
    delete thread.object;

    Exit_Thread thread2;
    thread2.object = nullptr;
    thread2.code = 53;
    thread2.result = 0;
    QMutexLocker locker2(&thread2.mutex);
    thread2.start();
    thread2.exit(thread2.code);
    thread2.cond.wait(locker2.mutex());
    QVERIFY(thread2.wait(five_minutes));
    QCOMPARE(thread2.result, thread2.code);
}

void tst_QThread::start()
{
    QThread::Priority priorities[] = {
        QThread::IdlePriority,
        QThread::LowestPriority,
        QThread::LowPriority,
        QThread::NormalPriority,
        QThread::HighPriority,
        QThread::HighestPriority,
        QThread::TimeCriticalPriority,
        QThread::InheritPriority
    };
    const int prio_count = sizeof(priorities) / sizeof(QThread::Priority);

    for (int i = 0; i < prio_count; ++i) {
        Simple_Thread thread;
        QVERIFY(!thread.isFinished());
        QVERIFY(!thread.isRunning());
        QMutexLocker locker(&thread.mutex);
        thread.start(priorities[i]);
        QVERIFY(thread.isRunning());
        QVERIFY(!thread.isFinished());
        thread.cond.wait(locker.mutex());
        QVERIFY(thread.wait(five_minutes));
        QVERIFY(thread.isFinished());
        QVERIFY(!thread.isRunning());
    }
}

void tst_QThread::terminate()
{
#if defined(Q_OS_ANDROID)
    QSKIP("Thread termination is not supported on Android.");
#endif
#if defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer)
    QSKIP("Thread termination might result in stack underflow address sanitizer errors.");
#endif

    Terminate_Thread thread;
    {
        QMutexLocker locker(&thread.mutex);
        thread.start();
        QVERIFY(thread.cond.wait(locker.mutex(), five_minutes));
        thread.terminate();
        thread.cond.wakeOne();
    }
    QVERIFY(thread.wait(five_minutes));
}

void tst_QThread::quit()
{
    Quit_Thread thread;
    thread.object = new Quit_Object;
    thread.object->moveToThread(&thread);
    thread.result = -1;
    QVERIFY(!thread.isFinished());
    QVERIFY(!thread.isRunning());
    QMutexLocker locker(&thread.mutex);
    thread.start();
    QVERIFY(thread.isRunning());
    QVERIFY(!thread.isFinished());
    thread.cond.wait(locker.mutex());
    QVERIFY(thread.wait(five_minutes));
    QVERIFY(thread.isFinished());
    QVERIFY(!thread.isRunning());
    QCOMPARE(thread.result, 0);
    delete thread.object;

    Quit_Thread thread2;
    thread2.object = nullptr;
    thread2.result = -1;
    QMutexLocker locker2(&thread2.mutex);
    thread2.start();
    thread2.quit();
    thread2.cond.wait(locker2.mutex());
    QVERIFY(thread2.wait(five_minutes));
    QCOMPARE(thread2.result, 0);
}

void tst_QThread::started()
{
    SignalRecorder recorder;
    Simple_Thread thread;
    connect(&thread, SIGNAL(started()), &recorder, SLOT(slot()), Qt::DirectConnection);
    thread.start();
    QVERIFY(thread.wait(five_minutes));
    QVERIFY(recorder.wasActivated());
}

void tst_QThread::finished()
{
    SignalRecorder recorder;
    Simple_Thread thread;
    connect(&thread, SIGNAL(finished()), &recorder, SLOT(slot()), Qt::DirectConnection);
    thread.start();
    QVERIFY(thread.wait(five_minutes));
    QVERIFY(recorder.wasActivated());
}

void tst_QThread::terminated()
{
#if defined(Q_OS_ANDROID)
    QSKIP("Thread termination is not supported on Android.");
#endif
#if defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer)
    QSKIP("Thread termination might result in stack underflow address sanitizer errors.");
#endif

    SignalRecorder recorder;
    Terminate_Thread thread;
    connect(&thread, SIGNAL(finished()), &recorder, SLOT(slot()), Qt::DirectConnection);
    {
        QMutexLocker locker(&thread.mutex);
        thread.start();
        thread.cond.wait(locker.mutex());
        thread.terminate();
        thread.cond.wakeOne();
    }
    QVERIFY(thread.wait(five_minutes));
    QVERIFY(recorder.wasActivated());
}

void tst_QThread::exec()
{
    class MultipleExecThread : public QThread
    {
    public:
        int res1, res2;

        MultipleExecThread() : res1(-2), res2(-2) { }

        void run() override
        {
            {
                Exit_Object o;
                o.thread = this;
                o.code = 1;
                QTimer::singleShot(100, &o, SLOT(slot()));
                res1 = exec();
            }
            {
                Exit_Object o;
                o.thread = this;
                o.code = 2;
                QTimer::singleShot(100, &o, SLOT(slot()));
                res2 = exec();
            }
        }
    };

    MultipleExecThread thread;
    thread.start();
    QVERIFY(thread.wait());

    QCOMPARE(thread.res1, 1);
    QCOMPARE(thread.res2, 2);
}

void tst_QThread::sleep()
{
    Sleep_Thread thread;
    thread.sleepType = Sleep_Thread::Second;
    thread.interval = 2;
    thread.start();
    QVERIFY(thread.wait(five_minutes));
    QVERIFY2(thread.elapsed >= 2000, qPrintable(msgElapsed(thread.elapsed)));
}

void tst_QThread::msleep()
{
    Sleep_Thread thread;
    thread.sleepType = Sleep_Thread::Millisecond;
    thread.interval = 120;
    thread.start();
    QVERIFY(thread.wait(five_minutes));
#if defined (Q_OS_WIN) // May no longer be needed
    QVERIFY2(thread.elapsed >= 100, qPrintable(msgElapsed(thread.elapsed)));
#else
    QVERIFY2(thread.elapsed >= 120, qPrintable(msgElapsed(thread.elapsed)));
#endif
}

void tst_QThread::usleep()
{
    Sleep_Thread thread;
    thread.sleepType = Sleep_Thread::Microsecond;
    thread.interval = 120000;
    thread.start();
    QVERIFY(thread.wait(five_minutes));
#if defined (Q_OS_WIN) // May no longer be needed
    QVERIFY2(thread.elapsed >= 100, qPrintable(msgElapsed(thread.elapsed)));
#else
    QVERIFY2(thread.elapsed >= 120, qPrintable(msgElapsed(thread.elapsed)));
#endif
}

typedef void (*FunctionPointer)(void *);
void noop(void*) { }

#if defined Q_OS_UNIX
    typedef pthread_t ThreadHandle;
#elif defined Q_OS_WIN
    typedef HANDLE ThreadHandle;
#endif

#ifdef Q_OS_WIN
#define WIN_FIX_STDCALL __stdcall
#else
#define WIN_FIX_STDCALL
#endif

class NativeThreadWrapper
{
public:
    NativeThreadWrapper() : qthread(nullptr), waitForStop(false) {}
    void start(FunctionPointer functionPointer = noop, void *data = nullptr);
    void startAndWait(FunctionPointer functionPointer = noop, void *data = nullptr);
    void join();
    void setWaitForStop() { waitForStop = true; }
    void stop();

    ThreadHandle nativeThreadHandle;
    QThread *qthread;
    QWaitCondition startCondition;
    QMutex mutex;
    bool waitForStop;
    QWaitCondition stopCondition;
protected:
    static void *runUnix(void *data);
    static unsigned WIN_FIX_STDCALL runWin(void *data);

    FunctionPointer functionPointer;
    void *data;
};

void NativeThreadWrapper::start(FunctionPointer functionPointer, void *data)
{
    this->functionPointer = functionPointer;
    this->data = data;
#if defined Q_OS_UNIX
    const int state = pthread_create(&nativeThreadHandle, nullptr, NativeThreadWrapper::runUnix, this);
    Q_UNUSED(state)
#elif defined Q_OS_WIN
    unsigned thrdid = 0;
    nativeThreadHandle = (Qt::HANDLE) _beginthreadex(NULL, 0, NativeThreadWrapper::runWin, this, 0, &thrdid);
#endif
}

void NativeThreadWrapper::startAndWait(FunctionPointer functionPointer, void *data)
{
    QMutexLocker locker(&mutex);
    start(functionPointer, data);
    startCondition.wait(locker.mutex());
}

void NativeThreadWrapper::join()
{
#if defined Q_OS_UNIX
    pthread_join(nativeThreadHandle, nullptr);
#elif defined Q_OS_WIN
    WaitForSingleObjectEx(nativeThreadHandle, INFINITE, FALSE);
    CloseHandle(nativeThreadHandle);
#endif
}

void *NativeThreadWrapper::runUnix(void *that)
{
    NativeThreadWrapper *nativeThreadWrapper = reinterpret_cast<NativeThreadWrapper*>(that);

    // Adopt thread, create QThread object.
    nativeThreadWrapper->qthread = QThread::currentThread();

    // Release main thread.
    {
        QMutexLocker lock(&nativeThreadWrapper->mutex);
        nativeThreadWrapper->startCondition.wakeOne();
    }

    // Run function.
    nativeThreadWrapper->functionPointer(nativeThreadWrapper->data);

    // Wait for stop.
    {
        QMutexLocker lock(&nativeThreadWrapper->mutex);
        if (nativeThreadWrapper->waitForStop)
            nativeThreadWrapper->stopCondition.wait(lock.mutex());
    }

    return nullptr;
}

unsigned WIN_FIX_STDCALL NativeThreadWrapper::runWin(void *data)
{
    runUnix(data);
    return 0;
}

void NativeThreadWrapper::stop()
{
    QMutexLocker lock(&mutex);
    waitForStop = false;
    stopCondition.wakeOne();
}

static bool threadAdoptedOk = false;
static QThread *mainThread;
void testNativeThreadAdoption(void *)
{
    threadAdoptedOk = (QThread::currentThreadId() != nullptr
                       && QThread::currentThread() != nullptr
                       && QThread::currentThread() != mainThread);
}
void tst_QThread::nativeThreadAdoption()
{
    threadAdoptedOk = false;
    mainThread = QThread::currentThread();
    NativeThreadWrapper nativeThread;
    nativeThread.setWaitForStop();
    nativeThread.startAndWait(testNativeThreadAdoption);
    QVERIFY(nativeThread.qthread);

    nativeThread.stop();
    nativeThread.join();

    QVERIFY(threadAdoptedOk);
}

void adoptedThreadAffinityFunction(void *arg)
{
    QThread **affinity = reinterpret_cast<QThread **>(arg);
    QThread *current = QThread::currentThread();
    affinity[0] = current;
    affinity[1] = current->thread();
}

void tst_QThread::adoptedThreadAffinity()
{
    QThread *affinity[2] = { nullptr, nullptr };

    NativeThreadWrapper thread;
    thread.startAndWait(adoptedThreadAffinityFunction, affinity);
    thread.join();

    // adopted thread (deleted) should have affinity to itself
    QCOMPARE(static_cast<const void *>(affinity[0]),
             static_cast<const void *>(affinity[1]));
}

void tst_QThread::adoptedThreadSetPriority()
{
    NativeThreadWrapper nativeThread;
    nativeThread.setWaitForStop();
    nativeThread.startAndWait();

    // change the priority of a running thread
    QCOMPARE(nativeThread.qthread->priority(), QThread::InheritPriority);
    nativeThread.qthread->setPriority(QThread::IdlePriority);
    QCOMPARE(nativeThread.qthread->priority(), QThread::IdlePriority);
    nativeThread.qthread->setPriority(QThread::LowestPriority);
    QCOMPARE(nativeThread.qthread->priority(), QThread::LowestPriority);
    nativeThread.qthread->setPriority(QThread::LowPriority);
    QCOMPARE(nativeThread.qthread->priority(), QThread::LowPriority);
    nativeThread.qthread->setPriority(QThread::NormalPriority);
    QCOMPARE(nativeThread.qthread->priority(), QThread::NormalPriority);
    nativeThread.qthread->setPriority(QThread::HighPriority);
    QCOMPARE(nativeThread.qthread->priority(), QThread::HighPriority);
    nativeThread.qthread->setPriority(QThread::HighestPriority);
    QCOMPARE(nativeThread.qthread->priority(), QThread::HighestPriority);
    nativeThread.qthread->setPriority(QThread::TimeCriticalPriority);
    QCOMPARE(nativeThread.qthread->priority(), QThread::TimeCriticalPriority);

    nativeThread.stop();
    nativeThread.join();
}

void tst_QThread::adoptedThreadExit()
{
    NativeThreadWrapper nativeThread;
    nativeThread.setWaitForStop();

    nativeThread.startAndWait();
    QVERIFY(nativeThread.qthread);
    QVERIFY(nativeThread.qthread->isRunning());
    QVERIFY(!nativeThread.qthread->isFinished());

    nativeThread.stop();
    nativeThread.join();
}

void adoptedThreadExecFunction(void *)
{
    QThread  * const adoptedThread = QThread::currentThread();
    QEventLoop eventLoop(adoptedThread);

    const int code = 1;
    Exit_Object o;
    o.thread = adoptedThread;
    o.code = code;
    QTimer::singleShot(100, &o, SLOT(slot()));

    const int result = eventLoop.exec();
    QCOMPARE(result, code);
}

void tst_QThread::adoptedThreadExec()
{
    NativeThreadWrapper nativeThread;
    nativeThread.start(adoptedThreadExecFunction);
    nativeThread.join();
}

/*
    Test that you get the finished signal when an adopted thread exits.
*/
void tst_QThread::adoptedThreadFinished()
{
    NativeThreadWrapper nativeThread;
    nativeThread.setWaitForStop();
    nativeThread.startAndWait();

    QObject::connect(nativeThread.qthread, SIGNAL(finished()), &QTestEventLoop::instance(), SLOT(exitLoop()));

    nativeThread.stop();
    nativeThread.join();

    QTestEventLoop::instance().enterLoop(5);
    QVERIFY(!QTestEventLoop::instance().timeout());
}

void tst_QThread::adoptedThreadExecFinished()
{
    NativeThreadWrapper nativeThread;
    nativeThread.setWaitForStop();
    nativeThread.startAndWait(adoptedThreadExecFunction);

    QObject::connect(nativeThread.qthread, SIGNAL(finished()), &QTestEventLoop::instance(), SLOT(exitLoop()));

    nativeThread.stop();
    nativeThread.join();

    QTestEventLoop::instance().enterLoop(5);
    QVERIFY(!QTestEventLoop::instance().timeout());
}

void tst_QThread::adoptMultipleThreads()
{
#if defined(Q_OS_WIN)
    // need to test lots of threads, so that we exceed MAXIMUM_WAIT_OBJECTS in qt_adopted_thread_watcher()
    const int numThreads = 200;
#else
    const int numThreads = 5;
#endif
    QList<NativeThreadWrapper*> nativeThreads;

    SignalRecorder recorder;

    for (int i = 0; i < numThreads; ++i) {
        nativeThreads.append(new NativeThreadWrapper());
        nativeThreads.at(i)->setWaitForStop();
        nativeThreads.at(i)->startAndWait();
        QObject::connect(nativeThreads.at(i)->qthread, SIGNAL(finished()), &recorder, SLOT(slot()));
    }

    QObject::connect(nativeThreads.at(numThreads - 1)->qthread, SIGNAL(finished()), &QTestEventLoop::instance(), SLOT(exitLoop()));

    for (int i = 0; i < numThreads; ++i) {
        nativeThreads.at(i)->stop();
        nativeThreads.at(i)->join();
        delete nativeThreads.at(i);
    }

    QTestEventLoop::instance().enterLoop(5);
    QVERIFY(!QTestEventLoop::instance().timeout());
    QCOMPARE(recorder.activationCount.loadRelaxed(), numThreads);
}

void tst_QThread::adoptMultipleThreadsOverlap()
{
#if defined(Q_OS_WIN)
    // need to test lots of threads, so that we exceed MAXIMUM_WAIT_OBJECTS in qt_adopted_thread_watcher()
    const int numThreads = 200;
#else
    const int numThreads = 5;
#endif
    QList<NativeThreadWrapper*> nativeThreads;

    SignalRecorder recorder;

    for (int i = 0; i < numThreads; ++i) {
        nativeThreads.append(new NativeThreadWrapper());
        nativeThreads.at(i)->setWaitForStop();
        nativeThreads.at(i)->mutex.lock();
        nativeThreads.at(i)->start();
    }
    for (int i = 0; i < numThreads; ++i) {
        nativeThreads.at(i)->startCondition.wait(&nativeThreads.at(i)->mutex);
        QObject::connect(nativeThreads.at(i)->qthread, SIGNAL(finished()), &recorder, SLOT(slot()));
        nativeThreads.at(i)->mutex.unlock();
    }

    QObject::connect(nativeThreads.at(numThreads - 1)->qthread, SIGNAL(finished()), &QTestEventLoop::instance(), SLOT(exitLoop()));

    for (int i = 0; i < numThreads; ++i) {
        nativeThreads.at(i)->stop();
        nativeThreads.at(i)->join();
        delete nativeThreads.at(i);
    }

    QTestEventLoop::instance().enterLoop(5);
    QVERIFY(!QTestEventLoop::instance().timeout());
    QCOMPARE(recorder.activationCount.loadRelaxed(), numThreads);
}

void tst_QThread::adoptedThreadBindingStatus()
{
    NativeThreadWrapper nativeThread;
    nativeThread.setWaitForStop();

    nativeThread.startAndWait();
    QVERIFY(nativeThread.qthread);
    auto privThread = static_cast<QThreadPrivate *>(QObjectPrivate::get(nativeThread.qthread));
    QVERIFY(privThread->m_statusOrPendingObjects.bindingStatus());

    nativeThread.stop();
    nativeThread.join();
}

// Disconnects on WinCE
void tst_QThread::stressTest()
{
    if (QTestPrivate::isRunningArmOnX86())
        QSKIP("Qemu uses too much memory for each thread. Test would run out of memory.");

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < one_minute) {
        Current_Thread t;
        t.start();
        t.wait(one_minute);
    }
}

class Syncronizer : public QObject
{ Q_OBJECT
public slots:
    void setProp(int p) {
        if(m_prop != p) {
            m_prop = p;
            emit propChanged(p);
        }
    }
signals:
    void propChanged(int);
public:
    Syncronizer() : m_prop(42) {}
    int m_prop;
};

void tst_QThread::exitAndStart()
{
    QThread thread;
    thread.exit(555); //should do nothing

    thread.start();

    //test that the thread is running by executing queued connected signal there
    Syncronizer sync1;
    sync1.moveToThread(&thread);
    Syncronizer sync2;
    sync2.moveToThread(&thread);
    connect(&sync2, SIGNAL(propChanged(int)), &sync1, SLOT(setProp(int)), Qt::QueuedConnection);
    connect(&sync1, SIGNAL(propChanged(int)), &thread, SLOT(quit()), Qt::QueuedConnection);
    QMetaObject::invokeMethod(&sync2, "setProp", Qt::QueuedConnection , Q_ARG(int, 89));
    QTRY_VERIFY(thread.wait(10));
    QCOMPARE(sync2.m_prop, 89);
    QCOMPARE(sync1.m_prop, 89);
}

void tst_QThread::exitAndExec()
{
    class Thread : public QThread {
    public:
        QSemaphore sem1;
        QSemaphore sem2;
        volatile int value;
        void run() override
        {
            sem1.acquire();
            value = exec();  //First entrence
            sem2.release();
            value = exec(); // Second loop
        }
    };
    Thread thread;
    thread.value = 0;
    thread.start();
    thread.exit(556);
    thread.sem1.release(); //should exit the first loop
    thread.sem2.acquire();
    int v = thread.value;
    QCOMPARE(v, 556);

    //test that the thread is running by executing queued connected signal there
    Syncronizer sync1;
    sync1.moveToThread(&thread);
    Syncronizer sync2;
    sync2.moveToThread(&thread);
    connect(&sync2, SIGNAL(propChanged(int)), &sync1, SLOT(setProp(int)), Qt::QueuedConnection);
    connect(&sync1, SIGNAL(propChanged(int)), &thread, SLOT(quit()), Qt::QueuedConnection);
    QMetaObject::invokeMethod(&sync2, "setProp", Qt::QueuedConnection , Q_ARG(int, 89));
    QTRY_VERIFY(thread.wait(10));
    QCOMPARE(sync2.m_prop, 89);
    QCOMPARE(sync1.m_prop, 89);
}

void tst_QThread::connectThreadFinishedSignalToObjectDeleteLaterSlot()
{
    QThread thread;
    QObject *object = new QObject;
    QPointer<QObject> p = object;
    QVERIFY(!p.isNull());
    connect(&thread, SIGNAL(started()), &thread, SLOT(quit()), Qt::DirectConnection);
    connect(&thread, SIGNAL(finished()), object, SLOT(deleteLater()));
    object->moveToThread(&thread);
    thread.start();
    QVERIFY(thread.wait(30000));
    QVERIFY(p.isNull());
}

class Waiting_Thread : public QThread
{
public:
    enum { WaitTime = 800 };
    QMutex mutex;
    QWaitCondition cond1;
    QWaitCondition cond2;

    void run() override
    {
        QMutexLocker locker(&mutex);
        cond1.wait(&mutex);
        cond2.wait(&mutex, WaitTime);
    }
};

void tst_QThread::wait2()
{
    QElapsedTimer timer;
    Waiting_Thread thread;
    thread.start();
    timer.start();
    QVERIFY(!thread.wait(Waiting_Thread::WaitTime));
    qint64 elapsed = timer.elapsed(); // On Windows, we sometimes get (WaitTime - 9).
    QVERIFY2(elapsed >= Waiting_Thread::WaitTime - 10,
             qPrintable(msgElapsed(elapsed)));

    timer.start();
    thread.cond1.wakeOne();
    QVERIFY(thread.wait(/*Waiting_Thread::WaitTime * 1.4*/));
    elapsed = timer.elapsed();
    QVERIFY2(elapsed - Waiting_Thread::WaitTime >= -1,
             qPrintable(msgElapsed(elapsed)));
}

class SlowSlotObject : public QObject
{
    Q_OBJECT
public:
    QMutex mutex;
    QWaitCondition cond;
public slots:
    void slowSlot() {
        QMutexLocker locker(&mutex);
        cond.wait(&mutex);
    }
};

void tst_QThread::wait3_slowDestructor()
{
    SlowSlotObject slow;
    QThread thread;
    QObject::connect(&thread, &QThread::finished,
                     &slow, &SlowSlotObject::slowSlot, Qt::DirectConnection);
    QElapsedTimer timer;

    thread.start();
    thread.quit();
    // Calling quit() will cause the thread to finish and enter the blocking slowSlot().

    timer.start();
    {
        // Ensure thread finishes quickly after the checks - regardless of success:
        QScopeGuard wakeSlow([&slow]() -> void { slow.cond.wakeOne(); });
        QVERIFY(!thread.wait(Waiting_Thread::WaitTime));
        const qint64 elapsed = timer.elapsed();
        QVERIFY2(elapsed >= Waiting_Thread::WaitTime - 1,
                 qPrintable(QString::fromLatin1("elapsed: %1").arg(elapsed)));
    }
    QVERIFY(thread.wait(one_minute));
}

void tst_QThread::destroyFinishRace()
{
    class Thread : public QThread { void run() override {} };
    for (int i = 0; i < 15; i++) {
        Thread *thr = new Thread;
        connect(thr, SIGNAL(finished()), thr, SLOT(deleteLater()));
        QPointer<QThread> weak(static_cast<QThread*>(thr));
        thr->start();
        while (weak) {
            qApp->processEvents();
            qApp->processEvents();
            qApp->processEvents();
            qApp->processEvents();
        }
    }
}

void tst_QThread::startFinishRace()
{
    class Thread : public QThread {
    public:
        Thread() : i (50) {}
        void run() override
        {
            i--;
            if (!i) disconnect(this, SIGNAL(finished()), nullptr, nullptr);
        }
        int i;
    };
    for (int i = 0; i < 15; i++) {
        Thread thr;
        connect(&thr, SIGNAL(finished()), &thr, SLOT(start()));
        thr.start();
        while (!thr.isFinished() || thr.i != 0) {
            qApp->processEvents();
            qApp->processEvents();
            qApp->processEvents();
            qApp->processEvents();
        }
        QCOMPARE(thr.i, 0);
    }
}

void tst_QThread::startAndQuitCustomEventLoop()
{
    struct Thread : QThread {
        void run() override { QEventLoop().exec(); }
    };

   for (int i = 0; i < 5; i++) {
       Thread t;
       t.start();
       t.quit();
       t.wait();
   }
}

class FinishedTestObject : public QObject {
    Q_OBJECT
public:
    FinishedTestObject() : ok(false) {}
    bool ok;
public slots:
    void slotFinished() {
        QThread *t = qobject_cast<QThread *>(sender());
        ok = t && t->isFinished() && !t->isRunning();
    }
};

void tst_QThread::isRunningInFinished()
{
    for (int i = 0; i < 15; i++) {
        QThread thread;
        thread.start();
        FinishedTestObject localObject;
        FinishedTestObject inThreadObject;
        localObject.setObjectName("...");
        inThreadObject.moveToThread(&thread);
        connect(&thread, SIGNAL(finished()), &localObject, SLOT(slotFinished()));
        connect(&thread, SIGNAL(finished()), &inThreadObject, SLOT(slotFinished()));
        QEventLoop loop;
        connect(&thread, SIGNAL(finished()), &loop, SLOT(quit()));
        QMetaObject::invokeMethod(&thread, "quit", Qt::QueuedConnection);
        loop.exec();
        QVERIFY(!thread.isRunning());
        QVERIFY(thread.isFinished());
        QVERIFY(localObject.ok);
        QVERIFY(inThreadObject.ok);
    }
}

class DummyEventDispatcher : public QAbstractEventDispatcherV2
{
    Q_OBJECT
public:
    bool processEvents(QEventLoop::ProcessEventsFlags) override {
        visited.storeRelaxed(true);
        emit awake();
        QCoreApplication::sendPostedEvents();
        return false;
    }
    void registerSocketNotifier(QSocketNotifier *) override {}
    void unregisterSocketNotifier(QSocketNotifier *) override {}
    void registerTimer(Qt::TimerId, Duration, Qt::TimerType, QObject *) override {}
    bool unregisterTimer(Qt::TimerId) override { return false; }
    bool unregisterTimers(QObject *) override { return false; }
    QList<TimerInfoV2> timersForObject(QObject *) const override { return {}; }
    Duration remainingTime(Qt::TimerId) const override { return 0s; }
    void wakeUp() override {}
    void interrupt() override {}

#ifdef Q_OS_WIN
    bool registerEventNotifier(QWinEventNotifier *) { return false; }
    void unregisterEventNotifier(QWinEventNotifier *) { }
#endif

    QBasicAtomicInt visited; // bool
};

class ThreadObj : public QObject
{
    Q_OBJECT
public slots:
    void visit() {
        emit visited();
    }
signals:
    void visited();
};

void tst_QThread::customEventDispatcher()
{
    QThread thr;
    // there should be no ED yet
    QVERIFY(!thr.eventDispatcher());
    DummyEventDispatcher *ed = new DummyEventDispatcher;
    thr.setEventDispatcher(ed);
    // the new ED should be set
    QCOMPARE(thr.eventDispatcher(), ed);
    // test the alternative API of QAbstractEventDispatcher
    QCOMPARE(QAbstractEventDispatcher::instance(&thr), ed);
    thr.start();
    // start() should not overwrite the ED
    QCOMPARE(thr.eventDispatcher(), ed);

    ThreadObj obj;
    obj.moveToThread(&thr);
    // move was successful?
    QCOMPARE(obj.thread(), &thr);
    QEventLoop loop;
    connect(&obj, SIGNAL(visited()), &loop, SLOT(quit()), Qt::QueuedConnection);
    QMetaObject::invokeMethod(&obj, "visit", Qt::QueuedConnection);
    loop.exec();
    // test that the ED has really been used
    QVERIFY(ed->visited.loadRelaxed());

    QPointer<DummyEventDispatcher> weak_ed(ed);
    QVERIFY(!weak_ed.isNull());
    thr.quit();
    // wait for thread to be stopped
    QVERIFY(thr.wait(30000));
    // test that ED has been deleted
    QVERIFY(weak_ed.isNull());
}

class Job : public QObject
{
    Q_OBJECT
public:
    Job(QThread *thread, int deleteDelay, bool *flag, QObject *parent = nullptr)
      : QObject(parent), quitLocker(thread), exitThreadCalled(*flag)
    {
        exitThreadCalled = false;
        moveToThread(thread);
        QTimer::singleShot(deleteDelay, this, SLOT(deleteLater()));
        QTimer::singleShot(1000, this, SLOT(exitThread()));
    }

private slots:
    void exitThread()
    {
        exitThreadCalled = true;
        thread()->exit(1);
    }

private:
    QEventLoopLocker quitLocker;
public:
    bool &exitThreadCalled;
};

void tst_QThread::quitLock()
{
    QThread thread;
    bool exitThreadCalled;

    QEventLoop loop;
    connect(&thread, SIGNAL(finished()), &loop, SLOT(quit()));

    Job *job;

    thread.start();
    job = new Job(&thread, 500, &exitThreadCalled);
    QCOMPARE(job->thread(), &thread);
    loop.exec();
    QVERIFY(!exitThreadCalled);

    thread.start();
    job = new Job(&thread, 2500, &exitThreadCalled);
    QCOMPARE(job->thread(), &thread);
    loop.exec();
    QVERIFY(exitThreadCalled);

    delete job;
}

void tst_QThread::create()
{
    {
        const auto &function = [](){};
        QScopedPointer<QThread> thread(QThread::create(function));
        QVERIFY(thread);
        QVERIFY(!thread->isRunning());
        thread->start();
        QVERIFY(thread->wait());
    }

    {
        // no side effects before starting
        int i = 0;
        const auto &function = [&i]() { i = 42; };
        QScopedPointer<QThread> thread(QThread::create(function));
        QVERIFY(thread);
        QVERIFY(!thread->isRunning());
        QCOMPARE(i, 0);
        thread->start();
        QVERIFY(thread->wait());
        QCOMPARE(i, 42);
    }

    {
        // control thread progress
        QSemaphore semaphore1;
        QSemaphore semaphore2;

        const auto &function = [&semaphore1, &semaphore2]() -> void
        {
            semaphore1.acquire();
            semaphore2.release();
        };

        QScopedPointer<QThread> thread(QThread::create(function));

        QVERIFY(thread);
        thread->start();
        QTRY_VERIFY(thread->isRunning());
        semaphore1.release();
        semaphore2.acquire();
        QVERIFY(thread->wait());
        QVERIFY(!thread->isRunning());
    }

    {
        // ignore return values
        const auto &function = []() { return 42; };
        QScopedPointer<QThread> thread(QThread::create(function));
        QVERIFY(thread);
        QVERIFY(!thread->isRunning());
        thread->start();
        QVERIFY(thread->wait());
    }

    {
        // return value of create
        QScopedPointer<QThread> thread;
        QSemaphore s;
        const auto &function = [&thread, &s]() -> void
        {
            s.acquire();
            QCOMPARE(thread.data(), QThread::currentThread());
        };

        thread.reset(QThread::create(function));
        QVERIFY(thread);
        thread->start();
        QTRY_VERIFY(thread->isRunning());
        s.release();
        QVERIFY(thread->wait());
    }

    {
        // move-only parameters
        struct MoveOnlyValue {
            explicit MoveOnlyValue(int v) : v(v) {}
            ~MoveOnlyValue() = default;
            MoveOnlyValue(const MoveOnlyValue &) = delete;
            MoveOnlyValue(MoveOnlyValue &&) = default;
            MoveOnlyValue &operator=(const MoveOnlyValue &) = delete;
            MoveOnlyValue &operator=(MoveOnlyValue &&) = default;
            int v;
        };

        struct MoveOnlyFunctor {
            explicit MoveOnlyFunctor(int *i) : i(i) {}
            ~MoveOnlyFunctor() = default;
            MoveOnlyFunctor(const MoveOnlyFunctor &) = delete;
            MoveOnlyFunctor(MoveOnlyFunctor &&) = default;
            MoveOnlyFunctor &operator=(const MoveOnlyFunctor &) = delete;
            MoveOnlyFunctor &operator=(MoveOnlyFunctor &&) = default;
            int operator()() { return (*i = 42); }
            int *i;
        };

        {
            int i = 0;
            MoveOnlyFunctor f(&i);
            QScopedPointer<QThread> thread(QThread::create(std::move(f)));
            QVERIFY(thread);
            QVERIFY(!thread->isRunning());
            thread->start();
            QVERIFY(thread->wait());
            QCOMPARE(i, 42);
        }

        {
            int i = 0;
            MoveOnlyValue mo(123);
            auto moveOnlyFunction = [&i, mo = std::move(mo)]() { i = mo.v; };
            QScopedPointer<QThread> thread(QThread::create(std::move(moveOnlyFunction)));
            QVERIFY(thread);
            QVERIFY(!thread->isRunning());
            thread->start();
            QVERIFY(thread->wait());
            QCOMPARE(i, 123);
        }

        {
            int i = 0;
            const auto &function = [&i](MoveOnlyValue &&mo) { i = mo.v; };
            QScopedPointer<QThread> thread(QThread::create(function, MoveOnlyValue(123)));
            QVERIFY(thread);
            QVERIFY(!thread->isRunning());
            thread->start();
            QVERIFY(thread->wait());
            QCOMPARE(i, 123);
        }

        {
            int i = 0;
            const auto &function = [&i](MoveOnlyValue &&mo) { i = mo.v; };
            MoveOnlyValue mo(-1);
            QScopedPointer<QThread> thread(QThread::create(function, std::move(mo)));
            QVERIFY(thread);
            QVERIFY(!thread->isRunning());
            thread->start();
            QVERIFY(thread->wait());
            QCOMPARE(i, -1);
        }
    }

    {
        // simple parameter passing
        int i = 0;
        const auto &function = [&i](int j, int k) { i = j * k; };
        QScopedPointer<QThread> thread(QThread::create(function, 3, 4));
        QVERIFY(thread);
        QVERIFY(!thread->isRunning());
        QCOMPARE(i, 0);
        thread->start();
        QVERIFY(thread->wait());
        QCOMPARE(i, 12);
    }

    {
        // ignore return values (with parameters)
        const auto &function = [](double d) { return d * 2.0; };
        QScopedPointer<QThread> thread(QThread::create(function, 3.14));
        QVERIFY(thread);
        QVERIFY(!thread->isRunning());
        thread->start();
        QVERIFY(thread->wait());
    }

    {
        // handling of pointers to member functions, std::ref, etc.
        struct S {
            S() : v(0) {}
            void doSomething() { ++v; }
            int v;
        };

        S object;

        QCOMPARE(object.v, 0);

        QScopedPointer<QThread> thread;
        thread.reset(QThread::create(&S::doSomething, object));
        QVERIFY(thread);
        QVERIFY(!thread->isRunning());
        thread->start();
        QVERIFY(thread->wait());

        QCOMPARE(object.v, 0); // a copy was passed, this should still be 0

        thread.reset(QThread::create(&S::doSomething, std::ref(object)));
        QVERIFY(thread);
        QVERIFY(!thread->isRunning());
        thread->start();
        QVERIFY(thread->wait());

        QCOMPARE(object.v, 1);

        thread.reset(QThread::create(&S::doSomething, &object));
        QVERIFY(thread);
        QVERIFY(!thread->isRunning());
        thread->start();
        QVERIFY(thread->wait());

        QCOMPARE(object.v, 2);
    }

    {
        // std::ref into ordinary reference
        int i = 42;
        const auto &function = [](int &i) { i *= 2; };
        QScopedPointer<QThread> thread(QThread::create(function, std::ref(i)));
        QVERIFY(thread);
        thread->start();
        QVERIFY(thread->wait());
        QCOMPARE(i, 84);
    }

#ifndef QT_NO_EXCEPTIONS
    {
        // exceptions when copying/decaying the arguments are thrown at build side and won't terminate
        class ThreadException : public std::exception
        {
        };

        struct ThrowWhenCopying
        {
            ThrowWhenCopying() = default;
            ThrowWhenCopying(const ThrowWhenCopying &)
            {
                throw ThreadException();
            }
            ~ThrowWhenCopying() = default;
            ThrowWhenCopying &operator=(const ThrowWhenCopying &) = default;
        };

        const auto &function = [](const ThrowWhenCopying &){};
        QScopedPointer<QThread> thread;
        ThrowWhenCopying t;
        QVERIFY_THROWS_EXCEPTION(ThreadException, thread.reset(QThread::create(function, t)));
        QVERIFY(!thread);
    }
#endif // QT_NO_EXCEPTIONS
}

void tst_QThread::createDestruction()
{
    for (int delay : {0, 10, 20}) {
        auto checkForInterruptions = []() {
            for (;;) {
                if (QThread::currentThread()->isInterruptionRequested())
                    return;
                QThread::sleep(1ms);
            }
        };

        QScopedPointer<QThread> thread(QThread::create(checkForInterruptions));
        QSignalSpy finishedSpy(thread.get(), &QThread::finished);
        QVERIFY(finishedSpy.isValid());

        thread->start();
        if (delay)
            QThread::msleep(delay);
        thread.reset();

        QCOMPARE(finishedSpy.size(), 1);
    }

    for (int delay : {0, 10, 20}) {
        auto runEventLoop = []() {
            QEventLoop loop;
            loop.exec();
        };

        QScopedPointer<QThread> thread(QThread::create(runEventLoop));
        QSignalSpy finishedSpy(thread.get(), &QThread::finished);
        QVERIFY(finishedSpy.isValid());

        thread->start();
        if (delay)
            QThread::msleep(delay);
        thread.reset();

        QCOMPARE(finishedSpy.size(), 1);
    }

    for (int delay : {0, 10, 20}) {
        auto runEventLoop = [delay]() {
            if (delay)
                QThread::msleep(delay);
            QEventLoop loop;
            loop.exec();
        };

        QScopedPointer<QThread> thread(QThread::create(runEventLoop));
        QSignalSpy finishedSpy(thread.get(), &QThread::finished);
        QVERIFY(finishedSpy.isValid());

        thread->start();
        thread.reset();

        QCOMPARE(finishedSpy.size(), 1);
    }
}

class StopableJob : public QObject
{
    Q_OBJECT
public:
    StopableJob (QSemaphore &sem) : sem(sem) {}
    QSemaphore &sem;
public Q_SLOTS:
    void run() {
        sem.release();
        while (!thread()->isInterruptionRequested())
            QTest::qSleep(10);
        sem.release();
        Q_EMIT finished();
    }
Q_SIGNALS:
    void finished();
};

void tst_QThread::requestTermination()
{
    QThread thread;
    QVERIFY(!thread.isInterruptionRequested());
    QSemaphore sem;
    StopableJob *j  = new StopableJob(sem);
    j->moveToThread(&thread);
    connect(&thread, &QThread::started, j, &StopableJob::run);
    connect(j, &StopableJob::finished, &thread, &QThread::quit, Qt::DirectConnection);
    connect(&thread, &QThread::finished, j, &QObject::deleteLater);
    thread.start();
    QVERIFY(!thread.isInterruptionRequested());
    sem.acquire();
    QVERIFY(!thread.wait(1000));
    thread.requestInterruption();
    sem.acquire();
    QVERIFY(thread.wait(1000));
    QVERIFY(!thread.isInterruptionRequested());
}

/*
    This is a regression test for QTBUG-96846.

    Incorrect system thread ID cleanup can cause QThread::wait() to report that
    a thread is trying to wait for itself.
*/
void tst_QThread::threadIdReuse()
{
    // It's important that those thread ID's are not accessed concurrently
    Qt::HANDLE threadId1;

    auto thread1Fn = [&threadId1]() -> void { threadId1 = QThread::currentThreadId(); };
    QScopedPointer<QThread> thread1(QThread::create(thread1Fn));
    thread1->start();
    QVERIFY(thread1->wait());

    // If the system thread allocated for thread1 is destroyed before thread2 is started,
    // at least on some versions of Linux the system thread ID for thread2 would be the
    // same as one that was used for thread1.

    // The system thread may be alive for some time after returning from QThread::wait()
    // because the implementation is using detachable threads, so some additional time is
    // required for the system thread to terminate. Not waiting long enough here would result
    // in a new system thread ID being allocated for thread2 and this test passing even without
    // a fix for QTBUG-96846.
    bool threadIdReused = false;

    for (int i = 0; i < 42; i++) {
        QThread::sleep(1ms);

        Qt::HANDLE threadId2;
        bool waitOk = false;

        auto waitForThread1 = [&thread1, &threadId2, &waitOk]() -> void {
            threadId2 = QThread::currentThreadId();
            waitOk = thread1->wait();
        };

        QScopedPointer<QThread> thread2(QThread::create(waitForThread1));
        thread2->start();
        QVERIFY(thread2->wait());
        QVERIFY(waitOk);

        if (threadId1 == threadId2) {
            qDebug("Thread ID reused at iteration %d", i);
            threadIdReused = true;
            break;
        }
    }

    if (!threadIdReused) {
        QSKIP("Thread ID was not reused");
    }
}

class WaitToRun_Thread : public QThread
{
    Q_OBJECT
public:
    void run() override
    {
        emit running();
        QThread::exec();
    }

Q_SIGNALS:
    void running();
};


void tst_QThread::terminateAndPrematureDestruction()
{
#if defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer)
    QSKIP("Thread termination might result in stack underflow address sanitizer errors.");
#endif

    WaitToRun_Thread thread;
    QSignalSpy spy(&thread, &WaitToRun_Thread::running);
    thread.start();
    QVERIFY(spy.wait(500));

    QScopedPointer<QObject> obj(new QObject);
    QPointer<QObject> pObj(obj.data());
    obj->deleteLater();

    thread.terminate();
    QVERIFY2(pObj, "object was deleted prematurely!");
    thread.wait(500);
}

void tst_QThread::terminateAndDoubleDestruction()
{
#if defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer)
    QSKIP("Thread termination might result in stack underflow address sanitizer errors.");
#endif

    class ChildObject : public QObject
    {
    public:
        ChildObject(QObject *parent)
            : QObject(parent)
        {
            QSignalSpy spy(&thread, &WaitToRun_Thread::running);
            thread.start();
            spy.wait(500);
        }

        ~ChildObject()
        {
            QVERIFY2(!inDestruction, "Double object destruction!");
            inDestruction = true;
            thread.terminate();
            thread.wait(500);
        }

        bool inDestruction = false;
        WaitToRun_Thread thread;
    };

    class TestObject : public QObject
    {
    public:
        TestObject()
            : child(new ChildObject(this))
        {
        }

        ~TestObject()
        {
            child->deleteLater();
        }

        ChildObject *child = nullptr;
    };

    TestObject obj;
}

void tst_QThread::bindingListCleanupAfterDelete()
{
    QThread t;
    auto optr = std::make_unique<QObject>();
    optr->moveToThread(&t);
    auto threadPriv =  static_cast<QThreadPrivate *>(QObjectPrivate::get(&t));
    auto list = threadPriv->m_statusOrPendingObjects.list();
    QVERIFY(list);
    optr.reset();
    QVERIFY(list->empty());
}

QTEST_MAIN(tst_QThread)
#include "tst_qthread.moc"
