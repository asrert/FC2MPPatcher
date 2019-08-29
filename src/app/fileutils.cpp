#include <QCryptographicHash>
#include <QStringList>
#include <QDebug>

#include "fileutils.h"
#include "global.h"

QString FileUtils::checkSum(QFile file)
{
    if (file.open(QFile::ReadOnly)) {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(&file);
        file.close();

        return hash.result().toHex();
    }

    return QString();
}

bool FileUtils::isValid(const QDir &dir, const FileEntry &fileEntry, const TargetEntry &target, bool patched)
{
    QString fileCheckSum = checkSum(dir.filePath(fileEntry.getName()));
    const char* targetCheckSum = patched ? target.getCheckSumPatched() : target.getCheckSum();

    return fileCheckSum == targetCheckSum;
}

QString FileUtils::appendToName(const QDir &dir, const FileEntry &fileEntry, const QString &append)
{
    QStringList split = QString(fileEntry.getName()).split(".");
    QString suffix = '.' + split.takeLast();

    return dir.filePath(split.join(QString()) + append + suffix);
}

bool FileUtils::copy(const QDir &dir, const FileEntry &fileEntry, bool backup)
{
    QString fileName = dir.filePath(fileEntry.getName());
    QString fileCopyName = appendToName(dir, fileEntry, game_backup_suffix);
    QFile file = fileName;
    QFile fileCopy = fileCopyName;

    if (backup) {
        if (!fileCopy.exists()) {
            return file.rename(fileCopyName) & file.copy(fileName);
        }
    } else {
        if (fileCopy.exists()) {
            return file.remove() & fileCopy.rename(fileName);
        }
    }

    return false;
}

bool FileUtils::backup(const QDir &dir, const FileEntry &fileEntry)
{
    bool result = copy(dir, fileEntry, true);

    if (result) {
        qDebug() << QT_TR_NOOP(QString("Backing up file: %1").arg(fileEntry.getName()));
    }

    return result;
}

bool FileUtils::restore(const QDir &dir, const FileEntry &fileEntry)
{
    bool result = copy(dir, fileEntry, false);

    if (result) {
        qDebug() << QT_TR_NOOP(QString("Restoring file: %1").arg(fileEntry.getName()));
    }

    return result;
}
