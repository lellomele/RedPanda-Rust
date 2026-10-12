// Copyright (C) 2026 Raffaele Mele
// SPDX-License-Identifier: GPL-3.0-or-later
#include "rusttools.h"
#include <QSettings>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QReadWriteLock>
#include <QCoreApplication>
namespace {
QString settingsFilename;
QReadWriteLock settingsLock;
QString profile(){QReadLocker locker(&settingsLock);return settingsFilename;}
}
void RustTools::setSettingsFile(const QString &file){QWriteLocker locker(&settingsLock);settingsFilename=file;}
QString RustTools::settingsFile(){return profile();}
QStringList RustTools::names(){return {"cargo","rustc","rustfmt","rust-analyzer","codelldb"};}
QString RustTools::configuredPath(const QString &name){
    const QString file=profile();if(file.isEmpty())return {};
    QSettings settings(file,QSettings::IniFormat);return settings.value("RustTools/"+name).toString();
}
QString RustTools::edition(){
    const QString file=profile();if(file.isEmpty())return "2024";
    QSettings settings(file,QSettings::IniFormat);const QString value=settings.value("RustTools/edition","2024").toString();
    return QStringList{"2015","2018","2021","2024"}.contains(value)?value:QStringLiteral("2024");
}
QString RustTools::detectedPath(const QString &name){
    if(!names().contains(name))return {};
#ifdef Q_OS_WIN
    const QString binary=name+".exe";
#else
    const QString binary=name;
#endif
    const QString variable=name=="cargo"?"CARGO":name=="rustc"?"RUSTC":name=="rustfmt"?"RUSTFMT":name=="codelldb"?"REDPANDA_CODELLDB":"REDPANDA_RUST_ANALYZER";
    const QString environment=qEnvironmentVariable(qPrintable(variable));
    if(!environment.isEmpty()&&QFileInfo(environment).isExecutable())return QFileInfo(environment).absoluteFilePath();
    if(name=="codelldb"){
        const auto bundled=QDir(QCoreApplication::applicationDirPath()).filePath("tools/codelldb/adapter/"+binary);
        if(QFileInfo(bundled).isExecutable())return bundled;
        QDir extensions(QDir::home().filePath(".vscode/extensions"));
        auto installed=extensions.entryList({"vadimcn.vscode-lldb-*"},QDir::Dirs,QDir::Name|QDir::Reversed);
        for(const auto &folder:installed){const auto path=extensions.filePath(folder+"/adapter/"+binary);if(QFileInfo(path).isExecutable())return path;}
    }
    const QString home=qEnvironmentVariable("CARGO_HOME",QDir::home().filePath(".cargo"));
    const QString local=QDir(home).filePath("bin/"+binary);
    if(QFileInfo(local).isExecutable())return local;
    return QStandardPaths::findExecutable(binary);
}
QString RustTools::effectivePath(const QString &name){
    if(!names().contains(name))return {};
    const QString configured=configuredPath(name);
    return configured.isEmpty()?detectedPath(name):configured;
}
bool RustTools::save(const QMap<QString,QString> &paths,const QString &selectedEdition,QString &error){
    if(!QStringList{"2015","2018","2021","2024"}.contains(selectedEdition)){error="Edizione Rust non valida.";return false;}
    for(const QString &name:names()){
        const QString path=paths.value(name).trimmed();
        if(!path.isEmpty()&&(!QFileInfo(path).isFile()||!QFileInfo(path).isExecutable())){
            error=QString("Il percorso di %1 non indica un eseguibile: %2").arg(name,path);return false;
        }
    }
    const QString file=profile();if(file.isEmpty()){error="Profilo delle impostazioni non disponibile.";return false;}
    QSettings settings(file,QSettings::IniFormat);QMap<QString,QVariant> old;
    settings.allKeys();
    if(settings.status()!=QSettings::NoError){error="Il profilo delle impostazioni non è leggibile; nessun valore modificato.";return false;}
    for(const QString &name:names())old.insert(name,settings.value("RustTools/"+name));
    old.insert("edition",settings.value("RustTools/edition"));
    for(const QString &name:names()){
        const QString path=paths.value(name).trimmed();
        if(path.isEmpty())settings.remove("RustTools/"+name);
        else settings.setValue("RustTools/"+name,QFileInfo(path).absoluteFilePath());
    }
    settings.setValue("RustTools/edition",selectedEdition);settings.sync();
    if(settings.status()!=QSettings::NoError){
        // Restore cached values too: a rejected save must not alter the running toolchain.
        for(auto i=old.cbegin();i!=old.cend();++i){
            if(i.value().isValid())settings.setValue("RustTools/"+i.key(),i.value());
            else settings.remove("RustTools/"+i.key());
        }
        error="Impossibile salvare le impostazioni degli strumenti Rust.";return false;
    }
    error.clear();return true;
}
