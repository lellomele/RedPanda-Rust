// Copyright (C) 2026 Raffaele Mele
// SPDX-License-Identifier: GPL-3.0-or-later
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "rustworkspace.h"
#include "rusttools.h"
#include "settings.h"
#include "editor.h"
#include "editormanager.h"
#include "systemconsts.h"
#include "widgets/issuestable.h"
#include <QInputDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QLineEdit>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTextCursor>
#include <QComboBox>
#include <QRegularExpression>
#include <QFileInfo>
#include <QDir>

void MainWindow::initRustWorkspace() {
    RustTools::setSettingsFile(pSettings->filename());
    mRustWorkspace=new RustWorkspace(this);
    mCompilerSet->hide();ui->toolbarCompilerSet->setVisible(false);
    ui->actionCompile->setText(tr("Compila Rust"));
    ui->actionRebuild->setText(tr("Controlla Rust"));
    ui->actionRun->setText(tr("Esegui Rust"));
    ui->actionCompiler_Options->setText(tr("Apri progetto Cargo..."));
    ui->actionNew_Project->setText(tr("Nuovo progetto Rust..."));
    ui->actionNew->setText(tr("Nuovo file Rust"));
    ui->actionReformat_Code->setText(tr("Formatta con rustfmt"));
    for(const QString &name: {"actionDebug","actionInterrupt","actionStep_Over","actionStep_Into","actionStep_Out","actionRun_To_Cursor","actionContinue","actionAdd_Watch","actionAdd_Watchpoint","actionView_CPU_Window","actionGenerate_Assembly","actionGenerate_GIMPLE","actionPreprocess","actionMakeClean","actionMakefileClean","actionNew_GAS_File","actionNew_NASM_File","actionRun_Parameters","actionC_C_Header","actionC_File","actionCPP_File","actionEGE_Manual","actionOI_Wiki","actionTurtle_Graphics_Manual","actionDocument","actionIA_32_Assembly_Language_Reference_Manual","actionx86_Assembly_Language_Reference_Manual"}) {
        if(auto action=findChild<QAction *>(name))action->setVisible(false);
    }
    ui->menuProject->menuAction()->setVisible(false);
    ui->menuFile_Type->menuAction()->setVisible(false);
    ui->tabMessages->setTabText(ui->tabMessages->indexOf(ui->tabToolsOutput),tr("Cargo / Console"));
    ui->txtToolsOutput->setMaximumBlockCount(10000);
    auto open=ui->menuFile->addAction(tr("Apri progetto Cargo..."));
    connect(open,&QAction::triggered,this,&MainWindow::openRustWorkspace);
    for(const auto &command:QStringList{"test","clippy"}){
        auto action=ui->menuExecute->addAction(command=="test"?tr("Test Rust"):tr("Analizza con Clippy"));
        connect(action,&QAction::triggered,this,[this,command]{runRustCommand(command);});
        connect(mRustWorkspace,&RustWorkspace::busyChanged,action,[action](bool busy){action->setEnabled(!busy);});
    }
    // Reuse the original output tab and supply stdin to console applications.
    auto container=new QWidget(ui->tabToolsOutput);auto layout=new QVBoxLayout(container);layout->setContentsMargins(0,0,0,0);
    ui->tabToolsOutput->layout()->removeWidget(ui->txtToolsOutput);layout->addWidget(ui->txtToolsOutput);
    auto line=new QHBoxLayout;auto input=new QLineEdit(container);auto eof=new QPushButton(tr("Fine input"),container);
    input->setPlaceholderText(tr("Input del programma: scrivi e premi Invio"));input->setEnabled(false);eof->setEnabled(false);
    line->addWidget(input);line->addWidget(eof);layout->addLayout(line);ui->tabToolsOutput->layout()->addWidget(container);
    connect(input,&QLineEdit::returnPressed,this,[this,input]{mRustWorkspace->sendInput(input->text());input->clear();});
    connect(eof,&QPushButton::clicked,mRustWorkspace,&RustWorkspace::closeInput);
    connect(mRustWorkspace,&RustWorkspace::output,this,[this](const QString &text){
        QTextCursor cursor=ui->txtToolsOutput->textCursor();cursor.movePosition(QTextCursor::End);cursor.insertText(text);
        ui->txtToolsOutput->setTextCursor(cursor);ui->txtToolsOutput->ensureCursorVisible();
    });
    connect(mRustWorkspace,&RustWorkspace::issue,ui->tableIssues,&IssuesTable::addIssue);
    connect(mRustWorkspace,&RustWorkspace::busyChanged,this,[this,input,eof](bool busy){input->setEnabled(busy);eof->setEnabled(busy);updateCompileActions();});
}
void MainWindow::applyRustUiRestrictions(){
    for(QWidget *tab:{ui->tabProject,ui->tabWatch,ui->tabStructure,ui->tabProblemSet})showHideInfosTab(tab,false);
    for(QWidget *tab:{ui->tabDebug,ui->tabProblem})showHideMessagesTab(tab,false);
    for(QAction *action:{ui->actionProject,ui->actionWatch,ui->actionStructure,ui->actionProblem_Set,ui->actionDebug_Window,ui->actionProblem})action->setVisible(false);
    ui->toolbarCompilerSet->setVisible(false);
    ui->menuRefactor->menuAction()->setVisible(false);
    ui->actionInterrupt->setVisible(false);
}
bool MainWindow::runRustCommand(const QString &command){
    if(mRustWorkspace->busy())return false;
    auto editor=mEditorManager->getEditor();
    // Save the active Rust document before deciding whether it belongs to Cargo.
    if(editor && editor->fileType()==FileType::RustSource && (editor->modified()||editor->isNew())){
        if(!editor->save())return false;
    }
    QString root=editor?RustWorkspace::findRoot(editor->filename()):QString();
    if(editor && editor->fileType()==FileType::RustSource && root.isEmpty()){
        ui->txtToolsOutput->clear();ui->tableIssues->clearIssues();
        stretchMessagesPanel(true);ui->tabMessages->setCurrentWidget(ui->tabToolsOutput);
        return mRustWorkspace->startFile(editor->filename(),command);
    }
    if(root.isEmpty())root=mRustRoot;
    if(root.isEmpty()){openRustWorkspace();root=mRustRoot;}
    if(root.isEmpty())return false;
    // A failed or cancelled save must abort the command, preserving the document.
    const QString prefix=QDir::cleanPath(root)+"/";
    for(int i=0;i<mEditorManager->pageCount();++i){
        Editor *e=(*mEditorManager)[i];
        const QString path=QDir::fromNativeSeparators(QFileInfo(e->filename()).absoluteFilePath());
        if((e==editor||path.startsWith(prefix,PATH_SENSITIVITY)) && (e->modified()||e->isNew())){
            if(!e->save())return false;
        }
    }
    if(editor){const auto detected=RustWorkspace::findRoot(editor->filename());if(!detected.isEmpty())root=detected;}
    mRustRoot=root;ui->txtToolsOutput->clear();ui->tableIssues->clearIssues();
    stretchMessagesPanel(true);ui->tabMessages->setCurrentWidget(ui->tabToolsOutput);
    return mRustWorkspace->start(root,command);
}
void MainWindow::openRustWorkspace(){
    if(mRustWorkspace->busy())return;
    const QString manifest=QFileDialog::getOpenFileName(this,tr("Apri progetto Cargo"),mRustRoot,"Cargo.toml (Cargo.toml)");
    if(manifest.isEmpty())return;mRustRoot=QFileInfo(manifest).absolutePath();
    setFilesViewRoot(mRustRoot);
    const QDir directory(mRustRoot);
    openFile(manifest);
    if(QFileInfo::exists(directory.filePath("src/main.rs")))openFile(directory.filePath("src/main.rs"));
    else if(QFileInfo::exists(directory.filePath("src/lib.rs")))openFile(directory.filePath("src/lib.rs"));
    updateCompileActions();
}
void MainWindow::createRustWorkspace(){
    if(mRustWorkspace->busy())return;
    const QString parent=QFileDialog::getExistingDirectory(this,tr("Cartella per il nuovo progetto Rust"));
    if(parent.isEmpty())return;
    bool ok=false;const QString name=QInputDialog::getText(this,tr("Nuovo progetto Rust"),tr("Nome del progetto"),QLineEdit::Normal,"rust_app",&ok);
    if(!ok)return;
    if(!QRegularExpression("^[A-Za-z][A-Za-z0-9_-]{0,63}$").match(name).hasMatch() || QFileInfo::exists(QDir(parent).filePath(name))){
        QMessageBox::warning(this,tr("Nome non valido"),tr("Usa lettere, numeri, trattini o underscore e un nome di cartella nuovo."));return;
    }
    const QString target=QDir(parent).filePath(name);
    auto connection=std::make_shared<QMetaObject::Connection>();
    *connection=connect(mRustWorkspace,&RustWorkspace::finished,this,[this,target,connection](int code,bool cancelled){
        disconnect(*connection);
        if(code==0&&!cancelled){mRustRoot=target;setFilesViewRoot(target);openFile(QDir(target).filePath("src/main.rs"));}
    });
    ui->txtToolsOutput->clear();stretchMessagesPanel(true);ui->tabMessages->setCurrentWidget(ui->tabToolsOutput);
    if(!mRustWorkspace->start(parent,"new",{name,"--vcs","none"}))disconnect(*connection);
}
