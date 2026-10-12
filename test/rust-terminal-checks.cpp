// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QProcess>
#include "rustterminal.h"
#include "rusttools.h"
#include "processjob.h"
#ifdef Q_OS_WIN
#include <windows.h>
#endif
class RustTerminalChecks final:public QObject{
    Q_OBJECT
    QTemporaryDir mTemp;
    QString mProgram;
private slots:
    void initTestCase(){
        QVERIFY(mTemp.isValid());const auto source=mTemp.filePath("console.rs");mProgram=mTemp.filePath("console.exe");
        QFile file(source);QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"RS(use std::io::{self, Write};
fn main(){
 let args:Vec<String>=std::env::args().skip(1).collect();
 if args.first().map(|s|s.as_str())==Some("--child-loop"){
   std::fs::write(&args[1],std::process::id().to_string()).unwrap();
   loop{std::thread::sleep(std::time::Duration::from_millis(50));}
 }
 if args.first().map(|s|s.as_str())==Some("--spawn-child"){
   let _child=std::process::Command::new(std::env::current_exe().unwrap()).args(["--child-loop",&args[1]]).spawn().unwrap();
   loop{std::thread::sleep(std::time::Duration::from_millis(50));}
 }
 println!("ARGS {:?}",args);
 println!("ENV {}",std::env::var("RPR_TERMINAL_TEST").unwrap_or_default());
 let mut text=String::new();io::stdout().flush().unwrap();
 io::stdin().read_line(&mut text).unwrap();
 println!("INPUT {}",text.trim());
})RS");file.close();
        QProcess compiler;compiler.start(RustTools::detectedPath("rustc"),{"--edition","2024",source,"-o",mProgram});
        QVERIFY(compiler.waitForFinished(30000));QCOMPARE(compiler.exitCode(),0);
    }
    void unicodeArgumentsEnvironmentAndInput(){
        RustTerminal terminal;QSignalSpy started(&terminal,&RustTerminal::started),done(&terminal,&RustTerminal::finished),errors(&terminal,&RustTerminal::error);
        QString output;connect(&terminal,&RustTerminal::output,this,[&](const QString &text){output+=text;});
        auto environment=QProcessEnvironment::systemEnvironment();environment.insert("RPR_TERMINAL_TEST",QString::fromUtf8("città 🦀"));
        QVERIFY(terminal.start(mProgram,{"two words","a\"b","path with trailing\\",""},mTemp.path(),environment));
        QTRY_COMPARE_WITH_TIMEOUT(started.count(),1,10000);
        QVERIFY(terminal.write(QString::fromUtf8("ciao città 🦀\r").toUtf8()));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,10000);QCOMPARE(done.first()[0].toInt(),0);QCOMPARE(errors.count(),0);
        QVERIFY2(output.contains(QString::fromUtf8("INPUT ciao città 🦀")),qPrintable(output));
        QVERIFY(output.contains(QString::fromUtf8("ENV città 🦀")));QVERIFY(output.contains("two words"));QVERIFY(output.contains("a\\\"b"));
        QVERIFY(!terminal.active());
    }
    void cancellationOwnsChildProcesses(){
#ifdef Q_OS_WIN
        const auto pidFile=mTemp.filePath("child.pid");QFile::remove(pidFile);
        RustTerminal terminal;QSignalSpy started(&terminal,&RustTerminal::started),done(&terminal,&RustTerminal::finished);
        QVERIFY(terminal.start(mProgram,{"--spawn-child",pidFile},mTemp.path(),QProcessEnvironment::systemEnvironment()));
        QTRY_COMPARE_WITH_TIMEOUT(started.count(),1,10000);QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(pidFile),10000);
        QFile file(pidFile);QVERIFY(file.open(QIODevice::ReadOnly));const DWORD pid=file.readAll().trimmed().toUInt();QVERIFY(pid>0);
        HANDLE child=OpenProcess(SYNCHRONIZE,FALSE,pid);QVERIFY(child);
        terminal.stop();QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,10000);
        QCOMPARE(WaitForSingleObject(child,10000),DWORD(WAIT_OBJECT_0));CloseHandle(child);
#else
        QSKIP("Windows Job Object process ownership check");
#endif
    }
    void destructionDuringStartup(){
        auto terminal=new RustTerminal;
        QVERIFY(terminal->start(mProgram,{},mTemp.path(),QProcessEnvironment::systemEnvironment()));
        delete terminal;QTest::qWait(1000);
    }
    void processJobOwnsMetadataChildren(){
#ifdef Q_OS_WIN
        const auto pidFile=mTemp.filePath("metadata-child.pid");QFile::remove(pidFile);HANDLE child=nullptr;
        {
            ProcessJob job;QProcess process;QVERIFY(job.protect(process));process.start(mProgram,{"--spawn-child",pidFile});QVERIFY(process.waitForStarted(5000));
            QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(pidFile),10000);QFile file(pidFile);QVERIFY(file.open(QIODevice::ReadOnly));const DWORD pid=file.readAll().trimmed().toUInt();QVERIFY(pid>0);
            child=OpenProcess(SYNCHRONIZE,FALSE,pid);QVERIFY(child);job.terminate();QVERIFY(process.waitForFinished(5000));
        }
        QCOMPARE(WaitForSingleObject(child,10000),DWORD(WAIT_OBJECT_0));CloseHandle(child);
#else
        QSKIP("Windows Job Object process ownership check");
#endif
    }
    void invalidProgram(){
        RustTerminal terminal;QVERIFY(!terminal.start(mTemp.filePath("missing.exe"),{},mTemp.path(),QProcessEnvironment::systemEnvironment()));
        QVERIFY(!terminal.active());
    }
};
QTEST_GUILESS_MAIN(RustTerminalChecks)
#include "rust-terminal-checks.moc"
