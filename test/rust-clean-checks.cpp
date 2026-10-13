// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QtConcurrent>
#include <QThread>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include "rustcleanplan.h"
class RustCleanChecks final:public QObject {
    Q_OBJECT
    QTemporaryDir mTemp;QString root,target;
    static bool put(const QString &path,const QByteArray &data){QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(data)==data.size();}
private slots:
    void init(){QVERIFY(mTemp.isValid());root=mTemp.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));target=root+"/target";QVERIFY(QDir().mkpath(target+"/debug"));QVERIFY(put(root+"/Cargo.toml","[package]"));QVERIFY(put(target+"/.rustc_info.json","{\"rustc_fingerprint\":123}"));QVERIFY(put(target+"/debug/app.exe","owned test artifact"));}
    void authenticArtifacts(){auto plan=RustClean::inspect(target,root,{root+"/Cargo.toml"});QVERIFY2(plan.valid,qPrintable(plan.error));QVERIFY(plan.entries>=3);QVERIFY(plan.bytes>0);QVERIFY(!plan.fingerprint.isEmpty());}
    void rejectRootAndAncestors(){QVERIFY(!RustClean::inspect(root,root,{}).valid);QVERIFY(!RustClean::inspect(mTemp.path(),root,{}).valid);}
    void rejectUnmarked(){QVERIFY(QFile::remove(target+"/.rustc_info.json"));QVERIFY(!RustClean::inspect(target,root,{}).valid);}
    void protectSourcesAndManifest(){QVERIFY(put(target+"/main.rs","fn main(){}"));QVERIFY(!RustClean::inspect(target,root,{}).valid);QVERIFY(QFile::remove(target+"/main.rs"));QVERIFY(put(target+"/Cargo.toml","[package]"));QVERIFY(!RustClean::inspect(target,root,{}).valid);}
    void protectOpenDocuments(){QVERIFY(!RustClean::inspect(target,root,{target+"/debug/app.exe"}).valid);}
    void generatedSource(){QVERIFY(QDir().mkpath(target+"/debug/build/example/out"));QVERIFY(put(target+"/debug/build/example/out/bindings.rs","// generated"));QVERIFY(RustClean::inspect(target,root,{}).valid);}
    void changedAfterPreview(){const auto plan=RustClean::inspect(target,root,{});QVERIFY(plan.valid);QVERIFY(put(target+"/debug/new.exe","new artifact"));QString error;QVERIFY(!RustClean::recycle(plan,root,{},error));QVERIFY(QFileInfo::exists(target+"/debug/app.exe"));}
    void cancelledBeforeMovePreservesAllFiles(){const auto plan=RustClean::inspect(target,root,{});QVERIFY(plan.valid);const auto cancel=std::make_shared<std::atomic_bool>(true);QString error;QVERIFY(!RustClean::recycle(plan,root,{},error,cancel));QVERIFY(error.contains("annullata"));QVERIFY(QFileInfo::exists(target+"/debug/app.exe"));QVERIFY(QFileInfo::exists(target+"/.rustc_info.json"));}
    void recycleOwnedTarget(){
#ifdef Q_OS_WIN
        const auto plan=RustClean::inspect(target,root,{});QVERIFY(plan.valid);QString error;QVERIFY2(RustClean::recycle(plan,root,{},error),qPrintable(error));QVERIFY(!QFileInfo::exists(target));QVERIFY(QFileInfo::exists(root+"/Cargo.toml"));
#else
        QSKIP("Native Windows recycle bin integration");
#endif
    }
    void recycleInBackground(){
#ifdef Q_OS_WIN
        const auto plan=RustClean::inspect(target,root,{});QVERIFY(plan.valid);const auto workspace=root;
        auto future=QtConcurrent::run([plan,workspace]{QString error;RustClean::recycle(plan,workspace,{},error);return error;});future.waitForFinished();
        QVERIFY2(future.result().isEmpty(),qPrintable(future.result()));QVERIFY(!QFileInfo::exists(target));QVERIFY(QFileInfo::exists(root+"/Cargo.toml"));
#else
        QSKIP("Native Windows recycle bin integration");
#endif
    }
    void directoryHandleKeepsOnlyEmptyFolders(){
#ifdef Q_OS_WIN
        const auto plan=RustClean::inspect(target,root,{});QVERIFY(plan.valid);
        HANDLE held=CreateFileW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(target).utf16()),FILE_LIST_DIRECTORY,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);QVERIFY(held!=INVALID_HANDLE_VALUE);
        QString error;const bool recycled=RustClean::recycle(plan,root,{},error);CloseHandle(held);
        QVERIFY2(recycled,qPrintable(error));QVERIFY(!QFileInfo::exists(target+"/debug/app.exe"));QVERIFY(!QFileInfo::exists(target+"/.rustc_info.json"));QVERIFY(QFileInfo::exists(root+"/Cargo.toml"));
#else
        QSKIP("Native Windows directory handle ownership");
#endif
    }
    void temporaryFileLockAndPartialFailurePreserveRetry(){
#ifdef Q_OS_WIN
        const auto plan=RustClean::inspect(target,root,{});QVERIFY(plan.valid);const auto app=QDir::toNativeSeparators(target+"/debug/app.exe");
        HANDLE held=CreateFileW(reinterpret_cast<const wchar_t *>(app.utf16()),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);QVERIFY(held!=INVALID_HANDLE_VALUE);
        QString error;const bool moved=RustClean::recycle(plan,root,{},error);CloseHandle(held);QVERIFY(!moved);QVERIFY(!error.isEmpty());
        QVERIFY(QFileInfo::exists(target+"/.rustc_info.json"));const auto retry=RustClean::inspect(target,root,{});QVERIFY2(retry.valid,qPrintable(retry.error));
        QVERIFY2(RustClean::recycle(retry,root,{},error),qPrintable(error));QVERIFY(QFileInfo::exists(root+"/Cargo.toml"));
#else
        QSKIP("Native Windows sharing and recycle integration");
#endif
    }
};
QTEST_GUILESS_MAIN(RustCleanChecks)
#include "rust-clean-checks.moc"
