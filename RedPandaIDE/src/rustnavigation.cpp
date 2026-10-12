// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "rustnavigation.h"
#include "rustlspclient.h"
#include "rustworkspace.h"
#include "editor.h"
#include "editormanager.h"
#include <QTreeWidget>
#include <QInputDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QMessageBox>
#include <QRegularExpression>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QStringConverter>
namespace {
QString text(Editor *editor){return editor->text().replace("\r\n","\n").replace('\r','\n');}
QString path(const QString &value){return QDir::cleanPath(QFileInfo(value).absoluteFilePath());}
bool rust(Editor *editor){return editor&&editor->syntaxer()&&editor->syntaxer()->language()==QSynedit::ProgrammingLanguage::Rust;}
}
RustNavigation::RustNavigation(EditorManager *manager,RustLspClient *client,QTreeWidget *outline,QWidget *window,const std::function<Editor *(const QString &)> &open):QObject(window),mManager(manager),mClient(client),mWindow(window),mOutline(outline),mOpen(open){
    mRefresh.setSingleShot(true);mRefresh.setInterval(500);connect(&mRefresh,&QTimer::timeout,this,&RustNavigation::refreshOutline);
    connect(client,&RustLspClient::result,this,&RustNavigation::receive);
    connect(client,&RustLspClient::readyChanged,this,[this](bool ready){if(ready)mRefresh.start();else{mPending.clear();mOutline->clear();}});
    connect(manager,&EditorManager::editorOpenned,this,&RustNavigation::attachEditors);
    connect(outline,&QTreeWidget::itemActivated,this,[this](QTreeWidgetItem *item){
        if(mOutlinePath.isEmpty())return;auto editor=mOpen(mOutlinePath);const auto position=item->data(0,Qt::UserRole).toJsonObject();
        const int line=position.value("line").toInt(-1),character=position.value("character").toInt(-1);
        if(editor&&line>=0&&line<editor->lineCount()&&character>=0&&character<=editor->lineText(line).size())editor->setCaretXYCentered({character,line});
    });attachEditors();
}
void RustNavigation::attachEditors(){
    for(int i=0;i<mManager->pageCount();++i){auto editor=(*mManager)[i];if(editor->property("rustNavigationAttached").toBool())continue;editor->setProperty("rustNavigationAttached",true);
        connect(editor,&Editor::focusInOccured,this,[this]{mOutline->clear();mOutlinePath.clear();mRefresh.start();});
        connect(editor,&QSynedit::QSynEdit::changed,this,[this,editor]{
            for(int id:mPending.keys())if(mPending[id].editor==editor){if(mClient)mClient->cancel(id);mPending.remove(id);}mRefresh.start();
        });
        connect(editor,&Editor::aboutToDestroy,this,[this,editor]{
            for(int id:mPending.keys())if(mPending[id].editor==editor){if(mClient)mClient->cancel(id);mPending.remove(id);}
            if(mOutlinePath==path(editor->filename())){mOutlinePath.clear();mOutline->clear();}mRefresh.start();
        },Qt::DirectConnection);
    }
    mOutline->clear();mOutlinePath.clear();mRefresh.start();
}
Editor *RustNavigation::active()const{auto editor=mManager->getEditor();return rust(editor)&&!editor->isNew()?editor:nullptr;}
bool RustNavigation::synchronize(){
    if(!mClient)return false;mSnapshot.clear();
    for(int i=0;i<mManager->pageCount();++i){auto editor=(*mManager)[i];if(!rust(editor)||editor->isNew())continue;const auto value=text(editor);mSnapshot.insert(path(editor->filename()),value);mClient->updateDocument(editor->filename(),value);}
    return mClient->synchronizeAll()&&mClient->analysisReady();
}
int RustNavigation::request(const QString &method,const QJsonObject &extra,bool edit){
    const auto editor=active();if(!editor||!synchronize()){emit status(tr("Apri un file Rust salvato e attendi l'avvio dell'analisi."));return 0;}
    const auto position=editor->caretXY();const auto id=mClient->requestAt(method,editor->filename(),position.line,position.ch,extra);
    if(id)mPending.insert(id,{method,text(editor),editor->wordAtCursor(),editor,edit?mSnapshot:QHash<QString,QString>{}});return id;
}
void RustNavigation::references(){request("textDocument/references",{{"context",QJsonObject{{"includeDeclaration",true}}}});}
void RustNavigation::rename(){
    const auto editor=active();if(!editor||editor->readOnly())return;bool ok=false;const auto name=QInputDialog::getText(mWindow,tr("Rinomina simbolo Rust"),tr("Nuovo nome:"),QLineEdit::Normal,editor->wordAtCursor(),&ok);
    if(!ok)return;const QRegularExpression identifier("^(?:r#)?[\\p{L}_][\\p{L}\\p{N}_]*$");
    if(!identifier.match(name).hasMatch()){emit status(tr("Il nuovo nome non è un identificatore Rust valido."));return;}request("textDocument/rename",{{"newName",name}},true);
}
int RustNavigation::renameTo(const QString &name){return request("textDocument/rename",{{"newName",name}},true);}
void RustNavigation::macroExpansion(){request("rust-analyzer/expandMacro");}
void RustNavigation::codeActions(){
    const auto editor=active();if(!editor)return;const auto point=editor->caretXY();
    const auto position=QJsonObject{{"line",point.line},{"character",point.ch}};
    request("textDocument/codeAction",{{"range",QJsonObject{{"start",position},{"end",position}}},{"context",QJsonObject{{"diagnostics",QJsonArray{}}}}},true);
}
void RustNavigation::searchSymbols(){
    bool ok=false;const auto query=QInputDialog::getText(mWindow,tr("Simboli Rust"),tr("Nome o parte del nome:"),QLineEdit::Normal,{},&ok);
    if(!ok||query.trimmed().isEmpty()||!synchronize())return;
    const int id=mClient->requestWorkspace("workspace/symbol",{{"query",query.trimmed()}});if(id)mPending.insert(id,{"workspace/symbol",{}, {},nullptr,{}});
}
void RustNavigation::refreshOutline(){
    const auto editor=active();if(!editor){mOutline->clear();mOutlinePath.clear();return;}
    if(!synchronize()){mRefresh.start(500);return;}
    for(int id:mPending.keys())if(mPending[id].method=="textDocument/documentSymbol"){mClient->cancel(id);mPending.remove(id);}
    const auto id=mClient->requestDocument("textDocument/documentSymbol",editor->filename());if(id)mPending.insert(id,{"textDocument/documentSymbol",text(editor),{},editor,{}});
}
void RustNavigation::fillOutline(const QJsonArray &symbols,QTreeWidgetItem *parent,int depth,int &count){
    if(depth>24)return;for(const auto &entry:symbols){if(++count>10000)return;const auto symbol=entry.toObject();
        auto item=parent?new QTreeWidgetItem(parent):new QTreeWidgetItem(mOutline);item->setText(0,symbol.value("name").toString().left(300));
        item->setText(1,symbol.value("detail").toString().left(300));auto range=symbol.value("selectionRange").toObject();if(range.isEmpty())range=symbol.value("location").toObject().value("range").toObject();
        item->setData(0,Qt::UserRole,range.value("start"));fillOutline(symbol.value("children").toArray(),item,depth+1,count);
    }
}
void RustNavigation::receive(int id,const QString &,const QString &,int,const QJsonValue &value){
    if(!mPending.contains(id))return;const auto pending=mPending.take(id);
    if(pending.editor&&(!rust(pending.editor)||text(pending.editor)!=pending.text))return;
    if(pending.method=="textDocument/documentSymbol"){
        if(!pending.editor||pending.editor!=active())return;mOutline->clear();mOutlinePath=path(pending.editor->filename());int count=0;fillOutline(value.toArray(),nullptr,0,count);mOutline->expandToDepth(1);
    }else if(pending.method=="textDocument/references")emit referencesReady(value.toArray());
    else if(pending.method=="rust-analyzer/expandMacro"){
        const auto object=value.toObject();if(!object.value("expansion").toString().isEmpty())emit generatedText(tr("Espansione %1").arg(object.value("name").toString()),object.value("expansion").toString());else emit status(tr("Nessuna macro espandibile alla posizione corrente."));
    }else if(pending.method=="textDocument/rename"){
        if(pending.editor&&value.isObject())applyEdits(value.toObject(),pending.editor,pending.snapshot,true,pending.oldName);
        else emit status(tr("rust-analyzer non ha proposto una rinomina."));
    }else if(pending.method=="workspace/symbol"){
        QJsonArray locations;for(const auto &entry:value.toArray()){const auto symbol=entry.toObject();auto location=symbol.value("location").toObject();location.insert("name",symbol.value("name"));if(location.contains("range"))locations.append(location);}emit referencesReady(locations);
    }else if(pending.method=="textDocument/codeAction"){
        const auto actions=value.toArray();QStringList titles;QVector<QJsonObject> supported;
        for(const auto &entry:actions){const auto action=entry.toObject();if((action.contains("edit")||action.contains("data"))&&!action.contains("disabled")){supported.append(action);titles.append(action.value("title").toString());}}
        if(supported.isEmpty()){emit status(tr("Nessuna azione applicabile alla posizione corrente."));return;}
        bool ok=false;const auto title=QInputDialog::getItem(mWindow,tr("Azioni Rust"),tr("Azione:"),titles,0,false,&ok);if(!ok)return;
        const auto chosen=supported[titles.indexOf(title)];
        if(pending.editor&&chosen.contains("edit"))applyEdits(chosen.value("edit").toObject(),pending.editor,pending.snapshot,true);
        else if(mClient&&pending.editor){const auto id=mClient->requestWorkspace("codeAction/resolve",chosen);if(id){auto next=pending;next.method="codeAction/resolve";mPending.insert(id,next);}}
    }else if(pending.method=="codeAction/resolve"){
        if(pending.editor&&value.toObject().contains("edit"))applyEdits(value.toObject().value("edit").toObject(),pending.editor,pending.snapshot,true);
        else emit status(tr("L'azione richiede un comando del server non supportato."));
    }
}
bool RustNavigation::applyEdits(const QJsonObject &workspaceEdit,Editor *source,const QHash<QString,QString> &snapshot,bool preview,const QString &oldName){
    if(!source||source->isNew()||source->readOnly())return false;
    auto reject=[&](const QString &message){emit status(message);return false;};
    auto root=RustWorkspace::findRoot(source->filename());if(root.isEmpty())root=QFileInfo(source->filename()).absolutePath();root=QFileInfo(root).canonicalFilePath();
    if(root.isEmpty())return reject(tr("Cartella del progetto non disponibile."));
    QHash<QString,QJsonArray> changes;QHash<QString,int> versions;
    auto collect=[&](const QString &uri,const QJsonArray &edits,int version){
        const QUrl url(uri);if(!url.isLocalFile())return false;const auto file=path(url.toLocalFile());const auto canonical=QFileInfo(file).canonicalFilePath();
#ifdef Q_OS_WIN
        const auto sensitivity=Qt::CaseInsensitive;
#else
        const auto sensitivity=Qt::CaseSensitive;
#endif
        if(canonical.isEmpty()||!canonical.startsWith(root+'/',sensitivity))return false;
        if(changes.size()>=200&&!changes.contains(file))return false;for(const auto &edit:edits)changes[file].append(edit);versions[file]=version;return true;
    };
    const auto simple=workspaceEdit.value("changes").toObject();for(auto it=simple.begin();it!=simple.end();++it)if(!it.value().isArray()||!collect(it.key(),it.value().toArray(),-1))return reject(tr("Modifiche fuori dal progetto o non valide; nessun testo modificato."));
    for(const auto &entry:workspaceEdit.value("documentChanges").toArray()){
        const auto edit=entry.toObject();if(edit.contains("kind"))return reject(tr("L'azione richiede operazioni sui file. Usa i comandi di gestione dei file del progetto; nessun testo modificato."));
        const auto document=edit.value("textDocument").toObject();if(!collect(document.value("uri").toString(),edit.value("edits").toArray(),document.value("version").toInt(-1)))return reject(tr("Modifica del documento non valida."));
    }
    if(changes.isEmpty())return reject(tr("Nessuna modifica proposta."));
    struct Candidate{QString file,before,after;QVector<RustTextEdits::Edit> edits;QPointer<Editor> editor;};QVector<Candidate> candidates;int count=0;
    for(auto it=changes.begin();it!=changes.end();++it){
        Candidate candidate;candidate.file=it.key();candidate.editor=mManager->getOpenedEditor(it.key());
        if(candidate.editor){if(candidate.editor->readOnly())return reject(tr("Un documento è in sola lettura."));candidate.before=text(candidate.editor);
            if(snapshot.contains(it.key())&&snapshot[it.key()]!=candidate.before)return reject(tr("Un documento è cambiato durante la richiesta; ripetere l'azione."));
            if(versions[it.key()]>=0&&mClient&&mClient->version(it.key())!=versions[it.key()])return reject(tr("Versione del documento superata."));
        }else{
            QFile file(it.key());if(!file.open(QIODevice::ReadOnly)||file.size()>8*1024*1024)return reject(tr("Documento non leggibile o troppo grande."));
            QStringDecoder decoder(QStringDecoder::Utf8);candidate.before=decoder(file.readAll());if(decoder.hasError())return reject(tr("Il documento non è UTF-8 valido."));candidate.before.replace("\r\n","\n").replace('\r','\n');
        }
        QString error;if(!RustTextEdits::plan(candidate.before,it.value(),candidate.edits,error))return reject(error);
        if(!oldName.isEmpty())for(const auto &edit:candidate.edits){auto original=candidate.before.mid(edit.start,edit.end-edit.start),expected=oldName;if(original.startsWith("r#"))original=original.mid(2);if(expected.startsWith("r#"))expected=expected.mid(2);
            if(original!=expected)return reject(tr("Il testo da rinominare non corrisponde alla proposta; nessun documento modificato."));}
        if(!RustTextEdits::applyChecked(candidate.before,candidate.edits,candidate.after,error))return reject(error);
        count+=candidate.edits.size();candidates.append(candidate);
    }
    if(preview){
        QDialog dialog(mWindow);dialog.setObjectName("rustWorkspaceEditPreview");dialog.setWindowTitle(tr("Anteprima modifiche Rust"));dialog.resize(800,500);auto layout=new QVBoxLayout(&dialog);
        layout->addWidget(new QLabel(tr("%1 modifiche in %2 documenti. Il testo verrà aggiornato negli editor; i file saranno scritti solo con Salva/Salva tutto.").arg(count).arg(candidates.size()),&dialog));
        auto split=new QSplitter(&dialog);auto list=new QTreeWidget(split);list->setHeaderLabels({tr("File"),tr("Modifiche")});auto after=new QPlainTextEdit(split);after->setReadOnly(true);after->setLineWrapMode(QPlainTextEdit::NoWrap);layout->addWidget(split);
        for(int i=0;i<candidates.size();++i){const auto &candidate=candidates[i];auto item=new QTreeWidgetItem(list,{candidate.file,QString::number(candidate.edits.size())});item->setData(0,Qt::UserRole,i);}list->resizeColumnToContents(0);
        connect(list,&QTreeWidget::currentItemChanged,&dialog,[&](QTreeWidgetItem *item){if(item)after->setPlainText(candidates[item->data(0,Qt::UserRole).toInt()].after);});list->setCurrentItem(list->topLevelItem(0));
        auto buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);connect(buttons->button(QDialogButtonBox::Apply),&QPushButton::clicked,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if(dialog.exec()!=QDialog::Accepted)return false;
    }
    // Open and validate every target before changing any buffer. Saving remains
    // explicit and reuses the inherited atomic-save and recovery machinery.
    for(auto &candidate:candidates){if(!candidate.editor)candidate.editor=mOpen(candidate.file);if(!candidate.editor||candidate.editor->readOnly()||text(candidate.editor)!=candidate.before)return reject(tr("Un documento è cambiato o non si apre; nessuna modifica applicata."));}
    for(auto &candidate:candidates){auto editor=candidate.editor.data();editor->beginEditing();const auto cleanup=finally([editor]{editor->endEditing();});
        editor->setCaretAndSelection({0,0},{0,0},editor->fileEnd());editor->setSelText(candidate.after);
    }
    synchronize();emit editsApplied(candidates.size(),count);emit status(tr("Modifiche applicate negli editor. Usa Salva tutto per scriverle su disco."));return true;
}
