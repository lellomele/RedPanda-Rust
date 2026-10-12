// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "rustterminal.h"
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <atomic>
#include <thread>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <QProcess>
#endif
namespace {
#ifdef Q_OS_WIN
QString quoteArgument(const QString &text){
    if(!text.isEmpty()&&!text.contains(' ')&&!text.contains('\t')&&!text.contains('"'))return text;
    QString result="\"";int slashes=0;
    for(const QChar value:text){
        if(value=='\\'){++slashes;continue;}
        if(value=='"'){result+=QString(slashes*2+1,'\\');result+='"';}
        else{result+=QString(slashes,'\\');result+=value;}slashes=0;
    }
    return result+QString(slashes*2,'\\')+'"';
}
#endif
}
// Blocking native console I/O stays on dedicated threads. The QObject wrapper
// can be destroyed immediately: its worker owns every native resource until it
// has stopped, and queued signal connections are removed automatically.
class RustTerminalWorker final:public QThread{
    Q_OBJECT
public:
    QString program,directory;QStringList arguments;QProcessEnvironment environment;
    bool enqueue(const QByteArray &bytes){
        QMutexLocker lock(&mMutex);if(mStop||mPending.size()+bytes.size()>16384)return false;
        mPending+=bytes;mInputReady.wakeAll();return true;
    }
    void requestStop(){
        mStop=true;QMutexLocker lock(&mMutex);mInputReady.wakeAll();
#ifdef Q_OS_WIN
        if(mJob)TerminateJobObject(mJob,1);
        if(mWriterHandle)CancelSynchronousIo(mWriterHandle);
#endif
    }
signals:
    void launched(qint64 pid);
    void bytes(const QByteArray &data);
    void failure(const QString &message);
    void completed(int code);
protected:
    void run()override{
#ifdef Q_OS_WIN
        HANDLE inputRead=nullptr,inputWrite=nullptr,outputRead=nullptr,outputWrite=nullptr;
        HPCON console=nullptr;PROCESS_INFORMATION process{};LPPROC_THREAD_ATTRIBUTE_LIST attributes=nullptr;
        QByteArray attributeBytes;std::thread reader,writer;int exitCode=-1;
        auto cleanup=[&]{
            mStop=true;{QMutexLocker lock(&mMutex);mInputReady.wakeAll();if(mWriterHandle)CancelSynchronousIo(mWriterHandle);if(mJob)TerminateJobObject(mJob,1);}
            if(writer.joinable())writer.join();
            if(inputRead){CloseHandle(inputRead);inputRead=nullptr;}
            if(outputWrite){CloseHandle(outputWrite);outputWrite=nullptr;}
            if(console){ClosePseudoConsole(console);console=nullptr;}
            if(reader.joinable())reader.join();
            if(attributes)DeleteProcThreadAttributeList(attributes);
            for(HANDLE handle:{inputRead,inputWrite,outputRead,outputWrite,process.hThread,process.hProcess})if(handle)CloseHandle(handle);
            QMutexLocker lock(&mMutex);if(mJob){CloseHandle(mJob);mJob=nullptr;}
            if(mWriterHandle){CloseHandle(mWriterHandle);mWriterHandle=nullptr;}
        };
        auto fail=[&](const QString &message){emit failure(message+QString(" (Windows %1)").arg(GetLastError()));};
        do{
            if(!CreatePipe(&inputRead,&inputWrite,nullptr,0)||!CreatePipe(&outputRead,&outputWrite,nullptr,0)){fail(tr("Impossibile creare i flussi del terminale"));break;}
            const HRESULT result=CreatePseudoConsole(COORD{120,35},inputRead,outputWrite,0,&console);
            if(FAILED(result)){emit failure(tr("ConPTY non disponibile (Windows 10 1809 o successivo richiesto). Codice %1.").arg(quint32(result)));break;}
            {
                QMutexLocker lock(&mMutex);mJob=CreateJobObjectW(nullptr,nullptr);
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};info.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                if(!mJob||!SetInformationJobObject(mJob,JobObjectExtendedLimitInformation,&info,sizeof(info))){fail(tr("Impossibile proteggere il terminale"));break;}
            }
            SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,2,0,&size);attributeBytes.resize(size);
            attributes=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeBytes.data());
            if(!InitializeProcThreadAttributeList(attributes,2,0,&size)){attributes=nullptr;fail(tr("Impossibile inizializzare il terminale"));break;}
            if(!UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,console,sizeof(console),nullptr,nullptr)||
                !UpdateProcThreadAttribute(attributes,0,PROC_THREAD_ATTRIBUTE_JOB_LIST,&mJob,sizeof(mJob),nullptr,nullptr)){
                fail(tr("Impossibile associare la console al processo"));break;
            }
            reader=std::thread([this,outputRead]{
                QByteArray buffer(16384,Qt::Uninitialized);DWORD count=0;qint64 total=0;
                while(ReadFile(outputRead,buffer.data(),DWORD(buffer.size()),&count,nullptr)&&count){
                    total+=count;
                    if(total<=16*1024*1024)emit bytes(buffer.left(count));
                    else if(!mStop){emit failure(tr("Output del terminale oltre 16 MiB; sessione interrotta."));requestStop();}
                }
            });
            const auto bridge=QDir(QCoreApplication::applicationDirPath()).filePath("rust-console-bridge.exe");
            QStringList commandParts{quoteArgument(QDir::toNativeSeparators(bridge)),quoteArgument(QDir::toNativeSeparators(program))};
            for(const auto &argument:arguments)commandParts.append(quoteArgument(argument));
            std::wstring command=commandParts.join(' ').toStdWString();
            auto keys=environment.keys();std::sort(keys.begin(),keys.end(),[](const QString &a,const QString &b){return a.compare(b,Qt::CaseInsensitive)<0;});
            std::wstring env;for(const auto &key:keys){env+=(key+'='+environment.value(key)).toStdWString();env.push_back(0);}env.push_back(0);
            const auto cwd=QDir::toNativeSeparators(directory).toStdWString();const auto executable=QDir::toNativeSeparators(bridge).toStdWString();
            STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attributes;
            if(mStop)break;
            if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,
                EXTENDED_STARTUPINFO_PRESENT|CREATE_UNICODE_ENVIRONMENT,env.data(),cwd.c_str(),&startup.StartupInfo,&process)){
                fail(tr("Avvio del terminale non riuscito"));break;
            }
            CloseHandle(inputRead);inputRead=nullptr;CloseHandle(outputWrite);outputWrite=nullptr;
            if(mStop){TerminateJobObject(mJob,1);break;}
            emit launched(process.dwProcessId);
            writer=std::thread([this,inputWrite]{
                {QMutexLocker lock(&mMutex);DuplicateHandle(GetCurrentProcess(),GetCurrentThread(),GetCurrentProcess(),&mWriterHandle,0,FALSE,DUPLICATE_SAME_ACCESS);}
                while(!mStop){
                    QByteArray pending;
                    {QMutexLocker lock(&mMutex);while(mPending.isEmpty()&&!mStop)mInputReady.wait(&mMutex);if(mStop)break;pending=mPending.left(4096);mPending.remove(0,pending.size());}
                    DWORD written=0;
                    if(!WriteFile(inputWrite,pending.constData(),DWORD(pending.size()),&written,nullptr)||written!=DWORD(pending.size())){
                        if(!mStop)emit failure(tr("Input del terminale non disponibile."));break;
                    }
                }
            });
            while(!mStop){const auto wait=WaitForSingleObject(process.hProcess,100);if(wait==WAIT_OBJECT_0)break;if(wait==WAIT_FAILED){fail(tr("Attesa del terminale non riuscita"));break;}}
            DWORD code=1;if(GetExitCodeProcess(process.hProcess,&code)&&code!=STILL_ACTIVE)exitCode=int(code);
        }while(false);
        cleanup();emit completed(exitCode);
