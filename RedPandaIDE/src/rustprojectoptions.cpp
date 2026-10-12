// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "rustprojectoptions.h"
#include "rusttools.h"
#include <QSettings>
#include <QFileInfo>
#include <QDir>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRegularExpression>
namespace {
QString key(const QString &root){
    auto path=QDir::cleanPath(QFileInfo(root).absoluteFilePath());
#ifdef Q_OS_WIN
    path=path.toCaseFolded();
#endif
    return "RustProjects/"+QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(),QCryptographicHash::Sha256).toHex())+"/options";
}
bool strings(const QJsonValue &value,QStringList &result){
    if(!value.isArray()||value.toArray().size()>256)return false;
    for(const auto &entry:value.toArray()){if(!entry.isString())return false;result.append(entry.toString());}return true;
}
QJsonArray array(const QStringList &values){QJsonArray result;for(const auto &value:values)result.append(value);return result;}
}
QString RustProjectOptions::runDirectory(const QString &root)const{return workingDirectory.isEmpty()?QDir(root).absolutePath():QDir(root).absoluteFilePath(workingDirectory);}
bool RustProjectOptions::validate(const QString &root,QString &error)const{
    auto reject=[&](const QString &text){error=text;return false;};
    if(!QStringList{"","bin","lib","test","example","bench"}.contains(targetKind))return reject("Tipo di target Rust non valido.");
    const QRegularExpression name("^[A-Za-z0-9_][A-Za-z0-9_.-]{0,127}$");
    for(const auto &value:{package,targetName,targetTriple})if(!value.isEmpty()&&!name.match(value).hasMatch())return reject("Nome di package, target o piattaforma non valido.");
    if(!targetKind.isEmpty()&&targetKind!="lib"&&targetName.isEmpty())return reject("Specificare il nome del target selezionato.");
    if(features.size()>256||arguments.size()>256||rustFlags.size()>256||environment.size()>100)return reject("Troppe opzioni Rust.");
    const QRegularExpression feature("^[A-Za-z0-9_][A-Za-z0-9_./+-]{0,127}$");
    for(const auto &value:features)if(!feature.match(value).hasMatch())return reject("Nome di feature non valido.");
    qsizetype length=0;
    for(const auto &list:{arguments,rustFlags})for(const auto &value:list){if(value.contains(QChar(0))||value.size()>8192)return reject("Argomento troppo lungo o contenente un carattere nullo.");length+=value.size()+1;}
    if(length>24000)return reject("Gli argomenti superano il limite di 24.000 caratteri.");
    const QRegularExpression variable("^[A-Za-z_][A-Za-z0-9_]{0,127}$");
    for(auto it=environment.begin();it!=environment.end();++it)if(!variable.match(it.key()).hasMatch()||it.value().size()>8192||it.value().contains(QChar(0)))return reject("Variabile d'ambiente non valida.");
    if(workingDirectory.size()>4096||workingDirectory.contains(QChar(0))||(!root.isEmpty()&&!QFileInfo(runDirectory(root)).isDir()))return reject("La cartella di esecuzione non esiste.");
    error.clear();return true;
}
QJsonObject RustProjectOptions::json()const{
    QJsonObject env;for(auto it=environment.begin();it!=environment.end();++it)env.insert(it.key(),it.value());
    return {{"version",1},{"release",release},{"allFeatures",allFeatures},{"noDefaultFeatures",noDefaultFeatures},
        {"package",package},{"targetKind",targetKind},{"targetName",targetName},{"targetTriple",targetTriple},{"workingDirectory",workingDirectory},
        {"features",array(features)},{"arguments",array(arguments)},{"rustFlags",array(rustFlags)},{"environment",env}};
}
bool RustProjectOptions::decode(const QJsonObject &object,RustProjectOptions &result,QString &error){
    if(object.value("version").toInt()!=1){error="Formato delle opzioni Rust non supportato.";return false;}
    RustProjectOptions candidate;
    for(const auto &field:{"release","allFeatures","noDefaultFeatures"})if(!object.value(field).isBool()){error="Opzione Rust non booleana.";return false;}
    candidate.release=object.value("release").toBool();candidate.allFeatures=object.value("allFeatures").toBool();candidate.noDefaultFeatures=object.value("noDefaultFeatures").toBool();
    for(const auto &field:{"package","targetKind","targetName","targetTriple","workingDirectory"})if(!object.value(field).isString()){error="Opzione Rust non testuale.";return false;}
    candidate.package=object.value("package").toString();candidate.targetKind=object.value("targetKind").toString();candidate.targetName=object.value("targetName").toString();candidate.targetTriple=object.value("targetTriple").toString();candidate.workingDirectory=object.value("workingDirectory").toString();
    if(!strings(object.value("features"),candidate.features)||!strings(object.value("arguments"),candidate.arguments)||!strings(object.value("rustFlags"),candidate.rustFlags)||!object.value("environment").isObject()){
        error="Elenco di opzioni Rust non valido.";return false;
    }
    const auto env=object.value("environment").toObject();if(env.size()>100){error="Troppe variabili d'ambiente.";return false;}
    for(auto it=env.begin();it!=env.end();++it){if(!it.value().isString()){error="Valore d'ambiente non testuale.";return false;}candidate.environment.insert(it.key(),it.value().toString());}
    if(!candidate.validate({},error))return false;result=candidate;return true;
}
RustProjectOptions RustProjectOptions::load(const QString &root,QString *error){
    if(error)error->clear();const auto file=RustTools::settingsFile();if(file.isEmpty())return {};
    QSettings settings(file,QSettings::IniFormat);const auto bytes=settings.value(key(root)).toByteArray();if(bytes.isEmpty())return {};
    if(bytes.size()>128*1024){if(error)*error="Opzioni Rust oltre il limite di 128 KiB.";return {};}
    QJsonParseError parse;const auto data=QJsonDocument::fromJson(bytes,&parse);RustProjectOptions result;QString message;
    if(settings.status()!=QSettings::NoError||parse.error!=QJsonParseError::NoError||!data.isObject()||!decode(data.object(),result,message)){
        if(error)*error=message.isEmpty()?QString("Impossibile leggere le opzioni Rust; usati i valori predefiniti."):message;return {};
    }
    return result;
}
bool RustProjectOptions::save(const QString &root,QString &error)const{
    if(!validate(root,error))return false;const auto file=RustTools::settingsFile();if(file.isEmpty()){error="Profilo delle impostazioni non disponibile.";return false;}
    const auto bytes=QJsonDocument(json()).toJson(QJsonDocument::Compact);if(bytes.size()>128*1024){error="Opzioni oltre il limite di 128 KiB.";return false;}
    QSettings settings(file,QSettings::IniFormat);settings.allKeys();if(settings.status()!=QSettings::NoError){error="Profilo non leggibile.";return false;}
    const auto entry=key(root);const auto previous=settings.value(entry);settings.setValue(entry,bytes);settings.sync();
    if(settings.status()!=QSettings::NoError){if(previous.isValid())settings.setValue(entry,previous);else settings.remove(entry);error="Impossibile salvare le opzioni Rust; i valori precedenti restano attivi.";return false;}
    error.clear();return true;
}
QStringList RustProjectOptions::cargoArguments(const QString &command)const{
    QStringList result;
    const bool build=QStringList{"build","check","run","test","clippy","bench","doc","asm","llvm","debug-test","debug-build"}.contains(command);
    if(!package.isEmpty()&&command!="new"&&command!="fetch")result<<"--package"<<package;
    if(build){
        if(release&&command!="debug-test")result<<"--release";
        if(!targetTriple.isEmpty())result<<"--target"<<targetTriple;
        if(!targetKind.isEmpty()&&command!="debug-test"){
            if(targetKind=="lib")result<<"--lib";
            else result<<"--"+targetKind<<targetName;
        }
        if(allFeatures)result<<"--all-features";else if(!features.isEmpty())result<<"--features"<<features.join(',');
        if(noDefaultFeatures)result<<"--no-default-features";
    }
    if(command=="clean"){if(release)result<<"--release";if(!targetTriple.isEmpty())result<<"--target"<<targetTriple;}
    if(command=="fetch"&&!targetTriple.isEmpty())result<<"--target"<<targetTriple;
    return result;
}
QProcessEnvironment RustProjectOptions::processEnvironment()const{
    auto result=QProcessEnvironment::systemEnvironment();for(auto it=environment.begin();it!=environment.end();++it)result.insert(it.key(),it.value());
    if(!rustFlags.isEmpty())result.insert("CARGO_ENCODED_RUSTFLAGS",rustFlags.join(QChar(0x1f)));return result;
}
