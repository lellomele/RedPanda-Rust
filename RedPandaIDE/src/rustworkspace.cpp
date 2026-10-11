// Copyright (C) 2026 Raffaele Mele
// SPDX-License-Identifier: GPL-3.0-or-later
#include "rustworkspace.h"
#include "rusttools.h"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QProcessEnvironment>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSet>
RustWorkspace::RustWorkspace(QObject *parent):QObject(parent){
    mProcess.setProcessChannelMode(QProcess::MergedChannels);
    mStartTimer.setSingleShot(true);mStartTimer.setInterval(15000);
    mFinishTimer.setSingleShot(true);mFinishTimer.setInterval(2000);
    connect(&mStartTimer,&QTimer::timeout,this,[this]{emit output(tr("Avvio del processo scaduto.\n"));stop();});
    connect(&mFinishTimer,&QTimer::timeout,&mProcess,&QProcess::kill);
    connect(&mProcess,&QProcess::started,this,[this]{mStartTimer.stop();emit busyChanged(true);});
    connect(&mProcess,&QProcess::readyReadStandardOutput,this,&RustWorkspace::drain);
    connect(&mProcess,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
        emit output(tr("Errore del processo: %1\n").arg(mProcess.errorString()));
        if(error==QProcess::FailedToStart){mStartTimer.stop();releaseJob();emit busyChanged(false);emit finished(-1,mCancelled);}
    });
    connect(&mProcess,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status){
        mStartTimer.stop();mFinishTimer.stop();drain();
        if(!mPending.isEmpty()){handleLine(mPending);mPending.clear();}
        releaseJob();
        if(code==0 && status==QProcess::NormalExit && !mCancelled && !mRunAfterBuild.isEmpty()){
            const QString executable=mRunAfterBuild;mRunAfterBuild.clear();mDirectDiagnostics=false;
            if(launch(executable,{},false))return;
            emit busyChanged(false);emit finished(-1,false);return;
        }
        mRunAfterBuild.clear();
        emit output(tr("\nProcesso terminato: %1%2\n").arg(code).arg(mCancelled?tr(" (annullato)"):(status==QProcess::CrashExit?tr(" (arresto anomalo)"):QString())));
        emit busyChanged(false);emit finished(status==QProcess::CrashExit?-1:code,mCancelled);
    });
}
RustWorkspace::~RustWorkspace(){stop();if(busy())mProcess.waitForFinished(2000);releaseJob();}
QString RustWorkspace::findTool(const QString &name){
    return RustTools::effectivePath(name);
}
QString RustWorkspace::findRoot(const QString &file){
    QDir directory(QFileInfo(file).isDir()?file:QFileInfo(file).absolutePath());
    do{if(QFileInfo(directory.filePath("Cargo.toml")).isFile())return directory.absolutePath();}while(directory.cdUp());
    return {};
}
bool RustWorkspace::prepareJob(){
#ifdef Q_OS_WIN
    mJob=CreateJobObjectW(nullptr,nullptr);if(!mJob)return false;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    info.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(mJob,JobObjectExtendedLimitInformation,&info,sizeof(info))){releaseJob();return false;}
    SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);
    mAttributeStorage.resize(bytes);mStartup={};
    mStartup.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(mAttributeStorage.data());
    if(!InitializeProcThreadAttributeList(mStartup.lpAttributeList,1,0,&bytes)){mStartup.lpAttributeList=nullptr;releaseJob();return false;}
    if(!UpdateProcThreadAttribute(mStartup.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_JOB_LIST,&mJob,sizeof(mJob),nullptr,nullptr)){releaseJob();return false;}
    mProcess.setCreateProcessArgumentsModifier([this](QProcess::CreateProcessArguments *args){
        mStartup.StartupInfo=*reinterpret_cast<STARTUPINFOW *>(args->startupInfo);
        mStartup.StartupInfo.cb=sizeof(mStartup);args->startupInfo=reinterpret_cast<Q_STARTUPINFO *>(&mStartup);
        args->flags|=EXTENDED_STARTUPINFO_PRESENT;
    });
