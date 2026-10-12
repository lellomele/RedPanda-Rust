// Copyright (C) 2026 Raffaele Mele
// SPDX-License-Identifier: GPL-3.0-or-later
#include "rusttoolswidget.h"
#include "../rusttools.h"
#include "../utils.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QFileDialog>
#include <QProcess>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
RustToolsWidget::RustToolsWidget(const QString &name,const QString &group,IconsManager *icons,QWidget *parent)
    :SettingsWidget(name,group,icons,parent){
    auto *layout=new QFormLayout(this);
    auto *help=new QLabel(tr("Lascia vuoto un percorso per usare il rilevamento automatico. I percorsi salvati vengono usati da compilazione, esecuzione e formattazione."),this);
    help->setWordWrap(true);layout->addRow(help);
    for(const QString &tool:RustTools::names()){
        auto *row=new QWidget(this);auto *line=new QHBoxLayout(row);line->setContentsMargins(0,0,0,0);
        auto *path=new QLineEdit(row);path->setObjectName("rustTool_"+tool);path->setPlaceholderText(tr("Automatico"));
        auto *browse=new QPushButton(tr("Sfoglia..."),row);line->addWidget(path);line->addWidget(browse);
        layout->addRow(tool,row);mPaths.insert(tool,path);
        auto *status=new QLabel(this);status->setObjectName("rustToolStatus_"+tool);status->setTextFormat(Qt::PlainText);status->setWordWrap(true);
        layout->addRow(QString(),status);mResults.insert(tool,status);
        connect(browse,&QPushButton::clicked,this,[this,tool,path]{
            const QString chosen=QFileDialog::getOpenFileName(this,tr("Scegli %1").arg(tool),path->text(),tr("Eseguibili (*.exe);;Tutti i file (*)"));
            if(!chosen.isEmpty())path->setText(QDir::toNativeSeparators(chosen));
        });
    }
    mEdition=new QComboBox(this);mEdition->setObjectName("rustSingleFileEdition");mEdition->addItems({"2015","2018","2021","2024"});
    layout->addRow(tr("Edizione dei file singoli"),mEdition);
    auto *editionHelp=new QLabel(tr("Nei progetti Cargo l'edizione viene letta da Cargo.toml. Rust-analyzer fornisce analisi semantica, completamento e informazioni sui simboli. Dopo aver cambiato i percorsi, usa Strumenti → Riavvia rust-analyzer. I sorgenti della libreria standard richiedono il componente rust-src di rustup."),this);
    editionHelp->setWordWrap(true);layout->addRow(editionHelp);
    mVerify=new QPushButton(tr("Verifica strumenti"),this);mVerify->setObjectName("verifyRustTools");layout->addRow(mVerify);
    mProbe=new QProcess(this);mProbe->setProcessChannelMode(QProcess::MergedChannels);
    mTimeout=new QTimer(this);mTimeout->setSingleShot(true);mTimeout->setInterval(5000);
    connect(mTimeout,&QTimer::timeout,this,[this]{mTimedOut=true;mProbe->kill();});
    connect(mProbe,&QProcess::readyReadStandardOutput,this,[this]{
        const QByteArray chunk=mProbe->readAllStandardOutput();
        if(mOutput.size()+chunk.size()>4096){mTimedOut=true;mProbe->kill();}else mOutput+=chunk;
    });
    connect(mProbe,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
        if(error==QProcess::FailedToStart){mTimeout->stop();mResults.value(mCurrent)->setText(tr("Avvio fallito: %1").arg(mProbe->errorString()));QTimer::singleShot(0,this,&RustToolsWidget::probeNext);}
    });
    connect(mProbe,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status){
        mTimeout->stop();mOutput+=mProbe->readAllStandardOutput().left(4096-mOutput.size());
        const QString text=QString::fromUtf8(mOutput).trimmed();
        mResults.value(mCurrent)->setText(mTimedOut?tr("Verifica interrotta: tempo o output eccessivi."):
            code==0&&status==QProcess::NormalExit?(mProbeDescription.isEmpty()?text:mProbeDescription):tr("Verifica fallita (%1): %2").arg(code).arg(text));
        QTimer::singleShot(0,this,&RustToolsWidget::probeNext);
    });
    connect(mVerify,&QPushButton::clicked,this,[this]{mVerify->setEnabled(false);mQueue=RustTools::names();probeNext();});
}
RustToolsWidget::~RustToolsWidget(){mTimeout->stop();if(mProbe->state()!=QProcess::NotRunning){disconnect(mProbe,nullptr,this,nullptr);mProbe->kill();mProbe->waitForFinished(500);}}
void RustToolsWidget::probeNext(){
    if(mQueue.isEmpty()){mVerify->setEnabled(true);return;}
    mCurrent=mQueue.takeFirst();mOutput.clear();mTimedOut=false;mProbeDescription.clear();
    QString path=mPaths.value(mCurrent)->text().trimmed();if(path.isEmpty())path=RustTools::detectedPath(mCurrent);
    if(path.isEmpty()){mResults.value(mCurrent)->setText(tr("Non trovato"));QTimer::singleShot(0,this,&RustToolsWidget::probeNext);return;}
    QStringList arguments{"--version"};
    if(mCurrent=="codelldb"){
        // The adapter's CLI has no --version option. --help verifies that the
        // adapter loads; its distribution metadata supplies the version.
        arguments={"--help"};mProbeDescription=tr("Adapter CodeLLDB disponibile: %1").arg(path);
        QFile metadata(QDir(QFileInfo(path).absolutePath()).filePath("../package.json"));
        if(metadata.open(QIODevice::ReadOnly)&&metadata.size()<=256*1024){
            const auto version=QJsonDocument::fromJson(metadata.readAll()).object().value("version").toString();
            if(!version.isEmpty())mProbeDescription=tr("CodeLLDB %1 — adapter disponibile: %2").arg(version,path);
        }
    }
    mResults.value(mCurrent)->setText(tr("Verifica in corso: %1").arg(path));mTimeout->start();mProbe->start(path,arguments);
}
void RustToolsWidget::doLoad(){
    for(const QString &tool:RustTools::names()){
        mPaths.value(tool)->setText(QDir::toNativeSeparators(RustTools::configuredPath(tool)));
        mResults.value(tool)->setText(tr("Percorso effettivo: %1").arg(QDir::toNativeSeparators(RustTools::effectivePath(tool))));
    }
    mEdition->setCurrentText(RustTools::edition());
}
void RustToolsWidget::doSave(){
    QMap<QString,QString> paths;for(const QString &tool:RustTools::names())paths.insert(tool,mPaths.value(tool)->text());
    QString error;if(!RustTools::save(paths,mEdition->currentText(),error))throw FileError(error);
}
