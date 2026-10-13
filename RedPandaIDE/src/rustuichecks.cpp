// Copyright (C) 2026 Raffaele Mele
// SPDX-License-Identifier: GPL-3.0-or-later
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "editor.h"
#include "editormanager.h"
#include "rustworkspace.h"
#include "rusttools.h"
#include "rustlspclient.h"
#include "rustdebugsession.h"
#include "rustdebugpanel.h"
#include "rustprojectdialog.h"
#include "rustcleandialog.h"
#include "rustprojectoptions.h"
#include "rustnavigation.h"
#include "codesnippetsmanager.h"
#include "widgets/aboutdialog.h"
#include "settingsdialog/settingsdialog.h"
#include "sessionrecovery.h"
#include "sessionstore.h"
#include "settings.h"
#include "debugger/debugger.h"
#include "debugger/debuggermodels.h"
#ifdef ENABLE_VCS
#include "vcs/gitmanager.h"
#endif
#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QTimer>
#include <QMessageBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCursor>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QListWidget>
#include <QTreeWidget>
#include <QTreeView>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QHeaderView>
#include <QTreeWidgetItemIterator>
#include <QPersistentModelIndex>
#include <QContextMenuEvent>
#include <QMenu>
#include <QUrl>
#include <QtTest>
#include <limits>
#include <qsynedit/syntaxer/rust.h>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

