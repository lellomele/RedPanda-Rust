// Copyright (C) 2026 Raffaele Mele
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef REDPANDA_RUSTWORKSPACE_H
#define REDPANDA_RUSTWORKSPACE_H
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QStringConverter>
#include <QTemporaryDir>
#include "common.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif
class RustWorkspace final : public QObject {
    Q_OBJECT
public:
    explicit RustWorkspace(QObject *parent=nullptr);
    ~RustWorkspace();
    static QString findTool(const QString &name);
    static QString findRoot(const QString &file);
    bool busy() const {return mProcess.state()!=QProcess::NotRunning;}
    bool start(const QString &root,const QString &command,const QStringList &extra={});
    bool startFile(const QString &file,const QString &command);
    void stop();
    void sendInput(const QString &text);
    void closeInput();
signals:
    void output(const QString &text);
    void issue(PCompileIssue value);
    void busyChanged(bool busy);
    void finished(int exitCode,bool cancelled);
private:
    void drain();
    void handleLine(const QByteArray &line);
    void releaseJob();
    bool prepareJob();
    bool launch(const QString &program,const QStringList &args,bool diagnostics);
    QProcess mProcess;
    QTimer mStartTimer,mFinishTimer;
    QByteArray mPending;
    QStringDecoder mDecoder{QStringDecoder::Utf8};
    QString mRoot,mCommand;
    QString mRunAfterBuild;
    std::unique_ptr<QTemporaryDir> mFileBuild;
    bool mDirectDiagnostics=false;
    qint64 mBytes=0;
    bool mCancelled=false,mBuilding=true;
    int mIssues=0;
#ifdef Q_OS_WIN
    HANDLE mJob=nullptr;
    STARTUPINFOEXW mStartup{};
    QByteArray mAttributeStorage;
#endif
};
#endif
