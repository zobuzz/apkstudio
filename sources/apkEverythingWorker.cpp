#include <QDebug>
#include <QSettings>
#include <QFileInfo>
#include "apkEverythingWorker.h"
#include "processutils.h"


ApkEverythingWorker::ApkEverythingWorker(const QString &folder, bool aapt2, QObject *parent)
    : QObject(parent), m_Aapt2(aapt2), m_Folder(folder)
{
}

void ApkEverythingWorker::recompile()
{
    emit started();
#ifdef QT_DEBUG
    qDebug() << "Recompiling" << m_Folder;
#endif
    const QString java = ProcessUtils::javaExe();
    const QString apktool = ProcessUtils::apktoolJar();
    if (java.isEmpty() || apktool.isEmpty()) {
        emit recompileFailed(m_Folder);
        return;
    }
    QString heap("-Xmx%1m");
    heap = heap.arg(QString::number(ProcessUtils::javaHeapSize()));
    QStringList args;
    args << heap << "-jar" << apktool;
    args << "b" << m_Folder;
    if (m_Aapt2) {
        args << "--use-aapt2";
    }
    ProcessResult result = ProcessUtils::runCommand(java, args);
#ifdef QT_DEBUG
    qDebug() << "Apktool returned code" << result.code;
#endif
    if (result.code != 0) {
        emit recompileFailed(m_Folder);
        return;
    }

    if(sign()) {
        install();
    }

    emit recompileFinished(m_Folder);
    emit finished();
}


bool ApkEverythingWorker::sign()
{
    // emit started();
#ifdef QT_DEBUG
    qDebug() << "Signing" << m_Apk;
#endif

    QSettings settings;

    QString m_Keystore;
    QString m_KeystorePassword;
    QString m_Alias;
    QString m_AliasPassword;
    bool m_Zipalign;

    m_Keystore = settings.value("signing_keystore").toString(),
    m_KeystorePassword = settings.value("signing_keystore_password").toString(),
    m_Alias = settings.value("signing_alias").toString(),
    m_AliasPassword = settings.value("signing_alias_password").toString(),
    m_Zipalign = settings.value("signing_zipalign", true).toBool();

    // 从带-decompiled后缀的路径中提取原始apk文件名
    QString originalApkName;
    if (m_Folder.endsWith("-decompiled")) {
        QFileInfo fileInfo(m_Folder);
        QString folderName = fileInfo.fileName();
        originalApkName = folderName.left(folderName.length() - 11); 
    }
    
    m_Apk = m_Folder + "/dist/" + originalApkName;

    const QString java = ProcessUtils::javaExe();
    const QString uas = ProcessUtils::uberApkSignerJar();
    if (java.isEmpty() || uas.isEmpty()) {
        emit signFailed(m_Apk);
        return false;
    }
    QString heap("-Xmx%1m");
    heap = heap.arg(QString::number(ProcessUtils::javaHeapSize()));
    QStringList args;
    args << heap << "-jar" << uas;
    args << "-a" << m_Apk << "--allowResign" << "--overwrite";
    if (!m_Keystore.isEmpty() && !m_Alias.isEmpty()) {
        args << "--ks" << m_Keystore;
        args << "--ksPass" << m_KeystorePassword;
        args << "--ksAlias" << m_Alias;
        args << "--ksKeyPass" << m_AliasPassword;
    }
    if (!m_Zipalign) {
        args << "--skipZipAlign";
    }
    ProcessResult result = ProcessUtils::runCommand(java, args);
#ifdef QT_DEBUG
    qDebug() << "Uber APK Signer returned code" << result.code;
#endif
    if (result.code != 0) {
        emit signFailed(m_Apk);
        return false;
    }
    emit signFinished(m_Apk);
    return true;
    // emit finished();    
}

void ApkEverythingWorker::install()
{
    // emit started();
#ifdef QT_DEBUG
    qDebug() << "Installing" << m_Apk;
#endif
    const QString adb = ProcessUtils::adbExe();
    if (adb.isEmpty()) {
        emit installFailed(m_Apk);
        return;
    }
    QStringList args;
    args << "install" << m_Apk;
    ProcessResult result = ProcessUtils::runCommand(adb, args);
#ifdef QT_DEBUG
    qDebug() << "ADB returned code" << result.code;
#endif
    if (result.code != 0) {
        emit installFailed(m_Apk);
        return;
    }
    emit installFinished(m_Apk);
    // emit finished();
}
