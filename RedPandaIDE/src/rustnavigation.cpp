// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "rustnavigation.h"
#include "rustlspclient.h"
#include "rustworkspace.h"
#include "editor.h"
#include "editormanager.h"
#include <QTreeWidget>
#include <QTreeView>
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
#include <QComboBox>
#include <cmath>
namespace {
QString text(Editor *editor){return editor->text().replace("\r\n","\n").replace('\r','\n');}
QString path(const QString &value){return QDir::cleanPath(QFileInfo(value).absoluteFilePath());}
QString prompt(QWidget *parent,const QString &title,const QString &label,const QString &initial,bool &ok,const QStringList &items={}){
    auto dialog=new QInputDialog(parent);const QPointer<QInputDialog> guard(dialog);
    dialog->setWindowTitle(title);dialog->setLabelText(label);
    if(!items.isEmpty()){dialog->setComboBoxItems(items);dialog->setComboBoxEditable(false);}
    dialog->setTextValue(initial);const int result=dialog->exec();ok=guard&&result==QDialog::Accepted;
    const auto value=ok?guard->textValue():QString{};if(guard)guard->deleteLater();return value;
}
bool rust(Editor *editor){return editor&&!editor->removing()&&editor->syntaxer()&&editor->syntaxer()->language()==QSynedit::ProgrammingLanguage::Rust;}
}
RustNavigation::RustNavigation(EditorManager *manager,RustLspClient *client,QTreeView *outline,QWidget *window,const std::function<Editor *(const QString &)> &open):QObject(window),mManager(manager),mClient(client),mWindow(window),mOutline(outline),mOutlineModel(new RustOutlineModel(outline)),mOpen(open){
    outline->setModel(mOutlineModel);outline->setUniformRowHeights(true);
    connect(mOutlineModel,&RustOutlineModel::rejected,this,&RustNavigation::status);
    mRefresh.setSingleShot(true);mRefresh.setInterval(500);connect(&mRefresh,&QTimer::timeout,this,&RustNavigation::refreshOutline);
    connect(client,&RustLspClient::result,this,&RustNavigation::receive);
    connect(client,&RustLspClient::readyChanged,this,[this](bool ready){if(ready)mRefresh.start();else{mPending.clear();if(mOutlineModel)mOutlineModel->clear();}});
    connect(manager,&EditorManager::editorOpenned,this,&RustNavigation::attachEditors);
    connect(outline,&QTreeView::activated,this,&RustNavigation::activateOutline);attachEditors();
}
void RustNavigation::activateOutline(const QModelIndex &index){
    if(!mOutlineModel)return;const auto target=mOutlineModel->location(index);if(!target.valid)return;
    const QPointer<RustNavigation> self(this);const QPointer<Editor> editor=mOpen(target.file);
    if(!self||!editor)return;
    if(text(editor)!=target.source){mRefresh.start();emit status(tr("La struttura è superata: aggiornamento in corso."));return;}
    if(target.line>=0&&target.line<editor->lineCount()&&target.character>=0&&target.character<=editor->lineText(target.line).size())editor->setCaretXYCentered({target.character,target.line});
}
void RustNavigation::setOutlineOptions(bool name,bool type,bool traits){if(mOutlineModel)mOutlineModel->setOptions(name,type,traits);}
void RustNavigation::setOutlineStyler(RustOutlineModel::Styler styler){
    const QPointer<RustNavigation> self(this);if(mOutlineModel)mOutlineModel->setStyler(std::move(styler));if(self&&mOutline)mOutline->viewport()->update();
}
void RustNavigation::attachEditors(){
    if(!mManager)return;
    for(int i=0;i<mManager->pageCount();++i){auto editor=(*mManager)[i];if(!editor||editor->removing()||editor->property("rustNavigationAttached").toBool())continue;editor->setProperty("rustNavigationAttached",true);
        connect(editor,&Editor::focusInOccured,this,[this,editor]{
            const QPointer<RustNavigation> self(this);
            if(mOutlineModel&&mOutlineModel->file()!=path(editor->filename()))mOutlineModel->clear();
            if(self)mRefresh.start();
        });
        connect(editor,&QSynedit::QSynEdit::changed,this,[this,editor]{
            if(cancelRequests(editor))mRefresh.start();
        });
        connect(editor,&Editor::aboutToDestroy,this,[this,editor]{
            const auto closedPath=path(editor->filename());if(!cancelRequests(editor))return;
            const QPointer<RustNavigation> self(this);
            if(mOutlineModel&&mOutlineModel->file()==closedPath)mOutlineModel->clear();if(self)mRefresh.start();
        },Qt::DirectConnection);
    }
    const QPointer<RustNavigation> self(this);if(mOutlineModel)mOutlineModel->clear();if(self)mRefresh.start();
}
Editor *RustNavigation::active()const{auto editor=mManager?mManager->getEditor():nullptr;return rust(editor)&&!editor->isNew()?editor:nullptr;}
bool RustNavigation::cancelRequests(Editor *editor){
    const QPointer<RustNavigation> self(this);const auto ids=mPending.keys();
    for(int id:ids)if(mPending.value(id).editor==editor){mPending.remove(id);if(mClient)mClient->cancel(id);if(!self)return false;}return true;
}
bool RustNavigation::synchronize(){
    if(!mClient||!mManager)return false;const QPointer<RustNavigation> self(this);mSnapshot.clear();
    QList<QPointer<Editor>> editors;for(int i=0;i<mManager->pageCount();++i)editors.append((*mManager)[i]);
    for(const auto &editor:editors){if(!rust(editor)||editor->isNew())continue;const auto value=text(editor);const auto file=editor->filename();mSnapshot.insert(path(file),value);
        const int version=mClient->updateDocument(file,value);if(!self||!mClient||!mManager||!version||!rust(editor)||text(editor)!=value)return false;
    }
    const bool ready=mClient->synchronizeAll();return self&&mClient&&ready&&mClient->analysisReady();
}
int RustNavigation::request(const QString &method,const QJsonObject &extra,bool edit){
    const QPointer<RustNavigation> self(this);const QPointer<Editor> editor(active());const auto before=editor?text(editor):QString{};
    const bool ready=editor&&synchronize();if(!self)return 0;
    if(!ready||!mClient||!rust(editor)||editor!=active()||text(editor)!=before){emit status(tr("Apri un file Rust salvato e attendi l'avvio dell'analisi."));return 0;}
    const auto position=editor->caretXY();const auto word=editor->wordAtCursor();const auto file=editor->filename();
    const auto id=mClient->requestAt(method,file,position.line,position.ch,extra);
    if(!self||!rust(editor)||!mClient)return 0;
    if(id)mPending.insert(id,{method,before,word,editor,edit?mSnapshot:QHash<QString,QString>{}});return id;
}
void RustNavigation::references(){request("textDocument/references",{{"context",QJsonObject{{"includeDeclaration",true}}}});}
void RustNavigation::rename(){
    const QPointer<RustNavigation> self(this);const QPointer<Editor> editor(active());if(!editor||editor->readOnly())return;
    const auto before=text(editor);const auto caret=editor->caretXY();bool ok=false;
    const auto name=prompt(mWindow,tr("Rinomina simbolo Rust"),tr("Nuovo nome:"),editor->wordAtCursor(),ok);
    if(!self||!ok)return;
    if(!editor||editor!=active()||editor->readOnly()||text(editor)!=before||editor->caretXY()!=caret){emit status(tr("Il documento o la posizione sono cambiati: ripetere la rinomina."));return;}const QRegularExpression identifier("^(?:r#)?[\\p{L}_][\\p{L}\\p{N}_]*$");
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
    const QPointer<RustNavigation> self(this);bool ok=false;const auto query=prompt(mWindow,tr("Simboli Rust"),tr("Nome o parte del nome:"),{},ok);
    if(!self||!ok||query.trimmed().isEmpty()||!synchronize()||!self)return;
    const int id=mClient->requestWorkspace("workspace/symbol",{{"query",query.trimmed()}});if(id)mPending.insert(id,{"workspace/symbol",{}, {},nullptr,{}});
}
void RustNavigation::refreshOutline(){
    const QPointer<RustNavigation> self(this);const QPointer<Editor> editor(active());if(!editor){if(mOutlineModel)mOutlineModel->clear();return;}
    const bool synchronized=synchronize();if(!self)return;if(!synchronized){mRefresh.start(500);return;}
    if(!rust(editor)||editor!=active()||!mClient)return;
    for(int id:mPending.keys())if(mPending.value(id).method=="textDocument/documentSymbol"){mPending.remove(id);mClient->cancel(id);if(!self||!mClient||!rust(editor))return;}
    const auto source=text(editor),file=editor->filename();const auto id=mClient->requestDocument("textDocument/documentSymbol",file);if(!self||!rust(editor))return;
    if(id)mPending.insert(id,{"textDocument/documentSymbol",source,{},editor,{}});
}
void RustNavigation::receive(int id,const QString &,const QString &,int,const QJsonValue &value){
    if(!mPending.contains(id))return;const auto pending=mPending.take(id);
    if(pending.editor&&(!rust(pending.editor)||text(pending.editor)!=pending.text))return;
    if(pending.method=="textDocument/documentSymbol"){
        if(!pending.editor||pending.editor!=active()||!mOutlineModel)return;
        const QPointer<RustNavigation> self(this);QString error;
        if(!value.isArray()){emit status(tr("Risposta Struttura Rust non valida."));return;}
        const bool ok=mOutlineModel->setSymbols(value.toArray(),path(pending.editor->filename()),pending.text,error);
        if(!self)return;if(!ok){emit status(error);return;}if(mOutline)mOutline->expandToDepth(1);
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
        const QPointer<RustNavigation> self(this);bool ok=false;const auto title=prompt(mWindow,tr("Azioni Rust"),tr("Azione:"),titles.first(),ok,titles);
        if(!self||!ok||!pending.editor)return;
        if(!rust(pending.editor)||pending.editor->readOnly()||text(pending.editor)!=pending.text){emit status(tr("Il documento è cambiato durante la scelta dell'azione: ripetere la richiesta."));return;}
        const int chosenIndex=titles.indexOf(title);if(chosenIndex<0||chosenIndex>=supported.size())return;
        const auto chosen=supported[chosenIndex];
        if(pending.editor&&chosen.contains("edit"))applyEdits(chosen.value("edit").toObject(),pending.editor,pending.snapshot,true);
        else if(mClient&&pending.editor){const auto id=mClient->requestWorkspace("codeAction/resolve",chosen);if(id){auto next=pending;next.method="codeAction/resolve";mPending.insert(id,next);}}
    }else if(pending.method=="codeAction/resolve"){
        if(pending.editor&&value.toObject().contains("edit"))applyEdits(value.toObject().value("edit").toObject(),pending.editor,pending.snapshot,true);
        else emit status(tr("L'azione richiede un comando del server non supportato."));
    }
}
bool RustNavigation::applyEdits(const QJsonObject &workspaceEdit,Editor *source,const QHash<QString,QString> &snapshot,bool preview,const QString &oldName){
    if(mApplyingEdits||!mManager||!source||source->isNew()||source->readOnly())return false;
    const QPointer<RustNavigation> self(this);const QPointer<Editor> sourceGuard(source);
    mApplyingEdits=true;const auto release=finally([self]{if(self)self->mApplyingEdits=false;});
    auto reject=[&](const QString &message){emit status(message);return false;};
    auto root=RustWorkspace::findRoot(source->filename());if(root.isEmpty())root=QFileInfo(source->filename()).absolutePath();root=QFileInfo(root).canonicalFilePath();
    if(root.isEmpty())return reject(tr("Cartella del progetto non disponibile."));
    if((workspaceEdit.contains("changes")&&!workspaceEdit.value("changes").isObject())
        ||(workspaceEdit.contains("documentChanges")&&!workspaceEdit.value("documentChanges").isArray()))return reject(tr("Formato delle modifiche non valido."));
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
        if(!entry.isObject())return reject(tr("Modifica del documento non valida."));
        const auto edit=entry.toObject();if(edit.contains("kind"))return reject(tr("L'azione richiede operazioni sui file. Usa i comandi di gestione dei file del progetto; nessun testo modificato."));
        const auto document=edit.value("textDocument").toObject();const auto version=document.value("version");
        if(!edit.value("edits").isArray()||!edit.value("textDocument").isObject()
            ||(!version.isNull()&&!version.isUndefined()&&(!version.isDouble()||version.toDouble()<0||version.toDouble()>INT_MAX||std::floor(version.toDouble())!=version.toDouble())))return reject(tr("Modifica del documento non valida."));
        if(!collect(document.value("uri").toString(),edit.value("edits").toArray(),version.toInt(-1)))return reject(tr("Modifica del documento non valida."));
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
        auto dialog=new QDialog(mWindow);const QPointer<QDialog> dialogGuard(dialog);dialog->setObjectName("rustWorkspaceEditPreview");dialog->setWindowTitle(tr("Anteprima modifiche Rust"));dialog->resize(800,500);auto layout=new QVBoxLayout(dialog);
        layout->addWidget(new QLabel(tr("%1 modifiche in %2 documenti. Il testo verrà aggiornato negli editor; i file saranno scritti solo con Salva/Salva tutto.").arg(count).arg(candidates.size()),dialog));
        auto split=new QSplitter(dialog);auto list=new QTreeWidget(split);list->setHeaderLabels({tr("File"),tr("Modifiche")});auto after=new QPlainTextEdit(split);after->setReadOnly(true);after->setLineWrapMode(QPlainTextEdit::NoWrap);layout->addWidget(split);
        for(int i=0;i<candidates.size();++i){const auto &candidate=candidates[i];auto item=new QTreeWidgetItem(list,{candidate.file,QString::number(candidate.edits.size())});item->setData(0,Qt::UserRole,i);}list->resizeColumnToContents(0);
        connect(list,&QTreeWidget::currentItemChanged,dialog,[candidates,after](QTreeWidgetItem *item){if(!item)return;const int row=item->data(0,Qt::UserRole).toInt();if(row>=0&&row<candidates.size())after->setPlainText(candidates[row].after);});list->setCurrentItem(list->topLevelItem(0));
        auto buttons=new QDialogButtonBox(QDialogButtonBox::Apply|QDialogButtonBox::Cancel,dialog);layout->addWidget(buttons);connect(buttons->button(QDialogButtonBox::Apply),&QPushButton::clicked,dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::reject);
        const int result=dialog->exec();if(dialogGuard)dialogGuard->deleteLater();
        if(!self||!sourceGuard||!dialogGuard||result!=QDialog::Accepted)return false;
    }
    // Open and validate every target before changing any buffer. Saving remains
    // explicit and reuses the inherited atomic-save and recovery machinery.
    for(auto &candidate:candidates){
        if(!candidate.editor)candidate.editor=mOpen(candidate.file);
        if(!self||!sourceGuard||!mManager)return false;
        if(!candidate.editor||candidate.editor->removing()||candidate.editor->readOnly()||mManager->getOpenedEditor(candidate.file)!=candidate.editor||path(candidate.editor->filename())!=candidate.file||text(candidate.editor)!=candidate.before)return reject(tr("Un documento è cambiato o non si apre; nessuna modifica applicata."));
    }
    // Opening the last file can alter an earlier target. Revalidate the entire
    // set after all open/focus callbacks, before starting any editing block.
    for(const auto &candidate:candidates)if(!candidate.editor||candidate.editor->removing()||candidate.editor->readOnly()||mManager->getOpenedEditor(candidate.file)!=candidate.editor||path(candidate.editor->filename())!=candidate.file||text(candidate.editor)!=candidate.before)return reject(tr("Un documento è cambiato durante l'apertura; nessuna modifica applicata."));
    QVector<QPointer<Editor>> editing;editing.reserve(candidates.size());
    const auto cleanup=finally([&editing]{for(const auto &editor:editing)if(editor)editor->endEditing();});
    for(const auto &candidate:candidates){const auto editor=candidate.editor;if(!editor||editor->removing())return false;editor->beginEditing();editing.append(editor);}
    for(auto &candidate:candidates){const auto editor=candidate.editor;
        if(!self||!editor||editor->removing()||editor->readOnly()||text(editor)!=candidate.before)return self?reject(tr("Un documento è cambiato durante l'applicazione. Controlla i buffer e usa Annulla per le modifiche già applicate.")):false;
        editor->setCaretAndSelection({0,0},{0,0},editor->fileEnd());
        if(!self||!editor)return false;editor->setSelText(candidate.after);
    }
    // Close every block before publishing the result; changed notifications
    // then observe the complete set rather than the first changed document.
    while(!editing.isEmpty()){const auto editor=editing.takeLast();if(editor)editor->endEditing();if(!self)return false;}
    synchronize();if(!self)return false;emit editsApplied(candidates.size(),count);if(!self)return true;
    emit status(tr("Modifiche applicate negli editor. Usa Salva tutto per scriverle su disco."));return true;
}
