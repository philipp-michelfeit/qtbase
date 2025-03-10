// Copyright (C) 2022 The Qt Company Ltd.
// Copyright (C) 2015 Ivan Komissarov <ABBAPOH@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qstorageinfo.h"
#include "qstorageinfo_p.h"

#include "qdebug.h"

QT_BEGIN_NAMESPACE

QT_IMPL_METATYPE_EXTERN(QStorageInfo)

/*!
    \class QStorageInfo
    \inmodule QtCore
    \since 5.4
    \brief Provides information about currently mounted storage and drives.

    \ingroup io
    \ingroup shared

    \compares equality

    Allows retrieving information about the volume's space, its mount point,
    label, and filesystem name.

    You can create an instance of QStorageInfo by passing the path to the
    volume's mount point as a constructor parameter, or you can set it using
    the setPath() method. The static mountedVolumes() method can be used to get the
    list of all mounted filesystems.

    QStorageInfo always caches the retrieved information, but you can call
    refresh() to invalidate the cache.

    The following example retrieves the most common information about the root
    volume of the system, and prints information about it.

    \snippet code/src_corelib_io_qstorageinfo.cpp 2
*/

QStorageInfo::QStorageInfo(QStorageInfoPrivate &dd)
    : d(&dd)
{
}

/*!
    Constructs an empty QStorageInfo object.

    Objects created with the default constructor will be invalid and therefore
    not ready for use.

    \sa setPath(), isReady(), isValid()
*/
QStorageInfo::QStorageInfo()
    : d(new QStorageInfoPrivate)
{
}

/*!
    Constructs a new QStorageInfo object that gives information about the volume
    mounted at \a path.

    If you pass a directory or file, the QStorageInfo object will refer to the
    volume where this directory or file is located.
    You can check if the created object is correct using the isValid() method.

    The following example shows how to get the volume on which the application is
    located. It is recommended to always check that the volume is ready and valid.

    \snippet code/src_corelib_io_qstorageinfo.cpp 0

    \sa setPath()
*/
QStorageInfo::QStorageInfo(const QString &path)
    : d(new QStorageInfoPrivate)
{
    setPath(path);
}

/*!
    Constructs a new QStorageInfo object that gives information about the volume
    containing the \a dir folder.
*/
QStorageInfo::QStorageInfo(const QDir &dir)
    : d(new QStorageInfoPrivate)
{
    setPath(dir.absolutePath());
}

/*!
    Constructs a new QStorageInfo object that is a copy of the \a other QStorageInfo object.
*/
QStorageInfo::QStorageInfo(const QStorageInfo &other)
    : d(other.d)
{
}

/*!
    Destroys the QStorageInfo object and frees its resources.
*/
QStorageInfo::~QStorageInfo()
{
}

/*!
    Makes a copy of the QStorageInfo object \a other and assigns it to this QStorageInfo object.
*/
QStorageInfo &QStorageInfo::operator=(const QStorageInfo &other)
{
    d = other.d;
    return *this;
}

/*!
    \fn QStorageInfo &QStorageInfo::operator=(QStorageInfo &&other)

    Assigns \a other to this QStorageInfo instance.
*/

/*!
    \fn void QStorageInfo::swap(QStorageInfo &other)

    Swaps this volume info with \a other. This function is very fast and
    never fails.
*/

/*!
    Sets this QStorageInfo object to the filesystem mounted where \a path is located.

    \a path can either be a root path of the filesystem, a directory, or a file
    within that filesystem.

    \sa rootPath()
*/
void QStorageInfo::setPath(const QString &path)
{
    if (d->rootPath == path)
        return;
    d.detach();
    d->rootPath = path;
    d->doStat();
}

/*!
    Returns the mount point of the filesystem this QStorageInfo object
    represents.

    On Windows, it returns the volume letter in case the volume is not mounted to
    a directory.

    Note that the value returned by rootPath() is the real mount point of a
    volume, and may not be equal to the value passed to the constructor or setPath()
    method. For example, if you have only the root volume in the system, and
    pass '/directory' to setPath(), then this method will return '/'.

    \sa setPath(), device()
*/
QString QStorageInfo::rootPath() const
{
    return d->rootPath;
}