int MainWindow::runRustUiChecks(const QString &outputDir){
    QTimer modalCloser;
    modalCloser.setInterval(50);
    connect(&modalCloser,&QTimer::timeout,this,[]{
        for(auto *w:QApplication::topLevelWidgets())if(auto *m=qobject_cast<QMessageBox *>(w))
            m->done(m->standardButtons().testFlag(QMessageBox::Yes)?QMessageBox::Yes:QMessageBox::Ok);
    });
    modalCloser.start();
    QJsonArray checks;int failed=0;
    const auto check=[&](const QString &name,bool ok){qInfo().noquote()<<"RUST_UI_CHECK"<<name<<(ok?"PASS":"FAIL");checks.append(QJsonObject{{"name",name},{"passed",ok}});if(!ok)++failed;};
    const int savedEditorFont=pSettings->editor().fontSize(),savedUiFont=pSettings->environment().interfaceFontSize();const auto savedZoom=pSettings->environment().iconZoomFactor();
    pSettings->editor().setFontSize(INT_MAX);pSettings->environment().setInterfaceFontSize(INT_MIN);pSettings->environment().setIconZoomFactor(std::numeric_limits<double>::quiet_NaN());
    check("Corrupt font and icon values are bounded",pSettings->editor().fontSize()==72&&pSettings->environment().interfaceFontSize()==6&&pSettings->environment().iconZoomFactor()==1.0);
    pSettings->editor().setFontSize(savedEditorFont);pSettings->environment().setInterfaceFontSize(savedUiFont);pSettings->environment().setIconZoomFactor(savedZoom);
    const auto put=[](const QString &path,const QByteArray &content){
        // Test-side external writers can race a background source reader on
        // Windows. Retry an atomic replacement, preserving the old file.
        for(int attempt=0;attempt<10;++attempt){QSaveFile file(path);if(file.open(QIODevice::WriteOnly)&&file.write(content)==content.size()&&file.commit())return true;
            QEventLoop wait;QTimer::singleShot(30,&wait,&QEventLoop::quit);wait.exec();}
        return false;
    };
    const auto outlineInput=qEnvironmentVariable("RUST_OUTLINE_CHECK_SOURCE");
    const auto outlineSource=outlineInput.isEmpty()?QString{}:QFileInfo(outlineInput).absoluteFilePath();
    if(!outlineSource.isEmpty()){
        const auto phase=qEnvironmentVariable("RUST_OUTLINE_CHECK_PHASE");
        if(phase=="resume"){
            const auto name=findChild<QAction *>("rustOutlineSortName"),type=findChild<QAction *>("rustOutlineSortType"),traits=findChild<QAction *>("rustOutlineShowTraits");
            check("Structure preferences survive restart",name&&type&&traits&&name->isChecked()&&type->isChecked()&&!traits->isChecked());
            check("Interrupted session restores open file",mEditorManager->getOpenedEditor(outlineSource)!=nullptr);
            check("Interrupted session restores working folder",QDir::cleanPath(pSettings->environment().currentFolder())==QDir::cleanPath(QFileInfo(outlineSource).absolutePath()));
            check("Interrupted session restores process working directory",QDir::cleanPath(QDir::currentPath())==QDir::cleanPath(QFileInfo(outlineSource).absolutePath()));
        }
        setFilesViewRoot(QFileInfo(outlineSource).absolutePath(),true);
        auto editor=openFile(outlineSource);
        auto outline=findChild<QTreeView *>("rustOutline");
        auto model=mRustNavigation?mRustNavigation->outlineModel():nullptr;
        const std::function<QModelIndex(const QString &,const QModelIndex &)> findSymbol=[&](const QString &name,const QModelIndex &parent)->QModelIndex{
            if(!model)return {};for(int row=0;row<model->rowCount(parent);++row){const auto index=model->index(row,0,parent);if(model->data(index,RustOutlineModel::NameRole)==name)return index;const auto child=findSymbol(name,index);if(child.isValid())return child;}return {};
        };
        check("Reported Rust file opens",editor&&outline);
        if(editor&&outline){
            ui->tabExplorer->setCurrentWidget(ui->tabStructure);
            int completed=0;
            for(int attempt=0;attempt<60&&completed<20;++attempt){
                QModelIndex target;QElapsedTimer deadline;deadline.start();
                while(!target.isValid()&&deadline.elapsed()<45000){
                    target=findSymbol("set",{});if(!target.isValid())QTest::qWait(25);
                }
                check(QString("ContaLitri::set exists %1").arg(completed),target.isValid());if(!target.isValid())break;
                const auto position=model->location(target);const QPersistentModelIndex index(target);
                outline->expandAll();outline->scrollTo(target);outline->setCurrentIndex(target);outline->setFocus();QTest::qWait(30);
                if(!index.isValid()){check("Outline refresh invalidates test index safely",true);continue;}
                const auto point=outline->visualRect(index).center();
                // QTest's double-click event does not include the preceding
                // press/release needed by QAbstractItemView's pressed index.
                if(completed%4==3)QTest::keyClick(outline,Qt::Key_Return);else {QTest::mouseClick(outline->viewport(),Qt::LeftButton,Qt::NoModifier,point);QTest::mouseDClick(outline->viewport(),Qt::LeftButton,Qt::NoModifier,point);}QTest::qWait(30);
                check(QString("Structure activation reaches set %1").arg(completed),editor->caretY()==position.line&&editor->caretX()==position.character);
                check(QString("Source unchanged after navigation %1").arg(completed),!editor->modified());++completed;
            }
            check("Twenty actual structure activations completed",completed==20);
            check("Class icon restored",!model->data(findSymbol("ContaLitri",{}),Qt::DecorationRole).value<QIcon>().isNull());
            check("Method icon restored",!model->data(findSymbol("set",{}),Qt::DecorationRole).value<QIcon>().isNull());
            check("Field icon restored",!model->data(findSymbol("counter",{}),Qt::DecorationRole).value<QIcon>().isNull());
            const auto colored=[&](const QString &name,int minimumHue,int maximumHue){
                const auto pixels=model->data(findSymbol(name,{}),Qt::DecorationRole).value<QIcon>().pixmap(32,32).toImage();int count=0;
                for(int y=0;y<pixels.height();++y)for(int x=0;x<pixels.width();++x){const auto c=pixels.pixelColor(x,y);if(c.alpha()>200&&c.hsvSaturation()>100&&c.hue()>=minimumHue&&c.hue()<=maximumHue)++count;}return count>50;
            };
            check("Class icon has blue category color",colored("ContaLitri",180,260));
            check("Field icon has red category color",colored("counter",0,25));
            check("Method icon has category color",colored("set",0,359));
            check("Structure header matches original layout",outline->isHeaderHidden());
            check("Structure icons follow tree font size",outline->iconSize()==QSize(qMax(8,outline->fontMetrics().capHeight()+2),qMax(8,outline->fontMetrics().capHeight()+2))&&outline->iconSize().height()<=outline->fontMetrics().height());
            const int oldFontSize=pSettings->environment().interfaceFontSize();int smallIcon=0,largeIcon=0;
            for(const int fontSize:{8,18}){pSettings->environment().setInterfaceFontSize(fontSize);applySettings();QTest::qWait(30);
                const int wanted=qMax(8,outline->fontMetrics().capHeight()+2);check(QString("Structure icon tracks preference font %1").arg(fontSize),outline->iconSize()==QSize(wanted,wanted));if(fontSize==8)smallIcon=wanted;else largeIcon=wanted;
            }
            check("Structure icon grows proportionally with font",smallIcon<largeIcon);pSettings->environment().setInterfaceFontSize(oldFontSize);applySettings();
            const auto names=[&](const QModelIndex &parent){QStringList result;for(int row=0;row<model->rowCount(parent);++row)result.append(model->data(model->index(row,0,parent),RustOutlineModel::NameRole).toString());return result;};
            auto sortName=findChild<QAction *>("rustOutlineSortName"),sortType=findChild<QAction *>("rustOutlineSortType");
            check("Existing sort controls connected",sortName&&sortType);
            if(sortName&&sortType){
                sortName->setChecked(false);sortType->setChecked(false);mRustNavigation->setOutlineOptions(false,false,true);const auto orderedSource=names(findSymbol("ContaLitri",{}));
                sortName->trigger();const auto alpha=names(findSymbol("ContaLitri",{}));auto expected=orderedSource;std::sort(expected.begin(),expected.end(),[](const QString&a,const QString&b){return a.compare(b,Qt::CaseInsensitive)<0;});check("Sort name button changes tree alphabetically",alpha==expected&&alpha!=orderedSource);
                sortType->trigger();const auto typed=names(findSymbol("ContaLitri",{}));check("Sort type button groups fields after methods",!typed.isEmpty()&&typed.last()=="counter");
                sortName->trigger();sortType->trigger();check("Sort buttons restore source order",names(findSymbol("ContaLitri",{}))==orderedSource);
            }
        }
        QTest::qWait(1200);
        QFile journal(QDir(pSettings->dirs().config()).filePath("lastopens.json"));
        const bool readable=journal.open(QIODevice::ReadOnly);
        const auto state=readable?QJsonDocument::fromJson(journal.readAll()).object():QJsonObject{};
        check("Open-session journal exists before closing",readable);
        check("Working folder persisted before closing",QDir::cleanPath(state.value("workingFolder").toString())==QDir::cleanPath(QFileInfo(outlineSource).absolutePath()));
        bool recorded=false;for(const auto &entry:state.value("files").toArray())if(QFileInfo(entry.toObject().value("filename").toString()).absoluteFilePath()==QFileInfo(outlineSource).absoluteFilePath())recorded=true;
        check("Saved source path persisted before closing",recorded);
        if(outline){const auto before=editor?editor->caretXY():QSynedit::CharPos{};outline->activated({});check("Invalid outline activation is harmless",!editor||editor->caretXY()==before);}
        if(phase=="persist"||phase=="interrupt"){
            auto name=findChild<QAction *>("rustOutlineSortName"),type=findChild<QAction *>("rustOutlineSortType"),traits=findChild<QAction *>("rustOutlineShowTraits");
            if(name&&!name->isChecked())name->trigger();if(type&&!type->isChecked())type->trigger();if(traits&&traits->isChecked())traits->trigger();
            check("Structure preference journal commits",pSettings->sync()==QSettings::NoError);
        }
        QFile report(outputDir+"/checks.json");if(report.open(QIODevice::WriteOnly))report.write(QJsonDocument(QJsonObject{{"failed",failed},{"checks",checks}}).toJson());
        report.close();
        if(outline)outline->expandAll();grab().save(outputDir+"/outline.png");
#ifdef Q_OS_WIN
        if(phase=="interrupt"){qInfo()<<"RUST_QA intentionally terminating isolated session";TerminateProcess(GetCurrentProcess(),86);}
#endif
        return failed?1:0;
    }
    {
        // These probes change only files generated inside the isolated QA root.
        const auto first=outputDir+"/reentry-first.rs",second=outputDir+"/reentry-second.rs";
        check("Reentry fixtures",put(first,"fn first() {}\n")&&put(second,"fn second() {}\n"));
        QPointer<Editor> editor(openFile(first));
        if(editor){
            const auto connection=connect(editor,&QSynedit::QSynEdit::changed,this,[&,editor]{if(editor)mEditorManager->forceCloseEditor(editor);});
            editor->setSelText("//closed from callback\n");disconnect(connection);
            check("Close from changed detaches tab before return",!mEditorManager->getOpenedEditor(first));
            check("Close from changed defers destruction",editor&&editor->removing());
            QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);check("Deferred editor destroyed",!editor);
        }else check("Close callback fixture opens",false);
        const auto closedOnOpen=connect(mEditorManager,&EditorManager::editorOpenned,this,[&]{if(auto e=mEditorManager->getOpenedEditor(first))mEditorManager->forceCloseEditor(e);});
        check("Open callback closes target safely",openFile(first)==nullptr);disconnect(closedOnOpen);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QPointer<Editor> firstEditor(openFile(first));
        const auto before=firstEditor?firstEditor->text():QString{};
        const auto edit=[](const QString &replacement){return QJsonObject{{"range",QJsonObject{{"start",QJsonObject{{"line",0},{"character",3}}},{"end",QJsonObject{{"line",0},{"character",8}}}}},{"newText",replacement}};};
        const QJsonObject proposed{{"changes",QJsonObject{{QUrl::fromLocalFile(first).toString(),QJsonArray{edit("other")}},
            {QUrl::fromLocalFile(second).toString(),QJsonArray{QJsonObject{{"range",QJsonObject{{"start",QJsonObject{{"line",0},{"character",3}}},{"end",QJsonObject{{"line",0},{"character",9}}}}},{"newText","other2"}}}}}}};
        if(firstEditor){
            const auto changeEarlier=connect(mEditorManager,&EditorManager::editorOpenned,this,[&]{if(firstEditor&&mEditorManager->getOpenedEditor(second))firstEditor->setContent(before+"\n// callback");});
            check("Workspace edit rejects earlier target changed on final open",!mRustNavigation->applyEdits(proposed,firstEditor,{{first,before}},false));disconnect(changeEarlier);
            const auto actualCallbackText=firstEditor->text().replace("\r\n","\n").replace('\r','\n');const auto expectedCallbackText=(before+"\n// callback").replace("\r\n","\n").replace('\r','\n');
            check("Rejected workspace edit preserves callback text",actualCallbackText==expectedCallbackText);
            if(actualCallbackText!=expectedCallbackText)qWarning()<<"QA callback text actual/expected"<<actualCallbackText<<expectedCallbackText;
            auto secondEditor=mEditorManager->getOpenedEditor(second);check("Rejected workspace edit leaves second target unchanged",secondEditor&&secondEditor->text().contains("fn second"));
            firstEditor->setContent(before);firstEditor->setModified(false);
            bool once=true;QTimer mutatePreview;mutatePreview.setInterval(20);connect(&mutatePreview,&QTimer::timeout,this,[&]{
                for(auto w:QApplication::topLevelWidgets())if(w->objectName()=="rustWorkspaceEditPreview")if(auto d=qobject_cast<QDialog *>(w)){if(once){once=false;firstEditor->setContent(before+"// preview\n");}d->accept();}
            });mutatePreview.start();check("Workspace edit rejects document changed in preview",!mRustNavigation->applyEdits(proposed,firstEditor,{},true));mutatePreview.stop();
            check("Preview rejection leaves second target unchanged",secondEditor&&secondEditor->text().contains("fn second"));
            if(secondEditor)mEditorManager->forceCloseEditor(secondEditor);if(firstEditor)mEditorManager->forceCloseEditor(firstEditor);
        }else check("Workspace edit fixture opens",false);
    }
    QDir().mkpath(outputDir+"/cargo-project/src");const QString root=outputDir+"/cargo-project";
    check("manifest",put(root+"/Cargo.toml","[package]\nname=\"rust_ui_probe\"\nversion=\"0.1.0\"\nedition=\"2024\"\n"));
    const QString source=root+"/src/main.rs";
    check("source",put(source,"fn main(){println!(\"RUST_GUI_OK\");}\n"));
    mRustRoot=root;setFilesViewRoot(root);Editor *editor=openFile(source);
    if(editor){mEditorManager->activeEditor(editor,true);updateEditorActions(editor);}
    check("Rust editor",editor && editor->syntaxer()->language()==QSynedit::ProgrammingLanguage::Rust);
    check("Identity",windowTitle().contains("Red Panda Rust"));
    check("Icon",!windowIcon().isNull());
    check("Compile enabled",ui->actionCompile->isEnabled());
    check("Rust formatting action enabled",ui->actionReformat_Code->isEnabled());
    check("C++ compiler selector hidden",!mCompilerSet->isVisible());
    check("Rust debug exposed",ui->actionDebug->isVisible());
    check("C++ problem panels hidden",ui->tabExplorer->indexOf(ui->tabProblemSet)<0 && ui->tabMessages->indexOf(ui->tabProblem)<0);
    if(editor){
        editor->setCaretXY({0,0});editor->setSelText(QString::fromUtf8("// salvataggio città € Rust\n"));
        check("Atomic source save",editor->save());
        QFile saved(source);check("Saved UTF-8 text",saved.open(QIODevice::ReadOnly)&&saved.readAll().contains(QString::fromUtf8("città € Rust").toUtf8()));saved.close();
        const bool autoFormat=pSettings->editor().autoFormatWhenSaved();pSettings->editor().setAutoFormatWhenSaved(true);
        editor->setContent("fn main(){let n=1;println!(\"RUST_GUI_OK {n}\");}");
        check("Rust automatic format on save succeeds",editor->save());check("Rust automatic save uses rustfmt buffer",editor->text().contains("let n = 1;")&&editor->text().contains("RUST_GUI_OK"));
        pSettings->editor().setAutoFormatWhenSaved(autoFormat);
        const QString oldFilename=editor->filename();
        editor->setCaretXY({0,0});editor->setSelText("// preserved after save failure\n");
        const QString modifiedText=editor->text();
        check("Failure blocker",put(outputDir+"/save-blocker","not a directory"));
        check("Save As failure reported",!editor->saveAs(outputDir+"/save-blocker/main.rs",false));
        check("Failed save preserves filename",editor->filename()==oldFilename);
        check("Failed save preserves modified text",editor->modified()&&editor->text()==modifiedText);
        check("Retry source save",editor->save());
    }
    int result=-999;bool timedOut=false;bool cancelled=false;
    const auto run=[&](const QString &command){
        result=-999;timedOut=false;cancelled=false;
        QEventLoop loop;QTimer timeout;timeout.setSingleShot(true);timeout.setInterval(60000);
        auto c=connect(mRustWorkspace,&RustWorkspace::finished,&loop,[&](int code,bool stop){result=code;cancelled=stop;loop.quit();});
        connect(&timeout,&QTimer::timeout,&loop,[&]{timedOut=true;mRustWorkspace->stop();loop.quit();});
        bool started=false;
        if(command=="build"||command=="run"||command=="check"){
            QAction *action=command=="build"?ui->actionCompile:command=="run"?ui->actionRun:ui->actionRebuild;
            if(action->isEnabled()){action->trigger();started=mRustWorkspace->busy()||result!=-999;}
        }else started=runRustCommand(command);
        if(started&&result==-999){timeout.start();loop.exec();}
        disconnect(c);return started&&!timedOut;
    };
    check("Cargo build GUI",run("build")&&result==0);
    check("Cargo run GUI",run("run")&&result==0&&ui->txtToolsOutput->toPlainText().contains("RUST_GUI_OK"));
    check("Cargo fmt GUI",run("fmt")&&result==0);
    // Decline an external file reload to verify that in-memory text survives.
    QTimer::singleShot(0,this,[]{for(auto *w:QApplication::topLevelWidgets())if(auto *m=qobject_cast<QMessageBox *>(w))m->done(QMessageBox::No);});
    QApplication::processEvents();
    if(editor) mEditorManager->closeEditor(editor,true,true);
    check("Invalid source",put(source,QString::fromUtf8("fn main() { let crab=\"🦀\"; let _:u32 = \"invalid\"; }\n").toUtf8()));
    editor=openFile(source);
    check("Compiler error handled",run("check")&&result!=0);
    check("Original issues table",ui->tableIssues->count()>0);
    if(editor){bool navigated=false;for(int row=0;row<ui->tableIssues->count();++row){const auto issue=ui->tableIssues->issue(row);if(issue&&issue->type==CompileIssueType::Error&&issue->filename==source){on_tableIssues_doubleClicked(ui->tableIssues->issuesModel()->index(row,0));navigated=editor->caretY()==0&&editor->caretX()==editor->lineText(0).indexOf("\"invalid\"");break;}}
        check("Rust compiler diagnostic navigates exact UTF-16 position",navigated);
    }
    check("GUI remained enabled",ui->actionCompile->isEnabled());
    if(editor)mEditorManager->closeEditor(editor,true,true);
    check("Corrected source",put(source,"fn main() { println!(\"RUST_GUI_RECOVERED\"); }\n"));
    editor=openFile(source);check("Retry after error",run("run")&&result==0&&ui->txtToolsOutput->toPlainText().contains("RUST_GUI_RECOVERED"));
    // An unrelated .rs must ignore the previously selected Cargo workspace.
    const QString standalone=outputDir+"/singolo con-spazi.rs";
    check("Standalone source",put(standalone,"fn main(){println!(\"SINGLE_GUI_OK\");}\n"));
    editor=openFile(standalone);
    check("Standalone build GUI",run("build")&&result==0);
    check("Standalone run GUI",run("run")&&result==0&&ui->txtToolsOutput->toPlainText().contains("SINGLE_GUI_OK"));
    check("Standalone uses rustc",ui->txtToolsOutput->toPlainText().contains("rustc.exe"));
    if(editor)mEditorManager->closeEditor(editor,true,true);
    check("Standalone error source",put(standalone,"fn main(){let _:u32=\"invalid\";}\n"));
    editor=openFile(standalone);
    check("Standalone diagnostic GUI",run("check")&&result!=0&&ui->tableIssues->count()>0);
    if(editor)mEditorManager->closeEditor(editor,true,true);
    check("Standalone restored source",put(standalone,"fn main(){println!(\"SINGLE_GUI_OK\");}\n"));
    editor=openFile(standalone);check("Standalone retry GUI",run("run")&&result==0);
    if(editor)mEditorManager->closeEditor(editor,true,true);
    QFile fixture(QStringLiteral(RUST_TEST_DATA_DIR)+"/rust-large.rs");
    check("Large Rust fixture",fixture.open(QIODevice::ReadOnly));
    const QString hoverFile=outputDir+"/hover-large.rs";
    check("Hover fixture copy",put(hoverFile,fixture.readAll()));
    Editor *hover=openFile(hoverFile);
    if(hover){
        check("Large fixture length",hover->lineCount()>=3000);
        hover->setCaretXY({4,20});hover->setSelText("o");
        check("Exact implo edit",hover->lineText(20).startsWith("implo TestError"));
        check("Rust never uses C++ expression state",hover->getExpressionAtPosition({2,20}).isEmpty());
        hover->setCaretXYCentered({2,20});QApplication::processEvents();
        if(QGuiApplication::platformName()=="offscreen"){
            const QPoint point=hover->displayCoordToPixels(hover->bufferToDisplayPos({2,20}))+QPoint(2,2);
            QCursor::setPos(hover->mapToGlobal(point));
            for(int i=0;i<10;++i)check("Hover timer implo",QMetaObject::invokeMethod(hover,"onTooltipTimer",Qt::DirectConnection));
            QMouseEvent move(QEvent::MouseMove,QPointF(point),Qt::NoButton,Qt::NoButton,Qt::ControlModifier);
            QApplication::sendEvent(hover->viewport(),&move);
            check("Ctrl hover preserves text",hover->lineText(20).startsWith("implo"));
        }
        mEditorManager->closeEditor(hover,true,true);
    }else check("Hover editor",false);
    {
        QElapsedTimer elapsed;elapsed.start();bool survived=true;
        for(int cycle=0;cycle<100;++cycle){
            auto large=openFile(hoverFile);
            if(!large||large->lineCount()<3000){survived=false;break;}
            large->setCaretXYCentered({4,20});QApplication::processEvents();
            if(!mEditorManager->closeEditor(large,true,true)){survived=false;break;}
            QApplication::processEvents();
        }
        check("100 large document open close cycles",survived);
        checks.append(QJsonObject{{"name","Large document lifecycle duration"},{"passed",true},{"milliseconds",elapsed.elapsed()}});
    }
    {
        const QString semanticPath=outputDir+"/semantica città.rs";
        const QByteArray semanticSource="fn semantic_answer() -> i32 { 42 }\nfn main(){ let value = seman; println!(\"{}\", value); }\n";
        check("Semantic source",put(semanticPath,semanticSource));
        auto semantic=openFile(semanticPath);
        if(semantic){
            QApplication::setActiveWindow(this);activateWindow();
            semantic->setCaretXY({28,1});semantic->setFocus();QApplication::processEvents();
            check("Semantic editor focused",semantic->hasFocus());
            auto popup=semantic->findChild<QListWidget *>("rustSemanticCompletion");
            QElapsedTimer deadline;deadline.start();bool completed=false;
            while(!completed&&deadline.elapsed()<45000){
                QKeyEvent request(QEvent::KeyPress,Qt::Key_Space,Qt::ControlModifier);QApplication::sendEvent(semantic,&request);
                QEventLoop wait;QTimer::singleShot(500,&wait,&QEventLoop::quit);wait.exec();
                if(popup&&popup->isVisible())for(int i=0;i<popup->count();++i)if(popup->item(i)->text().startsWith("semantic_answer")){
                    popup->setCurrentRow(i);QKeyEvent accept(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);
                    QApplication::sendEvent(semantic,&accept);QEventLoop settle;QTimer::singleShot(500,&settle,&QEventLoop::quit);settle.exec();
                    completed=semantic->text().contains("value = semantic_answer()");break;
                }
            }
            check("Real semantic completion insertion",completed);
            if(!completed){
                QJsonArray rows;if(popup)for(int i=0;i<std::min(10,popup->count());++i)rows.append(popup->item(i)->text());
                check("Semantic diagnostic snapshot",put(outputDir+"/semantic-state.json",QJsonDocument(QJsonObject{
                    {"focused",semantic->hasFocus()},{"ready",mRustLanguageClient->ready()},
                    {"caretLine",semantic->caretY()},{"caretCharacter",semantic->caretX()},
                    {"text",semantic->text()},{"rows",rows},{"popupVisible",popup&&popup->isVisible()}}).toJson()));
            }
            if(completed){
                semantic->undo();check("Semantic completion single undo",semantic->text().contains("value = seman;"));
                semantic->redo();check("Semantic completion redo",semantic->text().contains("value = semantic_answer()"));
                check("Save semantic completion",semantic->save());
                check("Completed Rust code compiles",run("check")&&result==0);
                const auto definitionReached=[&]{QElapsedTimer clock;clock.start();while(semantic->caretY()!=0&&clock.elapsed()<10000)QTest::qWait(30);return semantic->caretY()==0;};
                const int callColumn=semantic->lineText(1).indexOf("semantic_answer")+2;
                semantic->setCaretXY({callColumn,1});mEditorManager->activeEditor(semantic,true);updateEditorActions(semantic);
                check("Rust definition menu enabled",ui->actionGoto_Definition->isEnabled());ui->actionGoto_Definition->trigger();check("Rust definition menu navigates",definitionReached());
                semantic->setCaretXY({callColumn,1});ui->actionGoto_Declaration->trigger();check("Rust declaration menu navigates",definitionReached());
                const auto click=semantic->displayCoordToPixels(semantic->bufferToDisplayPos({callColumn,1}))+QPoint(2,semantic->textHeight()/2);
                QTest::mouseClick(semantic->viewport(),Qt::LeftButton,Qt::ControlModifier,click);check("Rust Ctrl click navigates",definitionReached());
                const bool enabled=pSettings->codeCompletion().enabled();const auto stableText=semantic->text();pSettings->codeCompletion().setEnabled(false);
                QKeyEvent disabledRequest(QEvent::KeyPress,Qt::Key_Space,Qt::ControlModifier);QApplication::sendEvent(semantic,&disabledRequest);
                check("Rust completion enable preference takes effect",popup&&!popup->isVisible()&&semantic->text()==stableText);pSettings->codeCompletion().setEnabled(enabled);
            }
            mEditorManager->closeEditor(semantic,true,true);
        }else check("Semantic editor",false);
    }
    {
        check("Native Rust snippet library",codeSnippetManager()->snippetsForLanguage("rust").size()>=10);
        auto newFile=mEditorManager->newEditor("snippet test.rs",ENCODING_UTF8,FileType::RustSource,{},false,true);
        check("Rust new file template",newFile&&newFile->text().contains("fn main()"));
        if(newFile){
            const auto snippets=codeSnippetManager()->snippetsForLanguage("rust");
            for(const auto &snippet:snippets)if(snippet->prefix=="for"){
                newFile->setContent("");newFile->insertCodeSnippet(snippet->code);check("Rust native snippet insertion",newFile->text().contains("for item in items")&&newFile->selText()=="item");
                newFile->undo();check("Rust native snippet undo",newFile->text().isEmpty());break;
            }
            mEditorManager->closeEditor(newFile,true,true);
        }
    }
    {
        const auto root=outputDir+"/rename-project";QDir().mkpath(root+"/src");
        check("Refactor manifest",put(root+"/Cargo.toml","[package]\nname=\"rename_ui\"\nversion=\"0.1.0\"\nedition=\"2024\"\n"));
        const auto mainPath=root+"/src/main.rs",helperPath=root+"/src/helpers.rs";
        check("Refactor main",put(mainPath,"mod helpers;\nuse helpers::answer;\nfn main(){println!(\"{}\",answer());}\n"));check("Refactor helper",put(helperPath,"pub fn answer()->i32{42}\n"));
        auto mainEditor=openFile(mainPath);auto helperEditor=openFile(helperPath);
        const auto mainBefore=mainEditor?mainEditor->text():QString(),helperBefore=helperEditor?helperEditor->text():QString();
        const auto waitFor=[](const std::function<bool()> &predicate,int timeout){QElapsedTimer elapsed;elapsed.start();while(!predicate()&&elapsed.elapsed()<timeout){QEventLoop loop;QTimer::singleShot(30,&loop,&QEventLoop::quit);loop.exec();}return predicate();};
        if(mainEditor&&helperEditor){
            QApplication::setActiveWindow(this);helperEditor->setFocus();helperEditor->setCaretXY({9,0});
            auto outline=findChild<QTreeView *>("rustOutline");check("Rust outline populated",waitFor([&]{return outline&&outline->model()->rowCount()>0;},30000));
            QJsonArray locations;const auto refs=connect(mRustNavigation,&RustNavigation::referencesReady,this,[&](const QJsonArray &value){locations=value;});
            mRustNavigation->references();check("GUI Rust references",waitFor([&]{return locations.size()>=3;},15000));disconnect(refs);
            for(auto widget:QApplication::topLevelWidgets())if(widget->objectName()=="rustReferences")widget->close();
            QApplication::setActiveWindow(this);helperEditor->setFocus();helperEditor->setCaretXY({9,0});
            int documents=0;const auto applied=connect(mRustNavigation,&RustNavigation::editsApplied,this,[&](int count,int){documents=count;});
            QTimer acceptPreview;acceptPreview.setInterval(30);connect(&acceptPreview,&QTimer::timeout,this,[]{for(auto widget:QApplication::topLevelWidgets())if(widget->objectName()=="rustWorkspaceEditPreview")if(auto dialog=qobject_cast<QDialog *>(widget))dialog->accept();});acceptPreview.start();
            check("GUI Rust rename requested",mRustNavigation->renameTo("renamed_answer")>0);check("GUI Rust multi-file rename",waitFor([&]{return documents==2;},30000));acceptPreview.stop();disconnect(applied);
            check("Renamed buffers",mainEditor->text().contains("renamed_answer()")&&helperEditor->text().contains("fn renamed_answer"));
            QFile unchanged(helperPath);check("Rename keeps disk until save",unchanged.open(QIODevice::ReadOnly)&&unchanged.readAll().contains("fn answer"));unchanged.close();
            mainEditor->undo();helperEditor->undo();check("Multi-file rename undo",mainEditor->text()==mainBefore&&helperEditor->text()==helperBefore);
            mainEditor->redo();helperEditor->redo();check("Multi-file rename redo",mainEditor->text().contains("renamed_answer")&&helperEditor->text().contains("renamed_answer"));
            check("Renamed code compiles",run("check")&&result==0);
            mEditorManager->closeEditor(helperEditor,true,true);mEditorManager->closeEditor(mainEditor,true,true);
        }else check("Refactor editors",false);
    }
    {
        QDir().mkpath(outputDir+"/config-cwd");RustProjectDialog options(outputDir,this);options.show();QApplication::processEvents();
        auto profile=options.findChild<QComboBox *>("rustBuildProfile");auto arguments=options.findChild<QPlainTextEdit *>("rustRunArguments");
        auto environment=options.findChild<QPlainTextEdit *>("rustRunEnvironment");auto directory=options.findChild<QLineEdit *>("rustRunDirectory");auto buttons=options.findChild<QDialogButtonBox *>();
        check("Project options fields",profile&&arguments&&environment&&directory&&buttons);
        if(profile&&arguments&&environment&&directory&&buttons){
            profile->setCurrentIndex(1);arguments->setPlainText(QString::fromUtf8("two words\ncittà"));environment->setPlainText("RPR_GUI_OPTIONS=ENV_GUI_OK");directory->setText("config-cwd");
            check("Project options screenshot",options.grab().save(outputDir+"/project-options.png"));buttons->button(QDialogButtonBox::Save)->click();
            const auto saved=RustProjectOptions::load(outputDir);check("Project options saved",options.result()==QDialog::Accepted&&saved.release&&saved.arguments.size()==2&&saved.environment.value("RPR_GUI_OPTIONS")=="ENV_GUI_OK");
            const auto configuredFile=outputDir+"/configured.rs";
            check("Configured program source",put(configuredFile,"fn main(){println!(\"ARGS {:?}\",std::env::args().skip(1).collect::<Vec<_>>());println!(\"{}\",std::env::var(\"RPR_GUI_OPTIONS\").unwrap());}\n"));
            auto configured=openFile(configuredFile);check("GUI configured run",configured&&run("run")&&result==0&&ui->txtToolsOutput->toPlainText().contains("two words")&&ui->txtToolsOutput->toPlainText().contains("ENV_GUI_OK"));
            if(configured)mEditorManager->closeEditor(configured,true,true);
            QString error;check("Reset test project options",RustProjectOptions{}.save(outputDir,error));
        }else options.reject();
    }
    {
        const QString debugFile=outputDir+"/debug reale.rs";
        check("Debug source",put(debugFile,"fn main(){\n    let mut total=7_i32;\n    total+=2;\n    println!(\"DEBUG_UI {}\",total);\n    let values: Vec<i32>=(0..450).collect();\n    println!(\"VALUES_UI {}\",values.len());\n    let mut input=String::new();\n    std::io::stdin().read_line(&mut input).unwrap();\n    println!(\"INPUT_UI {}\",input.trim());\n}\n"));
        auto debugEditor=openFile(debugFile);
        const auto waitFor=[](const std::function<bool()> &predicate,int timeout){
            QElapsedTimer elapsed;elapsed.start();while(!predicate()&&elapsed.elapsed()<timeout){QEventLoop loop;QTimer::singleShot(30,&loop,&QEventLoop::quit);loop.exec();}return predicate();
        };
        if(debugEditor){
            debugEditor->setCaretXY({0,3});debugEditor->setFocus();QApplication::processEvents();
            const auto gutterPoint=[&](int line){const auto pixel=debugEditor->displayCoordToPixels(debugEditor->bufferToDisplayPos({0,line}));return QPoint(qMax(2,debugEditor->gutterWidth()/2),pixel.y()+debugEditor->textHeight()/2);};
            QTest::mouseClick(debugEditor->viewport(),Qt::LeftButton,Qt::NoModifier,gutterPoint(3));
            check("Rust breakpoint by gutter mouse click",debugEditor->hasBreakpoint(3));
            QApplication::setActiveWindow(this);mEditorManager->activeEditor(debugEditor,true);debugEditor->setCaretXY({0,3});debugEditor->setFocus();QApplication::processEvents();
            QTest::keyClick(debugEditor,Qt::Key_F4,Qt::ControlModifier);
            check("Rust breakpoint removal by keyboard",!debugEditor->hasBreakpoint(3));
            QTest::keyClick(debugEditor,Qt::Key_F4,Qt::ControlModifier);
            check("Rust breakpoint insertion by keyboard",debugEditor->hasBreakpoint(3));
            debugEditor->toggleBreakpoint(-1);debugEditor->toggleBreakpoint(debugEditor->lineCount());
            check("Invalid breakpoint rows ignored",!debugEditor->hasBreakpoint(-1)&&!debugEditor->hasBreakpoint(debugEditor->lineCount()));
            debugEditor->setCaretXY({0,0});debugEditor->setSelText("// inserted before breakpoint\n");
            check("Breakpoint follows inserted line",debugEditor->hasBreakpoint(4)&&!debugEditor->hasBreakpoint(3));
            debugEditor->undo();check("Breakpoint follows undo",debugEditor->hasBreakpoint(3));
            check("Breakpoint model rejects invalid row",!mDebugger->breakpointModel()->breakpoint(-1,false)&&!mDebugger->breakpointModel()->breakpoint(999999,false));
            const auto bpFile=outputDir+"/breakpoints.json";mDebugger->saveForNonproject(bpFile);
            check("Breakpoint persistence file written",QFileInfo::exists(bpFile));debugEditor->clearBreakpoints();
            mDebugger->loadForNonproject(bpFile);mEditorManager->updateEditorBreakpoints();
            check("Breakpoint restored from disk",debugEditor->hasBreakpoint(3));
            debugEditor->clearBreakpoints();mDebugger->saveForNonproject(bpFile);mDebugger->loadForNonproject(bpFile);mEditorManager->updateEditorBreakpoints();
            check("Deleted breakpoint stays deleted after save and reload",!debugEditor->hasBreakpoint(3));
            QTest::mouseClick(debugEditor->viewport(),Qt::LeftButton,Qt::NoModifier,gutterPoint(3));
            const auto corrupt=outputDir+"/corrupt-debug.json";put(corrupt,"{invalid");bool repaired=false;
            try{mDebugger->saveForNonproject(corrupt);QFile config(corrupt);repaired=config.open(QIODevice::ReadOnly)&&QJsonDocument::fromJson(config.readAll()).object().value("breakpoints").isArray();}catch(const FileError &){}
            check("Corrupt debugger settings do not crash atomic save",repaired&&debugEditor->hasBreakpoint(3));
            bool contextSeen=false;QTimer contextCloser;contextCloser.setInterval(20);
            connect(&contextCloser,&QTimer::timeout,this,[&]{if(auto menu=qobject_cast<QMenu *>(QApplication::activePopupWidget())){
                contextSeen=menu->actions().contains(ui->actionToggle_Breakpoint)&&menu->actions().contains(ui->actionRun_To_Cursor);
                menu->close();}});
            const QPoint contextPoint(debugEditor->gutterWidth()+20,gutterPoint(3).y());QCursor::setPos(debugEditor->viewport()->mapToGlobal(contextPoint));
            contextCloser.start();QContextMenuEvent contextEvent(QContextMenuEvent::Mouse,contextPoint,debugEditor->viewport()->mapToGlobal(contextPoint));
            QApplication::sendEvent(debugEditor->viewport(),&contextEvent);contextCloser.stop();
            check("Rust editor context menu offers breakpoint and run to cursor",contextSeen);
            bool sourceListSeen=false;QTimer listCloser;listCloser.setInterval(20);
            connect(&listCloser,&QTimer::timeout,this,[&]{for(auto widget:QApplication::topLevelWidgets())if(auto dialog=qobject_cast<QDialog *>(widget))if(dialog->objectName()=="rustSourceBreakpointDialog"){
                auto table=dialog->findChild<QTableView *>("rustSourceBreakpointTable");sourceListSeen=table&&table->model()->rowCount()>0;dialog->grab().save(outputDir+"/source-breakpoints.png");dialog->reject();}});
            listCloser.start();if(auto button=mRustDebugPanel->findChild<QPushButton *>("rustSourceBreakpoints"))button->click();listCloser.stop();
            check("Rust source breakpoint list accessible",sourceListSeen);
            check("Save debug source",debugEditor->save());mRustDebugPanel->addWatch("total");
            check("Debug button enabled",ui->actionDebug->isEnabled());ui->actionDebug->trigger();
            const bool paused=waitFor([this]{return mRustDebugger->state()==RustDebugSession::State::Stopped;},45000);
            check("GUI debug breakpoint",paused);
            auto stack=mRustDebugPanel->findChild<QTreeWidget *>("rustDebugStack");
            auto variables=mRustDebugPanel->findChild<QTreeWidget *>("rustDebugVariables");
            auto watches=mRustDebugPanel->findChild<QTreeWidget *>("rustDebugWatches");
            check("GUI debug stack",paused&&waitFor([&]{return stack&&stack->topLevelItemCount()>0;},10000));
            check("GUI debug variables",paused&&waitFor([&]{if(!variables)return false;for(auto item:variables->findItems("total",Qt::MatchExactly|Qt::MatchRecursive,0))if(item->text(1)=="9")return true;return false;},10000));
            check("GUI debug watch",paused&&waitFor([&]{if(!watches)return false;for(auto item:watches->findItems("total",Qt::MatchExactly,0))if(item->text(1)=="9")return true;return false;},10000));
            check("Build disabled during debug",!ui->actionCompile->isEnabled());
            if(paused){
                auto total=variables?variables->findItems("total",Qt::MatchExactly|Qt::MatchRecursive,0).value(0):nullptr;
                QTimer editValue;editValue.setInterval(20);connect(&editValue,&QTimer::timeout,this,[]{for(auto widget:QApplication::topLevelWidgets())if(auto dialog=qobject_cast<QInputDialog *>(widget)){dialog->setTextValue("11");dialog->accept();}});editValue.start();
                if(total)variables->itemDoubleClicked(total,1);editValue.stop();
                check("GUI debug variable editing",waitFor([&]{if(!watches)return false;for(auto item:watches->findItems("total",Qt::MatchExactly,0))if(item->text(1)=="11")return true;return false;},10000));
                int subsequentStops=0;auto stoppedConnection=connect(mRustDebugger,&RustDebugSession::stopped,this,[&]{++subsequentStops;});
                QTest::mouseClick(debugEditor->viewport(),Qt::LeftButton,Qt::NoModifier,gutterPoint(3));
                debugEditor->setCaretXY({0,5});debugEditor->setFocus();QTest::keyClick(debugEditor,Qt::Key_F5,Qt::ControlModifier);
                check("GUI debug run to cursor",waitFor([this,&subsequentStops]{return subsequentStops>0&&mRustDebugger->state()==RustDebugSession::State::Stopped;},15000));disconnect(stoppedConnection);
                check("Run to cursor reaches requested source row",debugEditor->caretY()==5);
                const bool collection=waitFor([&]{return variables&&!variables->findItems("values",Qt::MatchExactly|Qt::MatchRecursive,0).isEmpty();},10000);
                auto values=collection?variables->findItems("values",Qt::MatchExactly|Qt::MatchRecursive,0).value(0):nullptr;if(values)values->setExpanded(true);
                check("GUI first variable page",values&&waitFor([&]{return values->childCount()==201;},10000));
                if(values&&values->childCount()==201)variables->itemDoubleClicked(values->child(200),0);
                check("GUI second variable page",values&&values->childCount()==401);
                if(values&&values->childCount()==401)variables->itemDoubleClicked(values->child(400),0);
                check("GUI last variable page",values&&values->childCount()==451);
                resize(1250,900);QApplication::processEvents();check("GUI debug readable height",mRustDebugPanel->height()>=320);
                check("GUI variable values column visible",variables&&variables->columnWidth(1)>=60&&variables->header()->sectionViewportPosition(1)<variables->viewport()->width());
                check("GUI debug screenshot",mRustDebugPanel->grab().save(outputDir+"/debug.png"));
                auto input=mRustDebugPanel->findChild<QLineEdit *>("rustDebugInput");
                if(input){input->setText(QString::fromUtf8("città 🦀"));QMetaObject::invokeMethod(input,"returnPressed",Qt::DirectConnection);}
                ui->actionContinue->trigger();
                check("GUI debug completion",waitFor([this]{return mRustDebugger->state()==RustDebugSession::State::Idle;},30000));
                const auto console=mRustDebugPanel->findChild<QPlainTextEdit *>("rustDebugConsole");
                check("GUI debug program input output",console&&console->toPlainText().contains("DEBUG_UI 11")&&console->toPlainText().contains(QString::fromUtf8("INPUT_UI città 🦀")));
            }
            if(mRustDebugger->active()){mRustDebugger->stop();waitFor([this]{return !mRustDebugger->active();},10000);}
            debugEditor->setCaretXY({0,3});debugEditor->setFocus();QApplication::processEvents();
            check("Run to cursor enabled before starting debug",ui->actionRun_To_Cursor->isEnabled());
            QTest::keyClick(debugEditor,Qt::Key_F5,Qt::ControlModifier);
            check("Run to cursor starts a debug session",waitFor([this]{return mRustDebugger->state()==RustDebugSession::State::Stopped;},45000));
            check("Direct run to cursor selects requested row",waitFor([&]{return debugEditor->caretY()==3;},10000));
            debugEditor->setCaretXY({0,0});debugEditor->setSelText("// modified during debug\n");
            ui->actionRun_To_Cursor->trigger();QApplication::processEvents();
            check("Run to cursor rejects changed source",mRustDebugger->state()==RustDebugSession::State::Stopped&&statusBar()->currentMessage().contains("testo"));
            debugEditor->undo();
            const bool lateVariables=waitFor([&]{return variables&&!variables->findItems("total",Qt::MatchExactly|Qt::MatchRecursive,0).isEmpty();},10000);
            auto lateTotal=lateVariables?variables->findItems("total",Qt::MatchExactly|Qt::MatchRecursive,0).value(0):nullptr;
            bool interruptedDialog=false;QTimer stopDuringEdit;stopDuringEdit.setInterval(20);
            connect(&stopDuringEdit,&QTimer::timeout,this,[&]{for(auto widget:QApplication::topLevelWidgets())if(auto dialog=qobject_cast<QInputDialog *>(widget)){
                if(!interruptedDialog){interruptedDialog=true;mRustDebugger->stop();}dialog->setTextValue("999");dialog->accept();
            }});stopDuringEdit.start();if(lateTotal)variables->itemDoubleClicked(lateTotal,1);stopDuringEdit.stop();
            check("Stop during variable dialog invalidates deleted tree node safely",interruptedDialog&&mRustDebugger->state()!=RustDebugSession::State::Stopped);
            ui->actionStop_Execution->trigger();
            check("Debug stop action returns to idle",waitFor([this]{return mRustDebugger->state()==RustDebugSession::State::Idle;},10000));
            debugEditor->setModified(false);
            mEditorManager->closeEditor(debugEditor,true,true);
        }else check("Debug editor",false);
    }
    {
        auto sourceEditor=openFile(source);if(sourceEditor)mEditorManager->activeEditor(sourceEditor,true);mRustRoot=root;
        const auto protectedArtifact=root+"/target/debug/opened-artifact.txt";check("Live protection artifact fixture",put(protectedArtifact,"artifact kept when opened"));
        {
            RustCleanDialog preview(root,{source},this);bool nowOpened=false;preview.setProtectedFilesProvider([&]{return nowOpened?QStringList{protectedArtifact}:QStringList{};});
            preview.show();QElapsedTimer clock;clock.start();auto move=preview.findChild<QPushButton *>("rustRecycleArtifacts");
            while(move&&!move->isEnabled()&&clock.elapsed()<20000)QTest::qWait(30);
            check("Live protection preview prepared",move&&move->isEnabled());if(move&&move->isEnabled()){nowOpened=true;move->click();clock.restart();
                while(!move->isEnabled()&&clock.elapsed()<10000)QTest::qWait(30);
                const auto status=preview.findChild<QLabel *>("rustCleanStatus");check("Document opened after clean preview is protected",move->isEnabled()&&status&&status->text().contains("documento aperto")&&QFileInfo::exists(protectedArtifact)&&QFileInfo::exists(root+"/target/.rustc_info.json"));
            }preview.reject();
        }
        auto action=findChild<QAction *>("rust-command-clean");bool previewSeen=false;QTimer closer,deadline;closer.setInterval(30);deadline.setSingleShot(true);
        const auto closePreview=[&](bool recycle){for(auto widget:QApplication::topLevelWidgets())if(auto dialog=qobject_cast<RustCleanDialog *>(widget)){
            auto button=dialog->findChild<QPushButton *>("rustRecycleArtifacts");if(button&&button->isEnabled()){
                previewSeen=true;closer.stop();dialog->grab().save(outputDir+"/clean-preview.png");if(recycle)button->click();else dialog->reject();
            }
        }};
        auto connection=connect(&closer,&QTimer::timeout,this,[&]{closePreview(false);});
        connect(&deadline,&QTimer::timeout,this,[]{for(auto widget:QApplication::topLevelWidgets())if(auto dialog=qobject_cast<RustCleanDialog *>(widget))dialog->reject();});
        closer.start();deadline.start(25000);if(action)action->trigger();closer.stop();deadline.stop();disconnect(connection);
        check("Cargo clean preview and cancel",previewSeen&&QFileInfo::exists(root+"/target")&&QFileInfo::exists(source));
        previewSeen=false;connection=connect(&closer,&QTimer::timeout,this,[&]{closePreview(true);});closer.start();deadline.start(25000);if(action)action->trigger();closer.stop();deadline.stop();disconnect(connection);
        QDirIterator remaining(root+"/target",QDir::Files|QDir::Hidden|QDir::System,QDirIterator::Subdirectories);
        check("Cargo clean recycle with source protection",previewSeen&&!remaining.hasNext()&&QFileInfo::exists(source)&&QFileInfo::exists(root+"/Cargo.toml"));
        check("Cargo rebuild after recycling",sourceEditor&&run("build")&&result==0);
    }
