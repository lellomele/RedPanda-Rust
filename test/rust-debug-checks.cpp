// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QProcess>
#include <QTcpSocket>
#include <QTimer>
#include <QJsonDocument>
#include "rustdebugsession.h"
#include "rusttools.h"
#include "rustvariablepages.h"
static int fakeAdapter(QCoreApplication &app){
    const auto args=app.arguments();const auto mode=qEnvironmentVariable("REDPANDA_FAKE_DAP_MODE");
    if(mode=="crash")return 9;
    const int portAt=args.indexOf("--connect"),tokenAt=args.indexOf("--auth-token");
    if(portAt<0||tokenAt<0)return 8;QTcpSocket socket;socket.connectToHost(QHostAddress::LocalHost,args[portAt+1].toUShort());
    if(!socket.waitForConnected(5000))return 7;
    const auto token=args[tokenAt+1].toUtf8();QByteArray input;
    if(socket.waitForReadyRead(5000))input=socket.readAll();
    const auto end=input.indexOf("\r\n\r\n");const auto request=QJsonDocument::fromJson(end<0?QByteArray{}:input.mid(end+4)).object();
    const auto body=QJsonDocument(QJsonObject{{"seq",1},{"type","response"},{"request_seq",request.value("seq")},{"command","initialize"},{"success",true},{"body",QJsonObject{}}}).toJson(QJsonDocument::Compact);
    QByteArray frame;
    if(mode=="auth")frame="Auth-Token: wrong-token\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body;
    else frame="Auth-Token: "+token+"\r\nContent-Length: -5\r\n\r\n";
    socket.write(frame);socket.waitForBytesWritten(5000);
    QTimer::singleShot(10000,&app,&QCoreApplication::quit);return app.exec();
}
class RustDebugChecks final:public QObject{
    Q_OBJECT
    QTemporaryDir mTemp;QString mFile,mProgram,mAdapter,mLoopFile,mLoopProgram;
    static QJsonObject response(QSignalSpy &spy,int id){
        QElapsedTimer timer;timer.start();
        while(timer.elapsed()<15000){
            for(const auto &entry:spy)if(entry[0].toInt()==id){
                if(!entry[3].toBool()){QTest::qFail(qPrintable(entry[4].toString()),__FILE__,__LINE__);return {};}
                return entry[2].toJsonObject();
            }
            QTest::qWait(20);
        }
        QTest::qFail("DAP response timeout",__FILE__,__LINE__);return {};
    }
private slots:
    void initTestCase(){
        QVERIFY(mTemp.isValid());mFile=mTemp.filePath("debug con accenti.rs");mProgram=mTemp.filePath("debug.exe");
        mAdapter=qEnvironmentVariable("REDPANDA_TEST_CODELLDB");if(mAdapter.isEmpty())mAdapter=RustTools::detectedPath("codelldb");QVERIFY(QFileInfo(mAdapter).isExecutable());
        QFile file(mFile);QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"RS(fn main(){
    let mut total = 7_i32;
    total += 2;
    println!("DEBUG_RUST {}", total);
    let values: Vec<i32> = (0..450).collect();
    println!("VECTOR {}", values.len());
    let mut input=String::new();
    std::io::stdin().read_line(&mut input).unwrap();
    println!("INPUT {}", input.trim());
})RS");file.close();
        QProcess compiler;compiler.start(RustTools::detectedPath("rustc"),{"--edition","2024","--crate-name","debug_checks","-g","-C","opt-level=0",mFile,"-o",mProgram});
        QVERIFY(compiler.waitForFinished(30000));QVERIFY2(compiler.exitCode()==0,compiler.readAllStandardError().constData());
        mLoopFile=mTemp.filePath("loop.rs");mLoopProgram=mTemp.filePath("loop.exe");QFile loopFile(mLoopFile);QVERIFY(loopFile.open(QIODevice::WriteOnly));
        loopFile.write("fn main(){\n    let mut total=0_i32;\n    for i in 0..5{\n        total+=i;\n    }\n    println!(\"LOOP {}\",total);\n}\n");loopFile.close();
        compiler.start(RustTools::detectedPath("rustc"),{"--edition","2024","--crate-name","loop_checks","-g","-C","opt-level=0",mLoopFile,"-o",mLoopProgram});QVERIFY(compiler.waitForFinished(30000));QVERIFY2(compiler.exitCode()==0,compiler.readAllStandardError().constData());
    }
    void init(){RustTools::setSettingsFile(mTemp.filePath("settings.ini"));QString error;QVERIFY2(RustTools::save({{"codelldb",mAdapter}},"2024",error),qPrintable(error));}
    void cleanup(){RustTools::setSettingsFile({});qunsetenv("REDPANDA_FAKE_DAP_MODE");QTest::qWait(300);}
    void launchInspectStepMemoryAndInput(){
        RustDebugSession session;QSignalSpy stops(&session,&RustDebugSession::stopped),replies(&session,&RustDebugSession::reply),done(&session,&RustDebugSession::finished),errors(&session,&RustDebugSession::error);
        QString output;connect(&session,&RustDebugSession::output,this,[&](const QString &text){output+=text;});
        session.setBreakpoints(mFile,{QJsonObject{{"line",4}}});QVERIFY(session.launch(mProgram,mTemp.path()));
        QTRY_COMPARE_WITH_TIMEOUT(stops.count(),1,30000);QCOMPARE(session.state(),RustDebugSession::State::Stopped);
        const auto stack=response(replies,session.request("stackTrace",{{"threadId",session.threadId()},{"levels",10}}));
        QVERIFY(!stack.value("stackFrames").toArray().isEmpty());const auto frame=stack.value("stackFrames").toArray().first().toObject();const int frameId=frame.value("id").toInt();QVERIFY(frameId>0);
        QCOMPARE(frame.value("line").toInt(),4);
        const auto evaluated=response(replies,session.request("evaluate",{{"expression","total"},{"frameId",frameId},{"context","watch"}}));QCOMPARE(evaluated.value("result").toString(),QString("9"));
        const auto scopes=response(replies,session.request("scopes",{{"frameId",frameId}}));QVERIFY(!scopes.value("scopes").toArray().isEmpty());
        const int reference=scopes.value("scopes").toArray().first().toObject().value("variablesReference").toInt();QVERIFY(reference>0);
        const auto variables=response(replies,session.request("variables",{{"variablesReference",reference},{"start",0},{"count",100}}));
        bool found=false;for(const auto &entry:variables.value("variables").toArray())if(entry.toObject().value("name")=="total"&&entry.toObject().value("value")=="9")found=true;QVERIFY(found);
        const auto address=frame.value("instructionPointerReference").toString();QVERIFY(!address.isEmpty());
        const auto assembly=response(replies,session.request("disassemble",{{"memoryReference",address},{"instructionCount",5}}));QVERIFY(!assembly.value("instructions").toArray().isEmpty());
        const auto memory=response(replies,session.request("readMemory",{{"memoryReference",address},{"count",16}}));QVERIFY(!memory.value("data").toString().isEmpty());
        session.setBreakpoints(mFile,{});QTest::qWait(100);session.command("next");QTRY_COMPARE_WITH_TIMEOUT(stops.count(),2,15000);
        QVERIFY(session.sendInput(QString::fromUtf8("ciao città 🦀")));session.command("continue");
        QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,30000);QCOMPARE(done.first()[0].toInt(),0);QCOMPARE(errors.count(),0);
        QVERIFY2(output.contains("DEBUG_RUST 9")&&output.contains(QString::fromUtf8("INPUT ciao città 🦀")),qPrintable(output));
    }
    void runToCursorAndCancellation(){
        RustDebugSession session;QSignalSpy stops(&session,&RustDebugSession::stopped),done(&session,&RustDebugSession::finished),replies(&session,&RustDebugSession::reply);
        QVERIFY(session.launch(mProgram,mTemp.path(),{},QProcessEnvironment::systemEnvironment(),true));QTRY_COMPARE_WITH_TIMEOUT(stops.count(),1,30000);
        session.runTo(mFile,5);QTRY_VERIFY_WITH_TIMEOUT(stops.count()>=2,15000);
        const auto stack=response(replies,session.request("stackTrace",{{"threadId",session.threadId()},{"levels",1}}));
        QCOMPARE(stack.value("stackFrames").toArray().first().toObject().value("line").toInt(),6);
        session.stop();QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,10000);QCOMPARE(session.state(),RustDebugSession::State::Idle);
    }
    void launchDirectlyToCursor(){
        RustDebugSession session;QSignalSpy stops(&session,&RustDebugSession::stopped),done(&session,&RustDebugSession::finished),replies(&session,&RustDebugSession::reply),errors(&session,&RustDebugSession::error);
        QVERIFY(session.launch(mProgram,mTemp.path(),{},QProcessEnvironment::systemEnvironment(),false,mFile,5));
        QTRY_COMPARE_WITH_TIMEOUT(stops.count(),1,30000);
        const auto stack=response(replies,session.request("stackTrace",{{"threadId",session.threadId()},{"levels",1}}));
        QCOMPARE(stack.value("stackFrames").toArray().first().toObject().value("line").toInt(),6);
        QCOMPARE(errors.count(),0);QVERIFY(session.sendInput("direct-run-to"));session.command("continue");
        QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,30000);QCOMPARE(stops.count(),1);QCOMPARE(done.first()[0].toInt(),0);
    }
    void rejectedRunToPreservesStoppedSession(){
        RustDebugSession session;QSignalSpy stops(&session,&RustDebugSession::stopped),done(&session,&RustDebugSession::finished),errors(&session,&RustDebugSession::error);
        QVERIFY(session.launch(mProgram,mTemp.path(),{},QProcessEnvironment::systemEnvironment(),true));QTRY_COMPARE_WITH_TIMEOUT(stops.count(),1,30000);
        session.runTo(mTemp.filePath("missing.rs"),1);QCOMPARE(errors.count(),1);QCOMPARE(session.state(),RustDebugSession::State::Stopped);
        const auto unknown=mTemp.filePath("not-compiled.rs");QFile source(unknown);QVERIFY(source.open(QIODevice::WriteOnly));source.write("fn main(){println!(\"not in binary\");}\n");source.close();
        session.runTo(unknown,0);QTRY_VERIFY_WITH_TIMEOUT(errors.count()>=2,15000);
        QTest::qWait(150);QCOMPARE(session.state(),RustDebugSession::State::Stopped);QCOMPARE(stops.count(),1);
        session.runTo(mFile,5);QTRY_COMPARE_WITH_TIMEOUT(stops.count(),2,15000);
        session.stop();QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,10000);
    }
    void manyBreakpointUpdatesAreNotDropped(){
        RustDebugSession session;QSignalSpy stops(&session,&RustDebugSession::stopped),replies(&session,&RustDebugSession::reply),done(&session,&RustDebugSession::finished);
        QVERIFY(session.launch(mProgram,mTemp.path(),{},QProcessEnvironment::systemEnvironment(),true));QTRY_COMPARE_WITH_TIMEOUT(stops.count(),1,30000);
        replies.clear();
        for(int i=0;i<60;++i)session.setBreakpoints(mTemp.filePath(QString("empty-%1.rs").arg(i)),{});
        const auto responses=[&]{int count=0;for(const auto &reply:replies)if(reply[1].toString()=="setBreakpoints"&&reply[3].toBool())++count;return count;};
        QTRY_VERIFY_WITH_TIMEOUT(responses()>=60,15000);
        session.runTo(mFile,5);QTRY_COMPARE_WITH_TIMEOUT(stops.count(),2,15000);
        session.stop();QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,10000);
    }
    void variableEditingAndDataBreakpoint(){
        RustDebugSession session;QSignalSpy stops(&session,&RustDebugSession::stopped),replies(&session,&RustDebugSession::reply),done(&session,&RustDebugSession::finished);
        QString output;connect(&session,&RustDebugSession::output,this,[&](const QString &text){output+=text;});
        session.setBreakpoints(mFile,{QJsonObject{{"line",3}}});QVERIFY(session.launch(mProgram,mTemp.path()));QTRY_COMPARE_WITH_TIMEOUT(stops.count(),1,30000);
        QVERIFY(session.capabilities().value("supportsSetVariable").toBool());QVERIFY(session.capabilities().value("supportsDataBreakpoints").toBool());
        const auto stack=response(replies,session.request("stackTrace",{{"threadId",session.threadId()},{"levels",1}}));const int frame=stack.value("stackFrames").toArray().first().toObject().value("id").toInt();
        const auto scopes=response(replies,session.request("scopes",{{"frameId",frame}}));const int reference=scopes.value("scopes").toArray().first().toObject().value("variablesReference").toInt();
        const auto changed=response(replies,session.request("setVariable",{{"variablesReference",reference},{"name","total"},{"value","11"}}));QCOMPARE(changed.value("value").toString(),QString("11"));
        const auto info=response(replies,session.request("dataBreakpointInfo",{{"variablesReference",reference},{"name","total"},{"frameId",frame}}));QVERIFY2(!info.value("dataId").toString().isEmpty(),QJsonDocument(info).toJson().constData());
        const auto configured=response(replies,session.request("setDataBreakpoints",{{"breakpoints",QJsonArray{QJsonObject{{"dataId",info.value("dataId")},{"accessType","write"}}}}}));
        QVERIFY2(configured.value("breakpoints").toArray().first().toObject().value("verified").toBool(),QJsonDocument(configured).toJson().constData());
        session.setBreakpoints(mFile,{});QTest::qWait(100);session.command("continue");QTRY_COMPARE_WITH_TIMEOUT(stops.count(),2,15000);
        QVERIFY2(stops.last()[1].toString().contains("data")||stops.last()[1].toString().contains("watch"),qPrintable(stops.last()[1].toString()));
        response(replies,session.request("setDataBreakpoints",{{"breakpoints",QJsonArray{}}}));QVERIFY(session.sendInput("data-check"));session.command("continue");QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,30000);QVERIFY2(output.contains("DEBUG_RUST 13"),qPrintable(output));
    }
    void variablesBeyondFirstPage(){
        RustDebugSession session;QSignalSpy stops(&session,&RustDebugSession::stopped),replies(&session,&RustDebugSession::reply),done(&session,&RustDebugSession::finished);
        session.setBreakpoints(mFile,{QJsonObject{{"line",6}}});QVERIFY(session.launch(mProgram,mTemp.path()));QTRY_COMPARE_WITH_TIMEOUT(stops.count(),1,30000);
        const auto stack=response(replies,session.request("stackTrace",{{"threadId",session.threadId()},{"levels",1}}));const int frame=stack.value("stackFrames").toArray().first().toObject().value("id").toInt();
        const auto scopes=response(replies,session.request("scopes",{{"frameId",frame}}));const int scope=scopes.value("scopes").toArray().first().toObject().value("variablesReference").toInt();
        const auto locals=response(replies,session.request("variables",{{"variablesReference",scope},{"start",0},{"count",200}}));int reference=0;
        for(const auto &entry:locals.value("variables").toArray())if(entry.toObject().value("name")=="values")reference=entry.toObject().value("variablesReference").toInt();QVERIFY(reference>0);
        const auto all=response(replies,session.request("variables",{{"variablesReference",reference}}));RustVariablePages pages;pages.store(reference,all.value("variables").toArray());
        const auto first=pages.page(reference,0),second=pages.page(reference,200),last=pages.page(reference,400);
        QCOMPARE(first.size(),200);QCOMPARE(second.size(),200);QVERIFY(last.size()>=50);QVERIFY(first.first()!=second.first());QVERIFY(!pages.more(reference,600));
        QCOMPARE(pages.page(reference,-1).size(),0);pages.clear();QCOMPARE(pages.page(reference,0).size(),0);
        session.stop();QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,10000);
    }
    void functionBreakpoint(){
        RustDebugSession session;QSignalSpy stops(&session,&RustDebugSession::stopped),replies(&session,&RustDebugSession::reply),done(&session,&RustDebugSession::finished),errors(&session,&RustDebugSession::error);
        session.setFunctionBreakpoints({QJsonObject{{"name","debug_checks::main"}}});QVERIFY(session.launch(mProgram,mTemp.path()));QTRY_COMPARE_WITH_TIMEOUT(stops.count(),1,30000);
        const auto stack=response(replies,session.request("stackTrace",{{"threadId",session.threadId()},{"levels",1}}));QVERIFY(stack.value("stackFrames").toArray().first().toObject().value("name").toString().contains("debug_checks::main"));
        QCOMPARE(errors.count(),0);session.setFunctionBreakpoints({});QTest::qWait(100);QVERIFY(session.sendInput("functions"));session.command("continue");QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,30000);
    }
    void conditionalHitAndLogBreakpoints_data(){QTest::addColumn<QString>("mode");QTest::newRow("condition")<<QString("condition");QTest::newRow("hit-count")<<QString("hit");QTest::newRow("logpoint")<<QString("log");}
    void conditionalHitAndLogBreakpoints(){
        QFETCH(QString,mode);RustDebugSession session;QSignalSpy stops(&session,&RustDebugSession::stopped),replies(&session,&RustDebugSession::reply),done(&session,&RustDebugSession::finished),errors(&session,&RustDebugSession::error);
        QString output;connect(&session,&RustDebugSession::output,this,[&](const QString &text){output+=text;});QJsonObject bp{{"line",4}};QJsonArray breakpoints;
        if(mode=="condition")bp.insert("condition","i == 3");else if(mode=="hit")bp.insert("hitCondition","3");else bp.insert("logMessage","ITER {i}");breakpoints.append(bp);if(mode=="log")breakpoints.append(QJsonObject{{"line",6}});
        session.setBreakpoints(mLoopFile,breakpoints);QVERIFY(session.launch(mLoopProgram,mTemp.path()));QTRY_COMPARE_WITH_TIMEOUT(stops.count(),1,30000);
        const auto stack=response(replies,session.request("stackTrace",{{"threadId",session.threadId()},{"levels",1}}));const auto frame=stack.value("stackFrames").toArray().first().toObject();
        QCOMPARE(frame.value("line").toInt(),mode=="log"?6:4);
        if(mode=="log"){QTRY_VERIFY_WITH_TIMEOUT(output.contains("ITER 0")&&output.contains("ITER 4"),5000);}
        else{const auto value=response(replies,session.request("evaluate",{{"expression","i"},{"frameId",frame.value("id")},{"context","watch"}}));QCOMPARE(value.value("result").toString(),mode=="condition"?QString("3"):QString("2"));}
        QCOMPARE(errors.count(),0);session.setBreakpoints(mLoopFile,{});QTest::qWait(100);session.command("continue");QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,15000);QVERIFY2(output.contains("LOOP 10"),qPrintable(output));
    }
    void failedStartAndRetry(){
        RustDebugSession session;QSignalSpy errors(&session,&RustDebugSession::error),stops(&session,&RustDebugSession::stopped),done(&session,&RustDebugSession::finished);
        QVERIFY(!session.launch(mTemp.filePath("missing.exe"),mTemp.path()));QCOMPARE(session.state(),RustDebugSession::State::Idle);QVERIFY(!errors.isEmpty());
        QVERIFY(session.launch(mProgram,mTemp.path(),{},QProcessEnvironment::systemEnvironment(),true));QTRY_COMPARE_WITH_TIMEOUT(stops.count(),1,30000);session.stop();QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,10000);
    }
    void destroyDuringStart(){auto session=new RustDebugSession;QVERIFY(session->launch(mProgram,mTemp.path()));delete session;QTest::qWait(500);}
    void stopFromStartingCallbackAndRetry(){
        RustDebugSession session;QSignalSpy done(&session,&RustDebugSession::finished);bool once=true;
        connect(&session,&RustDebugSession::stateChanged,this,[&](RustDebugSession::State state){if(once&&state==RustDebugSession::State::Starting){once=false;session.stop();}});
        QVERIFY(!session.launch(mProgram,mTemp.path()));QTRY_COMPARE_WITH_TIMEOUT(session.state(),RustDebugSession::State::Idle,10000);QCOMPARE(done.count(),1);
        QVERIFY(session.launch(mProgram,mTemp.path(),{},QProcessEnvironment::systemEnvironment(),true));QTRY_COMPARE_WITH_TIMEOUT(session.state(),RustDebugSession::State::Stopped,30000);
        session.stop();QTRY_COMPARE_WITH_TIMEOUT(done.count(),2,10000);
    }
    void stopFromStoppedStateCallback(){
        RustDebugSession session;QSignalSpy stops(&session,&RustDebugSession::stopped),done(&session,&RustDebugSession::finished);
        connect(&session,&RustDebugSession::stateChanged,this,[&](RustDebugSession::State state){if(state==RustDebugSession::State::Stopped)session.stop();});
        QVERIFY(session.launch(mProgram,mTemp.path(),{},QProcessEnvironment::systemEnvironment(),true));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,30000);
        QCOMPARE(stops.count(),0);QCOMPARE(session.state(),RustDebugSession::State::Idle);
    }
    void finishCallbackCannotLaunchBeforeCleanupCompletes(){
        RustDebugSession session;QSignalSpy done(&session,&RustDebugSession::finished);bool accepted=true;
        const auto connection=connect(&session,&RustDebugSession::finished,this,[&]{accepted=session.launch(mProgram,mTemp.path());});
        QVERIFY(session.launch(mProgram,mTemp.path(),{},QProcessEnvironment::systemEnvironment(),true));QTRY_COMPARE_WITH_TIMEOUT(session.state(),RustDebugSession::State::Stopped,30000);
        session.stop();QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,10000);QVERIFY(!accepted);disconnect(connection);
        QVERIFY(session.launch(mProgram,mTemp.path(),{},QProcessEnvironment::systemEnvironment(),true));QTRY_COMPARE_WITH_TIMEOUT(session.state(),RustDebugSession::State::Stopped,30000);
        session.stop();QTRY_COMPARE_WITH_TIMEOUT(done.count(),2,10000);
    }
    void faultyAdapter_data(){QTest::addColumn<QByteArray>("mode");QTest::newRow("authentication")<<QByteArray("auth");QTest::newRow("malformed-header")<<QByteArray("header");QTest::newRow("early-exit")<<QByteArray("crash");}
    void faultyAdapter(){
        QFETCH(QByteArray,mode);qputenv("REDPANDA_FAKE_DAP_MODE",mode);QString message;
        QVERIFY(RustTools::save({{"codelldb",QCoreApplication::applicationFilePath()}},"2024",message));
        RustDebugSession session;QSignalSpy errors(&session,&RustDebugSession::error),done(&session,&RustDebugSession::finished);
        QVERIFY(session.launch(mProgram,mTemp.path()));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,10000);QVERIFY(!errors.isEmpty());QCOMPARE(session.state(),RustDebugSession::State::Idle);
    }
};
int main(int argc,char **argv){QCoreApplication app(argc,argv);if(!qEnvironmentVariable("REDPANDA_FAKE_DAP_MODE").isEmpty())return fakeAdapter(app);RustDebugChecks checks;return QTest::qExec(&checks,argc,argv);}
#include "rust-debug-checks.moc"
