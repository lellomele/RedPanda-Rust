// Copyright (C) 2026 Raffaele Mele
// SPDX-License-Identifier: GPL-3.0-or-later
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "editor.h"
#include "editormanager.h"
#include "rustworkspace.h"
#include "rusttools.h"
#include "widgets/aboutdialog.h"
#include "settingsdialog/settingsdialog.h"
#include "sessionrecovery.h"
#include <QApplication>
#include <QDir>
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
#include <qsynedit/syntaxer/rust.h>

int MainWindow::runRustUiChecks(const QString &outputDir){
    QTimer modalCloser;
    modalCloser.setInterval(50);
    connect(&modalCloser,&QTimer::timeout,this,[]{
        for(auto *w:QApplication::topLevelWidgets())if(auto *m=qobject_cast<QMessageBox *>(w))
            m->done(m->standardButtons().testFlag(QMessageBox::Yes)?QMessageBox::Yes:QMessageBox::Ok);
    });
    modalCloser.start();
    QJsonArray checks;int failed=0;
    const auto check=[&](const QString &name,bool ok){checks.append(QJsonObject{{"name",name},{"passed",ok}});if(!ok)++failed;};
    const auto put=[](const QString &path,const QByteArray &content){QSaveFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(content)==content.size()&&file.commit();};
    QDir().mkpath(outputDir+"/cargo-project/src");const QString root=outputDir+"/cargo-project";
    check("manifest",put(root+"/Cargo.toml","[package]\nname=\"rust_ui_probe\"\nversion=\"0.1.0\"\nedition=\"2024\"\n"));
    const QString source=root+"/src/main.rs";
    check("source",put(source,"fn main(){println!(\"RUST_GUI_OK\");}\n"));
    mRustRoot=root;setFilesViewRoot(root);Editor *editor=openFile(source);
    check("Rust editor",editor && editor->syntaxer()->language()==QSynedit::ProgrammingLanguage::Rust);
    check("Identity",windowTitle().contains("Red Panda Rust"));
    check("Icon",!windowIcon().isNull());
    check("Compile enabled",ui->actionCompile->isEnabled());
    check("Rust formatting action enabled",ui->actionReformat_Code->isEnabled());
    check("C++ compiler selector hidden",!mCompilerSet->isVisible());
    check("Debug not exposed",!ui->actionDebug->isVisible());
    check("C++ problem panels hidden",ui->tabExplorer->indexOf(ui->tabProblemSet)<0 && ui->tabMessages->indexOf(ui->tabProblem)<0);
    if(editor){
        editor->setCaretXY({0,0});editor->setSelText(QString::fromUtf8("// salvataggio città € Rust\n"));
        check("Atomic source save",editor->save());
        QFile saved(source);check("Saved UTF-8 text",saved.open(QIODevice::ReadOnly)&&saved.readAll().contains(QString::fromUtf8("città € Rust").toUtf8()));saved.close();
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
    check("Invalid source",put(source,"fn main() { let _:u32 = \"invalid\"; }\n"));
    editor=openFile(source);
    check("Compiler error handled",run("check")&&result!=0);
    check("Original issues table",ui->tableIssues->count()>0);
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
        AboutDialog about(this);about.resize(820,720);about.show();QApplication::processEvents();
        const auto home=about.findChild<QLabel *>("lblHomepage");
        const auto roy=about.findChild<QLabel *>("label");
        const auto mele=about.findChild<QLabel *>("lblForkCopyright");
        check("Dedicated GitHub URL",home&&home->text().contains("https://github.com/lellomele/RedPanda-Rust"));
        check("Original code scope",roy&&roy->text().contains("Roy Qu")&&roy->text().contains("QSynEdit"));
        check("Rust changes scope",mele&&mele->text().contains("Raffaele Mele")&&mele->text().contains("Rust/Cargo"));
        check("About screenshot",about.grab().save(outputDir+"/about.png"));about.close();
    }
    {
        auto options=SettingsDialog::optionDialog(colorManager(),iconsManager(),this);
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
        }
        check("Tools screenshot",options->grab().save(outputDir+"/tools.png"));options->close();
    }
    resize(1250,800);QApplication::processEvents();check("Screenshot",grab().save(outputDir+"/red-panda-rust.png"));
    QSaveFile report(outputDir+"/checks.json");
    if(report.open(QIODevice::WriteOnly)){report.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failed}}).toJson());if(!report.commit())++failed;}else ++failed;
    return failed?1:0;
}
