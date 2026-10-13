// Copyright (C) 2026 Raffaele Mele
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include "rustworkspace.h"
#include "rusttools.h"
#include <qsynedit/syntaxer/rust.h>
#include <qsynedit/syntaxer/cpp.h>
class RustChecks : public QObject {
    Q_OBJECT
private:
    static bool put(const QString &path,const QByteArray &text){
        QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(text)==text.size();
    }
    static QString tokens(QSynedit::RustSyntaxer &syntaxer,const QString &line){
        syntaxer.setLine(0,line,0);QString result;int steps=0;
        while(!syntaxer.eol()&&steps++<line.size()+2){result+=syntaxer.getToken();syntaxer.next();}
        return result;
    }
private slots:
    void initTestCase(){qRegisterMetaType<PCompileIssue>();}
    void cleanup(){RustTools::setSettingsFile({});}
    void reentrantStartFromOutputIsRejected(){
        QTemporaryDir temp;QVERIFY(temp.isValid());const auto file=temp.filePath("nested.rs");QVERIFY(put(file,"fn main() {}\n"));
        RustWorkspace workspace;QSignalSpy done(&workspace,&RustWorkspace::finished);bool attempted=false,accepted=true;
        connect(&workspace,&RustWorkspace::output,this,[&](const QString &){if(!attempted){attempted=true;accepted=workspace.startFile(file,"check");}});
        QVERIFY(workspace.startFile(file,"check"));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,30000);QVERIFY(attempted);QVERIFY(!accepted);QCOMPARE(done.first()[0].toInt(),0);
    }
    void cancelFromLaunchOutputAndRetry(){
        QTemporaryDir temp;QVERIFY(temp.isValid());const auto file=temp.filePath("cancel.rs");QVERIFY(put(file,"fn main() {}\n"));
        RustWorkspace workspace;bool once=true;connect(&workspace,&RustWorkspace::output,this,[&](const QString &){if(once){once=false;workspace.stop();}});
        QVERIFY(!workspace.startFile(file,"check"));QVERIFY(!workspace.busy());QSignalSpy done(&workspace,&RustWorkspace::finished);
        QVERIFY(workspace.startFile(file,"check"));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,30000);QCOMPARE(done.first()[0].toInt(),0);
    }
    void finishedCallbackCannotOverwriteCompletingOperation(){
        QTemporaryDir temp;QVERIFY(temp.isValid());const auto file=temp.filePath("finish.rs");QVERIFY(put(file,"fn main() {}\n"));
        RustWorkspace workspace;QSignalSpy done(&workspace,&RustWorkspace::finished);bool accepted=true;
        const auto connection=connect(&workspace,&RustWorkspace::finished,this,[&](int,bool){accepted=workspace.startFile(file,"build");});
        QVERIFY(workspace.startFile(file,"check"));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,30000);QVERIFY(!accepted);QCOMPARE(workspace.command(),QString("check"));disconnect(connection);
        QVERIFY(workspace.startFile(file,"build"));QTRY_COMPARE_WITH_TIMEOUT(done.count(),2,30000);QCOMPARE(done.last()[0].toInt(),0);
    }
    void syntaxState(){
        QSynedit::RustSyntaxer s;
        QCOMPARE(tokens(s,"/* outer /* inner */"),QString("/* outer /* inner */"));
        QVERIFY(s.isCommentNotFinished(s.getState()));
        QCOMPARE(tokens(s,"end */ fn main() {"),QString("end */ fn main() {"));
        QVERIFY(!s.isCommentNotFinished(s.getState()));QCOMPARE(s.getState()->braceLevel,1);
        tokens(s,"let a = r##\"raw \" text");QVERIFY(s.isStringNotFinished(s.getState()));
        tokens(s,"more\"##; }");QVERIFY(!s.isStringNotFinished(s.getState()));QCOMPARE(s.getState()->braceLevel,0);
        s.resetState();s.setLine(0,"'static",0);QVERIFY(!s.isStringNotFinished(s.getState()));
        tokens(s,"let c='\\u{1F980}';");QVERIFY(!s.isStringNotFinished(s.getState()));
        const QString code="fn f<'a>(x: &'a str) { /* n /* e */ s */ let t=br#\"a\"#; }";
        for(int n=0;n<=code.size();++n){s.resetState();QCOMPARE(tokens(s,code.left(n)),code.left(n));}
        s.resetState();tokens(s,"/* open");auto saved=s.getState();
        QSynedit::RustSyntaxer clone;clone.setState(saved);tokens(clone,"close */ fn");
        QVERIFY(!clone.isCommentNotFinished(clone.getState()));QVERIFY(s.isCommentNotFinished(saved));
    }
    void incompatibleHoverState(){
        QSynedit::RustSyntaxer rust;tokens(rust,"implo TestError {");
        QSynedit::CppSyntaxer cpp;cpp.setState(rust.getState());
        cpp.setLine(20,"implo TestError {",20);cpp.nextToEol();
        QVERIFY(cpp.eol());
    }
    void compilerSettings(){
        QTemporaryDir temp;QVERIFY(temp.isValid());const QString ini=temp.path()+"/settings.ini";
        const QString rustc=RustTools::detectedPath("rustc");QVERIFY(!rustc.isEmpty());
        RustTools::setSettingsFile(ini);QString error;
        QMap<QString,QString> paths;paths.insert("rustc",rustc);
        QVERIFY2(RustTools::save(paths,"2021",error),qPrintable(error));
        QCOMPARE(RustWorkspace::findTool("rustc"),QFileInfo(rustc).absoluteFilePath());QCOMPARE(RustTools::edition(),QString("2021"));
        paths.insert("rustc",temp.path()+"/missing.exe");QVERIFY(!RustTools::save(paths,"2024",error));
        QCOMPARE(RustTools::edition(),QString("2021"));QCOMPARE(RustTools::configuredPath("rustc"),QFileInfo(rustc).absoluteFilePath());
        const QString file=temp.path()+"/edition.rs";
        QVERIFY(put(file,"fn main(){let gen=7;println!(\"{}\",gen);}\n"));
        RustWorkspace workspace;QSignalSpy done(&workspace,&RustWorkspace::finished);
        QVERIFY(workspace.startFile(file,"check"));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,30000);QCOMPARE(done.first()[0].toInt(),0);
        paths.insert("rustc",rustc);QVERIFY(RustTools::save(paths,"2024",error));
        done.clear();QVERIFY(workspace.startFile(file,"check"));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,30000);QVERIFY(done.first()[0].toInt()!=0);
        const QString blocked=temp.path()+"/blocker";QVERIFY(put(blocked,"not a directory"));
        RustTools::setSettingsFile(blocked+"/settings.ini");QVERIFY(!RustTools::save(paths,"2021",error));
        QVERIFY(RustTools::configuredPath("rustc").isEmpty());
    }
    void cargoWorkflow(){
        QTemporaryDir temp(QDir::tempPath()+"/redpanda-rust spazio-XXXXXX");QVERIFY(temp.isValid());
        RustWorkspace workspace;QSignalSpy done(&workspace,&RustWorkspace::finished);
        QString output;connect(&workspace,&RustWorkspace::output,this,[&](const QString &text){output+=text;});
        QVERIFY(workspace.start(temp.path(),"new",{"rust_probe","--vcs","none"}));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,30000);QCOMPARE(done.last()[0].toInt(),0);
        const QString root=QDir(temp.path()).filePath("rust_probe");
        QCOMPARE(RustWorkspace::findRoot(root+"/src/main.rs"),root);
        const QString code=root+"/src/main.rs";
        QVERIFY(put(code,"fn main(){println!(\"RUST_OK\");}\n#[test]fn probe(){assert_eq!(2+2,4);}\n"));
        for(const auto &command:QStringList{"check","build","run","test","clippy","fmt"}){
            done.clear();output.clear();QVERIFY(workspace.start(root,command));
            QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,60000);
            QVERIFY2(done.last()[0].toInt()==0,qPrintable(output));
            if(command=="run")QVERIFY(output.contains("RUST_OK"));
        }
        QFile formatted(code);QVERIFY(formatted.open(QIODevice::ReadOnly));QVERIFY(formatted.readAll().contains("fn main() {"));formatted.close();
        QSignalSpy issues(&workspace,&RustWorkspace::issue);
        QVERIFY(put(code,"fn main() { let _: u32 = \"errore\"; }\n"));
        done.clear();QVERIFY(workspace.start(root,"check"));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,30000);
        QVERIFY(done.last()[0].toInt()!=0);QVERIFY(!issues.isEmpty());
        const auto issue=qvariant_cast<PCompileIssue>(issues.first()[0]);QVERIFY(issue);QCOMPARE(issue->line,0);QVERIFY(issue->column>0);
        QVERIFY(issue->filename.endsWith("src/main.rs"));QVERIFY(!workspace.busy());
        // Failed process creation must recover and allow another Cargo command.
        const QByteArray oldCargo=qgetenv("CARGO");const QString invalid=temp.path()+"/invalid.exe";
        QVERIFY(put(invalid,"invalid executable"));qputenv("CARGO",invalid.toUtf8());
        bool idleStateConsistent=true;const auto idle=connect(&workspace,&RustWorkspace::busyChanged,this,[&](bool busy){if(!busy)idleStateConsistent=idleStateConsistent&&!workspace.busy();});
        done.clear();QVERIFY(workspace.start(root,"check"));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,15000);QCOMPARE(done.last()[0].toInt(),-1);QVERIFY(idleStateConsistent);disconnect(idle);
        if(oldCargo.isNull())qunsetenv("CARGO");else qputenv("CARGO",oldCargo);
        QVERIFY(put(code,"use std::io::{self,Write};fn main(){print!(\"READY\");io::stdout().flush().unwrap();let mut s=String::new();io::stdin().read_line(&mut s).unwrap();println!(\"INPUT:{}\",s.trim());for b in [0xF0u8,0x9F,0xA6,0x80]{io::stdout().write_all(&[b]).unwrap();io::stdout().flush().unwrap();std::thread::sleep(std::time::Duration::from_millis(30));}println!();}\n"));
        done.clear();output.clear();bool sent=false;
        auto connection=connect(&workspace,&RustWorkspace::output,this,[&](const QString &text){if(text.contains("READY")&&!sent){sent=true;workspace.sendInput("ciao");}});
        QVERIFY(workspace.start(root,"run"));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,60000);
        disconnect(connection);QVERIFY2(sent,qPrintable(output));QCOMPARE(done.last()[0].toInt(),0);QVERIFY(output.contains("INPUT:ciao"));
        QVERIFY(output.contains(QString::fromUcs4(U"🦀")));QVERIFY(!output.contains(QChar::ReplacementCharacter));
        QVERIFY(put(code,"fn main(){if std::env::args().nth(1).as_deref()==Some(\"--child\"){loop{std::thread::sleep(std::time::Duration::from_millis(100));}}let child=std::process::Command::new(std::env::current_exe().unwrap()).arg(\"--child\").spawn().unwrap();println!(\"CHILD_PID:{} LOOP_READY\",child.id());loop{std::thread::sleep(std::time::Duration::from_millis(100));}}\n"));
        done.clear();output.clear();QVERIFY(workspace.start(root,"run"));QTRY_VERIFY_WITH_TIMEOUT(output.contains("LOOP_READY"),60000);
        const auto childMatch=QRegularExpression("CHILD_PID:([0-9]+)").match(output);QVERIFY(childMatch.hasMatch());
