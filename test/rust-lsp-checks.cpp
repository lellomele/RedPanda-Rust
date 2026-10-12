// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QThread>
#include <cstdio>
#ifdef Q_OS_WIN
#include <io.h>
#include <fcntl.h>
#endif
#include "rustlspclient.h"
#include "rusttools.h"

namespace {
void frame(QJsonObject object,bool fragmented=false){
    object.insert("jsonrpc","2.0");const auto body=QJsonDocument(object).toJson(QJsonDocument::Compact);
    const auto bytes="Content-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body;
    if(fragmented){for(int offset=0;offset<bytes.size();offset+=7){const auto count=std::min(7,int(bytes.size())-offset);std::fwrite(bytes.constData()+offset,1,count,stdout);std::fflush(stdout);QThread::msleep(1);}}
    else {std::fwrite(bytes.constData(),1,bytes.size(),stdout);std::fflush(stdout);}
}
int fakeServer(){
#ifdef Q_OS_WIN
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
    const auto logPath=qEnvironmentVariable("REDPANDA_FAKE_LSP_LOG");
    const auto mode=qEnvironmentVariable("REDPANDA_FAKE_LSP_MODE");
    QHash<QString,int> versions;
    for(;;){
        char line[8192];int length=-1;
        while(std::fgets(line,sizeof(line),stdin)){
            const QByteArray header(line);if(header=="\r\n"||header=="\n")break;
            if(header.startsWith("Content-Length:"))length=header.mid(15).trimmed().toInt();
        }
        if(length<0||length>16*1024*1024)return 0;
        QByteArray body(length,Qt::Uninitialized);if(std::fread(body.data(),1,length,stdin)!=size_t(length))return 0;
        const auto message=QJsonDocument::fromJson(body).object();const auto method=message.value("method").toString();
        const auto params=message.value("params").toObject();
        if(!logPath.isEmpty()){QFile log(logPath);if(log.open(QIODevice::Append)){log.write(body);log.write("\n");}}
        if(method=="initialize"){
            if(mode=="crash")return 9;
            if(mode=="header"){std::fwrite("Content-Length: -8\r\n\r\n",1,23,stdout);std::fflush(stdout);QThread::msleep(30000);return 9;}
            frame({{"id",message.value("id")},{"result",QJsonObject{{"capabilities",QJsonObject{{"positionEncoding",mode=="encoding"?"utf-8":"utf-16"}}}}}},true);
            frame({{"id","configuration-1"},{"method","workspace/configuration"},{"params",QJsonObject{{"items",QJsonArray{QJsonObject{{"section","rust-analyzer"}}}}}}});
        }else if(method=="textDocument/didOpen"||method=="textDocument/didChange"){
            const auto document=params.value("textDocument").toObject();const auto uri=document.value("uri").toString();
            versions[uri]=document.value("version").toInt();
            frame({{"method","textDocument/publishDiagnostics"},{"params",QJsonObject{{"uri",uri},{"version",versions[uri]},
                {"diagnostics",QJsonArray{QJsonObject{{"message","fake semantic error"},{"severity",1},
                {"range",QJsonObject{{"start",QJsonObject{{"line",0},{"character",3}}},{"end",QJsonObject{{"line",0},{"character",4}}}}}}}}}}});
        }else if(method=="textDocument/completion"){
            if(mode=="delay")QThread::msleep(500);
            frame({{"id",message.value("id")},{"result",QJsonArray{QJsonObject{{"label",QString::fromUtf8("città_🦀")},{"insertText","completed"}}}}},true);
        }else if(method=="textDocument/hover"){
            frame({{"id",message.value("id")},{"result",QJsonObject{{"contents",QJsonObject{{"kind","plaintext"},{"value","fn sample() -> i32"}}}}}});
        }else if(method=="shutdown")frame({{"id",message.value("id")},{"result",QJsonValue::Null}});
        else if(method=="exit")return 0;
    }
}
bool put(const QString &path,const QByteArray &text){QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(text)==text.size();}
QList<QJsonObject> traffic(const QString &path){
    QFile file(path);QList<QJsonObject> messages;if(!file.open(QIODevice::ReadOnly))return messages;
    while(!file.atEnd())messages.append(QJsonDocument::fromJson(file.readLine()).object());return messages;
}
}
class RustLspChecks final:public QObject{
    Q_OBJECT
    QTemporaryDir mTemp;
    QString mFile,mLog;
private slots:
    void init(){
        QVERIFY(mTemp.isValid());mFile=mTemp.filePath("file con accenti.rs");mLog=mTemp.filePath("traffic.jsonl");
        QVERIFY(put(mFile,"fn main() {}\n"));QFile::remove(mLog);
        RustTools::setSettingsFile(mTemp.filePath("settings.ini"));QString error;
        QVERIFY2(RustTools::save({{"rust-analyzer",QCoreApplication::applicationFilePath()}},"2024",error),qPrintable(error));
        qputenv("REDPANDA_FAKE_LSP","1");qputenv("REDPANDA_FAKE_LSP_LOG",mLog.toUtf8());qunsetenv("REDPANDA_FAKE_LSP_MODE");
    }
    void cleanup(){RustTools::setSettingsFile({});qunsetenv("REDPANDA_FAKE_LSP");qunsetenv("REDPANDA_FAKE_LSP_LOG");qunsetenv("REDPANDA_FAKE_LSP_MODE");}
    void fragmentedFramesAndUtf16(){
        RustLspClient client;QSignalSpy result(&client,&RustLspClient::result),diagnostics(&client,&RustLspClient::diagnostics);
        QCOMPARE(client.updateDocument(mFile,QString::fromUtf8("fn città_🦀() {}\n")),1);QTRY_VERIFY_WITH_TIMEOUT(client.ready(),15000);
        const int id=client.requestAt("textDocument/completion",mFile,0,12);QVERIFY(id>0);
        QTRY_VERIFY_WITH_TIMEOUT(!result.isEmpty(),10000);QCOMPARE(result.first()[0].toInt(),id);
        QCOMPARE(result.first()[4].value<QJsonValue>().toArray().first().toObject().value("label").toString(),QString::fromUtf8("città_🦀"));
        QTRY_VERIFY_WITH_TIMEOUT(!diagnostics.isEmpty(),10000);
        bool configurationAnswered=false;for(const auto &message:traffic(mLog))if(message.value("id")=="configuration-1"&&message.contains("result"))configurationAnswered=true;
        QVERIFY(configurationAnswered);
    }
    void staleResponseAndCancellation(){
        qputenv("REDPANDA_FAKE_LSP_MODE","delay");RustLspClient client;QSignalSpy result(&client,&RustLspClient::result);
        client.updateDocument(mFile,"fn main() {}\n");QTRY_VERIFY_WITH_TIMEOUT(client.ready(),15000);
        const int stale=client.requestAt("textDocument/completion",mFile,0,3);QVERIFY(stale>0);
        QCOMPARE(client.updateDocument(mFile,"fn changed() {}\n"),2);QTest::qWait(900);QCOMPARE(result.count(),0);
        const int cancelled=client.requestAt("textDocument/completion",mFile,0,3);client.cancel(cancelled);QTest::qWait(900);QCOMPARE(result.count(),0);
        const int fresh=client.requestAt("textDocument/completion",mFile,0,3);QTRY_COMPARE_WITH_TIMEOUT(result.count(),1,10000);QCOMPARE(result.first()[0].toInt(),fresh);QCOMPARE(result.first()[3].toInt(),2);
    }
    void closeDuringStartupAndReopen(){
        RustLspClient client;client.updateDocument(mFile,"fn main() {}\n");client.closeDocument(mFile);
        QTest::qWait(500);QVERIFY(!client.ready());QCOMPARE(client.version(mFile),0);
        client.updateDocument(mFile,"fn reopened() {}\n");QTRY_VERIFY_WITH_TIMEOUT(client.ready(),15000);
        client.closeDocument(mFile);QCOMPARE(client.version(mFile),0);
        client.updateDocument(mFile,"fn reopened_twice() {}\n");QTRY_VERIFY_WITH_TIMEOUT(client.ready(),15000);
    }
    void boundedCrashRecoveryAndManualRestart(){
        qputenv("REDPANDA_FAKE_LSP_MODE","crash");RustLspClient client;QSignalSpy status(&client,&RustLspClient::status);
        client.updateDocument(mFile,"fn main() {}\n");
        QTRY_VERIFY_WITH_TIMEOUT([&]{for(const auto &entry:status)if(entry[0].toString().contains("tre errori"))return true;return false;}(),20000);
        int starts=0;for(const auto &message:traffic(mLog))if(message.value("method")=="initialize")++starts;QCOMPARE(starts,3);
        QTest::qWait(1000);QVERIFY(!client.ready());qunsetenv("REDPANDA_FAKE_LSP_MODE");client.restart();QTRY_VERIFY_WITH_TIMEOUT(client.ready(),15000);
    }
    void malformedHeaderRecovery(){
        qputenv("REDPANDA_FAKE_LSP_MODE","header");RustLspClient client;QSignalSpy status(&client,&RustLspClient::status);
        client.updateDocument(mFile,"fn main() {}\n");
        QTRY_VERIFY_WITH_TIMEOUT([&]{for(const auto &entry:status)if(entry[0].toString().contains("Content-Length"))return true;return false;}(),10000);
        qunsetenv("REDPANDA_FAKE_LSP_MODE");client.restart();QTRY_VERIFY_WITH_TIMEOUT(client.ready(),15000);
    }
    void unsupportedPositionEncoding(){
        qputenv("REDPANDA_FAKE_LSP_MODE","encoding");RustLspClient client;QSignalSpy status(&client,&RustLspClient::status);
        client.updateDocument(mFile,"fn main() {}\n");
        QTRY_VERIFY_WITH_TIMEOUT([&]{for(const auto &entry:status)if(entry[0].toString().contains("codifica"))return true;return false;}(),10000);
        QVERIFY(!client.ready());
    }
    void documentSizeLimit(){
        RustLspClient client;QSignalSpy status(&client,&RustLspClient::status);
        QCOMPARE(client.updateDocument(mFile,QString(4*1024*1024+1,'x')),0);QCOMPARE(client.version(mFile),0);QVERIFY(!status.isEmpty());
    }
    void realRustAnalyzer(){
        qunsetenv("REDPANDA_FAKE_LSP");QString error;QVERIFY(RustTools::save({},"2024",error));
        RustLspClient client;QSignalSpy result(&client,&RustLspClient::result);
        const QString text="fn answer() -> i32 { 42 }\nfn main(){ let value = answer(); println!(\"{}\", value); }\n";
        client.updateDocument(mFile,text);QTRY_VERIFY_WITH_TIMEOUT(client.ready(),30000);
        // Wait for project indexing, retrying a read-only semantic request.
        bool found=false;QElapsedTimer timer;timer.start();
        while(!found&&timer.elapsed()<45000){
            result.clear();client.requestAt("textDocument/hover",mFile,1,17);
            QTest::qWait(500);
            for(const auto &entry:result)if(entry[4].value<QJsonValue>().toObject().value("contents").toObject().value("value").toString().contains("i32"))found=true;
        }
        QVERIFY2(found,"rust-analyzer did not return semantic information for the standalone file");
    }
    void realReferencesRenameOutlineSignaturesAndMacros(){
        qunsetenv("REDPANDA_FAKE_LSP");QString error;QVERIFY(RustTools::save({},"2024",error));
        const auto root=mTemp.filePath("navigation-project");QDir().mkpath(root+"/src");
        QVERIFY(put(root+"/Cargo.toml","[package]\nname=\"navigation_checks\"\nversion=\"0.1.0\"\nedition=\"2024\"\n"));
        const auto main=root+"/src/main.rs",helper=root+"/src/helpers.rs";
        const QString mainText="mod helpers;\nuse helpers::answer;\nfn add(a:i32,b:i32)->i32 {a+b}\nfn main(){\n let value = answer();\n let result = add(value, 1);\n println!(\"{}\", result);\n}\n";
        const QString helperText="pub fn answer()->i32 {41}\n";QVERIFY(put(main,mainText.toUtf8()));QVERIFY(put(helper,helperText.toUtf8()));
        RustLspClient client;QSignalSpy results(&client,&RustLspClient::result);client.updateDocument(main,mainText);client.updateDocument(helper,helperText);QTRY_VERIFY_WITH_TIMEOUT(client.ready(),30000);
        const auto query=[&](const QString &method,const QString &file,int line,int character,const QJsonObject &extra,const std::function<bool(const QJsonValue &)> &valid){
            QElapsedTimer deadline;deadline.start();QJsonValue found;
            while(deadline.elapsed()<45000){
                results.clear();const auto id=file.isEmpty()?client.requestWorkspace(method,extra):method=="textDocument/documentSymbol"?client.requestDocument(method,file,extra):client.requestAt(method,file,line,character,extra);
                QElapsedTimer wait;wait.start();while(wait.elapsed()<700){QTest::qWait(20);for(const auto &entry:results)if(entry[0].toInt()==id&&valid(entry[4].value<QJsonValue>())){found=entry[4].value<QJsonValue>();return found;}}
            }
            return found;
        };
        const auto references=query("textDocument/references",helper,0,9,{{"context",QJsonObject{{"includeDeclaration",true}}}},[](const QJsonValue &value){return value.toArray().size()>=3;});QVERIFY(references.toArray().size()>=3);
        const auto renamed=query("textDocument/rename",helper,0,9,{{"newName","renamed_answer"}},[](const QJsonValue &value){const auto object=value.toObject();return object.contains("changes")||object.contains("documentChanges");});QVERIFY(renamed.isObject());
        const auto symbols=query("textDocument/documentSymbol",main,0,0,{},[](const QJsonValue &value){return !value.toArray().isEmpty();});QVERIFY(!symbols.toArray().isEmpty());
        const auto workspace=query("workspace/symbol",{},0,0,{{"query","answer"}},[](const QJsonValue &value){return !value.toArray().isEmpty();});QVERIFY(!workspace.toArray().isEmpty());
        const auto signature=query("textDocument/signatureHelp",main,5,18,{},[](const QJsonValue &value){return !value.toObject().value("signatures").toArray().isEmpty();});QVERIFY(!signature.toObject().value("signatures").toArray().isEmpty());
        const auto expansion=query("rust-analyzer/expandMacro",main,6,4,{},[](const QJsonValue &value){return !value.toObject().value("expansion").toString().isEmpty();});QVERIFY(!expansion.toObject().value("expansion").toString().isEmpty());
    }
};
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    if(qEnvironmentVariable("REDPANDA_FAKE_LSP")=="1")return fakeServer();
    RustLspChecks checks;return QTest::qExec(&checks,argc,argv);
}
#include "rust-lsp-checks.moc"
