// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "rustlspclient.h"
#include "rusttools.h"
#include "rustworkspace.h"
#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcessEnvironment>
#include <QCoreApplication>
#include <QSet>
#include <algorithm>

namespace {
constexpr qint64 MaxFrame=16*1024*1024;
constexpr qsizetype MaxDocument=4*1024*1024; // UTF-16 units
constexpr qsizetype MaxDocuments=128;
QString absolute(const QString &path){return QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());}
QProcessEnvironment toolEnvironment(){
    auto env=QProcessEnvironment::systemEnvironment();
    QStringList directories;
    for(const auto &name:QStringList{"rustc","cargo","rust-analyzer"}){
        const auto path=RustTools::effectivePath(name);
        if(!path.isEmpty())directories.append(QFileInfo(path).absolutePath());
    }
    directories.removeDuplicates();directories.append(env.value("PATH"));
    env.insert("PATH",directories.join(QDir::listSeparator()));
    const auto rustc=RustTools::effectivePath("rustc");
    if(!rustc.isEmpty())env.insert("RUSTC",rustc);
    const auto cargo=RustTools::effectivePath("cargo");
    if(!cargo.isEmpty())env.insert("CARGO",cargo);
    env.insert("CARGO_TERM_COLOR","never");
    return env;
}
}
QString RustLspClient::uri(const QString &path){return QUrl::fromLocalFile(absolute(path)).toString(QUrl::FullyEncoded);}
RustLspClient::RustLspClient(QObject *parent):QObject(parent){
    mClock.start();mFailuresWindow.start();
    mRestart.setSingleShot(true);mFlush.setSingleShot(true);mFlush.setInterval(180);
    mConfiguration.setSingleShot(true);mConfiguration.setInterval(500);
    mParse.setSingleShot(true);mParse.setInterval(0);
    mWatchdog.setInterval(500);mWatchdog.start();
    mProcess.setProcessChannelMode(QProcess::SeparateChannels);
    mProbe.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&mRestart,&QTimer::timeout,this,&RustLspClient::ensureStarted);
    connect(&mFlush,&QTimer::timeout,this,&RustLspClient::flushDocuments);
    connect(&mConfiguration,&QTimer::timeout,this,&RustLspClient::updateConfiguration);
    connect(&mParse,&QTimer::timeout,this,&RustLspClient::parseFrames);
    connect(&mProcess,&QProcess::readyReadStandardOutput,this,&RustLspClient::readOutput);
    connect(&mProcess,&QProcess::readyReadStandardError,this,[this]{mProcess.readAllStandardError();});
    connect(&mProcess,&QProcess::started,this,&RustLspClient::initialize);
    connect(&mProcess,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
        if(error==QProcess::FailedToStart||error==QProcess::ReadError||error==QProcess::WriteError)
            failed(tr("rust-analyzer: %1").arg(mProcess.errorString()));
    });
    connect(&mProcess,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int,QProcess::ExitStatus){
        if(!mStopping&&!mRecovering)failed(tr("rust-analyzer si è arrestato; recupero della sessione."));
        releaseJob();
    });
    connect(&mProbe,&QProcess::readyReadStandardOutput,this,[this]{
        mProbeOutput+=mProbe.readAllStandardOutput();
        if(mProbeOutput.size()>65536){mProbeOutput.clear();mProbe.kill();}
    });
    connect(&mProbe,&QProcess::readyReadStandardError,this,[this]{mProbe.readAllStandardError();});
    connect(&mProbe,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
        if(error==QProcess::FailedToStart&&mStarting&&!mStopping)startServer();
    });
    connect(&mProbe,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus exit){
        if(!mStarting||mStopping)return;
        if(code==0&&exit==QProcess::NormalExit){
            const auto lines=QString::fromUtf8(mProbeOutput).trimmed().split('\n');
            if(!lines.isEmpty()&&QFileInfo(lines.first().trimmed()).isDir()){
                mSysroot=QDir::fromNativeSeparators(lines.first().trimmed());mCfg={};
                for(int i=1;i<lines.size();++i)mCfg.append(lines[i].trimmed());
            }
        }
        startServer();
    });
    connect(&mWatchdog,&QTimer::timeout,this,[this]{
        if(mStarting&&mProbe.state()!=QProcess::NotRunning&&mClock.elapsed()>mProbeDeadline)mProbe.kill();
        if(mStarting&&mProbe.state()==QProcess::NotRunning&&mProcess.state()==QProcess::Starting&&mClock.elapsed()>mProbeDeadline){
            failed(tr("rust-analyzer: avvio del processo scaduto."));return;
        }
        const auto keys=mRequests.keys();
        for(int id:keys){
            if(!mRequests.contains(id)||mRequests[id].deadline>mClock.elapsed())continue;
            if(mRequests[id].method=="initialize"){failed(tr("rust-analyzer: inizializzazione scaduta."));break;}
            const auto pending=mRequests.take(id);notify("$/cancelRequest",{{"id",id}});
            emit result(id,pending.method,pending.path,pending.version,QJsonValue());
        }
    });
}
RustLspClient::~RustLspClient(){
    mRestart.stop();mWatchdog.stop();mFlush.stop();mConfiguration.stop();mParse.stop();
    if(mReady){send({{"id",mNextId++},{"method","shutdown"},{"params",QJsonValue::Null}});notify("exit",{});}
    mStopping=true;
    mProcess.closeWriteChannel();
    if(mProcess.state()!=QProcess::NotRunning&&!mProcess.waitForFinished(300))mProcess.kill();
    stopProcesses();mProcess.waitForFinished(700);mProbe.waitForFinished(300);releaseJob();
}
int RustLspClient::version(const QString &path)const{const auto it=mDocuments.constFind(uri(path));return it==mDocuments.cend()?0:it->version;}
int RustLspClient::updateDocument(const QString &path,const QString &text){
    if(path.isEmpty()||text.size()>MaxDocument){
        if(!path.isEmpty())closeDocument(path);
        emit status(tr("Analisi Rust sospesa: documento oltre 8 MiB o non salvato."));return 0;
    }
    const auto key=uri(path);const auto full=absolute(path);
    qsizetype total=text.size();for(auto it=mDocuments.cbegin();it!=mDocuments.cend();++it)if(it.key()!=key)total+=it->text.size();
    if(total>32*1024*1024){closeDocument(path);emit status(tr("Limite di memoria dei documenti Rust analizzati raggiunto."));return 0;}
    if(!mDocuments.contains(key)){
        if(mDocuments.size()>=MaxDocuments){emit status(tr("Limite di 128 documenti Rust analizzati raggiunto."));return 0;}
        mDocuments.insert(key,{full,text,1,-1});mConfiguration.start();
    }else{
        auto &doc=mDocuments[key];
        if(doc.text!=text){doc.text=text;++doc.version;emit diagnostics(doc.path,doc.version,{});}
    }
    if(!mRestart.isActive())ensureStarted();
    mFlush.start();return mDocuments[key].version;
}
void RustLspClient::closeDocument(const QString &path){
    const auto key=uri(path);if(!mDocuments.contains(key))return;
    if(mReady&&mDocuments[key].sentVersion>=0)notify("textDocument/didClose",{{"textDocument",QJsonObject{{"uri",key}}}});
    mDocuments.remove(key);mConfiguration.start();
    const auto ids=mRequests.keys();for(int id:ids)if(mRequests[id].path==absolute(path))cancel(id);
    if(mDocuments.isEmpty()){
        mRestart.stop();mConfiguration.stop();mStarting=false;mReady=false;mRecovering=true;
        mInput.clear();mExpected=-1;mFailures=0;mFailuresWindow.restart();
        clearPending();emit readyChanged(false);stopProcesses();emit status(tr("rust-analyzer: in attesa di un file Rust"));
    }
}
void RustLspClient::savedDocument(const QString &path){
    flushDocuments();if(mReady&&mDocuments.contains(uri(path)))notify("textDocument/didSave",{{"textDocument",QJsonObject{{"uri",uri(path)}}}});
}
void RustLspClient::ensureStarted(){
    if(mStopping||mDocuments.isEmpty()||mStarting||mReady)return;
    if(mProcess.state()!=QProcess::NotRunning||mProbe.state()!=QProcess::NotRunning){if(mRecovering)mRestart.start(300);return;}
    if(mFailures>=3)return;
    mRecovering=false;mStarting=true;mProbeOutput.clear();mSysroot.clear();mCfg={};
    emit status(tr("rust-analyzer: avvio dell'analisi…"));
    const auto rustc=RustTools::effectivePath("rustc");
    if(rustc.isEmpty()){startServer();return;}
    mProbe.setProcessEnvironment(toolEnvironment());mProbeDeadline=mClock.elapsed()+5000;
    mProbe.setWorkingDirectory(QFileInfo(mDocuments.cbegin()->path).absolutePath());
    mProbe.start(rustc,{"--print","sysroot","--print","cfg"});
}
bool RustLspClient::prepareJob(){
#ifdef Q_OS_WIN
    mJob=CreateJobObjectW(nullptr,nullptr);if(!mJob)return false;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};info.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(mJob,JobObjectExtendedLimitInformation,&info,sizeof(info))){releaseJob();return false;}
    SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);mAttributeStorage.resize(bytes);mStartup={};
    mStartup.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(mAttributeStorage.data());
    if(!InitializeProcThreadAttributeList(mStartup.lpAttributeList,1,0,&bytes)){mStartup.lpAttributeList=nullptr;releaseJob();return false;}
    if(!UpdateProcThreadAttribute(mStartup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_JOB_LIST,&mJob,sizeof(mJob),nullptr,nullptr)){releaseJob();return false;}
    mProcess.setCreateProcessArgumentsModifier([this](QProcess::CreateProcessArguments *args){
        mStartup.StartupInfo=*reinterpret_cast<STARTUPINFOW *>(args->startupInfo);mStartup.StartupInfo.cb=sizeof(mStartup);
        args->startupInfo=reinterpret_cast<Q_STARTUPINFO *>(&mStartup);args->flags|=EXTENDED_STARTUPINFO_PRESENT;
    });