/*!
    Returns the size (in bytes) available for the current user. It returns
    the total size available if the user is the root user or a system administrator.

    This size can be less than or equal to the free size returned by
    bytesFree() function.

    Returns -1 if QStorageInfo object is not valid.

    \sa bytesTotal(), bytesFree()
*/
qint64 QStorageInfo::bytesAvailable() const
{
    return d->bytesAvailable;
}

/*!
    Returns the number of free bytes in a volume. Note that if there are
    quotas on the filesystem, this value can be larger than the value
    returned by bytesAvailable().

    Returns -1 if QStorageInfo object is not valid.

    \sa bytesTotal(), bytesAvailable()
*/
qint64 QStorageInfo::bytesFree() const
{
    return d->bytesFree;
}

/*!
    Returns the total volume size in bytes.

    Returns -1 if QStorageInfo object is not valid.

    \sa bytesFree(), bytesAvailable()
*/
qint64 QStorageInfo::bytesTotal() const
{
    return d->bytesTotal;
}

/*!
    \since 5.6
    Returns the optimal transfer block size for this filesystem.

    Returns -1 if QStorageInfo could not determine the size or if the QStorageInfo
    object is not valid.
 */
int QStorageInfo::blockSize() const
{
    return d->blockSize;
}

/*!
    Returns the type name of the filesystem.

    This is a platform-dependent function, and filesystem names can vary
    between different operating systems. For example, on Windows filesystems
    they can be named \c NTFS, and on Linux they can be named \c ntfs-3g or \c fuseblk.

    \sa name()
*/
QByteArray QStorageInfo::fileSystemType() const
{
    return d->fileSystemType;
}

/*!
    Returns the device for this volume.

    For example, on Unix filesystems (including \macos), this returns the
    devpath like \c /dev/sda0 for local storages. On Windows, it returns the UNC
    path starting with \c \\\\?\\ for local storages (in other words, the volume GUID).

    \sa rootPath(), subvolume()
*/
QByteArray QStorageInfo::device() const
{
    return d->device;
}

/*!
    \since 5.9
    Returns the subvolume name for this volume.

    Some filesystem types allow multiple subvolumes inside one device, which
    may be mounted in different paths (e.g. 'bind' mounts on Unix, or Btrfs
    filesystem subvolumes). If the subvolume could be detected, its name is
    returned by this function. The format of the subvolume name is specific
    to each filesystem type.

    If this volume was not mounted from a subvolume of a larger filesystem or
    if the subvolume could not be detected, this function returns an empty byte
    array.

    \sa device()
*/
QByteArray QStorageInfo::subvolume() const
{
    return d->subvolume;
}

/*!
    Returns the human-readable name of a filesystem, usually called \c label.

    Not all filesystems support this feature. In this case, the value returned by
    this method could be empty. An empty string is returned if the file system
    does not support labels, or if no label is set.

    On Linux, retrieving the volume's label requires \c udev to be present in the
    system.

    \sa fileSystemType()
*/
QString QStorageInfo::name() const
{
    return d->name;
}

/*!
    Returns the volume's name, if available, or the root path if not.
*/
QString QStorageInfo::displayName() const
{
    if (!d->name.isEmpty())
        return d->name;
    return d->rootPath;
}

/*!
    \fn bool QStorageInfo::isRoot() const

    Returns true if this QStorageInfo represents the system root volume; false
    otherwise.

    On Unix filesystems, the root volume is a volume mounted on \c /. On Windows,
    the root volume is the volume where the OS is installed.

    \sa root()
*/

/*!
    Returns true if the current filesystem is protected from writing; false
    otherwise.
*/
bool QStorageInfo::isReadOnly() const
{
    return d->readOnly;
}

