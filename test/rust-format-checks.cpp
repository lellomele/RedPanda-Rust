// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QThread>
#include <QStandardPaths>
#include <cstdio>
#ifdef Q_OS_WIN
#include <io.h>
#include <fcntl.h>
#endif
#include "reformatter/rustformatter.h"
class RustFormatChecks:public QObject {
    Q_OBJECT
    QTemporaryDir mTemp;
    static bool put(const QString &path,const QByteArray &bytes){QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size();}
private slots:
    void cleanup(){qunsetenv("REDPANDA_FAKE_FORMAT");}
    void standaloneRustfmt(){
        const auto tool=QStandardPaths::findExecutable("rustfmt");QVERIFY(!tool.isEmpty());RustFormatter formatter(tool,mTemp.filePath("test.rs"),"2024");QString error;bool ok=false;
        const auto text=formatter.refomat("mod absent_module;fn main(){let n=1;println!(\"{n}\");}",error,ok);QVERIFY2(ok,qPrintable(error));QVERIFY(text.contains("let n = 1;"));QVERIFY(text.contains("mod absent_module;"));
    }
    void cargoEditionAndInheritance(){
        const auto root=mTemp.filePath("workspace");QVERIFY(QDir().mkpath(root+"/member/src"));QString error;
        QVERIFY(put(root+"/Cargo.toml","[workspace]\nmembers=['member']\n[workspace.package]\nedition='2021'\n"));
        QVERIFY(put(root+"/member/Cargo.toml","[package]\nname='member'\nversion='0.1.0'\nedition.workspace=true\n"));
        QCOMPARE(RustFormatter::editionForFile(root+"/member/src/lib.rs","2024",error),QString("2021"));QVERIFY(error.isEmpty());
        RustFormatter formatter(QStandardPaths::findExecutable("rustfmt"),root+"/member/src/lib.rs","2024");bool ok=false;
        QVERIFY(!formatter.refomat("pub fn gen(){let x=1;}",error,ok).isEmpty());QVERIFY2(ok,qPrintable(error));
        QVERIFY(put(root+"/member/Cargo.toml","[package]\nedition='2018'\n"));QCOMPARE(RustFormatter::editionForFile(root+"/member/src/lib.rs","2024",error),QString("2018"));
        QVERIFY(put(root+"/member/Cargo.toml","[package]\nname='legacy'\n"));QCOMPARE(RustFormatter::editionForFile(root+"/member/src/lib.rs","2024",error),QString("2015"));
        QVERIFY(put(root+"/member/Cargo.toml","[package]\nedition=2024\n"));QVERIFY(RustFormatter::editionForFile(root+"/member/src/lib.rs","2024",error).isEmpty());QVERIFY(!error.isEmpty());
        QVERIFY(put(root+"/member/Cargo.toml","bad = ["));QVERIFY(RustFormatter::editionForFile(root+"/member/src/lib.rs","2024",error).isEmpty());
    }
    void toolFailures_data(){QTest::addColumn<QByteArray>("mode");for(const auto &m:QList<QByteArray>{"crash","hang","oversize","invalid-utf8"})QTest::newRow(m.constData())<<m;}
    void toolFailures(){QFETCH(QByteArray,mode);qputenv("REDPANDA_FAKE_FORMAT",mode);RustFormatter formatter(QCoreApplication::applicationFilePath(),mTemp.filePath("fake.rs"),"2024",{},100);QString error;bool ok=true;QElapsedTimer timer;timer.start();
        QVERIFY(formatter.refomat("fn main(){}",error,ok).isEmpty());QVERIFY(!ok);QVERIFY(!error.isEmpty());QVERIFY(timer.elapsed()<3000);
    }
    void documentChangesDuringFormatting(){qputenv("REDPANDA_FAKE_FORMAT","delay");bool current=true;QTimer::singleShot(10,this,[&]{current=false;});RustFormatter formatter(QCoreApplication::applicationFilePath(),mTemp.filePath("fake.rs"),"2024",[&]{return current;},1000);QString error;bool ok=true;
        QVERIFY(formatter.refomat("fn main(){}",error,ok).isEmpty());QVERIFY(!ok);QVERIFY(error.contains("cambiato"));
    }
};
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);const auto mode=qgetenv("REDPANDA_FAKE_FORMAT");if(!mode.isEmpty()){
#ifdef Q_OS_WIN
        _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
#endif
        if(mode=="crash")return 17;if(mode=="hang")QThread::msleep(30000);
        if(mode=="oversize"){const QByteArray block(65536,'a');for(int i=0;i<160;++i)if(std::fwrite(block.constData(),1,block.size(),stdout)!=size_t(block.size()))break;std::fflush(stdout);return 0;}
        if(mode=="invalid-utf8"){const char bytes[]={char(0xff),char(0xfe)};std::fwrite(bytes,1,2,stdout);return 0;}
        if(mode=="delay")QThread::msleep(200);std::fputs("fn main() {}\n",stdout);return 0;
    }
    RustFormatChecks checks;return QTest::qExec(&checks,argc,argv);
}
#include "rust-format-checks.moc"