#endif
    return true;
}
void RustLspClient::releaseJob(){
#ifdef Q_OS_WIN
    if(mStartup.lpAttributeList){DeleteProcThreadAttributeList(mStartup.lpAttributeList);mStartup.lpAttributeList=nullptr;}
    if(mJob){CloseHandle(mJob);mJob=nullptr;}mAttributeStorage.clear();
#endif
}
void RustLspClient::stopProcesses(){
    mProbe.kill();
#ifdef Q_OS_WIN
    if(mJob)TerminateJobObject(mJob,1);
#endif
    mProcess.kill();
}
void RustLspClient::startServer(){
    if(mStopping||!mStarting||mProcess.state()!=QProcess::NotRunning)return;
    if(mDocuments.isEmpty()){mStarting=false;return;}
    const auto tool=RustTools::effectivePath("rust-analyzer");
    if(tool.isEmpty()||!QFileInfo(tool).isExecutable()){failed(tr("rust-analyzer non trovato. Configuralo in Rust → Strumenti del compilatore."));return;}
    releaseJob();if(!prepareJob()){failed(tr("Impossibile proteggere il processo rust-analyzer."));return;}
    mInput.clear();mExpected=-1;mProcess.setProcessEnvironment(toolEnvironment());
    mProcess.setWorkingDirectory(QFileInfo(mDocuments.cbegin()->path).absolutePath());
    mProcess.start(tool,{});
    // FailedToStart and initialize watchdog cover startup without blocking the UI.
    mProbeDeadline=mClock.elapsed()+10000;
}
QJsonObject RustLspClient::configuration()const{
    QJsonArray projects;QSet<QString> roots;QJsonArray standalone;
    auto documents=mDocuments.keys();std::sort(documents.begin(),documents.end());
    for(const auto &key:documents){
        const auto &doc=mDocuments[key];
        // F12 can open std/core sources already loaded through the sysroot.
        // Do not turn those crates into independent Cargo workspaces.
#ifdef Q_OS_WIN
        const auto sensitivity=Qt::CaseInsensitive;
#else
        const auto sensitivity=Qt::CaseSensitive;
#endif
        if(!mSysroot.isEmpty()&&doc.path.startsWith(mSysroot+"/",sensitivity))continue;
        const auto root=RustWorkspace::findRoot(doc.path);
        if(!root.isEmpty())roots.insert(QDir(root).filePath("Cargo.toml"));
        else standalone.append(QJsonObject{{"root_module",doc.path},{"edition",RustTools::edition()},
            {"deps",QJsonArray{}},{"cfg",mCfg},{"is_workspace_member",true}});
    }
    auto manifests=roots.values();std::sort(manifests.begin(),manifests.end());
    for(const auto &manifest:manifests)projects.append(manifest);
    if(!standalone.isEmpty()){
        QJsonObject project{{"crates",standalone}};
        if(!mSysroot.isEmpty()){
            project.insert("sysroot",mSysroot);
            const auto library=QDir(mSysroot).filePath("lib/rustlib/src/rust/library");
            if(QFileInfo::exists(QDir(library).filePath("std/src/lib.rs")))project.insert("sysroot_src",library);
        }
        projects.append(project);
    }
    return {{"linkedProjects",projects},{"checkOnSave",false},{"numThreads",2},
        {"cachePriming",QJsonObject{{"numThreads",2}}},
        {"completion",QJsonObject{{"limit",250}}},
        {"diagnostics",QJsonObject{{"experimental",QJsonObject{{"enable",true}}}}}};
}
void RustLspClient::initialize(){
    if(mDocuments.isEmpty()){mStarting=false;stopProcesses();return;}
    QJsonObject item{{"snippetSupport",false},{"insertReplaceSupport",false},{"deprecatedSupport",true},
        {"resolveSupport",QJsonObject{{"properties",QJsonArray{"additionalTextEdits"}}}}};
    QJsonObject text{{"synchronization",QJsonObject{{"didSave",true}}},
        {"completion",QJsonObject{{"completionItem",item},{"contextSupport",true}}},
        {"hover",QJsonObject{{"contentFormat",QJsonArray{"plaintext"}}}},
        {"publishDiagnostics",QJsonObject{{"versionSupport",true}}}};
    QJsonObject caps{{"general",QJsonObject{{"positionEncodings",QJsonArray{"utf-16"}}}},
        {"textDocument",text},{"workspace",QJsonObject{{"configuration",true},{"applyEdit",false}}},
        {"experimental",QJsonObject{{"serverStatusNotification",true}}}};
    const auto root=QFileInfo(mDocuments.cbegin()->path).absolutePath();
    mLastConfiguration=configuration();
    request("initialize",{{"processId",qint64(QCoreApplication::applicationPid())},
        {"clientInfo",QJsonObject{{"name","Red Panda Rust"},{"version",QStringLiteral(REDPANDA_CPP_VERSION)}}},
        {"rootUri",uri(root)},{"capabilities",caps},{"initializationOptions",mLastConfiguration}},{},30000);
}
bool RustLspClient::send(QJsonObject message){
    if(mProcess.state()!=QProcess::Running||mStopping||mRecovering)return false;
    message.insert("jsonrpc","2.0");const auto body=QJsonDocument(message).toJson(QJsonDocument::Compact);
    if(body.size()>MaxFrame||mProcess.bytesToWrite()+body.size()>32*1024*1024){failed(tr("rust-analyzer: coda di comunicazione troppo grande."));return false;}
    const auto frame="Content-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body;
    if(mProcess.write(frame)!=frame.size()){failed(tr("rust-analyzer: invio incompleto."));return false;}
    return true;
}
void RustLspClient::notify(const QString &method,const QJsonObject &params){send({{"method",method},{"params",params}});}
int RustLspClient::request(const QString &method,const QJsonObject &params,const QString &path,int timeout){
    if(mRequests.size()>=32)return 0;
    const int id=mNextId++;const auto full=path.isEmpty()?QString():absolute(path);
    mRequests.insert(id,{method,full,full.isEmpty()?0:version(full),mClock.elapsed()+timeout});
    if(!send({{"id",id},{"method",method},{"params",params}})){mRequests.remove(id);return 0;}return id;
}
void RustLspClient::flushDocuments(){
    if(!mReady)return;
    const auto keys=mDocuments.keys();QElapsedTimer budget;budget.start();
    for(const auto &key:keys){
        if(mDocuments[key].sentVersion==mDocuments[key].version)continue;
        if(mProcess.bytesToWrite()>4*1024*1024||budget.elapsed()>5){mFlush.start(50);return;}
        if(!flushDocument(key))return;
    }
}
bool RustLspClient::flushDocument(const QString &key){
    if(!mReady||!mDocuments.contains(key))return false;
    auto &doc=mDocuments[key];if(doc.sentVersion==doc.version)return true;
    const int next=doc.version;const auto text=doc.text;const bool opened=doc.sentVersion>=0;
    doc.sentVersion=next; // send() may fail; failed() resets every sentVersion.
    if(!opened)notify("textDocument/didOpen",{{"textDocument",QJsonObject{{"uri",key},{"languageId","rust"},{"version",next},{"text",text}}}});
    else notify("textDocument/didChange",{{"textDocument",QJsonObject{{"uri",key},{"version",next}}},
        {"contentChanges",QJsonArray{QJsonObject{{"text",text}}}}});
    return mReady;
}
void RustLspClient::updateConfiguration(){
    if(!mReady)return;
    const auto next=configuration();if(next==mLastConfiguration)return;
    mLastConfiguration=next;
    notify("workspace/didChangeConfiguration",{{"settings",QJsonObject{{"rust-analyzer",next}}}});
}
int RustLspClient::requestAt(const QString &method,const QString &path,int line,int character){
    if(!mReady||!mDocuments.contains(uri(path))||line<0||character<0)return 0;
    if(!flushDocument(uri(path)))return 0;
    return request(method,{{"textDocument",QJsonObject{{"uri",uri(path)}}},
        {"position",QJsonObject{{"line",line},{"character",character}}}},path);
}
int RustLspClient::resolveCompletion(const QString &path,const QJsonObject &item){
    if(!mReady||!mDocuments.contains(uri(path)))return 0;
    return request("completionItem/resolve",item,path);
}
void RustLspClient::cancel(int id){
    if(id&&mRequests.contains(id)){mRequests.remove(id);if(mReady)notify("$/cancelRequest",{{"id",id}});}
}
void RustLspClient::readOutput(){
    if(mRecovering||mStopping){mProcess.readAllStandardOutput();return;}
    while(mProcess.bytesAvailable()>0){
        mInput+=mProcess.read(65536);
        if(mInput.size()>MaxFrame+8192){failed(tr("rust-analyzer: messaggio oltre il limite di 16 MiB."));return;}
        if(!mParse.isActive())mParse.start();
        // Leave large bursts in QProcess and drain them on the next event turn.
        if(mInput.size()>MaxFrame/2)break;
    }
}
void RustLspClient::parseFrames(){
    QElapsedTimer budget;budget.start();int count=0;
    while(count<16&&budget.elapsed()<5){
        if(mExpected<0){
            const auto end=mInput.indexOf("\r\n\r\n");
            if(end<0){if(mInput.size()>8192)failed(tr("rust-analyzer: intestazione LSP non valida."));break;}
            if(end>8192){failed(tr("rust-analyzer: intestazione LSP troppo lunga."));return;}
            qint64 size=-1;
            for(auto line:mInput.left(end).split('\n')){
                line=line.trimmed();const auto separator=line.indexOf(':');
                if(separator>0&&line.left(separator).toLower()=="content-length"){
                    bool ok=false;const auto value=line.mid(separator+1).trimmed().toLongLong(&ok);
                    if(!ok||size!=-1||value<=0||value>MaxFrame){failed(tr("rust-analyzer: Content-Length non valido."));return;}size=value;
                }
            }
            if(size<0){failed(tr("rust-analyzer: Content-Length mancante."));return;}
            mExpected=size;mInput.remove(0,end+4);
        }
        if(mInput.size()<mExpected)break;
        const auto body=mInput.left(mExpected);mInput.remove(0,mExpected);mExpected=-1;
        QJsonParseError error;const auto parsed=QJsonDocument::fromJson(body,&error);
        if(error.error!=QJsonParseError::NoError||!parsed.isObject()){failed(tr("rust-analyzer: JSON non valido."));return;}
        handleMessage(parsed.object());++count;
        if(mRecovering||mStopping)return;
    }
    if(mProcess.bytesAvailable()>0)readOutput();
    if((mExpected>=0&&mInput.size()>=mExpected)||(mExpected<0&&mInput.contains("\r\n\r\n")))mParse.start();
}
void RustLspClient::handleMessage(const QJsonObject &message){
    if(message.value("jsonrpc")!="2.0"){failed(tr("rust-analyzer: versione JSON-RPC non valida."));return;}
    const auto method=message.value("method").toString();const auto params=message.value("params").toObject();
    if(!method.isEmpty()&&message.contains("id")){
        const auto id=message.value("id");QJsonValue answer=QJsonValue::Null;
        if(method=="workspace/configuration"){
            QJsonArray values;
            for(const auto &entry:params.value("items").toArray()){
                auto section=entry.toObject().value("section").toString();QJsonValue value=configuration();
                if(section.startsWith("rust-analyzer."))section=section.mid(14);
                else if(section=="rust-analyzer"||section.isEmpty())section.clear();
                else value=QJsonValue::Null;
                if(!section.isEmpty())for(const auto &part:section.split('.'))value=value.toObject().value(part);
                values.append(value.isUndefined()?QJsonValue(QJsonValue::Null):value);
            }answer=values;
        }else if(method=="workspace/applyEdit")answer=QJsonObject{{"applied",false},{"failureReason","Workspace edits are not supported"}};
        else if(method!="client/registerCapability"&&method!="client/unregisterCapability"&&method!="window/workDoneProgress/create"&&method!="window/showMessageRequest"){
            send({{"id",id},{"error",QJsonObject{{"code",-32601},{"message","Unsupported method"}}}});return;
        }
        send({{"id",id},{"result",answer}});return;
    }
    if(method=="textDocument/publishDiagnostics"){
        auto key=params.value("uri").toString();
#ifdef Q_OS_WIN
        if(!mDocuments.contains(key))for(const auto &candidate:mDocuments.keys())if(candidate.compare(key,Qt::CaseInsensitive)==0){key=candidate;break;}
#endif
        if(!mDocuments.contains(key))return;
        const auto &doc=mDocuments[key];
        if(doc.sentVersion!=doc.version)return;
        // Unversioned diagnostics cannot be proven current for an edited buffer.
        if(!params.value("version").isDouble()&&doc.version>1)return;
        if(params.value("version").isDouble()&&params.value("version").toInt(-1)!=doc.version)return;
        emit diagnostics(doc.path,doc.version,params.value("diagnostics").toArray());return;
    }
    if(method=="experimental/serverStatus"){
        const auto health=params.value("health").toString();
        const auto detail=params.value("message").toString().left(400);
        if(health!="ok"&&!detail.isEmpty())emit status(tr("rust-analyzer: %1").arg(detail));
        else emit status(params.value("quiescent").toBool()?tr("rust-analyzer: analisi semantica attiva"):tr("rust-analyzer: indicizzazione in corso…"));
        return;
    }
    if(method=="window/showMessage"){
        if(params.value("type").toInt()==1)emit status(tr("rust-analyzer: %1").arg(params.value("message").toString().left(300)));return;
    }
    if(!method.isEmpty())return;
    if(!message.value("id").isDouble())return;
    const int id=message.value("id").toInt();if(!mRequests.contains(id))return;
    const auto pending=mRequests.take(id);
    if(pending.method=="initialize"){
        if(message.contains("error")){failed(tr("rust-analyzer ha rifiutato l'inizializzazione."));return;}
        const auto capabilities=message.value("result").toObject().value("capabilities").toObject();
        const auto encoding=capabilities.value("positionEncoding").toString("utf-16");
        if(encoding!="utf-16"){failed(tr("rust-analyzer: codifica delle posizioni non supportata."));return;}
        if(!send({{"method","initialized"},{"params",QJsonObject{}}}))return;
        mReady=true;mStarting=false;flushDocuments();if(!mReady)return;emit readyChanged(true);
        emit status(tr("rust-analyzer: sessione pronta; indicizzazione in corso…"));return;
    }
    if(!pending.path.isEmpty()&&(!mDocuments.contains(uri(pending.path))||version(pending.path)!=pending.version))return;
    emit result(id,pending.method,pending.path,pending.version,message.value("result"));
}
void RustLspClient::clearPending(){
    const auto pending=mRequests;mRequests.clear();
    for(auto it=pending.cbegin();it!=pending.cend();++it)if(it.value().method!="initialize")
        emit result(it.key(),it.value().method,it.value().path,it.value().version,QJsonValue());
}
void RustLspClient::failed(const QString &reason){
    if(mStopping||mRecovering)return;
    mRecovering=true;mStarting=false;mReady=false;mFlush.stop();mParse.stop();mInput.clear();mExpected=-1;
    clearPending();emit readyChanged(false);
    for(auto it=mDocuments.begin();it!=mDocuments.end();++it){it->sentVersion=-1;emit diagnostics(it->path,it->version,{});}
    stopProcesses();
    if(mFailuresWindow.elapsed()>60000){mFailures=0;mFailuresWindow.restart();}
    ++mFailures;emit status(reason);
    if(mFailures<3&&!mDocuments.isEmpty())mRestart.start(1000*(1<<(mFailures-1)));
    else emit status(tr("rust-analyzer sospeso dopo tre errori. Usa Strumenti → Riavvia rust-analyzer."));
}
void RustLspClient::restart(){
    if(mStopping)return;
    mRecovering=true;mReady=false;mStarting=false;mFailures=0;mFailuresWindow.restart();
    mParse.stop();mInput.clear();mExpected=-1;
    clearPending();emit readyChanged(false);stopProcesses();
    for(auto it=mDocuments.begin();it!=mDocuments.end();++it){it->sentVersion=-1;emit diagnostics(it->path,it->version,{});}
    mRestart.start(300);
}
