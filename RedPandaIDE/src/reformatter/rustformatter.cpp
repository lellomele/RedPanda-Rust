// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "rustformatter.h"
#include "../processjob.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QEventLoop>
#include <QTimer>
#include <QStringConverter>
#include <QProcessEnvironment>
#include "../../../libs/tomlplusplus/toml.hpp"
namespace {
std::optional<toml::table> manifest(const QString &file,QString &error){
    QFile input(file);if(!input.open(QIODevice::ReadOnly)||input.size()>256*1024){error="Manifest Cargo non leggibile o oltre 256 KiB.";return {};}
    const auto bytes=input.readAll();if(input.error()!=QFile::NoError){error=input.errorString();return {};}
    try{return toml::parse(std::string_view(bytes.constData(),size_t(bytes.size())));}
    catch(const toml::parse_error &e){error="Manifest Cargo non valido: "+QString::fromUtf8(e.description().data(),e.description().size());return {};}
}
QString edition(const toml::node_view<const toml::node> &value,QString &error){
    const auto string=value.value<std::string>();const auto result=string?QString::fromStdString(*string):QString{};
    if(!QStringList{"2015","2018","2021","2024"}.contains(result)){error="Edizione Cargo non supportata.";return {};}
    return result;
}
}
RustFormatter::RustFormatter(QString tool,QString file,QString edition,std::function<bool()> current,int timeout):mTool(std::move(tool)),mFile(std::move(file)),mEdition(std::move(edition)),mCurrent(std::move(current)),mTimeout(qBound(50,timeout,10000)){}
QString RustFormatter::editionForFile(const QString &file,const QString &fallback,QString &error){
    error.clear();QDir folder(QFileInfo(file).absolutePath());QString cargo;
    do{const auto path=folder.filePath("Cargo.toml");if(QFileInfo(path).isFile()){cargo=path;break;}}while(folder.cdUp());
    if(cargo.isEmpty())return fallback;
    const auto table=manifest(cargo,error);if(!table)return {};
    const auto package=(*table)["package"];if(!package)return fallback;
    const auto value=package["edition"];if(!value)return "2015";
    if(value.is_string())return edition(value,error);
    if(!value.is_table()||!value["workspace"].value_or(false)){error="Edizione Cargo non valida.";return {};}
    QDir workspace=folder;const auto explicitRoot=package["workspace"].value<std::string>();if(explicitRoot)workspace=QDir(folder.filePath(QString::fromStdString(*explicitRoot)));
    for(;;){const auto path=workspace.filePath("Cargo.toml");
        if(QFileInfo(path).isFile()){const auto parent=manifest(path,error);if(!parent)return {};const auto inherited=(*parent)["workspace"]["package"]["edition"];if(inherited)return edition(inherited,error);}
        if(explicitRoot||!workspace.cdUp())break;
    }
    error="Edizione ereditata dal workspace Cargo non disponibile.";return {};
}
QString RustFormatter::refomat(const QString &content,QString &error,bool &ok){
    ok=false;error.clear();if(content.size()>4*1024*1024){error=tr("Testo oltre 8 MiB: formattazione sospesa.");return {};}
    if(!QFileInfo(mTool).isExecutable()){error=tr("rustfmt non trovato: configura gli strumenti Rust.");return {};}
    const auto language=editionForFile(mFile,mEdition,error);if(!error.isEmpty())return {};
    if(mCurrent&&!mCurrent()){error=tr("Documento cambiato durante la formattazione.");return {};}
    ProcessJob job;QProcess process;if(!job.protect(process)){error=tr("Impossibile proteggere il processo rustfmt.");return {};}
    QEventLoop loop;QTimer deadline;deadline.setSingleShot(true);QByteArray output,diagnostics;bool done=false,failed=false;
    const auto stop=[&](const QString &message){if(error.isEmpty())error=message;failed=true;job.terminate();process.kill();loop.quit();};
    QObject::connect(&deadline,&QTimer::timeout,&loop,[&]{stop(tr("Formattazione Rust scaduta; testo conservato."));});
    QObject::connect(&process,&QProcess::readyReadStandardOutput,&loop,[&]{const auto bytes=process.readAllStandardOutput();if(bytes.size()>8*1024*1024-output.size())stop(tr("Output rustfmt oltre 8 MiB."));else output+=bytes;});
    QObject::connect(&process,&QProcess::readyReadStandardError,&loop,[&]{const auto bytes=process.readAllStandardError();if(bytes.size()>65536-diagnostics.size())stop(tr("Diagnostica rustfmt oltre 64 KiB."));else diagnostics+=bytes;});
    QObject::connect(&process,&QProcess::errorOccurred,&loop,[&](QProcess::ProcessError){stop(process.errorString());});
    QObject::connect(&process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),&loop,[&](int code,QProcess::ExitStatus status){done=true;if(code||status!=QProcess::NormalExit){failed=true;if(error.isEmpty())error=QString::fromUtf8(diagnostics).left(4000);if(error.isEmpty())error=tr("rustfmt non ha completato la formattazione.");}loop.quit();});
    const auto input=content.toUtf8();QObject::connect(&process,&QProcess::started,&loop,[&]{if(process.write(input)!=input.size())stop(tr("Invio del testo a rustfmt incompleto."));process.closeWriteChannel();});
    auto environment=QProcessEnvironment::systemEnvironment();environment.insert("PATH",QFileInfo(mTool).absolutePath()+QDir::listSeparator()+environment.value("PATH"));process.setProcessEnvironment(environment);
    const auto directory=QFileInfo(mFile).absolutePath();QStringList arguments{"--edition",language,"--emit","stdout"};
    QDir config(directory);bool found=false;
    do{for(const auto &name:QStringList{".rustfmt.toml","rustfmt.toml"}){const auto file=config.filePath(name);if(QFileInfo(file).isFile()){arguments<<"--config-path"<<file;found=true;break;}}}while(!found&&config.cdUp());
    process.setWorkingDirectory(directory);deadline.start(mTimeout);process.start(mTool,arguments);
    if(!done&&!failed)loop.exec();deadline.stop();
    if(!done){job.terminate();process.kill();process.waitForFinished(500);if(!failed){failed=true;error=tr("Formattazione Rust interrotta; testo conservato.");}}
    if(failed)return {};if(mCurrent&&!mCurrent()){error=tr("Documento cambiato durante la formattazione; risultato ignorato.");return {};}
    QStringDecoder decode(QStringDecoder::Utf8);const QString result=decode(output);if(decode.hasError()){error=tr("Output rustfmt non UTF-8 valido.");return {};}
    if(result.isEmpty()&&!content.trimmed().isEmpty()){error=tr("rustfmt ha restituito un testo vuoto; documento conservato.");return {};}
    ok=true;return result;
}