#ifdef Q_OS_WIN
        HANDLE child=OpenProcess(SYNCHRONIZE,FALSE,childMatch.captured(1).toUInt());QVERIFY(child);
#endif
        QVERIFY(!workspace.start(root,"check"));workspace.stop();QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,15000);
        QVERIFY(done.last()[1].toBool());QVERIFY(!workspace.busy());
#ifdef Q_OS_WIN
        const DWORD childExit=WaitForSingleObject(child,2000);CloseHandle(child);QCOMPARE(childExit,DWORD(WAIT_OBJECT_0));
#endif
        done.clear();QVERIFY(workspace.start(root,"check"));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,30000);QCOMPARE(done.last()[0].toInt(),0);
        QVERIFY(put(code,"fn main(){let s=\"a\".repeat(8192);loop{println!(\"{}\",s);}}\n"));
        done.clear();output.clear();QVERIFY(workspace.start(root,"run"));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,60000);
        QVERIFY(done.last()[1].toBool());QVERIFY(output.contains("16 MiB"));QVERIFY(!workspace.busy());
    }
    void standaloneFile(){
        QTemporaryDir temp(QDir::tempPath()+"/rust singolo città-XXXXXX");QVERIFY(temp.isValid());
        const QString file=temp.path()+"/esempio con-spazi.rs";
        QVERIFY(put(file,"fn main(){println!(\"SINGLE_OK\");}\n#[test]fn prova(){assert_eq!(3+4,7);}\n"));
        QVERIFY(RustWorkspace::findRoot(file).isEmpty());
        RustWorkspace workspace;QSignalSpy done(&workspace,&RustWorkspace::finished),issues(&workspace,&RustWorkspace::issue);
        QString output;connect(&workspace,&RustWorkspace::output,this,[&](const QString &text){output+=text;});
        for(const auto &command:QStringList{"check","build","run","test","fmt"}){
            done.clear();output.clear();QVERIFY(workspace.startFile(file,command));
            QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,60000);QVERIFY2(done.first()[0].toInt()==0,qPrintable(output));
            if(command=="run")QVERIFY(output.contains("SINGLE_OK"));
            if(command=="test")QVERIFY(output.contains("1 passed"));
        }
        QVERIFY(!QFileInfo::exists(temp.path()+"/program.exe"));QVERIFY(!QFileInfo::exists(temp.path()+"/Cargo.toml"));
        QVERIFY(put(file,"fn main(){let _:u32=\"invalid\";}\n"));
        done.clear();output.clear();QVERIFY(workspace.startFile(file,"run"));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,30000);
        QVERIFY(done.first()[0].toInt()!=0);QVERIFY(!issues.isEmpty());QVERIFY(!output.contains("SINGLE_OK"));
        QCOMPARE(qvariant_cast<PCompileIssue>(issues.last()[0])->line,0);
        QVERIFY(put(file,"fn main(){println!(\"SINGLE_RETRY\");}\n"));
        done.clear();output.clear();QVERIFY(workspace.startFile(file,"run"));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,30000);
        QCOMPARE(done.first()[0].toInt(),0);QVERIFY(output.contains("SINGLE_RETRY"));
    }
};
QTEST_GUILESS_MAIN(RustChecks)
#include "rust-workflow-checks.moc"