#ifdef ENABLE_VCS
    {
        check("Git menu exposed",ui->menuGit->menuAction()->isVisible());
        check("Git detected",pSettings->vcs().gitOk());
        GitManager git;QString output;
        const auto repo=outputDir+"/git progetto città";QDir().mkpath(repo);
        check("Git repository creation",git.createRepository(repo,output));
        check("Git local identity",git.setUserName(repo,"Rust regression",output)&&git.setUserEmail(repo,"regression@example.invalid",output));
        const auto file=repo+"/città Rust.rs";
        check("Git source",put(file,"fn main(){}\n"));
        check("Git stage",git.add(repo,QFileInfo(file).fileName(),output));
        check("Git staged Unicode path",git.listStagedFiles(repo).contains(QFileInfo(file).fileName()));
        check("Git commit",git.commit(repo,"Prima revisione Rust città: \"quoted\" C:\\Rust",false,output));
        const auto history=git.log(repo,0,10);check("Git history",git.logCounts(repo)==1&&history.size()==1);
        check("Git preserves log quotes and backslashes",history.size()==1&&history.first()->title.contains("\"quoted\" C:\\Rust"));
        QString originalBranch;check("Git repository branch",git.hasRepository(repo,originalBranch)&&!originalBranch.isEmpty());
        check("Git branch creation",git.switchToBranch(repo,"rust-probe",true,false,false,false,false,false,output));
        check("Git branch commit source",put(file,"fn main(){println!(\"branch\");}\n"));
        check("Git branch commit",git.commit(repo,"Seconda revisione",true,output));
        check("Git switch original",git.switchToBranch(repo,originalBranch,false,false,false,false,false,false,output));
        check("Git fast forward merge",git.merge(repo,"rust-probe",false,true,false,false,output)&&git.logCounts(repo)==2);
        check("Git invalid reference reported",!git.switchToBranch(repo,"missing-probe-branch",false,false,false,false,false,false,output)&&output.contains("fatal:"));
        const auto remote=outputDir+"/git-remoto.git";QProcess createBare;createBare.start(pSettings->vcs().gitPath(),{"init","--bare",remote});
        check("Git local remote fixture",createBare.waitForFinished(10000)&&createBare.exitCode()==0);
        check("Git remote management",git.addRemote(repo,"origin",remote,output)&&git.getRemoteURL(repo,"origin")==remote);
        check("Git local push",git.push(repo,"origin",originalBranch,output));
        const auto cloneParent=outputDir+"/cloni";QDir().mkpath(cloneParent);
        check("Git local clone",git.clone(cloneParent,remote,output));const auto clone=cloneParent+"/git-remoto";
        check("Git third revision source",put(file,"fn main(){println!(\"updated\");}\n"));
        check("Git third commit and push",git.commit(repo,"Terza revisione",true,output)&&git.push(repo,"origin",originalBranch,output));
        check("Git local fetch",git.fetch(clone,output));check("Git local pull",git.pull(clone,output)&&git.logCounts(clone)==3);
        const auto old=pSettings->vcs().gitPath();pSettings->vcs().setGitPath(repo);
        check("Git rejects directory executable",!pSettings->vcs().gitOk());pSettings->vcs().setGitPath(old);
        auto options=SettingsDialog::optionDialog(colorManager(),iconsManager(),this);
        check("Git settings exposed",options->setCurrentWidget(tr("Git"),tr("Tools")));
    }
