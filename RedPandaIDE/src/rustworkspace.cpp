// Copyright (C) 2026 Raffaele Mele
// SPDX-License-Identifier: GPL-3.0-or-later
#include "rustworkspace.h"
#include "rustcoordinates.h"
#include <QPointer>
#include <QScopeGuard>
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
        const QPointer<RustWorkspace> self(this);const auto generation=mGeneration;
        if(error==QProcess::FailedToStart){mCompleting=true;mStartTimer.stop();releaseJob();}
        const auto cleanup=qScopeGuard([self,error]{if(self&&error==QProcess::FailedToStart)self->mCompleting=false;});
        emit output(tr("Errore del processo: %1\n").arg(mProcess.errorString()));if(!self||generation!=mGeneration)return;
        if(error==QProcess::FailedToStart){const bool cancelled=mCancelled;emit busyChanged(false);if(self&&generation==mGeneration)emit finished(-1,cancelled);}
        else if(error==QProcess::ReadError||error==QProcess::WriteError)stop();
    });
    connect(&mProcess,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status){
        const QPointer<RustWorkspace> self(this);const auto generation=mGeneration;mCompleting=true;
        const auto cleanup=qScopeGuard([self]{if(self)self->mCompleting=false;});
        mStartTimer.stop();mFinishTimer.stop();drain();if(!self||generation!=mGeneration)return;
        if(!mPending.isEmpty()){const auto pending=mPending;mPending.clear();handleLine(pending);if(!self||generation!=mGeneration)return;}
        releaseJob();
        if(code!=0||status!=QProcess::NormalExit||mCancelled)mArtifacts.clear();
        if(code==0&&status==QProcess::NormalExit&&!mCancelled&&!mExecuting&&mCommand=="run"&&mRunAfterBuild.isEmpty()){
            if(mArtifacts.size()==1)mRunAfterBuild=mArtifacts.first();
            else{emit output(tr("Selezionare un solo target eseguibile nelle opzioni Rust.\n"));if(!self)return;emit busyChanged(false);if(self)emit finished(-1,false);return;}
        }
        if(code==0 && status==QProcess::NormalExit && !mCancelled && !mRunAfterBuild.isEmpty()){
            const QString executable=mRunAfterBuild;mRunAfterBuild.clear();mDirectDiagnostics=false;
            mExecuting=true;if(launch(executable,mOptions.arguments,false)||!self)return;
            emit busyChanged(false);if(self)emit finished(-1,mCancelled);return;
        }
        mRunAfterBuild.clear();
        emit output(tr("\nProcesso terminato: %1%2\n").arg(code).arg(mCancelled?tr(" (annullato)"):(status==QProcess::CrashExit?tr(" (arresto anomalo)"):QString())));
        if(!self||generation!=mGeneration)return;const bool cancelled=mCancelled;
        emit busyChanged(false);if(self&&generation==mGeneration)emit finished(status==QProcess::CrashExit?-1:code,cancelled);
    });
}
RustWorkspace::~RustWorkspace(){disconnect(&mProcess,nullptr,this,nullptr);disconnect(this,nullptr,nullptr,nullptr);mStartTimer.stop();mFinishTimer.stop();
#ifdef Q_OS_WIN
    if(mJob)TerminateJobObject(mJob,1);
#endif
    mProcess.kill();mProcess.waitForFinished(700);releaseJob();}
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
    if(busy()||mPreparing||mCompleting)return false;
    const QPointer<RustWorkspace> self(this);mPreparing=true;++mGeneration;
    const auto cleanup=qScopeGuard([self]{if(self)self->mPreparing=false;});
    const QSet<QString> allowed={"build","check","run","test","clippy","fmt","new","doc","bench","clean","fetch","asm","llvm","debug-test","debug-build"};
    if(!allowed.contains(command)){emit output(tr("Comando Cargo non valido.\n"));return false;}
    const QString cargo=findTool("cargo");
    if(cargo.isEmpty()){emit output(tr("Cargo non trovato. Installare Rust da rustup.rs.\n"));return false;}
    mRoot=QDir(root).absolutePath();mCommand=command;
    if(!QFileInfo(mRoot).isDir()||(command!="new"&&!QFileInfo(QDir(mRoot).filePath("Cargo.toml")).isFile())){
        emit output(tr("Aprire un progetto contenente Cargo.toml.\n"));return false;
    }
    QString error;mOptions=RustProjectOptions::load(mRoot,&error);if(!error.isEmpty()){emit output(error+'\n');if(!self)return false;}
    if(command!="new"&&!mOptions.validate(mRoot,error)){emit output(error+'\n');return false;}
    mExecuting=false;mRunAfterBuild.clear();mDirectDiagnostics=false;
    mArtifacts.clear();
    const auto actual=(command=="run"||command=="debug-build")?QString("build"):(command=="asm"||command=="llvm")?QString("rustc"):command=="debug-test"?QString("test"):command;
    QStringList args{actual};
    if(command!="new")args<<"--manifest-path"<<QDir(mRoot).filePath("Cargo.toml");
    const bool diagnostics=QStringList{"build","check","run","test","clippy","bench","doc","asm","llvm","debug-test","debug-build"}.contains(command);
    if(diagnostics)args<<"--message-format=json";
    if(command!="new")args<<mOptions.cargoArguments(command);
    args<<extra;
    if(command=="debug-test")args<<"--no-run";
    if(command=="asm"||command=="llvm"){
        mFileBuild=std::make_unique<QTemporaryDir>(QDir::tempPath()+"/redpanda-rust-generated-XXXXXX");
        if(!mFileBuild->isValid()){emit output(tr("Impossibile creare la cartella degli output generati.\n"));return false;}
        const auto path=mFileBuild->filePath(command=="asm"?"output.s":"output.ll");mArtifacts.append(path);
        args<<"--"<<QString("--emit=%1=%2").arg(command=="asm"?"asm":"llvm-ir",path);
    }
    if((command=="test"||command=="bench")&&!mOptions.arguments.isEmpty())args<<"--"<<mOptions.arguments;
    return launch(cargo,args,diagnostics);
}
bool RustWorkspace::launch(const QString &program,const QStringList &args,bool diagnostics){
    releaseJob();if(!prepareJob()){emit output(tr("Impossibile proteggere il processo e i processi figli.\n"));return false;}
    mBytes=0;mIssues=0;mPending.clear();mCancelled=false;mBuilding=diagnostics;mDecoder.resetState();
    QProcessEnvironment environment=mOptions.processEnvironment();
    environment.insert("PATH",QFileInfo(program).absolutePath()+QDir::listSeparator()+environment.value("PATH"));
    environment.insert("CARGO_TERM_COLOR","never");environment.insert("RUST_BACKTRACE","1");
    if(mCommand=="debug-build"||mCommand=="debug-test"){environment.insert("CARGO_PROFILE_RELEASE_DEBUG","2");environment.insert("CARGO_PROFILE_DEV_DEBUG","2");}
    const auto rustc=findTool("rustc");if(!rustc.isEmpty())environment.insert("RUSTC",rustc);
    mProcess.setProcessEnvironment(environment);mProcess.setWorkingDirectory(mExecuting?mOptions.runDirectory(mRoot):mRoot);
    const QPointer<RustWorkspace> self(this);const auto generation=mGeneration;
    emit output(QFileInfo(program).fileName()+" "+args.join(' ')+"\n");
    if(!self||generation!=mGeneration)return false;
    if(mCancelled){releaseJob();return false;}
    mProcess.start(program,args);if(!self||generation!=mGeneration)return false;
    // QProcess may report FailedToStart synchronously. The accepted operation
    // has already emitted finished(-1); do not arm its startup watchdog.
    if(mProcess.state()==QProcess::NotRunning)return true;
    mStartTimer.start();emit busyChanged(true);return self&&generation==mGeneration;
}
bool RustWorkspace::startFile(const QString &file,const QString &command){
    if(busy()||mPreparing||mCompleting)return false;
    const QPointer<RustWorkspace> self(this);mPreparing=true;++mGeneration;
    const auto cleanup=qScopeGuard([self]{if(self)self->mPreparing=false;});
    const QFileInfo source(file);
    if(!source.isFile()||source.suffix().compare("rs",Qt::CaseInsensitive)!=0){emit output(tr("Salvare prima il file Rust (.rs).\n"));return false;}
    if(command=="clippy"){emit output(tr("Clippy richiede un progetto Cargo; per un file singolo usare Controlla.\n"));return false;}
    if(!QSet<QString>{"build","check","run","test","fmt","asm","llvm","doc","debug-test","debug-build"}.contains(command))return false;
    if(command=="doc"){emit output(tr("La documentazione richiede un progetto Cargo.\n"));return false;}
    const QString tool=findTool(command=="fmt"?"rustfmt":"rustc");
    if(tool.isEmpty()){emit output(tr("Strumento Rust non trovato: %1.\n").arg(command=="fmt"?"rustfmt":"rustc"));return false;}
    mRoot=source.absolutePath();mCommand=command;mRunAfterBuild.clear();mDirectDiagnostics=command!="fmt";
    QString error;mOptions=RustProjectOptions::load(mRoot,&error);if(!error.isEmpty()){emit output(error+'\n');if(!self)return false;}
    if(!mOptions.validate(mRoot,error)){emit output(error+'\n');return false;}mExecuting=false;
    mArtifacts.clear();
    if(command=="fmt")return launch(tool,{"--edition",RustTools::edition(),source.absoluteFilePath()},false);
    mFileBuild=std::make_unique<QTemporaryDir>(QDir::tempPath()+"/redpanda-rust-file-XXXXXX");
    if(!mFileBuild->isValid()){emit output(tr("Impossibile creare la cartella temporanea di compilazione.\n"));return false;}
#ifdef Q_OS_WIN
    const QString executable=mFileBuild->filePath("program.exe");
#else
    const QString executable=mFileBuild->filePath("program");
#endif
    if(command=="build"||command=="run"||command=="test"||command=="debug-test"||command=="debug-build")mArtifacts.append(executable);
    const auto output=command=="check"?mFileBuild->filePath("program.rmeta"):command=="asm"?mFileBuild->filePath("program.s"):command=="llvm"?mFileBuild->filePath("program.ll"):executable;
    QStringList args{source.absoluteFilePath(),"--crate-name","redpanda_file","--edition",RustTools::edition(),"--error-format=json","-g","-C",mOptions.release?"opt-level=3":"opt-level=0","-o",output};
    if(!mOptions.targetTriple.isEmpty())args<<"--target"<<mOptions.targetTriple;
    args<<mOptions.rustFlags;
    if(command=="check")args<<"--emit=metadata";
    if(command=="test"||command=="debug-test")args<<"--test";
    if(command=="asm"||command=="llvm"){args<<QString("--emit=%1").arg(command=="asm"?"asm":"llvm-ir");mArtifacts.append(output);}
    if(command=="run"||command=="test")mRunAfterBuild=executable;
    return launch(tool,args,true);
}
void RustWorkspace::stop(){
    if(!busy()&&!mPreparing&&!mCompleting)return;mCancelled=true;
#ifdef Q_OS_WIN
    if(!mJob || !TerminateJobObject(mJob,1)){
        const QPointer<RustWorkspace> self(this);emit output(tr("Interruzione diretta del processo.\n"));if(self)mProcess.kill();
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
    const QPointer<RustWorkspace> self(this);const auto generation=mGeneration;
    const QByteArray bytes=mProcess.readAllStandardOutput();mBytes+=bytes.size();
    if(mBytes>16*1024*1024){mPending.clear();emit output(tr("Output superiore a 16 MiB; operazione annullata.\n"));if(self)stop();return;}
    if(!mBuilding){emit output(mDecoder(bytes));return;}
    mPending+=bytes;
    int offset=0,index;
    while((index=mPending.indexOf('\n',offset))>=0){
        handleLine(mPending.mid(offset,index-offset));if(!self||generation!=mGeneration)return;offset=index+1;
        if(!mBuilding){const auto tail=mPending.mid(offset);mPending.clear();emit output(mDecoder(tail));return;}
    }
    mPending.remove(0,offset);
    if(mPending.size()>1024*1024){mPending.clear();emit output(tr("Riga di output superiore a 1 MiB; operazione annullata.\n"));if(self)stop();}
}
void RustWorkspace::handleLine(const QByteArray &line){
    const QPointer<RustWorkspace> self(this);const auto generation=mGeneration;
    QJsonParseError error;const auto json=QJsonDocument::fromJson(line,&error);
    if(mBuilding&&error.error==QJsonParseError::NoError&&json.isObject()){
        const auto record=json.object();const auto reason=record.value("reason").toString();
        if(reason=="build-finished"){mBuilding=false;return;}
        if(reason=="compiler-artifact"){
            const auto executable=record.value("executable").toString();
            if(mCommand!="asm"&&mCommand!="llvm"&&!executable.isEmpty()&&!mArtifacts.contains(executable))mArtifacts.append(executable);
            return;
        }
        if(reason=="build-script-executed")return;
        if(reason=="compiler-message" || (mDirectDiagnostics && record.value("$message_type").toString()=="diagnostic")){
            const auto message=mDirectDiagnostics?record:record.value("message").toObject();
            emit output(message.value("rendered").toString(message.value("message").toString())+'\n');
            if(!self||generation!=mGeneration||mCancelled||mIssues>=10000)return;
            int visited=0;for(const auto &value:message.value("spans").toArray()){
                if(++visited>10000)break;
                const auto span=value.toObject();if(!span.value("is_primary").toBool())continue;
                auto item=std::make_shared<CompileIssue>();
                item->filename=QDir::cleanPath(QDir::fromNativeSeparators(QDir(mRoot).absoluteFilePath(span.value("file_name").toString())));
                const int line=span.value("line_start").toInt();if(line<1)continue;
                const auto sourceLines=span.value("text").toArray();const auto source=sourceLines.isEmpty()?QString{}:sourceLines.first().toObject().value("text").toString();
                item->line=line-1;item->column=RustCoordinates::scalarColumnToUtf16(source,span.value("column_start").toInt());
                item->endColumn=span.value("line_end").toInt()==line?RustCoordinates::scalarColumnToUtf16(source,span.value("column_end").toInt()):int(source.size());
                item->description=message.value("message").toString();item->originalDescription=item->description;
                item->type=message.value("level").toString()=="error"?CompileIssueType::Error:CompileIssueType::Warning;
                if(!item->filename.isEmpty()){++mIssues;emit issue(item);}if(!self||generation!=mGeneration||mCancelled||mIssues>=10000)return;
            }return;
        }
    }
    emit output(QString::fromUtf8(line)+'\n');
}