#endif
    return true;
}
void RustWorkspace::releaseJob(){
#ifdef Q_OS_WIN
    if(mStartup.lpAttributeList){DeleteProcThreadAttributeList(mStartup.lpAttributeList);mStartup.lpAttributeList=nullptr;}
    if(mJob){CloseHandle(mJob);mJob=nullptr;}
    mAttributeStorage.clear();
#endif
}
bool RustWorkspace::start(const QString &root,const QString &command,const QStringList &extra){
    if(busy())return false;
    const QSet<QString> allowed={"build","check","run","test","clippy","fmt","new"};
    if(!allowed.contains(command)){emit output(tr("Comando Cargo non valido.\n"));return false;}
    const QString cargo=findTool("cargo");
    if(cargo.isEmpty()){emit output(tr("Cargo non trovato. Installare Rust da rustup.rs.\n"));return false;}
    mRoot=QDir(root).absolutePath();mCommand=command;
    if(!QFileInfo(mRoot).isDir()||(command!="new"&&!QFileInfo(QDir(mRoot).filePath("Cargo.toml")).isFile())){
        emit output(tr("Aprire un progetto contenente Cargo.toml.\n"));return false;
    }
    mRunAfterBuild.clear();mDirectDiagnostics=false;
    QStringList args{command};
    if(command!="new")args<<"--manifest-path"<<QDir(mRoot).filePath("Cargo.toml");
    if(command!="fmt"&&command!="new")args<<"--message-format=json";
    args<<extra;
    return launch(cargo,args,true);
}
bool RustWorkspace::launch(const QString &program,const QStringList &args,bool diagnostics){
    releaseJob();if(!prepareJob()){emit output(tr("Impossibile proteggere il processo e i processi figli.\n"));return false;}
    mBytes=0;mIssues=0;mPending.clear();mCancelled=false;mBuilding=diagnostics;mDecoder.resetState();
    QProcessEnvironment environment=QProcessEnvironment::systemEnvironment();
    environment.insert("PATH",QFileInfo(program).absolutePath()+QDir::listSeparator()+environment.value("PATH"));
    environment.insert("CARGO_TERM_COLOR","never");environment.insert("RUST_BACKTRACE","1");
    mProcess.setProcessEnvironment(environment);mProcess.setWorkingDirectory(mRoot);
    emit output(QFileInfo(program).fileName()+" "+args.join(' ')+"\n");
    mProcess.start(program,args);mStartTimer.start();emit busyChanged(true);return true;
}
bool RustWorkspace::startFile(const QString &file,const QString &command){
    if(busy())return false;
    const QFileInfo source(file);
    if(!source.isFile()||source.suffix().compare("rs",Qt::CaseInsensitive)!=0){emit output(tr("Salvare prima il file Rust (.rs).\n"));return false;}
    if(command=="clippy"){emit output(tr("Clippy richiede un progetto Cargo; per un file singolo usare Controlla.\n"));return false;}
    if(!QSet<QString>{"build","check","run","test","fmt"}.contains(command))return false;
    const QString tool=findTool(command=="fmt"?"rustfmt":"rustc");
    if(tool.isEmpty()){emit output(tr("Strumento Rust non trovato: %1.\n").arg(command=="fmt"?"rustfmt":"rustc"));return false;}
    mRoot=source.absolutePath();mCommand=command;mRunAfterBuild.clear();mDirectDiagnostics=command!="fmt";
    if(command=="fmt")return launch(tool,{"--edition",RustTools::edition(),source.absoluteFilePath()},false);
    mFileBuild=std::make_unique<QTemporaryDir>(QDir::tempPath()+"/redpanda-rust-file-XXXXXX");
    if(!mFileBuild->isValid()){emit output(tr("Impossibile creare la cartella temporanea di compilazione.\n"));return false;}
#ifdef Q_OS_WIN
    const QString executable=mFileBuild->filePath("program.exe");
#else
    const QString executable=mFileBuild->filePath("program");
#endif
    QStringList args{source.absoluteFilePath(),"--crate-name","redpanda_file","--edition",RustTools::edition(),"--error-format=json","-g","-o",command=="check"?mFileBuild->filePath("program.rmeta"):executable};
    if(command=="check")args<<"--emit=metadata";
    if(command=="test")args<<"--test";
    if(command=="run"||command=="test")mRunAfterBuild=executable;
    return launch(tool,args,true);
}
void RustWorkspace::stop(){
    if(!busy())return;mCancelled=true;
#ifdef Q_OS_WIN
    if(!mJob || !TerminateJobObject(mJob,1)){
        emit output(tr("Interruzione diretta del processo.\n"));mProcess.kill();
    }
#else
    mProcess.terminate();mFinishTimer.start();
#endif
}
void RustWorkspace::sendInput(const QString &text){
    if(!busy())return;
    const QByteArray bytes=(text+'\n').toUtf8();
    if(bytes.size()>65536||mProcess.bytesToWrite()+bytes.size()>65536){emit output(tr("Input troppo lungo o in attesa: riprovare.\n"));return;}
    if(mProcess.write(bytes)<0)emit output(tr("Invio dell'input fallito: %1\n").arg(mProcess.errorString()));
}
void RustWorkspace::closeInput(){if(busy())mProcess.closeWriteChannel();}
void RustWorkspace::drain(){
    const QByteArray bytes=mProcess.readAllStandardOutput();mBytes+=bytes.size();
    if(mBytes>16*1024*1024){mPending.clear();emit output(tr("Output superiore a 16 MiB; operazione annullata.\n"));stop();return;}
    if(!mBuilding){emit output(mDecoder(bytes));return;}
    mPending+=bytes;
    int offset=0,index;
    while((index=mPending.indexOf('\n',offset))>=0){
        handleLine(mPending.mid(offset,index-offset));offset=index+1;
        if(!mBuilding){emit output(mDecoder(mPending.mid(offset)));mPending.clear();return;}
    }
    mPending.remove(0,offset);
    if(mPending.size()>1024*1024){emit output(tr("Riga di output superiore a 1 MiB; operazione annullata.\n"));mPending.clear();stop();}
}
void RustWorkspace::handleLine(const QByteArray &line){
    QJsonParseError error;const auto json=QJsonDocument::fromJson(line,&error);
    if(mBuilding&&error.error==QJsonParseError::NoError&&json.isObject()){
        const auto record=json.object();const auto reason=record.value("reason").toString();
        if(reason=="build-finished"){mBuilding=false;return;}
        if(reason=="compiler-artifact"||reason=="build-script-executed")return;
        if(reason=="compiler-message" || (mDirectDiagnostics && record.value("$message_type").toString()=="diagnostic")){
            const auto message=mDirectDiagnostics?record:record.value("message").toObject();
            emit output(message.value("rendered").toString(message.value("message").toString())+'\n');
            if(mIssues++>=10000)return;
            for(const auto &value:message.value("spans").toArray()){
                const auto span=value.toObject();if(!span.value("is_primary").toBool())continue;
                auto item=std::make_shared<CompileIssue>();
                item->filename=QDir::cleanPath(QDir::fromNativeSeparators(QDir(mRoot).absoluteFilePath(span.value("file_name").toString())));
                item->line=span.value("line_start").toInt();item->column=span.value("column_start").toInt();item->endColumn=span.value("column_end").toInt();
                item->description=message.value("message").toString();item->originalDescription=item->description;
                item->type=message.value("level").toString()=="error"?CompileIssueType::Error:CompileIssueType::Warning;
                if(item->line>0&&!item->filename.isEmpty())emit issue(item);
            }return;
        }
    }
    emit output(QString::fromUtf8(line)+'\n');
}
