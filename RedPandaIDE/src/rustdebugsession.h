// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef REDPANDA_RUSTDEBUGSESSION_H
#define REDPANDA_RUSTDEBUGSESSION_H
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QHash>
#include <QSet>
#include "rustterminal.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif
class RustDebugSession final:public QObject{
    Q_OBJECT
public:
    enum class State {Idle,Starting,Running,Stopped,Stopping};Q_ENUM(State)
    explicit RustDebugSession(QObject *parent=nullptr);
    ~RustDebugSession();
    bool launch(const QString &program,const QString &directory,const QStringList &args={},
        const QProcessEnvironment &env=QProcessEnvironment::systemEnvironment(),bool stopOnEntry=false,
        const QString &runToFile={},int runToLine=-1);
    void stop();
    State state()const{return mState;}
    bool active()const{return mState!=State::Idle;}
    int threadId()const{return mThread;}
    void selectThread(int thread){if(mState==State::Stopped&&thread>0)mThread=thread;}
    QJsonObject capabilities()const{return mCapabilities;}
    int request(const QString &command,const QJsonObject &arguments={},int timeout=15000);
    void cancel(int id);
    void command(const QString &command);
    void runTo(const QString &file,int zeroBasedLine);
    void setBreakpoints(const QString &file,const QJsonArray &breakpoints);
    void setFunctionBreakpoints(const QJsonArray &breakpoints);
    bool sendInput(const QString &text);
signals:
    void stateChanged(RustDebugSession::State state);
    void output(const QString &text);
    void error(const QString &message);
    void event(const QString &name,const QJsonObject &body);
    void reply(int id,const QString &command,const QJsonObject &body,bool success,const QString &message);
    void stopped(int threadId,const QString &reason);
    void finished(int code);
private:
    struct Pending {QString command; qint64 deadline;quint64 generation;bool configuration=false;};
    void connected();
    void read();
    void parse();
    bool send(QJsonObject message);
    void handle(const QJsonObject &message);
    void configure();
    void finishConfiguration();
    void pumpConfiguration();
    void terminate(const QString &message={});
    void setState(State state);
    void complete();
    QJsonArray effectiveBreakpoints(const QString &file) const;
    void clearRunTo();
    void pumpBreakpointUpdates();
    bool prepareJob();
    void releaseJob();
    QTcpServer mServer;
    QPointer<QTcpSocket> mSocket;
    QProcess mAdapter;
    RustTerminal mTerminal;
    QTimer mWatchdog,mParse,mForceStop;
    QElapsedTimer mClock;
    QHash<int,Pending> mPending;
    QHash<QString,QJsonArray> mBreakpoints;
    QSet<QString> mDirtyBreakpointFiles;
    QJsonArray mFunctionBreakpoints;
    QByteArray mInput;
    QString mToken,mProgram,mDirectory,mAdapterPath;
    QStringList mArguments;
    QStringList mConfigurationFiles;
    QString mRunToFile;
    int mRunToLine=0,mRunToRequest=0;
    QProcessEnvironment mEnvironment;
    QJsonObject mCapabilities;
    qint64 mExpected=-1,mStartupDeadline=0;
    int mSequence=1,mThread=0,mReverseTerminal=0,mConfigurationPending=0,mExitCode=-1;
    quint64 mGeneration=0,mSessionEpoch=0;
    bool mAuthenticated=false,mConfigured=false,mStopOnEntry=false,mCompleting=false;
    State mState=State::Idle;
#ifdef Q_OS_WIN
    HANDLE mJob=nullptr;STARTUPINFOEXW mStartup{};QByteArray mAttributes;
#endif
};
Q_DECLARE_METATYPE(RustDebugSession::State)
#endif
