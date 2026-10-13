// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef REDPANDA_RUSTLSPCLIENT_H
#define REDPANDA_RUSTLSPCLIENT_H
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QHash>
#include <QElapsedTimer>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

// One asynchronous LSP session, shared by all open Rust documents. Text remains
// owned by the editor; the server can never write files or apply workspace edits.
class RustLspClient final : public QObject {
    Q_OBJECT
public:
    explicit RustLspClient(QObject *parent=nullptr);
    ~RustLspClient();
    int updateDocument(const QString &path,const QString &text);
    void closeDocument(const QString &path);
    void savedDocument(const QString &path);
    int requestAt(const QString &method,const QString &path,int line,int character,const QJsonObject &extra={});
    int requestDocument(const QString &method,const QString &path,const QJsonObject &extra={});
    int requestWorkspace(const QString &method,const QJsonObject &params);
    bool synchronizeAll();
    int resolveCompletion(const QString &path,const QJsonObject &item);
    void cancel(int id);
    void restart();
    void setSuspended(bool suspended);
    bool ready() const { return mReady; }
    bool analysisReady()const{return mReady&&mQuiescent;}
    int version(const QString &path) const;
    static QString uri(const QString &path);
signals:
    void status(const QString &text);
    void readyChanged(bool ready);
    void result(int id,const QString &method,const QString &path,int version,const QJsonValue &value);
    void diagnostics(const QString &path,int version,const QJsonArray &items);
private:
    struct Document { QString path,text; int version=1,sentVersion=-1; };
    struct Request { QString method,path; int version=0; qint64 deadline=0; };
    void ensureStarted();
    void startServer();
    void initialize();
    void readOutput();
    void parseFrames();
    void handleMessage(const QJsonObject &message);
    bool send(QJsonObject message);
    void notify(const QString &method,const QJsonObject &params);
    int request(const QString &method,const QJsonObject &params,const QString &path={},int timeout=10000);
    void flushDocuments();
    bool flushDocument(const QString &key);
    void updateConfiguration();
    QJsonObject configuration() const;
    void failed(const QString &reason);
    bool clearPending();
    bool resetSentVersions();
    void stopProcesses();
    bool prepareJob();
    void releaseJob();
    QProcess mProcess,mProbe;
    QTimer mWatchdog,mRestart,mFlush,mConfiguration,mParse,mIdle;
    QHash<QString,Document> mDocuments;
    QHash<int,Request> mRequests;
    QElapsedTimer mClock,mFailuresWindow;
    QByteArray mInput,mProbeOutput;
    QString mSysroot;
    QJsonArray mCfg;
    QJsonObject mLastConfiguration;
    int mNextId=1,mFailures=0,mNextDocumentVersion=1;
    quint64 mGeneration=0;
    qint64 mExpected=-1,mProbeDeadline=0;
    bool mReady=false,mStarting=false,mStopping=false,mRecovering=false;
    bool mQuiescent=false;
    bool mSuspended=false;
#ifdef Q_OS_WIN
    HANDLE mJob=nullptr;
    STARTUPINFOEXW mStartup{};
    QByteArray mAttributeStorage;
#endif
};
#endif
