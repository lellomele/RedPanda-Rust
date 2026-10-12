// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "rustdebugsession.h"
#include "rusttools.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QUuid>
#include <QSet>
namespace {constexpr qint64 MaxMessage=4*1024*1024;}
RustDebugSession::RustDebugSession(QObject *parent):QObject(parent),mTerminal(this){
    qRegisterMetaType<State>();mClock.start();mWatchdog.setInterval(250);mWatchdog.start();
    mParse.setSingleShot(true);mParse.setInterval(0);mForceStop.setSingleShot(true);mForceStop.setInterval(1500);
    mAdapter.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&mServer,&QTcpServer::newConnection,this,&RustDebugSession::connected);
    connect(&mParse,&QTimer::timeout,this,&RustDebugSession::parse);
    connect(&mForceStop,&QTimer::timeout,this,[this]{
#ifdef Q_OS_WIN
        if(mJob)TerminateJobObject(mJob,1);
#endif
        mAdapter.kill();mTerminal.stop();if(mAdapter.state()==QProcess::NotRunning)complete();
    });
    connect(&mAdapter,&QProcess::readyReadStandardOutput,this,[this]{mAdapter.readAllStandardOutput();});
    connect(&mAdapter,&QProcess::readyReadStandardError,this,[this]{
        const auto bytes=mAdapter.readAllStandardError();if(active()&&!bytes.isEmpty())emit output(QString::fromUtf8(bytes.left(8192)));
    });
    connect(&mAdapter,&QProcess::errorOccurred,this,[this](QProcess::ProcessError value){
        if(value==QProcess::FailedToStart||value==QProcess::ReadError||value==QProcess::WriteError)terminate(tr("CodeLLDB: %1").arg(mAdapter.errorString()));
    });
    connect(&mAdapter,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus exit){
        if(active()&&mState!=State::Stopping&&(exit==QProcess::CrashExit||mExitCode<0))emit error(tr("CodeLLDB si è arrestato prima della conclusione della sessione (codice %1).").arg(code));complete();
    });
    connect(&mTerminal,&RustTerminal::output,this,&RustDebugSession::output);
    connect(&mTerminal,&RustTerminal::error,this,[this](const QString &text){terminate(text);});
    connect(&mTerminal,&RustTerminal::started,this,[this](qint64 pid){
        if(!mReverseTerminal||!active())return;
        send({{"type","response"},{"request_seq",mReverseTerminal},{"command","runInTerminal"},{"success",true},
            {"body",QJsonObject{{"processId",pid},{"shellProcessId",pid}}}});mReverseTerminal=0;
    });
    connect(&mTerminal,&RustTerminal::finished,this,[this](int){
        if(mReverseTerminal){send({{"type","response"},{"request_seq",mReverseTerminal},{"command","runInTerminal"},{"success",false},{"message","Terminal failed to start"}});mReverseTerminal=0;}
    });
    connect(&mWatchdog,&QTimer::timeout,this,[this]{
        if(mState==State::Starting&&mStartupDeadline<mClock.elapsed()){terminate(tr("Avvio del debugger scaduto."));return;}
        const auto ids=mPending.keys();
        for(int id:ids){
            if(!mPending.contains(id)||mPending[id].deadline>mClock.elapsed())continue;
            const auto pending=mPending.take(id);
            emit reply(id,pending.command,{},false,tr("Richiesta debugger scaduta."));
            if(id==mRunToRequest){clearRunTo();emit error(tr("Esecuzione fino al cursore scaduta; il programma resta in pausa."));}
            if(pending.command=="initialize"||pending.command=="launch"||pending.command=="configurationDone"||pending.configuration){terminate(tr("Configurazione del debugger scaduta."));return;}
            if(QSet<QString>{"continue","next","stepIn","stepOut","pause"}.contains(pending.command)){terminate(tr("Il debugger non ha confermato il comando; sessione interrotta per evitare uno stato incoerente."));return;}
            if(pending.command=="setBreakpoints")emit error(tr("Aggiornamento dei breakpoint scaduto; verifica il collegamento al debugger."));
        }
        pumpBreakpointUpdates();
    });
}
RustDebugSession::~RustDebugSession(){
    mWatchdog.stop();mParse.stop();mForceStop.stop();mServer.close();mTerminal.stop();
    disconnect(&mAdapter,nullptr,this,nullptr);if(mSocket)disconnect(mSocket,nullptr,this,nullptr);
    if(mSocket){send({{"type","request"},{"command","disconnect"},{"arguments",QJsonObject{{"terminateDebuggee",true}}}});mSocket->abort();}
#ifdef Q_OS_WIN
    if(mJob)TerminateJobObject(mJob,1);
#endif
    mAdapter.kill();mAdapter.waitForFinished(700);releaseJob();
}
void RustDebugSession::setState(State state){if(mState==state)return;mState=state;emit stateChanged(state);}
bool RustDebugSession::prepareJob(){
#ifdef Q_OS_WIN
    mJob=CreateJobObjectW(nullptr,nullptr);if(!mJob)return false;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};info.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(mJob,JobObjectExtendedLimitInformation,&info,sizeof(info))){releaseJob();return false;}
    SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);mAttributes.resize(bytes);mStartup={};
    mStartup.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(mAttributes.data());
    if(!InitializeProcThreadAttributeList(mStartup.lpAttributeList,1,0,&bytes)){mStartup.lpAttributeList=nullptr;releaseJob();return false;}
    if(!UpdateProcThreadAttribute(mStartup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_JOB_LIST,&mJob,sizeof(mJob),nullptr,nullptr)){releaseJob();return false;}
    mAdapter.setCreateProcessArgumentsModifier([this](QProcess::CreateProcessArguments *args){
        mStartup.StartupInfo=*reinterpret_cast<STARTUPINFOW *>(args->startupInfo);mStartup.StartupInfo.cb=sizeof(mStartup);
        args->startupInfo=reinterpret_cast<Q_STARTUPINFO *>(&mStartup);args->flags|=EXTENDED_STARTUPINFO_PRESENT;
    });