/*!
    Returns true if the current filesystem is ready to work; false otherwise. For
    example, false is returned if the CD volume is not inserted.

    Note that fileSystemType(), name(), bytesTotal(), bytesFree(), and
    bytesAvailable() will return invalid data until the volume is ready.

    \sa isValid()
*/
bool QStorageInfo::isReady() const
{
    return d->ready;
}

/*!
    Returns true if the QStorageInfo specified by rootPath exists and is mounted
    correctly.

    \sa isReady()
*/
bool QStorageInfo::isValid() const
{
    return d->valid;
}

/*!
    Resets QStorageInfo's internal cache.

    QStorageInfo caches information about storage to speed up performance.
    QStorageInfo retrieves information during object construction and/or when calling
    the setPath() method. You have to manually reset the cache by calling this
    function to update storage information.
*/
void QStorageInfo::refresh()
{
    d.detach();
    d->doStat();
}

/*!
    Returns the list of QStorageInfo objects that corresponds to the list of currently
    mounted filesystems.

    On Windows, this returns the drives visible in the \gui{My Computer} folder. On Unix
    operating systems, it returns the list of all mounted filesystems (except for
    pseudo filesystems).

    Returns all currently mounted filesystems by default.

    The example shows how to retrieve all available filesystems, skipping read-only ones.

    \snippet code/src_corelib_io_qstorageinfo.cpp 1

    \sa root()
*/
QList<QStorageInfo> QStorageInfo::mountedVolumes()
{
    return QStorageInfoPrivate::mountedVolumes();
}

Q_GLOBAL_STATIC(QStorageInfo, getRoot, QStorageInfoPrivate::root())

/*!
    Returns a QStorageInfo object that represents the system root volume.

    On Unix systems this call returns the root ('/') volume; in Windows the volume where
    the operating system is installed.

    \sa isRoot()
*/
QStorageInfo QStorageInfo::root()
{
    return *getRoot();
}

/*!
    \fn bool QStorageInfo::operator==(const QStorageInfo &lhs, const QStorageInfo &rhs)

    Returns \c true if the QStorageInfo object \a lhs refers to the same drive or
    volume as the QStorageInfo object \a rhs; otherwise it returns \c false.

    Note that the result of comparing two invalid QStorageInfo objects is always
    positive.
*/

/*!
    \fn bool QStorageInfo::operator!=(const QStorageInfo &lhs, const QStorageInfo &rhs)

    Returns \c true if the QStorageInfo object \a lhs refers to a different drive or
    volume than the QStorageInfo object \a rhs; otherwise returns \c false.
*/

bool comparesEqual(const QStorageInfo &lhs, const QStorageInfo &rhs)
{
    if (lhs.d == rhs.d)
        return true;
    return lhs.device() == rhs.device() && lhs.rootPath() == rhs.rootPath();
}

#ifndef QT_NO_DEBUG_STREAM
QDebug operator<<(QDebug debug, const QStorageInfo &s)
{
    QDebugStateSaver saver(debug);
    debug.nospace();
    debug.noquote();
    debug << "QStorageInfo(";
    if (s.isValid()) {
        const QStorageInfoPrivate *d = s.d.constData();
        debug << '"' << d->rootPath << '"';
        if (!d->fileSystemType.isEmpty())
            debug << ", type=" << d->fileSystemType;
        if (!d->name.isEmpty())
            debug << ", name=\"" << d->name << '"';
        if (!d->device.isEmpty())
            debug << ", device=\"" << d->device << '"';
        if (!d->subvolume.isEmpty())
            debug << ", subvolume=\"" << d->subvolume << '"';
        if (d->readOnly)
            debug << " [read only]";
        debug << (d->ready ? " [ready]" : " [not ready]");
        if (d->bytesTotal > 0) {
            debug << ", bytesTotal=" << d->bytesTotal << ", bytesFree=" << d->bytesFree
                  << ", bytesAvailable=" << d->bytesAvailable;
        }
    } else {
        debug << "invalid";
    }
    debug << ')';
    return debug;
}
#endif // !QT_NO_DEBUG_STREAM

QT_END_NAMESPACE
