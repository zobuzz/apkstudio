#ifndef APK_EVERYTHING_WORKER_H
#define APK_EVERYTHING_WORKER_H

#include <QObject>

class ApkEverythingWorker : public QObject
{
    Q_OBJECT
public:
    explicit ApkEverythingWorker(const QString &folder, bool aapt2, QObject *parent = nullptr);
    void recompile();
    bool sign();
    void install();
private:
    bool m_Aapt2;
    QString m_Folder;
    QString m_Apk;
signals:

    void recompileFailed(const QString &folder);
    void recompileFinished(const QString &folder);

    void signFailed(const QString &apk);
    void signFinished(const QString &apk);

    void installFailed(const QString &apk);
    void installFinished(const QString &apk);

    void finished();
    void started();
};

#endif // APKDECOMPILEWORKER_H