#endif
    return true;
}
void RustDebugSession::releaseJob(){
#ifdef Q_OS_WIN
    if(mStartup.lpAttributeList){DeleteProcThreadAttributeList(mStartup.lpAttributeList);mStartup.lpAttributeList=nullptr;}
    if(mJob){CloseHandle(mJob);mJob=nullptr;}mAttributes.clear();
#endif
}
bool RustDebugSession::launch(const QString &program,const QString &directory,const QStringList &args,const QProcessEnvironment &environment,bool stopOnEntry,const QString &runToFile,int runToLine){
    if(active()||mTerminal.active()||mAdapter.state()!=QProcess::NotRunning)return false;
    if(!QFileInfo(program).isExecutable()||!QFileInfo(directory).isDir()){emit error(tr("Programma o cartella di debug non validi."));return false;}
    mAdapterPath=RustTools::effectivePath("codelldb");
    if(mAdapterPath.isEmpty()||!QFileInfo(mAdapterPath).isExecutable()){emit error(tr("CodeLLDB non trovato. Configuralo in Rust → Strumenti del compilatore."));return false;}
    mProgram=QFileInfo(program).absoluteFilePath();mDirectory=QFileInfo(directory).absoluteFilePath();mArguments=args;mEnvironment=environment;mStopOnEntry=stopOnEntry;
    mInput.clear();mPending.clear();mExpected=-1;mAuthenticated=false;mConfigured=false;mCapabilities={};mConfigurationPending=0;mReverseTerminal=0;mThread=0;mExitCode=-1;++mGeneration;
    mRunToFile.clear();mRunToRequest=0;mConfigurationFiles.clear();mDirtyBreakpointFiles.clear();
    if(!runToFile.isEmpty()){
        if(runToLine<0||!QFileInfo(runToFile).isFile()){emit error(tr("Sorgente o riga di destinazione non validi."));return false;}
        mRunToFile=QFileInfo(runToFile).absoluteFilePath();mRunToLine=runToLine+1;mStopOnEntry=false;
    }
    mToken=QUuid::createUuid().toString(QUuid::WithoutBraces);
    if(!mServer.listen(QHostAddress::LocalHost,0)){emit error(tr("Impossibile avviare il collegamento locale al debugger."));return false;}
    releaseJob();if(!prepareJob()){mServer.close();emit error(tr("Impossibile proteggere i processi del debugger."));return false;}
    QStringList arguments{"--connect",QString::number(mServer.serverPort()),"--auth-token",mToken};
#ifdef Q_OS_WIN
    const auto library=QDir(QFileInfo(mAdapterPath).absolutePath()).filePath("../lldb/bin/liblldb.dll");
#else
    const auto library=QDir(QFileInfo(mAdapterPath).absolutePath()).filePath("../lldb/lib/liblldb.so");
#endif
    if(QFileInfo::exists(library))arguments<<"--liblldb"<<QDir::cleanPath(library);
    auto env=environment;env.insert("PATH",QFileInfo(mAdapterPath).absolutePath()+QDir::listSeparator()+env.value("PATH"));
    mAdapter.setProcessEnvironment(env);mAdapter.setWorkingDirectory(mDirectory);
    mStartupDeadline=mClock.elapsed()+30000;setState(State::Starting);mAdapter.start(mAdapterPath,arguments);return true;
}
void RustDebugSession::connected(){
    while(mServer.hasPendingConnections()){
        auto socket=mServer.nextPendingConnection();
        if(mSocket||!active()||socket->peerAddress()!=QHostAddress::LocalHost){socket->abort();socket->deleteLater();continue;}
        mSocket=socket;socket->setParent(this);socket->setReadBufferSize(MaxMessage+8192);
        connect(socket,&QTcpSocket::readyRead,this,&RustDebugSession::read);
        connect(socket,&QTcpSocket::disconnected,this,[this,socket]{
            if(mSocket!=socket)return;mSocket=nullptr;socket->deleteLater();
            if(active()&&mState!=State::Stopping)terminate(tr("Collegamento al debugger interrotto."));
        });
        request("initialize",{{"clientID","red-panda-rust"},{"adapterID","lldb"},{"linesStartAt1",true},{"columnsStartAt1",true},
            {"pathFormat","path"},{"supportsRunInTerminalRequest",true},{"supportsVariableType",true},{"supportsVariablePaging",true},{"supportsMemoryReferences",true}},10000);
    }
}
bool RustDebugSession::send(QJsonObject message){
    if(!mSocket||mSocket->state()!=QAbstractSocket::ConnectedState)return false;
    message.insert("seq",mSequence++);const auto body=QJsonDocument(message).toJson(QJsonDocument::Compact);
    if(body.size()>MaxMessage||mSocket->bytesToWrite()+body.size()>8*1024*1024){terminate(tr("Coda del debugger oltre il limite."));return false;}
    const auto frame="Content-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body;
    return mSocket->write(frame)==frame.size();
}
int RustDebugSession::request(const QString &command,const QJsonObject &arguments,int timeout){
    if(!active()||mState==State::Stopping||!mSocket||mPending.size()>=32)return 0;
    const int id=mSequence;mPending.insert(id,{command,mClock.elapsed()+timeout,mGeneration,false});
    if(!send({{"type","request"},{"command",command},{"arguments",arguments}})){mPending.remove(id);return 0;}return id;
}
void RustDebugSession::cancel(int id){
    if(!mPending.contains(id))return;
    mPending.remove(id);if(active())send({{"type","request"},{"command","cancel"},{"arguments",QJsonObject{{"requestId",id}}}});
}
void RustDebugSession::read(){
    if(!mSocket)return;
    mInput+=mSocket->readAll();if(mInput.size()>MaxMessage+8192){terminate(tr("Messaggio debugger troppo grande."));return;}
    if(!mParse.isActive())mParse.start();
}
void RustDebugSession::parse(){
    QElapsedTimer budget;budget.start();int count=0;
    while(active()&&count<16&&budget.elapsed()<5){
        if(mExpected<0){
            const auto end=mInput.indexOf("\r\n\r\n");if(end<0){if(mInput.size()>8192)terminate(tr("Intestazione debugger troppo lunga."));return;}
            if(end>8192){terminate(tr("Intestazione debugger troppo lunga."));return;}
            qint64 length=-1;
            for(auto line:mInput.left(end).split('\n')){
                line=line.trimmed();const auto colon=line.indexOf(':');if(colon<=0)continue;
                const auto name=line.left(colon).toLower(),value=line.mid(colon+1).trimmed();
                if(name=="content-length"){
                    bool ok=false;const auto size=value.toLongLong(&ok);
                    if(!ok||size<=0||size>MaxMessage||length!=-1){terminate(tr("Lunghezza del messaggio debugger non valida."));return;}length=size;
                }else if(name=="auth-token"){
                    if(QString::fromUtf8(value)!=mToken){terminate(tr("Autenticazione del debugger fallita."));return;}mAuthenticated=true;
                }
            }
            if(length<0||!mAuthenticated){terminate(tr("Messaggio debugger non autenticato."));return;}
            mExpected=length;mInput.remove(0,end+4);
        }
        if(mInput.size()<mExpected)return;
        const auto body=mInput.left(mExpected);mInput.remove(0,mExpected);mExpected=-1;
        QJsonParseError error;const auto json=QJsonDocument::fromJson(body,&error);
        if(error.error!=QJsonParseError::NoError||!json.isObject()){terminate(tr("JSON debugger non valido."));return;}
        handle(json.object());++count;
    }
    if((mExpected<0&&mInput.contains("\r\n\r\n"))||(mExpected>=0&&mInput.size()>=mExpected))mParse.start();
}
void RustDebugSession::handle(const QJsonObject &message){
    const auto type=message.value("type").toString(),command=message.value("command").toString();
    if(type=="request"){
        const int sequence=message.value("seq").toInt();const auto args=message.value("arguments").toObject();
        if(command!="runInTerminal"||mReverseTerminal||mTerminal.active()){
            send({{"type","response"},{"request_seq",sequence},{"command",command},{"success",false},{"message","Unsupported reverse request"}});return;
        }
        const auto values=args.value("args").toArray();QStringList arguments;
        if(values.isEmpty()||values.size()>256){terminate(tr("Comando del terminale non valido."));return;}
        for(const auto &value:values){if(!value.isString()||value.toString().size()>8192){terminate(tr("Argomento del terminale non valido."));return;}arguments.append(value.toString());}
        const auto helper=QDir(QFileInfo(mAdapterPath).absolutePath()).filePath("../bin/codelldb-launch.exe");
        const auto legacyHelper=mAdapterPath;
#ifdef Q_OS_WIN
        const auto sensitivity=Qt::CaseInsensitive;
#else
        const auto sensitivity=Qt::CaseSensitive;
#endif
        const auto requested=QFileInfo(arguments.takeFirst()).canonicalFilePath();
        if(requested.isEmpty()||(requested.compare(QFileInfo(helper).canonicalFilePath(),sensitivity)!=0&&requested.compare(QFileInfo(legacyHelper).canonicalFilePath(),sensitivity)!=0)){
            terminate(tr("CodeLLDB ha richiesto un eseguibile del terminale non riconosciuto."));return;
        }
        auto env=mEnvironment;
        const auto environment=args.value("env").toObject();
        for(auto it=environment.begin();it!=environment.end();++it){if(it.value().isNull())env.remove(it.key());else if(it.value().isString())env.insert(it.key(),it.value().toString());else{terminate(tr("Ambiente del terminale non valido."));return;}}
        const auto cwd=args.value("cwd").toString();mReverseTerminal=sequence;
        if(!mTerminal.start(requested,arguments,cwd.isEmpty()?mDirectory:cwd,env))terminate(tr("Impossibile avviare il terminale di debug."));return;
    }
    if(type=="event"){
        const auto name=message.value("event").toString();const auto body=message.value("body").toObject();
        if(name=="initialized")configure();
        else if(name=="stopped"){
            ++mGeneration;mThread=body.value("threadId").toInt();setState(State::Stopped);
            clearRunTo();
            emit stopped(mThread,body.value("reason").toString());
        }
        else if(name=="continued"){++mGeneration;setState(State::Running);}
        else if(name=="exited")mExitCode=body.value("exitCode").toInt();
        else if(name=="terminated"){stop();}
        else if(name=="output")emit output(body.value("output").toString().left(65536));
        else if(name=="capabilities"){const auto next=body.value("capabilities").toObject();for(auto it=next.begin();it!=next.end();++it)mCapabilities.insert(it.key(),it.value());}
        emit event(name,body);return;
    }
    if(type!="response")return;
    const int id=message.value("request_seq").toInt();if(!mPending.contains(id))return;
    const auto pending=mPending.take(id);const bool success=message.value("success").toBool();const auto body=message.value("body").toObject();
    const auto failureMessage=message.value("message").toString();
    if(pending.command=="initialize"){
        if(!success){terminate(tr("Inizializzazione CodeLLDB fallita: %1").arg(failureMessage));return;}
        mCapabilities=body;QJsonArray args;for(const auto &value:mArguments)args.append(value);
        QJsonObject env;for(const auto &key:mEnvironment.keys())env.insert(key,mEnvironment.value(key));
        request("launch",{{"program",mProgram},{"cwd",mDirectory},{"args",args},{"env",env},{"sourceLanguages",QJsonArray{"rust"}},
            {"terminal","integrated"},{"stopOnEntry",mStopOnEntry}},30000);
    }else if(pending.command=="launch"){
        if(!success){terminate(tr("Avvio del programma fallito: %1").arg(failureMessage));return;}
        if(mState==State::Starting)setState(State::Running);
    }
    if(pending.command=="configurationDone"&&!success){terminate(tr("Configurazione del debugger fallita: %1").arg(failureMessage));return;}
    if(!success&&QSet<QString>{"continue","next","stepIn","stepOut"}.contains(pending.command)&&pending.generation==mGeneration){
        setState(State::Stopped);emit error(tr("Comando debugger rifiutato: %1").arg(failureMessage));emit stopped(mThread,"command-failed");
    }
    if(!success&&pending.command=="pause")emit error(tr("Il debugger non può interrompere il programma: %1").arg(failureMessage));
    if(id==mRunToRequest){
        const auto breakpoints=body.value("breakpoints").toArray();
        const auto target=breakpoints.isEmpty()?QJsonObject{}:breakpoints.last().toObject();
        mRunToRequest=0;
        if(!success||!target.value("verified").toBool()){
            const auto message=target.value("message").toString(failureMessage);
            clearRunTo();
            emit error(tr("Impossibile eseguire fino al cursore: %1").arg(message.isEmpty()?tr("la riga non corrisponde a codice eseguibile"):message));
            if(pending.configuration){terminate();return;}
        }else{
            const auto line=target.value("line").toInt(mRunToLine);
            if(line!=mRunToLine)emit output(tr("Il debugger ha spostato la destinazione dalla riga %1 alla riga eseguibile %2.\n").arg(mRunToLine).arg(line));
            if(!pending.configuration)this->command("continue");
        }
    }
    if(pending.configuration){
        if(!success){terminate(tr("Configurazione dei breakpoint fallita: %1").arg(failureMessage));return;}
        --mConfigurationPending;pumpConfiguration();
    }
    static const QSet<QString> queries{"stackTrace","scopes","variables","evaluate","disassemble","readMemory","threads","dataBreakpointInfo"};
    pumpBreakpointUpdates();
    if(queries.contains(pending.command)&&pending.generation!=mGeneration)return;
    if(pending.command=="setBreakpoints"||pending.command=="setFunctionBreakpoints"){
        if(!success)emit error(tr("Breakpoint: %1").arg(failureMessage));
        else for(const auto &value:body.value("breakpoints").toArray()){
            const auto breakpoint=value.toObject();if(!breakpoint.value("verified").toBool())emit error(tr("Breakpoint non attivo: %1").arg(breakpoint.value("message").toString()));
        }
    }
    emit reply(id,pending.command,body,success,failureMessage);
}
void RustDebugSession::configure(){
    if(mConfigured)return;mConfigured=true;mConfigurationPending=0;
    mConfigurationFiles=mBreakpoints.keys();mConfigurationFiles.sort();
    if(!mRunToFile.isEmpty()&&!mConfigurationFiles.contains(mRunToFile))mConfigurationFiles.append(mRunToFile);
    request("setExceptionBreakpoints",{{"filters",QJsonArray{"rust_panic"}}});
    if(mCapabilities.value("supportsFunctionBreakpoints").toBool()){
        const int id=request("setFunctionBreakpoints",{{"breakpoints",mFunctionBreakpoints}});
        if(id){mPending[id].configuration=true;++mConfigurationPending;}
    }
    pumpConfiguration();
}
void RustDebugSession::pumpConfiguration(){
    while(!mConfigurationFiles.isEmpty()&&mPending.size()<28){
        const auto file=mConfigurationFiles.takeFirst();const int id=request("setBreakpoints",{{"source",QJsonObject{{"path",file}}},{"breakpoints",effectiveBreakpoints(file)}});
        if(file==mRunToFile)mRunToRequest=id;
        if(id){mPending[id].configuration=true;++mConfigurationPending;}else{terminate(tr("Impossibile configurare i breakpoint."));return;}
    }
    if(mConfigurationFiles.isEmpty()&&!mConfigurationPending)finishConfiguration();
}
void RustDebugSession::finishConfiguration(){request("configurationDone");}
void RustDebugSession::setBreakpoints(const QString &file,const QJsonArray &values){
    const auto path=QFileInfo(file).absoluteFilePath();mBreakpoints.insert(path,values);
    if(active()&&mConfigured&&mState!=State::Stopping){mDirtyBreakpointFiles.insert(path);pumpBreakpointUpdates();}
}
void RustDebugSession::pumpBreakpointUpdates(){
    if(!mConfigured||(mState!=State::Running&&mState!=State::Stopped))return;
    while(!mDirtyBreakpointFiles.isEmpty()&&mPending.size()<24){
        const auto file=*mDirtyBreakpointFiles.begin();
        if(!request("setBreakpoints",{{"source",QJsonObject{{"path",file}}},{"breakpoints",effectiveBreakpoints(file)}}))return;
        mDirtyBreakpointFiles.remove(file);
    }
}
void RustDebugSession::setFunctionBreakpoints(const QJsonArray &values){
    mFunctionBreakpoints=values;
    if(active()&&mConfigured&&mState!=State::Stopping&&mCapabilities.value("supportsFunctionBreakpoints").toBool())
        request("setFunctionBreakpoints",{{"breakpoints",values}});
}
void RustDebugSession::command(const QString &command){
    if(command=="pause"&&mState==State::Running){request(command,{{"threadId",mThread}});return;}
    if(mState!=State::Stopped)return;
    if(QSet<QString>{"continue","next","stepIn","stepOut"}.contains(command)){
        ++mGeneration;
        if(request(command,{{"threadId",mThread}}))setState(State::Running);
        else emit error(tr("Il debugger non può accettare il comando; il programma resta in pausa."));
    }
}
void RustDebugSession::runTo(const QString &file,int zeroBasedLine){
    if(mState!=State::Stopped||zeroBasedLine<0||mRunToRequest)return;
    if(!QFileInfo(file).isFile()){emit error(tr("Il sorgente di destinazione non esiste."));return;}
    mRunToFile=QFileInfo(file).absoluteFilePath();mRunToLine=zeroBasedLine+1;
    mRunToRequest=request("setBreakpoints",{{"source",QJsonObject{{"path",mRunToFile}}},{"breakpoints",effectiveBreakpoints(mRunToFile)}});
    if(!mRunToRequest){clearRunTo();emit error(tr("Il debugger non può accettare la destinazione; riprova quando termina la richiesta corrente."));}
}
QJsonArray RustDebugSession::effectiveBreakpoints(const QString &file)const{
    auto values=mBreakpoints.value(file);
    if(file==mRunToFile)values.append(QJsonObject{{"line",mRunToLine}});
    return values;
}
void RustDebugSession::clearRunTo(){
    if(mRunToFile.isEmpty())return;
    const auto file=mRunToFile;mRunToFile.clear();mRunToRequest=0;mRunToLine=0;
    setBreakpoints(file,mBreakpoints.value(file));
}
bool RustDebugSession::sendInput(const QString &text){return active()&&mTerminal.write((text+'\r').toUtf8());}
void RustDebugSession::stop(){
    if(!active()||mState==State::Stopping)return;
    send({{"type","request"},{"command","disconnect"},{"arguments",QJsonObject{{"terminateDebuggee",true}}}});
    setState(State::Stopping);mPending.clear();mServer.close();mTerminal.stop();mForceStop.start();
}
void RustDebugSession::terminate(const QString &message){
    if(!active())return;if(!message.isEmpty())emit error(message);stop();
    if(mAdapter.state()==QProcess::NotRunning)complete();
}
void RustDebugSession::complete(){
    if(mCompleting||mState==State::Idle)return;mCompleting=true;
    mForceStop.stop();mParse.stop();mServer.close();mPending.clear();mReverseTerminal=0;mTerminal.stop();
    if(mSocket){disconnect(mSocket,nullptr,this,nullptr);mSocket->abort();mSocket->deleteLater();mSocket=nullptr;}
    mInput.clear();mExpected=-1;mRunToFile.clear();mRunToRequest=0;mDirtyBreakpointFiles.clear();releaseJob();setState(State::Idle);emit finished(mExitCode);mCompleting=false;
}
