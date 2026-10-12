// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "rustcleandialog.h"
#include "rusttools.h"
#include "rustprojectoptions.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent>
RustCleanDialog::RustCleanDialog(const QString &root,const QStringList &protectedFiles,QWidget *parent):QDialog(parent),mRoot(root),mProtected(protectedFiles){
    setObjectName("rustCleanPreview");setWindowTitle(tr("Pulizia degli artefatti Cargo"));resize(850,550);
    auto layout=new QVBoxLayout(this);mStatus=new QLabel(tr("Lettura della cartella target effettiva…"),this);mStatus->setTextFormat(Qt::PlainText);mStatus->setWordWrap(true);layout->addWidget(mStatus);
    mPreview=new QPlainTextEdit(this);mPreview->setReadOnly(true);mPreview->setObjectName("rustCleanFiles");layout->addWidget(mPreview);
    auto buttons=new QDialogButtonBox(this);mClean=buttons->addButton(tr("Sposta nel Cestino"),QDialogButtonBox::AcceptRole);mClean->setObjectName("rustRecycleArtifacts");mClean->setEnabled(false);mCancel=buttons->addButton(QDialogButtonBox::Cancel);mCancel->setDefault(true);layout->addWidget(buttons);
    connect(mClean,&QPushButton::clicked,this,&RustCleanDialog::recycle);connect(mCancel,&QPushButton::clicked,this,&RustCleanDialog::reject);
    mDeadline.setSingleShot(true);connect(&mDeadline,&QTimer::timeout,this,[this]{mJob.terminate();mProcess.kill();mStatus->setText(tr("Lettura Cargo scaduta. Nessun file rimosso."));});
    connect(&mProcess,&QProcess::readyReadStandardError,this,[this]{mProcess.readAllStandardError();});
    connect(&mProcess,&QProcess::readyReadStandardOutput,this,[this]{const auto bytes=mProcess.readAllStandardOutput();if(bytes.size()>4*1024*1024-mBytes.size()){mJob.terminate();mProcess.kill();mStatus->setText(tr("Metadati troppo grandi. Nessun file rimosso."));}else mBytes+=bytes;});
    connect(&mProcess,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart){mDeadline.stop();mStatus->setText(tr("Impossibile avviare Cargo. Nessun file rimosso."));}});
    connect(&mProcess,&QProcess::finished,this,[this](int code,QProcess::ExitStatus status){
        mDeadline.stop();if(code||status!=QProcess::NormalExit){mStatus->setText(tr("Metadati Cargo non disponibili. Nessun file rimosso."));return;}
        const auto object=QJsonDocument::fromJson(mBytes).object();mTarget=object.value("target_directory").toString();mWorkspace=object.value("workspace_root").toString();
        for(const auto &entry:object.value("packages").toArray()){const auto package=entry.toObject();mProtected.append(package.value("manifest_path").toString());for(const auto &value:package.value("targets").toArray())mProtected.append(value.toObject().value("src_path").toString());}
        mProtected.append(QDir(mWorkspace).filePath("Cargo.toml"));mProtected.append(QDir(mWorkspace).filePath("Cargo.lock"));inspect();
    });QTimer::singleShot(0,this,&RustCleanDialog::readMetadata);
}
RustCleanDialog::~RustCleanDialog(){mDeadline.stop();disconnect(&mProcess,nullptr,this,nullptr);mJob.terminate();if(mProcess.state()!=QProcess::NotRunning){mProcess.kill();mProcess.waitForFinished(1000);}}
void RustCleanDialog::reject(){if(!mMoving)QDialog::reject();}
void RustCleanDialog::readMetadata(){
    const auto cargo=RustTools::effectivePath("cargo");if(cargo.isEmpty()||!mJob.protect(mProcess)){mStatus->setText(tr("Cargo non disponibile. Nessun file rimosso."));return;}
    auto env=RustProjectOptions::load(mRoot).processEnvironment();env.insert("PATH",QFileInfo(cargo).absolutePath()+QDir::listSeparator()+env.value("PATH"));mProcess.setProcessEnvironment(env);mProcess.setWorkingDirectory(mRoot);
    mProcess.start(cargo,{"metadata","--no-deps","--format-version=1","--manifest-path",QDir(mRoot).filePath("Cargo.toml")});mDeadline.start(15000);
}
void RustCleanDialog::inspect(){
    mStatus->setText(tr("Verifica degli artefatti in %1…").arg(mTarget));auto watcher=new QFutureWatcher<RustClean::Plan>(this);
    connect(watcher,&QFutureWatcher<RustClean::Plan>::finished,this,[this,watcher]{mPlan=watcher->result();watcher->deleteLater();
        if(!mPlan.valid){mStatus->setText(mPlan.error);return;}
        mPreview->setPlainText(mPlan.preview.join('\n'));mClean->setEnabled(true);
        mStatus->setText(tr("Cartella: %1\n%2 elementi, %3 MiB. Tutti i file saranno spostati nel Cestino, incluse le configurazioni di compilazione e gli eventuali artefatti condivisi. Se Windows mantiene aperte le cartelle, resteranno vuote.\n%4").arg(mPlan.target).arg(mPlan.entries).arg(mPlan.bytes/1048576.0,0,'f',1).arg(mPlan.entries>2000?tr("Elenco limitato ai primi 2.000 elementi."):QString()));
    });const auto target=mTarget,workspace=mWorkspace;const auto protectedFiles=mProtected;watcher->setFuture(QtConcurrent::run([target,workspace,protectedFiles]{return RustClean::inspect(target,workspace,protectedFiles);}));
}
void RustCleanDialog::recycle(){
    if(!mPlan.valid||mMoving)return;mMoving=true;mClean->setEnabled(false);mCancel->setEnabled(false);mStatus->setText(tr("Spostamento nel Cestino…"));
    auto watcher=new QFutureWatcher<QString>(this);connect(watcher,&QFutureWatcher<QString>::finished,this,[this,watcher]{const auto error=watcher->result();watcher->deleteLater();mMoving=false;mCancel->setEnabled(true);if(error.isEmpty())accept();else{qWarning().noquote()<<"Cargo recycle:"<<error;mStatus->setText(error);}});
    const auto plan=mPlan;const auto workspace=mWorkspace;const auto protectedFiles=mProtected;
    watcher->setFuture(QtConcurrent::run([plan,workspace,protectedFiles]{QString error;RustClean::recycle(plan,workspace,protectedFiles,error);return error;}));
}
