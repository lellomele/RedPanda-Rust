// Copyright (C) 2026 Raffaele Mele
// SPDX-License-Identifier: GPL-3.0-or-later
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "rustworkspace.h"
#include "rusttools.h"
#include "rustlspclient.h"
#include "rusteditorsupport.h"
#include "rustdebugsession.h"
#include "rustdebugpanel.h"
#include "rustprojectdialog.h"
#include "rustcleandialog.h"
#include "rustnavigation.h"
#include "iconsmanager.h"
#include "colorscheme.h"
#include "debugger/debugger.h"
#include "debugger/debuggermodels.h"
#include "settings.h"
#include "editor.h"
#include "editormanager.h"
#include "systemconsts.h"
#include "widgets/issuestable.h"
#include "widgets/customfilesystemmodel.h"
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
#include <QTreeWidget>
#include <QTreeView>
#include <QMenu>
#include <QPointer>
#include <QUrl>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QTableView>
#include <QHeaderView>

void MainWindow::initRustWorkspace() {
    RustTools::setSettingsFile(pSettings->filename());
    mRustWorkspace=new RustWorkspace(this);
    mRustLanguageClient=new RustLspClient(this);
    mRustDebugger=new RustDebugSession(this);
    mRustDebugPanel=new RustDebugPanel(mRustDebugger,pSettings->filename(),ui->tabMessages);
    ui->tabMessages->addTab(mRustDebugPanel,tr("Debug Rust"));
    auto debugTabInfo=std::make_shared<TabWidgetInfo>();debugTabInfo->order=ui->tabMessages->count()-1;debugTabInfo->text=tr("Debug Rust");mTabMessagesData.insert(mRustDebugPanel,debugTabInfo);
    connect(mRustDebugPanel,&RustDebugPanel::sourceRequested,this,[this](const QString &file,int line,int){setActiveBreakpoint(file,line);});
    connect(mRustDebugPanel,&RustDebugPanel::sourceBreakpointsRequested,this,&MainWindow::configureRustSourceBreakpoints);
    connect(mRustDebugger,&RustDebugSession::stateChanged,this,[this](RustDebugSession::State state){
        if(state==RustDebugSession::State::Running||state==RustDebugSession::State::Idle)removeActiveBreakpoints();
        updateCompileActions();updateRustDebugActions();
    });
    connect(mRustDebugger,&RustDebugSession::error,this,[this](const QString &text){statusBar()->showMessage(text,12000);});
    auto breakpointModel=mDebugger->breakpointModel().get();
    auto breakpointSync=new QTimer(this);breakpointSync->setSingleShot(true);breakpointSync->setInterval(0);
    connect(breakpointSync,&QTimer::timeout,this,&MainWindow::syncRustBreakpoints);
    const auto changed=[breakpointSync]{breakpointSync->start();};
    connect(breakpointModel,&QAbstractItemModel::dataChanged,this,changed);
    connect(breakpointModel,&QAbstractItemModel::rowsRemoved,this,changed);
    connect(breakpointModel,&QAbstractItemModel::modelReset,this,changed);
    auto languageStatus=new QLabel(tr("rust-analyzer: in attesa di un file Rust"),this);
    languageStatus->setObjectName("rustAnalyzerStatus");
    languageStatus->setMaximumWidth(350);ui->statusbar->addPermanentWidget(languageStatus);
    connect(mRustLanguageClient,&RustLspClient::status,this,[this,languageStatus](const QString &text){
        languageStatus->setText(text.left(75));languageStatus->setToolTip(text);
        statusBar()->showMessage(text,8000);
    });
    auto attach=[this,breakpointSync]{
        for(int i=0;i<mEditorManager->pageCount();++i){
            auto editor=(*mEditorManager)[i];
            if(editor->findChild<RustEditorSupport *>("rustEditorSupport",Qt::FindDirectChildrenOnly))continue;
            auto support=new RustEditorSupport(editor,mRustLanguageClient);
            connect(editor,&Editor::breakpointAdded,this,[breakpointSync]{breakpointSync->start();});
            connect(editor,&Editor::breakpointRemoved,this,[breakpointSync]{breakpointSync->start();});
            connect(editor,&Editor::breakpointsCleared,this,[breakpointSync]{breakpointSync->start();});
            connect(support,&RustEditorSupport::definitionRequested,this,[this](const QString &file,int line,int character){
                if(auto target=openFile(file)){
                    if(line>=0&&line<target->lineCount()&&character>=0&&character<=target->lineText(line).size())
                        target->setCaretXYCentered({character,line});
                }
            });
        }
    };
    connect(mEditorManager,&EditorManager::editorOpenned,this,attach);attach();
    ui->classBrowser->hide();auto outline=new QTreeView(ui->tabStructure);outline->setObjectName("rustOutline");outline->setHeaderHidden(true);ui->tabStructure->layout()->addWidget(outline);
    mRustNavigation=new RustNavigation(mEditorManager,mRustLanguageClient,outline,this,[this](const QString &file){return openFile(file);});
    mRustNavigation->setOutlineOptions(pSettings->ui().classBrowserSortAlpha(),pSettings->ui().classBrowserSortType(),pSettings->ui().classBrowserShowInherited());
    mClassBrowser_Sort_By_Name->setObjectName("rustOutlineSortName");mClassBrowser_Sort_By_Type->setObjectName("rustOutlineSortType");
    mClassBrowser_Show_Inherited->setObjectName("rustOutlineShowTraits");
    mClassBrowser_Show_Inherited->setText(tr("Mostra implementazioni dei trait"));
    mClassBrowser_Show_Inherited->setToolTip(tr("Mostra o nasconde i blocchi impl Trait for Tipo; i metodi degli impl propri restano visibili."));
    const auto outlineAppearance=[this,outline]{
        const QPointer<MainWindow> self(this);const QPointer<QTreeView> view(outline);
        if(!mRustNavigation)return;
        mRustNavigation->setOutlineStyler([this](int kind,int visibility,bool associated){
            auto icon=IconsManager::PARSER_TYPE;auto category=StatementKind::Typedef;
            const auto method=visibility==2?IconsManager::PARSER_PUBLIC_METHOD:visibility==1?IconsManager::PARSER_PROTECTED_METHOD:IconsManager::PARSER_PRIVATE_METHOD;
            const auto field=visibility==2?IconsManager::PARSER_PUBLIC_VAR:visibility==1?IconsManager::PARSER_PROTECTED_VAR:IconsManager::PARSER_PRIVATE_VAR;
            switch(kind){
            case 1:case 2:case 3:case 4:icon=IconsManager::PARSER_NAMESPACE;category=StatementKind::Namespace;break;
            case 5:case 19:case 23:icon=IconsManager::PARSER_CLASS;category=StatementKind::Class;break;
            case 10:case 22:icon=IconsManager::PARSER_ENUM;category=StatementKind::EnumType;break;
            case 6:case 9:case 25:icon=method;category=StatementKind::Function;break;
            case 12:icon=associated?method:IconsManager::PARSER_GLOBAL_METHOD;category=StatementKind::Function;break;
            case 7:case 8:icon=field;category=StatementKind::Variable;break;
            case 13:icon=IconsManager::PARSER_GLOBAL_VAR;category=StatementKind::GlobalVariable;break;
            case 14:icon=IconsManager::PARSER_DEFINE;category=StatementKind::Preprocessor;break;
            default:break;
            }
            QColor color=palette().color(QPalette::Text);const auto entry=mStatementColors->value(category);if(entry)color=entry->foreground();
            return RustOutlineModel::Style{mIconsManager->getIcon(icon),color};
        });
        if(self&&view){const int size=qMax(8,view->fontMetrics().capHeight()+2);view->setIconSize(QSize(size,size));view->viewport()->update();}
    };
    connect(mIconsManager.get(),&IconsManager::actionIconsUpdated,mRustNavigation,outlineAppearance);outlineAppearance();
    outline->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(outline,&QTreeView::customContextMenuRequested,mRustNavigation,[this,outline](const QPoint &point){
        const QPointer<QTreeView> view(outline);const QPointer<RustNavigation> navigation(mRustNavigation);
        const QPersistentModelIndex index(outline->indexAt(point));
        auto menu=new QMenu(outline);const QPointer<QMenu> guard(menu);
        auto go=menu->addAction(tr("Vai al simbolo"));go->setEnabled(index.isValid());
        connect(go,&QAction::triggered,navigation,[navigation,index]{if(navigation)navigation->activateOutline(index);});
        menu->addSeparator();menu->addAction(mClassBrowser_Sort_By_Name);menu->addAction(mClassBrowser_Sort_By_Type);menu->addAction(mClassBrowser_Show_Inherited);
        menu->exec(outline->viewport()->mapToGlobal(point));if(guard)guard->deleteLater();
    });
    connect(mRustNavigation,&RustNavigation::status,this,[this](const QString &message){statusBar()->showMessage(message,12000);});
    connect(mRustNavigation,&RustNavigation::generatedText,this,[this](const QString &title,const QString &contents){
        const QPointer<MainWindow> self(this);const QPointer<Editor> editor=mEditorManager->newEditor(title+".rs",ENCODING_UTF8,FileType::RustSource,{},false,true);if(!self||!editor||editor->removing())return;editor->setContent(contents);if(!self||!editor||editor->removing())return;editor->setModified(false);editor->setReadOnly(true);editor->setAutoBackupEnabled(false);mEditorManager->activeEditor(editor,true);
    });
    connect(mRustNavigation,&RustNavigation::referencesReady,this,[this](const QJsonArray &locations){
        auto dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setObjectName("rustReferences");dialog->setWindowTitle(tr("Riferimenti / simboli Rust"));dialog->resize(850,500);auto layout=new QVBoxLayout(dialog);auto list=new QTreeWidget(dialog);list->setHeaderLabels({tr("Simbolo"),tr("File"),tr("Riga")});layout->addWidget(list);
        int count=0;for(const auto &entry:locations){if(++count>10000)break;const auto location=entry.toObject();const auto url=QUrl(location.value("uri").toString());if(!url.isLocalFile())continue;const auto point=location.value("range").toObject().value("start").toObject();
            auto row=new QTreeWidgetItem(list,{location.value("name").toString(),url.toLocalFile(),QString::number(point.value("line").toInt()+1)});row->setData(0,Qt::UserRole,location);}
        list->resizeColumnToContents(1);connect(list,&QTreeWidget::itemActivated,dialog,[this](QTreeWidgetItem *item){const auto value=item->data(0,Qt::UserRole).toJsonObject();const auto point=value.value("range").toObject().value("start").toObject();if(auto editor=openFile(QUrl(value.value("uri").toString()).toLocalFile()))editor->setCaretXYCentered({point.value("character").toInt(),point.value("line").toInt()});});dialog->show();
    });
    // The inherited editor menu is populated/cleared by its own callbacks.
    // Rust actions need independent ownership and must not enter that backup.
    auto rustRefactor=new QMenu(tr("Refactoring Rust"),ui->menubar);
    rustRefactor->setObjectName("rustRefactorMenu");ui->menubar->insertMenu(ui->menuProject->menuAction(),rustRefactor);
    for(const auto &item:QList<QPair<QString,std::function<void()>>>{{tr("Trova riferimenti Rust"),[this]{mRustNavigation->references();}},{tr("Rinomina simbolo Rust…"),[this]{mRustNavigation->rename();}},{tr("Azioni Rust…"),[this]{mRustNavigation->codeActions();}},{tr("Espandi macro Rust"),[this]{mRustNavigation->macroExpansion();}}}){
        auto action=rustRefactor->addAction(item.first);connect(action,&QAction::triggered,this,item.second);
    }
    auto symbols=ui->menuSearch->addAction(tr("Cerca simbolo Rust…"));connect(symbols,&QAction::triggered,mRustNavigation,&RustNavigation::searchSymbols);
    mRustRestartAnalyzer=new QAction(tr("Riavvia rust-analyzer"),this);
    ui->menuTools->addAction(mRustRestartAnalyzer);
    connect(mRustRestartAnalyzer,&QAction::triggered,mRustLanguageClient,&RustLspClient::restart);
    mCompilerSet->hide();ui->toolbarCompilerSet->setVisible(false);
    ui->actionCompile->setText(tr("Compila Rust"));
    ui->actionRebuild->setText(tr("Controlla Rust"));
    ui->actionRun->setText(tr("Esegui Rust"));
    ui->actionCompiler_Options->setText(tr("Apri progetto Cargo..."));
    ui->actionNew_Project->setText(tr("Nuovo progetto Rust..."));
    ui->actionNew->setText(tr("Nuovo file Rust"));
    ui->actionReformat_Code->setText(tr("Formatta con rustfmt"));
    ui->actionReparse_Code->setText(tr("Ricarica analisi Rust"));
    for(const QString &name: {"actionDebug","actionInterrupt","actionStep_Over","actionStep_Into","actionStep_Out","actionRun_To_Cursor","actionContinue","actionAdd_Watch","actionAdd_Watchpoint","actionView_CPU_Window","actionGenerate_Assembly","actionGenerate_GIMPLE","actionPreprocess","actionMakeClean","actionMakefileClean","actionNew_GAS_File","actionNew_NASM_File","actionRun_Parameters","actionC_C_Header","actionC_File","actionCPP_File","actionEGE_Manual","actionOI_Wiki","actionTurtle_Graphics_Manual","actionDocument","actionIA_32_Assembly_Language_Reference_Manual","actionx86_Assembly_Language_Reference_Manual"}) {
        if(auto action=findChild<QAction *>(name))action->setVisible(false);
    }
    for(auto action:{ui->actionDebug,ui->actionInterrupt,ui->actionStep_Over,ui->actionStep_Into,ui->actionStep_Out,ui->actionRun_To_Cursor,ui->actionContinue,ui->actionAdd_Watch,ui->actionAdd_Watchpoint,ui->actionView_CPU_Window})action->setVisible(true);
    ui->actionAdd_Watchpoint->setText(tr("Watchpoint sulla variabile Rust selezionata…"));
    ui->actionDebug->setText(tr("Avvia debug Rust"));ui->actionView_CPU_Window->setText(tr("Disassemblato Rust"));
    ui->actionCompiler_Options->setText(tr("Opzioni progetto / file Rust…"));ui->actionRun_Parameters->setText(tr("Argomenti e ambiente Rust…"));ui->actionRun_Parameters->setVisible(true);
    ui->actionGenerate_Assembly->setText(tr("Genera assembly Rust"));ui->actionGenerate_Assembly->setVisible(true);
    ui->actionGenerate_GIMPLE->setText(tr("Genera IR LLVM"));ui->actionGenerate_GIMPLE->setVisible(true);
    updateRustDebugActions();
    ui->menuProject->clear();ui->menuProject->setTitle(tr("Progetto Rust"));ui->menuProject->menuAction()->setVisible(true);
    auto projectOptions=ui->menuProject->addAction(tr("Opzioni progetto / file Rust…"));connect(projectOptions,&QAction::triggered,this,&MainWindow::configureRustProject);
    for(const auto &item:QList<QPair<QString,QString>>{{"doc",tr("Genera documentazione Cargo")},{"fetch",tr("Scarica dipendenze Cargo")},{"bench",tr("Benchmark Cargo")},{"clean",tr("Pulisci artefatti Cargo")}}){
        auto action=ui->menuProject->addAction(item.second);const auto command=item.first;
        action->setObjectName("rust-command-"+command);
        connect(action,&QAction::triggered,this,[this,command]{
            if(command=="clean"){
                if(mRustWorkspace->busy()||(mRustDebugger&&mRustDebugger->active()))return;
                const auto editor=mEditorManager->getEditor();auto root=editor?RustWorkspace::findRoot(editor->filename()):mRustRoot;if(root.isEmpty())root=mRustRoot;
                if(root.isEmpty()){statusBar()->showMessage(tr("Apri un progetto Cargo per pulirne gli artefatti."),10000);return;}
                QStringList protectedFiles;for(int i=0;i<mEditorManager->pageCount();++i)protectedFiles.append((*mEditorManager)[i]->filename());
                // Native directory watches can keep an artefact directory
                // open on Windows. Pause only this model during recycling.
                const auto fileOptions=mFileSystemModel->options();
                mFileSystemModel->setOption(QFileSystemModel::DontWatchForChanges,true);
                mRustLanguageClient->setSuspended(true);
                RustCleanDialog dialog(root,protectedFiles,this);
                const QPointer<MainWindow> self(this);dialog.setProtectedFilesProvider([self]{QStringList files;if(self)for(int i=0;i<self->mEditorManager->pageCount();++i)files.append((*self->mEditorManager)[i]->filename());return files;});
                connect(mEditorManager,&EditorManager::editorOpenned,&dialog,&RustCleanDialog::protectOpenedFiles);dialog.exec();
                if(!self)return;
                mRustLanguageClient->setSuspended(false);
                mFileSystemModel->setOptions(fileOptions);setFilesViewRoot(root);return;
            }
            runRustCommand(command);
        });
        connect(mRustWorkspace,&RustWorkspace::busyChanged,action,[this,action](bool busy){action->setEnabled(!busy&&(!mRustDebugger||!mRustDebugger->active()));});
    }
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
    connect(mRustWorkspace,&RustWorkspace::finished,this,[this](int code,bool cancelled){
        const auto command=mRustWorkspace->command();if(code||cancelled||(command!="asm"&&command!="llvm"))return;
        for(const auto &path:mRustWorkspace->artifacts()){
            QFile file(path);if(!file.open(QIODevice::ReadOnly)||file.size()>8*1024*1024)continue;
            const QPointer<MainWindow> self(this);const QPointer<Editor> editor=mEditorManager->newEditor(command=="asm"?"Assembly Rust.s":"IR Rust.ll",ENCODING_UTF8,command=="asm"?FileType::GAS:FileType::Other,{},false,true);
            if(!self||!editor||editor->removing())return;editor->setContent(QString::fromUtf8(file.readAll()));if(!self||!editor||editor->removing())return;editor->setModified(false);editor->setAutoBackupEnabled(false);editor->setReadOnly(true);mEditorManager->activeEditor(editor,true);
        }
    });
}
void MainWindow::applyRustUiRestrictions(){
    for(QWidget *tab:{ui->tabProject,ui->tabWatch,ui->tabProblemSet})showHideInfosTab(tab,false);
    showHideInfosTab(ui->tabStructure,pSettings->ui().showStructure());ui->actionStructure->setVisible(true);ui->actionStructure->setText(tr("Struttura Rust"));
    for(QWidget *tab:{ui->tabDebug,ui->tabProblem})showHideMessagesTab(tab,false);
    for(QAction *action:{ui->actionProject,ui->actionWatch,ui->actionProblem_Set,ui->actionProblem})action->setVisible(false);
    ui->actionDebug_Window->setVisible(true);ui->actionDebug_Window->setText(tr("Debug Rust"));
    if(mRustDebugPanel)showHideMessagesTab(mRustDebugPanel,pSettings->ui().showDebug());
    ui->toolbarCompilerSet->setVisible(false);
    ui->menuRefactor->menuAction()->setVisible(false);
    if(auto menu=findChild<QMenu *>("rustRefactorMenu"))menu->menuAction()->setVisible(true);
    ui->actionInterrupt->setVisible(true);
}
bool MainWindow::runRustCommand(const QString &command){
    if(mRustWorkspace->busy()||(mRustDebugger&&mRustDebugger->active()))return false;
    const QPointer<MainWindow> self(this);QPointer<Editor> editor=mEditorManager->getEditor();
    // Save the active Rust document before deciding whether it belongs to Cargo.
    if(editor && editor->fileType()==FileType::RustSource && (editor->modified()||editor->isNew())){
        if(!editor->save()||!self||!editor||editor->removing())return false;
    }
    QString root=editor?RustWorkspace::findRoot(editor->filename()):QString();
    if(editor && editor->fileType()==FileType::RustSource && root.isEmpty()){
        ui->txtToolsOutput->clear();ui->tableIssues->clearIssues();
        stretchMessagesPanel(true);ui->tabMessages->setCurrentWidget(ui->tabToolsOutput);
        return mRustWorkspace->startFile(editor->filename(),command);
    }
    if(root.isEmpty())root=mRustRoot;
    if(root.isEmpty()){openRustWorkspace();if(!self)return false;root=mRustRoot;}
    if(root.isEmpty())return false;
    // A failed or cancelled save must abort the command, preserving the document.
    const QString prefix=QDir::cleanPath(root)+"/";
    QList<QPointer<Editor>> documents;for(int i=0;i<mEditorManager->pageCount();++i)documents.append((*mEditorManager)[i]);
    for(const auto &e:documents){if(!e||e->removing())continue;
        const QString path=QDir::fromNativeSeparators(QFileInfo(e->filename()).absoluteFilePath());
        if((e==editor||path.startsWith(prefix,PATH_SENSITIVITY)) && (e->modified()||e->isNew())){
            if(!e->save()||!self||!e||e->removing())return false;
        }
    }
    if(!self||(editor&&editor->removing()))return false;
    for(const auto &e:documents)if(e&&!e->removing()&&(e==editor||QDir::fromNativeSeparators(QFileInfo(e->filename()).absoluteFilePath()).startsWith(prefix,PATH_SENSITIVITY))&&e->modified()){
        statusBar()->showMessage(tr("Un documento è cambiato durante il salvataggio: ripeti il comando."),12000);return false;
    }
    if(editor){const auto detected=RustWorkspace::findRoot(editor->filename());if(!detected.isEmpty())root=detected;}
    mRustRoot=root;ui->txtToolsOutput->clear();ui->tableIssues->clearIssues();
    stretchMessagesPanel(true);ui->tabMessages->setCurrentWidget(ui->tabToolsOutput);
    return mRustWorkspace->start(root,command);
}
void MainWindow::openRustWorkspace(){
    if(mRustWorkspace->busy())return;
    const QPointer<MainWindow> self(this);const QString manifest=QFileDialog::getOpenFileName(this,tr("Apri progetto Cargo"),mRustRoot,"Cargo.toml (Cargo.toml)");
    if(!self||manifest.isEmpty())return;mRustRoot=QFileInfo(manifest).absolutePath();
    setFilesViewRoot(mRustRoot);
    const QDir directory(mRustRoot);
    openFile(manifest);if(!self)return;
    if(QFileInfo::exists(directory.filePath("src/main.rs")))openFile(directory.filePath("src/main.rs"));
    else if(QFileInfo::exists(directory.filePath("src/lib.rs")))openFile(directory.filePath("src/lib.rs"));
    updateCompileActions();
}
void MainWindow::createRustWorkspace(){
    if(mRustWorkspace->busy())return;
    const QPointer<MainWindow> self(this);const QString parent=QFileDialog::getExistingDirectory(this,tr("Cartella per il nuovo progetto Rust"));
    if(!self||parent.isEmpty())return;
    bool ok=false;const QString name=QInputDialog::getText(this,tr("Nuovo progetto Rust"),tr("Nome del progetto"),QLineEdit::Normal,"rust_app",&ok);
    if(!self||!ok)return;
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
void MainWindow::updateRustDebugActions(){
    if(!mRustDebugger)return;
    const auto state=mRustDebugger->state();const bool stopped=state==RustDebugSession::State::Stopped;
    ui->actionInterrupt->setVisible(true);ui->actionInterrupt->setEnabled(state==RustDebugSession::State::Running);
    for(auto action:{ui->actionStep_Over,ui->actionStep_Into,ui->actionStep_Out,ui->actionRun_To_Cursor,ui->actionContinue,ui->actionView_CPU_Window})action->setEnabled(stopped);
    const auto editor=mEditorManager->getEditor();
    const bool rustSource=editor&&editor->fileType()==FileType::RustSource&&!editor->isNew();
    ui->actionRun_To_Cursor->setEnabled(rustSource&&(stopped||(state==RustDebugSession::State::Idle&&!mRustWorkspace->busy()&&!mRustPreparingDebug)));
    ui->actionToggle_Breakpoint->setEnabled(rustSource);
    ui->actionBreakpoint_property->setEnabled(rustSource&&editor->hasBreakpoint(editor->caretY()));
    ui->actionClear_all_breakpoints->setEnabled(rustSource);
    ui->actionAdd_Watch->setEnabled(true);
    ui->actionAdd_Watchpoint->setEnabled(stopped&&mRustDebugger->capabilities().value("supportsDataBreakpoints").toBool());
}
void MainWindow::syncRustBreakpoints(){
    if(!mRustDebugger||!mDebugger)return;
    QHash<QString,QJsonArray> groups;
    for(const auto &breakpoint:mDebugger->breakpointModel()->breakpoints(false)){
        if(!breakpoint->enabled||breakpoint->breakpointType!=BreakpointType::Breakpoint)continue;
        QJsonObject entry{{"line",breakpoint->line+1}};if(!breakpoint->condition.isEmpty())entry.insert("condition",breakpoint->condition);
        if(!breakpoint->hitCondition.isEmpty())entry.insert("hitCondition",breakpoint->hitCondition);
        if(!breakpoint->logMessage.isEmpty())entry.insert("logMessage",breakpoint->logMessage);
        groups[breakpoint->filename].append(entry);
    }
    for(const auto &file:mRustBreakpointFiles)if(!groups.contains(file))mRustDebugger->setBreakpoints(file,{});
    mRustBreakpointFiles.clear();for(auto it=groups.begin();it!=groups.end();++it){mRustDebugger->setBreakpoints(it.key(),it.value());mRustBreakpointFiles.insert(it.key());}
}
void MainWindow::editRustBreakpoint(int index,bool forProject){
    auto breakpoint=mDebugger->breakpointModel()->breakpoint(index,forProject);if(!breakpoint)return;
    QDialog dialog(this);dialog.setObjectName("rustBreakpointOptions");dialog.setWindowTitle(tr("Breakpoint Rust"));auto layout=new QVBoxLayout(&dialog);auto form=new QFormLayout;layout->addLayout(form);
    auto enabled=new QCheckBox(tr("Abilitato"),&dialog);enabled->setChecked(breakpoint->enabled);form->addRow(enabled);
    auto condition=new QLineEdit(breakpoint->condition,&dialog);condition->setObjectName("rustBreakpointCondition");condition->setMaxLength(4096);form->addRow(tr("Condizione:"),condition);
    auto hit=new QLineEdit(breakpoint->hitCondition,&dialog);hit->setObjectName("rustBreakpointHit");hit->setMaxLength(100);hit->setPlaceholderText(tr("Numero di incontri, ad esempio 3"));form->addRow(tr("Hit condition:"),hit);
    auto log=new QLineEdit(breakpoint->logMessage,&dialog);log->setObjectName("rustBreakpointLog");log->setMaxLength(4096);log->setPlaceholderText(tr("Se compilato, stampa senza fermare: total = {total}"));form->addRow(tr("Messaggio logpoint:"),log);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return;
    // The dialog may process events that remove/reorder breakpoints. Validate
    // identity again before writing back to the model.
    if(mDebugger->breakpointModel()->breakpoint(index,forProject)!=breakpoint)return;
    breakpoint->enabled=enabled->isChecked();breakpoint->hitCondition=hit->text();breakpoint->logMessage=log->text();
    breakpoint->timestamp=QDateTime::currentMSecsSinceEpoch();
    mDebugger->setBreakPointCondition(index,condition->text(),forProject);syncRustBreakpoints();
}
void MainWindow::configureRustSourceBreakpoints(){
    QDialog dialog(this);dialog.setObjectName("rustSourceBreakpointDialog");dialog.setWindowTitle(tr("Breakpoint sorgenti Rust"));dialog.resize(800,360);
    auto layout=new QVBoxLayout(&dialog);auto table=new QTableView(&dialog);table->setObjectName("rustSourceBreakpointTable");
    table->setModel(mDebugger->breakpointModel().get());table->setSelectionBehavior(QAbstractItemView::SelectRows);table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->horizontalHeader()->setStretchLastSection(true);layout->addWidget(table);
    auto tools=new QHBoxLayout;layout->addLayout(tools);auto properties=new QPushButton(tr("Proprietà…"),&dialog);auto remove=new QPushButton(tr("Rimuovi"),&dialog);
    properties->setObjectName("rustSourceBreakpointProperties");remove->setObjectName("rustSourceBreakpointRemove");tools->addWidget(properties);tools->addWidget(remove);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);tools->addWidget(buttons);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    const auto refresh=[=]{const bool valid=table->currentIndex().isValid();properties->setEnabled(valid);remove->setEnabled(valid);};
    connect(table->selectionModel(),&QItemSelectionModel::currentChanged,&dialog,refresh);refresh();
    connect(properties,&QPushButton::clicked,&dialog,[this,table]{if(table->currentIndex().isValid())editRustBreakpoint(table->currentIndex().row(),false);});
    connect(remove,&QPushButton::clicked,&dialog,[this,table]{if(!table->currentIndex().isValid())return;mDebugger->removeBreakpoint(table->currentIndex().row(),false);mEditorManager->updateEditorBreakpoints();syncRustBreakpoints();});
    connect(table,&QTableView::doubleClicked,&dialog,[this](const QModelIndex &index){const auto breakpoint=mDebugger->breakpointModel()->breakpoint(index.row(),false);if(!breakpoint)return;if(auto editor=openFile(breakpoint->filename))mEditorManager->activeEditorAndSetCaret(editor,{0,breakpoint->line});});
    dialog.exec();
}
void MainWindow::startRustDebug(const QString &runToFile,int runToLine){
    if(!mRustDebugger||mRustDebugger->active()||mRustWorkspace->busy()||mRustPreparingDebug)return;
    mRustPreparingDebug=true;auto connection=std::make_shared<QMetaObject::Connection>();
    QPointer<Editor> targetEditor=runToFile.isEmpty()?nullptr:mEditorManager->getEditor();
    const QString targetText=targetEditor?targetEditor->text():QString{};
    *connection=connect(mRustWorkspace,&RustWorkspace::finished,this,[this,connection,runToFile,runToLine,targetEditor,targetText](int code,bool cancelled){
        disconnect(*connection);mRustPreparingDebug=false;if(code!=0||cancelled)return;
        if(!runToFile.isEmpty()&&(!targetEditor||targetEditor->removing()||targetEditor->filename()!=runToFile||targetEditor->text()!=targetText)){
            statusBar()->showMessage(tr("Esecuzione fino al cursore annullata: il documento è stato chiuso o modificato durante la compilazione."),12000);return;
        }
        QStringList candidates;for(const auto &path:mRustWorkspace->artifacts())if(QFileInfo(path).isExecutable())candidates.append(path);
        if(candidates.isEmpty()){statusBar()->showMessage(tr("La compilazione non ha prodotto un eseguibile da sottoporre a debug."),10000);return;}
        const QPointer<MainWindow> self(this);QString program=candidates.first();if(candidates.size()>1){bool ok=false;program=QInputDialog::getItem(this,tr("Eseguibile Rust"),tr("Scegli il programma:"),candidates,0,false,&ok);if(!self||!ok)return;}
        if(mRustDebugger->active()||mRustWorkspace->busy())return;
        if(!runToFile.isEmpty()&&(!targetEditor||targetEditor->removing()||targetEditor->filename()!=runToFile||targetEditor->text()!=targetText)){statusBar()->showMessage(tr("Il sorgente è cambiato durante la scelta del programma: ripeti il debug."),12000);return;}
        syncRustBreakpoints();if(!self)return;
        const auto options=mRustWorkspace->options();const auto directory=options.runDirectory(mRustWorkspace->root());
        if(mRustDebugger->launch(program,directory,options.arguments,options.processEnvironment(),runToFile.isEmpty()&&mRustBreakpointFiles.isEmpty(),runToFile,runToLine)){
            showHideMessagesTab(mRustDebugPanel,true);ui->actionDebug_Window->setChecked(true);
            stretchMessagesPanel(true);ui->tabMessages->setCurrentWidget(mRustDebugPanel);updateCompileActions();
        }
    });
    if(!runRustCommand("debug-build")){disconnect(*connection);mRustPreparingDebug=false;}
}
void MainWindow::configureRustProject(){
    if(mRustWorkspace->busy()||(mRustDebugger&&mRustDebugger->active()))return;
    const QPointer<MainWindow> self(this);const QPointer<Editor> editor=mEditorManager->getEditor();auto root=editor?RustWorkspace::findRoot(editor->filename()):mRustRoot;
    if(root.isEmpty()&&editor)root=QFileInfo(editor->filename()).absolutePath();if(root.isEmpty()){openRustWorkspace();if(!self)return;root=mRustRoot;}if(root.isEmpty())return;
    auto dialog=new RustProjectDialog(root,this);const QPointer<RustProjectDialog> guard(dialog);dialog->exec();if(guard)guard->deleteLater();
}