#else
        QProcess process;process.setProcessEnvironment(environment);process.setWorkingDirectory(directory);process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(program,arguments);
        if(!process.waitForStarted(10000)){emit failure(process.errorString());emit completed(-1);return;}
        emit launched(process.processId());qint64 total=0;
        while(!mStop&&process.state()!=QProcess::NotRunning){
            QByteArray pending;{QMutexLocker lock(&mMutex);pending.swap(mPending);}if(!pending.isEmpty())process.write(pending);
            process.waitForReadyRead(50);const auto output=process.readAll();total+=output.size();
            if(total<=16*1024*1024)emit bytes(output);else{emit failure(tr("Output oltre 16 MiB."));mStop=true;}
        }
        if(mStop){process.kill();process.waitForFinished(1000);}emit completed(process.exitCode());
#endif
    }
private:
    QMutex mMutex;QWaitCondition mInputReady;QByteArray mPending;std::atomic_bool mStop{false};
#ifdef Q_OS_WIN
    HANDLE mJob=nullptr,mWriterHandle=nullptr;
#endif
};
RustTerminal::RustTerminal(QObject *parent):QObject(parent){}
RustTerminal::~RustTerminal(){if(mWorker)disconnect(mWorker,nullptr,this,nullptr);stop();}
bool RustTerminal::start(const QString &program,const QStringList &arguments,const QString &directory,const QProcessEnvironment &environment){
    if(active()||!QFileInfo(program).isExecutable()||!QFileInfo(directory).isDir())return false;
#ifdef Q_OS_WIN
    if(!QFileInfo::exists(QDir(QCoreApplication::applicationDirPath()).filePath("rust-console-bridge.exe"))){emit error(tr("Il componente rust-console-bridge.exe non è presente nella distribuzione."));return false;}
#endif
    mDecoder.resetState();auto worker=new RustTerminalWorker;
    worker->program=QFileInfo(program).absoluteFilePath();worker->arguments=arguments;worker->directory=directory;worker->environment=environment;mWorker=worker;
    connect(worker,&RustTerminalWorker::launched,this,&RustTerminal::started);
    connect(worker,&RustTerminalWorker::bytes,this,[this](const QByteArray &bytes){emit output(mDecoder(bytes));});
    connect(worker,&RustTerminalWorker::failure,this,&RustTerminal::error);
    connect(worker,&RustTerminalWorker::completed,this,[this](int code){mWorker=nullptr;emit finished(code);});
    connect(worker,&QThread::finished,worker,&QObject::deleteLater);worker->start();return true;
}
bool RustTerminal::write(const QByteArray &bytes){return mWorker&&mWorker->enqueue(bytes);}
void RustTerminal::stop(){if(mWorker)mWorker->requestStop();}
#include "rustterminal.moc"