#else
    check("Git integration compiled",false);
#endif
    {
        AboutDialog about(this);about.resize(820,720);about.show();QApplication::processEvents();
        const auto home=about.findChild<QLabel *>("lblHomepage");
        const auto roy=about.findChild<QLabel *>("label");
        const auto mele=about.findChild<QLabel *>("lblForkCopyright");
        check("Dedicated GitHub URL",home&&home->text().contains("https://github.com/lellomele/RedPanda-Rust"));
        check("Original code scope",roy&&roy->text().contains("Roy Qu")&&roy->text().contains("QSynEdit"));
        check("Rust changes scope",mele&&mele->text().contains("Raffaele Mele")&&mele->text().contains("Rust/Cargo"));
        check("Exact fork copyright",mele&&mele->text().contains(QString::fromUtf8("Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)")));
        check("About screenshot",about.grab().save(outputDir+"/about.png"));about.close();
    }
    {
        auto options=SettingsDialog::optionDialog(colorManager(),iconsManager(),this);
        const auto editorGroup=QCoreApplication::translate("SettingsDialog","Editor"),completionPage=QCoreApplication::translate("SettingsDialog","Code Completion"),tipsPage=QCoreApplication::translate("SettingsDialog","Tooltips");
        check("Rust completion preferences restored",options->setCurrentWidget(completionPage,editorGroup));
        auto parserShare=options->findChild<QCheckBox *>("chkEditorShareCodeParser");check("Rust completion page has no C++ parser option",parserShare&&parserShare->isHidden());
        check("Rust tooltip preferences restored",options->setCurrentWidget(tipsPage,editorGroup));
        check("Rust tools page",options->setCurrentWidget(tr("Strumenti del compilatore"),tr("Rust")));
        options->resize(1000,760);options->show();QApplication::processEvents();
        auto path=options->findChild<QLineEdit *>("rustTool_rustc");
        auto edition=options->findChild<QComboBox *>("rustSingleFileEdition");
        auto verify=options->findChild<QPushButton *>("verifyRustTools");
        check("Editable compiler fields",path&&edition&&verify);
        if(path&&edition&&verify){
            path->setText(RustTools::detectedPath("rustc"));edition->setCurrentText("2021");
            check("Apply Rust settings",QMetaObject::invokeMethod(options.get(),"on_btnApply_pressed",Qt::DirectConnection));
            check("Saved compiler path",!RustTools::configuredPath("rustc").isEmpty());
            check("Saved edition",RustTools::edition()=="2021");
            edition->setCurrentText("2024");QMetaObject::invokeMethod(options.get(),"on_btnApply_pressed",Qt::DirectConnection);
            verify->click();QEventLoop loop;QTimer poll,deadline;poll.setInterval(25);deadline.setSingleShot(true);deadline.setInterval(25000);
            connect(&poll,&QTimer::timeout,&loop,[&]{if(verify->isEnabled())loop.quit();});connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);
            poll.start();deadline.start();if(!verify->isEnabled())loop.exec();
            check("Asynchronous tool verification",verify->isEnabled());
            auto status=options->findChild<QLabel *>("rustToolStatus_rustc");
            check("Rustc version visible",status&&status->text().contains("rustc"));
            auto adapterStatus=options->findChild<QLabel *>("rustToolStatus_codelldb");
            check("CodeLLDB adapter verification uses supported CLI",adapterStatus&&adapterStatus->text().contains("CodeLLDB")&&!adapterStatus->text().contains("fallita"));
        }
        check("Tools screenshot",options->grab().save(outputDir+"/tools.png"));options->close();
    }
    {
        const auto original=outputDir+"/recovery-original.rs";const QByteArray originalBytes="fn main(){println!(\"ORIGINAL_ON_DISK\");}\n";
        const QString recoveredText=QString::fromUtf8("fn main(){println!(\"unsaved città 🦀\");}\n");
        check("Recovery original fixture",put(original,originalBytes));
        const auto directory=QDir(pSettings->dirs().config()).filePath("recovery");QDir().mkpath(directory);QString error;
        check("Abandoned Rust snapshot fixture",SessionStore::write(directory+"/qa-abandoned.json",{{original,recoveredText,0,4}},error));
        auto recovery=findChild<SessionRecovery *>();const int before=mEditorManager->pageCount();
        const bool enabled=pSettings->environment().recoveryEnabled();pSettings->environment().setRecoveryEnabled(true);
        if(recovery)recovery->restorePreviousSessions();
        check("Recovery dialog restores an abandoned document",recovery&&mEditorManager->pageCount()==before+1);
        auto restored=mEditorManager->getEditor();
        check("Recovered Rust text remains unsaved",restored&&restored->modified()&&restored->isNew()&&restored->text()==recoveredText);
        check("Recovered document retains Rust syntax",restored&&restored->syntaxer()->language()==QSynedit::ProgrammingLanguage::Rust);
        QFile originalFile(original);check("Recovery preserves original file",originalFile.open(QIODevice::ReadOnly)&&originalFile.readAll()==originalBytes);
        originalFile.close();if(restored&&mEditorManager->pageCount()>before)mEditorManager->closeEditor(restored,true,true);
        pSettings->environment().setRecoveryEnabled(enabled);
    }
    resize(1250,800);QApplication::processEvents();check("Screenshot",grab().save(outputDir+"/red-panda-rust.png"));
    // Every document belongs to the isolated QA profile. A failed check must
    // still close the test run instead of waiting on an invisible save prompt.
    for(int remaining=mEditorManager->pageCount();remaining>0;--remaining){
        auto document=(*mEditorManager)[0];if(!mEditorManager->closeEditor(document,true,true))break;
    }
    QSaveFile report(outputDir+"/checks.json");
    if(report.open(QIODevice::WriteOnly)){report.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failed}}).toJson());if(!report.commit())++failed;}else ++failed;
    return failed?1:0;
}
