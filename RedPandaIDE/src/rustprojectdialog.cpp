// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "rustprojectdialog.h"
#include "rusttools.h"
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QJsonDocument>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
RustProjectDialog::RustProjectDialog(const QString &root,QWidget *parent):QDialog(parent),mRoot(root){
    setWindowTitle(tr("Opzioni progetto / file Rust"));resize(700,730);setObjectName("rustProjectOptions");
    auto layout=new QVBoxLayout(this);auto form=new QFormLayout;layout->addLayout(form);
    mProfile=new QComboBox(this);mProfile->setObjectName("rustBuildProfile");mProfile->addItem("Debug",false);mProfile->addItem("Release",true);form->addRow(tr("Profilo:"),mProfile);
    mPackage=new QComboBox(this);mPackage->setObjectName("rustCargoPackage");mPackage->setEditable(true);form->addRow(tr("Package Cargo (vuoto = automatico):"),mPackage);
    mKind=new QComboBox(this);mKind->setObjectName("rustTargetKind");for(const auto &pair:QList<QPair<QString,QString>>{{tr("Automatico"),""},{tr("Binario"),"bin"},{tr("Libreria"),"lib"},{tr("Test d'integrazione"),"test"},{tr("Esempio"),"example"},{tr("Benchmark"),"bench"}})mKind->addItem(pair.first,pair.second);form->addRow(tr("Tipo di target:"),mKind);
    mTarget=new QComboBox(this);mTarget->setObjectName("rustTargetName");mTarget->setEditable(true);form->addRow(tr("Nome del target:"),mTarget);
    mTriple=new QLineEdit(this);mTriple->setObjectName("rustTargetTriple");mTriple->setPlaceholderText(tr("Automatico, oppure x86_64-pc-windows-msvc"));form->addRow(tr("Piattaforma:"),mTriple);
    mFeatures=new QLineEdit(this);mFeatures->setObjectName("rustCargoFeatures");mFeatures->setPlaceholderText(tr("Feature separate da virgole o spazi"));form->addRow(tr("Feature:"),mFeatures);
    mAllFeatures=new QCheckBox(tr("Abilita tutte le feature"),this);mNoDefault=new QCheckBox(tr("Disabilita le feature predefinite"),this);form->addRow(mAllFeatures);form->addRow(mNoDefault);
    mArguments=new QPlainTextEdit(this);mArguments->setObjectName("rustRunArguments");mArguments->setMaximumHeight(90);form->addRow(tr("Argomenti programma/test\n(un argomento per riga):"),mArguments);
    mEnvironment=new QPlainTextEdit(this);mEnvironment->setObjectName("rustRunEnvironment");mEnvironment->setMaximumHeight(90);mEnvironment->setPlaceholderText("RUST_BACKTRACE=1");form->addRow(tr("Ambiente\n(NOME=VALORE, una voce per riga):"),mEnvironment);
    auto directoryRow=new QWidget(this);auto directoryLayout=new QHBoxLayout(directoryRow);directoryLayout->setContentsMargins(0,0,0,0);mDirectory=new QLineEdit(directoryRow);mDirectory->setObjectName("rustRunDirectory");mDirectory->setPlaceholderText(tr("Vuoto = cartella del progetto/file"));auto browse=new QPushButton(tr("Sfoglia…"),directoryRow);directoryLayout->addWidget(mDirectory,1);directoryLayout->addWidget(browse);form->addRow(tr("Cartella di esecuzione:"),directoryRow);
    mFlags=new QPlainTextEdit(this);mFlags->setObjectName("rustAdditionalFlags");mFlags->setMaximumHeight(70);form->addRow(tr("Opzioni rustc aggiuntive\n(un argomento per riga):"),mFlags);
    mStatus=new QLabel(this);mStatus->setWordWrap(true);mStatus->setTextFormat(Qt::PlainText);layout->addWidget(mStatus);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,this);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,this,&RustProjectDialog::save);connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    const auto options=RustProjectOptions::load(root);mProfile->setCurrentIndex(options.release?1:0);mPackage->setEditText(options.package);mKind->setCurrentIndex(mKind->findData(options.targetKind));mTarget->setEditText(options.targetName);mTriple->setText(options.targetTriple);mFeatures->setText(options.features.join(", "));
    mAllFeatures->setChecked(options.allFeatures);mNoDefault->setChecked(options.noDefaultFeatures);mFeatures->setEnabled(!options.allFeatures);mArguments->setPlainText(options.arguments.join('\n'));mDirectory->setText(options.workingDirectory);mFlags->setPlainText(options.rustFlags.join('\n'));
    QStringList environment;for(auto it=options.environment.begin();it!=options.environment.end();++it)environment.append(it.key()+'='+it.value());mEnvironment->setPlainText(environment.join('\n'));
    connect(mAllFeatures,&QCheckBox::toggled,this,[this](bool value){mFeatures->setEnabled(!value);});
    connect(browse,&QPushButton::clicked,this,[this]{const auto directory=QFileDialog::getExistingDirectory(this,tr("Cartella di esecuzione"),mRoot);if(!directory.isEmpty())mDirectory->setText(directory);});
    connect(mPackage,&QComboBox::currentTextChanged,this,&RustProjectDialog::populateTargets);connect(mKind,qOverload<int>(&QComboBox::currentIndexChanged),this,&RustProjectDialog::populateTargets);
    mDeadline.setSingleShot(true);mDeadline.setInterval(15000);connect(&mDeadline,&QTimer::timeout,this,[this]{mMetadataJob.terminate();mMetadata.kill();mStatus->setText(tr("Lettura dei target scaduta; puoi specificarne i nomi manualmente."));});
    connect(&mMetadata,&QProcess::readyReadStandardOutput,this,[this]{const auto bytes=mMetadata.readAllStandardOutput();if(bytes.size()>4*1024*1024-mMetadataBytes.size()){mMetadataJob.terminate();mMetadata.kill();mStatus->setText(tr("Metadati Cargo troppo grandi."));}else mMetadataBytes+=bytes;});
    connect(&mMetadata,&QProcess::readyReadStandardError,this,[this]{mMetadata.readAllStandardError();});
    connect(&mMetadata,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus exit){
        mDeadline.stop();if(code!=0||exit!=QProcess::NormalExit){mStatus->setText(tr("I target Cargo non sono disponibili; i nomi possono essere inseriti manualmente."));return;}
        const auto document=QJsonDocument::fromJson(mMetadataBytes);if(!document.isObject()){mStatus->setText(tr("Metadati Cargo non validi."));return;}
        mPackages=document.object().value("packages").toArray();
        {QSignalBlocker blocker(mPackage);const auto selected=mPackage->currentText();mPackage->clear();mPackage->addItem("");for(const auto &value:mPackages)mPackage->addItem(value.toObject().value("name").toString());mPackage->setEditText(selected);}
        populateTargets();mStatus->setText(tr("Target Cargo caricati. La selezione del target viene usata da compilazione, esecuzione, test e debug."));
    });
    connect(&mMetadata,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart){mDeadline.stop();mStatus->setText(tr("Cargo non avviato; verifica il percorso nelle preferenze."));}});
    QTimer::singleShot(0,this,&RustProjectDialog::readMetadata);
}
RustProjectDialog::~RustProjectDialog(){
    mDeadline.stop();disconnect(&mMetadata,nullptr,this,nullptr);
    mMetadataJob.terminate();
    if(mMetadata.state()!=QProcess::NotRunning){mMetadata.kill();mMetadata.waitForFinished(1000);}
}
void RustProjectDialog::readMetadata(){
    const auto manifest=QDir(mRoot).filePath("Cargo.toml");if(!QFileInfo::exists(manifest)){mStatus->setText(tr("File singolo: le opzioni Cargo non si applicano. Profilo, piattaforma, argomenti e ambiente restano disponibili."));mPackage->setEnabled(false);mKind->setEnabled(false);mTarget->setEnabled(false);return;}
    const auto cargo=RustTools::effectivePath("cargo");if(cargo.isEmpty()){mStatus->setText(tr("Cargo non trovato."));return;}
    if(!mMetadataJob.protect(mMetadata)){mStatus->setText(tr("Impossibile proteggere il processo Cargo; inserisci i target manualmente."));return;}
    auto environment=QProcessEnvironment::systemEnvironment();environment.insert("PATH",QFileInfo(cargo).absolutePath()+QDir::listSeparator()+environment.value("PATH"));mMetadata.setProcessEnvironment(environment);mMetadata.setWorkingDirectory(mRoot);
    mStatus->setText(tr("Lettura dei package e target Cargo…"));mMetadata.start(cargo,{"metadata","--no-deps","--format-version=1","--manifest-path",manifest});mDeadline.start();
}
void RustProjectDialog::populateTargets(){
    QSignalBlocker blocker(mTarget);const auto selected=mTarget->currentText();mTarget->clear();mTarget->addItem("");const auto kind=mKind->currentData().toString(),package=mPackage->currentText();
    for(const auto &entry:mPackages){const auto item=entry.toObject();if(!package.isEmpty()&&item.value("name")!=package)continue;
        for(const auto &value:item.value("targets").toArray()){const auto target=value.toObject();const auto kinds=target.value("kind").toArray();
            if(kind.isEmpty()||kinds.contains(kind)||(kind=="lib"&&(kinds.contains("rlib")||kinds.contains("cdylib")||kinds.contains("staticlib"))))mTarget->addItem(target.value("name").toString());}}
    mTarget->setEditText(selected);mTarget->setEnabled(!kind.isEmpty()&&kind!="lib");
}
void RustProjectDialog::save(){
    RustProjectOptions options;options.release=mProfile->currentData().toBool();options.package=mPackage->currentText().trimmed();options.targetKind=mKind->currentData().toString();options.targetName=mTarget->currentText().trimmed();options.targetTriple=mTriple->text().trimmed();options.workingDirectory=mDirectory->text().trimmed();
    options.features=mFeatures->text().split(QRegularExpression("[,\\s]+"),Qt::SkipEmptyParts);options.allFeatures=mAllFeatures->isChecked();options.noDefaultFeatures=mNoDefault->isChecked();
    if(!mArguments->toPlainText().isEmpty())options.arguments=mArguments->toPlainText().split('\n');if(!mFlags->toPlainText().isEmpty())options.rustFlags=mFlags->toPlainText().split('\n');
    for(const auto &line:mEnvironment->toPlainText().split('\n')){if(line.trimmed().isEmpty())continue;const auto separator=line.indexOf('=');if(separator<=0){QMessageBox::warning(this,tr("Ambiente non valido"),tr("Usa NOME=VALORE per ogni variabile."));return;}options.environment.insert(line.left(separator).trimmed(),line.mid(separator+1));}
    QString error;if(!options.save(mRoot,error)){QMessageBox::warning(this,tr("Opzioni non salvate"),error);return;}accept();
}
