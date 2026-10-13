// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "rustcleanplan.h"
#include <QFileInfo>
#include <QDirIterator>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QDateTime>
#include <QThread>
#include <algorithm>
#ifdef Q_OS_WIN
#include <objbase.h>
#endif
namespace {
#ifdef Q_OS_WIN
constexpr auto sensitivity=Qt::CaseInsensitive;
#else
constexpr auto sensitivity=Qt::CaseSensitive;
#endif
bool inside(const QString &file,const QString &directory){return file.compare(directory,sensitivity)==0||file.startsWith(directory+'/',sensitivity);}
bool link(const QFileInfo &info){
    if(info.isSymLink())return true;
#if QT_VERSION >= QT_VERSION_CHECK(6,2,0)
    if(info.isJunction())return true;
#endif
    return false;
}
}
RustClean::Plan RustClean::inspect(const QString &target,const QString &workspace,const QStringList &protectedFiles){
    Plan plan;const QFileInfo info(target);const auto root=QFileInfo(workspace).canonicalFilePath();plan.target=info.canonicalFilePath();
    const auto fail=[&](const QString &message){plan.error=message;return plan;};
    if(root.isEmpty()||plan.target.isEmpty()||!info.isDir())return fail("La cartella degli artefatti non esiste.");
    if(inside(root,plan.target)||QDir(plan.target).isRoot())return fail("La cartella target comprende il progetto o una radice del filesystem.");
    for(QFileInfo parent=info;;parent=QFileInfo(parent.absolutePath())){
        if(link(parent))return fail("La cartella target attraversa un collegamento o una junction.");
        if(QDir(parent.absoluteFilePath()).isRoot())break;
    }
    for(const auto &path:protectedFiles){const auto file=QFileInfo(path).canonicalFilePath();if(!file.isEmpty()&&inside(file,plan.target))return fail("La cartella contiene un sorgente, un manifest o un documento aperto: "+path);}
    QFile marker(QDir(plan.target).filePath(".rustc_info.json"));
    if(!marker.open(QIODevice::ReadOnly)||marker.size()>65536||!QJsonDocument::fromJson(marker.readAll()).object().contains("rustc_fingerprint"))
        return fail("La cartella non è riconosciuta come output Cargo; nessun file verrà rimosso.");
    marker.close();QStringList records;
    const QRegularExpression generated("^(?:[^/]+/)?(?:debug|release)/build/[^/]+/out/");
    QDirIterator iterator(plan.target,QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);
    while(iterator.hasNext()){
        iterator.next();const auto item=iterator.fileInfo();const auto relative=QDir(plan.target).relativeFilePath(item.absoluteFilePath());
        if(++plan.entries>100000)return fail("Oltre 100.000 elementi: pulizia sospesa per mantenere l'IDE reattiva.");
        if(link(item))return fail("Collegamento o junction nella cartella target: "+relative);
        if(item.fileName()==".git"||item.fileName()=="Cargo.toml"||item.fileName()=="Cargo.lock")return fail("Configurazione o repository nella cartella target: "+relative);
        if(item.isFile()&&item.suffix().compare("rs",Qt::CaseInsensitive)==0&&!generated.match(relative).hasMatch())return fail("Sorgente Rust nella cartella target: "+relative);
        if(item.isFile()){plan.bytes+=item.size();plan.files.append(item.absoluteFilePath());plan.fileState.insert(item.absoluteFilePath(),{item.size(),item.lastModified().toMSecsSinceEpoch()});}
        // NTFS directory timestamps may settle after closed file writes. The
        // complete entry list already detects added/removed directories; only
        // regular file size/mtime belongs in the payload snapshot.
        records.append(relative+'\t'+(item.isDir()?QString("D"):QString("F\t")+QString::number(item.size())+'\t'+QString::number(item.lastModified().toMSecsSinceEpoch())));
        if(plan.preview.size()<2000)plan.preview.append(relative);
    }
    records.sort();QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(plan.target.toUtf8());
    for(const auto &record:records){hash.addData(record.toUtf8());hash.addData("\n");}
    plan.fingerprint=hash.result();plan.valid=true;return plan;
}
bool RustClean::recycle(const Plan &plan,const QString &workspace,const QStringList &protectedFiles,QString &error,const std::shared_ptr<std::atomic_bool> &cancel){
    const auto cancelled=[&]{if(cancel&&cancel->load()){error="Pulizia annullata; i file già spostati restano nel Cestino.";return true;}return false;};
    if(cancelled())return false;
    if(!plan.valid){error="Anteprima non valida.";return false;}
    const auto current=inspect(plan.target,workspace,protectedFiles);
    if(!current.valid||current.fingerprint!=plan.fingerprint){error=current.error.isEmpty()?"Gli artefatti sono cambiati dopo l'anteprima. Ripeti la pulizia.":current.error;return false;}
#ifdef Q_OS_WIN
    // Shell recycling is a COM operation. Qt pool threads have no apartment
    // by default, unlike the GUI thread used by the inherited cleanup action.
    const HRESULT apartment=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(apartment)&&apartment!=RPC_E_CHANGED_MODE){error="Impossibile inizializzare il Cestino di Windows.";return false;}
#endif
    QFile artifacts(plan.target);const bool moved=artifacts.moveToTrash();
    bool complete=moved;
    if(!moved){
        // Directory watches / current-directory handles may deny a folder
        // move while allowing its files to be recycled. Keep empty folders;
        // never fall back to permanent deletion or ignore changed contents.
        const auto remaining=inspect(plan.target,workspace,protectedFiles);
        if(!remaining.valid||remaining.fingerprint!=plan.fingerprint)error="Pulizia interrotta: il contenuto è cambiato. Verifica gli artefatti e il Cestino.";
        else{
            complete=true;
            auto files=plan.files;const auto marker=QDir(plan.target).filePath(".rustc_info.json");
            // Keep the Cargo marker until the last successful move, so a
            // partial failure can be inspected and retried safely.
            std::stable_sort(files.begin(),files.end(),[&](const QString &a,const QString &b){if(a==marker)return false;if(b==marker)return true;return a<b;});
            for(const auto &path:files){
                if(cancelled()){complete=false;break;}
                const auto expected=plan.fileState.value(path);bool recycled=false;QString failure;
                for(int attempt=0;attempt<4&&!recycled;++attempt){
                    if(cancelled()){failure=error;break;}
                    if(attempt)QThread::msleep(100); // worker thread, bounded transient sharing retry
                    const QFileInfo file(path);bool safe=file.canonicalFilePath().compare(path,sensitivity)==0;
                    for(QFileInfo parent(file.absolutePath());safe;parent=QFileInfo(parent.absolutePath())){if(link(parent)){safe=false;break;}if(parent.canonicalFilePath().compare(plan.target,sensitivity)==0)break;if(QDir(parent.absoluteFilePath()).isRoot()){safe=false;break;}}
                    if(!safe||link(file)||!file.isFile()||file.size()!=expected.first||file.lastModified().toMSecsSinceEpoch()!=expected.second){failure="Artefatto modificato dopo l'anteprima: "+path;break;}
                    QFile artifact(path);recycled=artifact.moveToTrash();failure=artifact.errorString();
                }
                if(!recycled){error="Pulizia interrotta; i file già spostati restano nel Cestino. "+path+": "+failure;complete=false;break;}
            }
        }
    }
#ifdef Q_OS_WIN
    if(SUCCEEDED(apartment))CoUninitialize();
#endif
    return complete;
}
