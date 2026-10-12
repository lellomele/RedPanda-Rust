// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include "rustprojectoptions.h"
#include "rustworkspace.h"
#include "rusttools.h"
class RustProjectChecks final:public QObject{
    Q_OBJECT
    QTemporaryDir mTemp;
    static bool put(const QString &path,const QByteArray &text){QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(text)==text.size();}
    static int run(RustWorkspace &workspace,const QString &root,const QString &command){QSignalSpy done(&workspace,&RustWorkspace::finished);if(!workspace.start(root,command))return -999;QElapsedTimer timer;timer.start();while(done.isEmpty()&&timer.elapsed()<45000)QTest::qWait(30);return done.isEmpty()?-998:done.first()[0].toInt();}
private slots:
    void init(){QVERIFY(mTemp.isValid());RustTools::setSettingsFile(mTemp.filePath("settings.ini"));}
    void cleanup(){RustTools::setSettingsFile({});}
    void validationAndRoundTrip(){
        const auto root=mTemp.filePath("settings-root");QDir().mkpath(root);QString error;RustProjectOptions options;
        options.release=true;options.package="package-one";options.targetKind="bin";options.targetName="app-one";options.targetTriple="x86_64-pc-windows-msvc";options.features={"one","package-two/feature"};
        options.arguments={"two words","",QString::fromUtf8("città 🦀")};options.environment.insert("KEY","a=b");options.rustFlags={"-C","overflow-checks=on"};
        QVERIFY2(options.save(root,error),qPrintable(error));const auto loaded=RustProjectOptions::load(root);QCOMPARE(loaded.json(),options.json());
        const auto args=loaded.cargoArguments("build");QVERIFY(args.contains("--release"));QVERIFY(args.contains("--package"));QVERIFY(args.contains("--bin"));
        options.environment.insert("BAD=KEY","bad");QVERIFY(!options.save(root,error));QCOMPARE(RustProjectOptions::load(root).json(),loaded.json());
        options=loaded;options.workingDirectory="missing";QVERIFY(!options.save(root,error));
        options=loaded;options.targetKind="bin";options.targetName.clear();QVERIFY(!options.validate(root,error));
        options=loaded;options.arguments={QString(25000,'a')};QVERIFY(!options.validate(root,error));
        RustProjectOptions preserved=loaded;QVERIFY(!RustProjectOptions::decode({{"version",99}},preserved,error));QCOMPARE(preserved.json(),loaded.json());
    }
    void isolatedProjectSettings(){
        const auto a=mTemp.filePath("one-settings"),b=mTemp.filePath("two-settings");QDir().mkpath(a);QDir().mkpath(b);QString error;RustProjectOptions one,two;one.arguments={"one"};two.arguments={"two"};
        QVERIFY(one.save(a,error));QVERIFY(two.save(b,error));QCOMPARE(RustProjectOptions::load(a).arguments,one.arguments);QCOMPARE(RustProjectOptions::load(b).arguments,two.arguments);
    }
    void cargoWorkspaceReleaseFeaturesArgumentsEnvironment(){
        const auto root=mTemp.filePath("workspace");QDir().mkpath(root+"/one/src");QDir().mkpath(root+"/two/src");QDir().mkpath(root+"/cwd");
        QVERIFY(put(root+"/Cargo.toml","[workspace]\nmembers=[\"one\",\"two\"]\nresolver=\"3\"\n"));
        QVERIFY(put(root+"/one/Cargo.toml","[package]\nname=\"one\"\nversion=\"0.1.0\"\nedition=\"2024\"\n[[bin]]\nname=\"alpha\"\npath=\"src/main.rs\"\n[features]\nspecial=[]\n"));
        QVERIFY(put(root+"/two/Cargo.toml","[package]\nname=\"two\"\nversion=\"0.1.0\"\nedition=\"2024\"\n"));QVERIFY(put(root+"/two/src/main.rs","fn main(){println!(\"OTHER_BINARY\");}\n"));
        QVERIFY(put(root+"/one/src/main.rs",R"RS(#[cfg(feature="special")] const VALUE:i32=7;
#[cfg(not(feature="special"))] const VALUE:i32=0;
fn main(){println!("VALUE {}",VALUE);println!("ARGS {:?}",std::env::args().skip(1).collect::<Vec<_>>());println!("ENV {}",std::env::var("RPR_CONFIG_TEST").unwrap());println!("CWD {}",std::env::current_dir().unwrap().display());}
#[test] fn works(){assert_eq!(2+2,4);}
)RS"));
        RustProjectOptions options;options.release=true;options.package="one";options.targetKind="bin";options.targetName="alpha";options.features={"special"};options.noDefaultFeatures=true;options.arguments={"two words",QString::fromUtf8("città")};options.environment.insert("RPR_CONFIG_TEST",QString::fromUtf8("valore 🦀"));options.workingDirectory="cwd";QString error;QVERIFY(options.save(root,error));
        RustWorkspace workspace;QString output;connect(&workspace,&RustWorkspace::output,this,[&](const QString &text){output+=text;});
        QCOMPARE(run(workspace,root,"run"),0);QVERIFY2(output.contains("VALUE 7")&&output.contains("two words")&&output.contains(QString::fromUtf8("ENV valore 🦀")),qPrintable(output));QVERIFY(output.contains("cwd"));QVERIFY(!output.contains("OTHER_BINARY"));QVERIFY(output.contains("--release"));
        output.clear();QCOMPARE(run(workspace,root,"debug-build"),0);QVERIFY(!workspace.artifacts().isEmpty());QVERIFY(QFileInfo::exists(workspace.artifacts().first()));
        options.arguments.clear();QVERIFY(options.save(root,error));output.clear();QCOMPARE(run(workspace,root,"test"),0);QVERIFY(output.contains("test result: ok"));
        QCOMPARE(run(workspace,root,"asm"),0);QVERIFY(!workspace.artifacts().isEmpty());QVERIFY(QFileInfo::exists(workspace.artifacts().first()));QVERIFY(workspace.artifacts().first().endsWith(".s"));
        QCOMPARE(run(workspace,root,"llvm"),0);QVERIFY(QFileInfo::exists(workspace.artifacts().first()));QVERIFY(workspace.artifacts().first().endsWith(".ll"));
        QCOMPARE(run(workspace,root,"doc"),0);QVERIFY(QFileInfo::exists(root+"/target/doc/alpha/index.html"));
        QCOMPARE(run(workspace,root,"debug-test"),0);QVERIFY(!workspace.artifacts().isEmpty());
        QCOMPARE(run(workspace,root,"clean"),0);
    }
    void standaloneArgumentsAndFlags(){
        const auto root=mTemp.filePath("single-config");QDir().mkpath(root);const auto file=root+"/single.rs";
        QVERIFY(put(file,"fn main(){println!(\"ARGS {:?}\",std::env::args().skip(1).collect::<Vec<_>>());println!(\"{}\",std::env::var(\"RPR_CONFIG_TEST\").unwrap());}\n"));
        RustProjectOptions options;options.release=true;options.arguments={"quoted arg",""};options.environment.insert("RPR_CONFIG_TEST","SINGLE_ENV_OK");options.rustFlags={"-C","overflow-checks=on"};QString error;QVERIFY(options.save(root,error));
        RustWorkspace workspace;QString output;connect(&workspace,&RustWorkspace::output,this,[&](const QString &text){output+=text;});QSignalSpy done(&workspace,&RustWorkspace::finished);
        QVERIFY(workspace.startFile(file,"run"));QTRY_COMPARE_WITH_TIMEOUT(done.count(),1,30000);QCOMPARE(done.first()[0].toInt(),0);QVERIFY(output.contains("quoted arg")&&output.contains("SINGLE_ENV_OK"));
    }
};
QTEST_GUILESS_MAIN(RustProjectChecks)
#include "rust-project-checks.moc"
