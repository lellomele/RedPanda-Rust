// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "rusteditorsupport.h"
#include "rustsnippet.h"
#include "editor.h"
#include "settings.h"
#include "settings/codecompletionsettings.h"
#include "settings/editorsettings.h"
#include <QListWidget>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QScreen>
#include <QGuiApplication>
#include <QUrl>
#include <QJsonValue>
#include <QFileInfo>
#include <algorithm>

namespace {
QString normalizedText(Editor *editor){return editor->text().replace("\r\n","\n").replace('\r','\n');}
QSynedit::CharPos position(const QJsonObject &value){return {value.value("character").toInt(-1),value.value("line").toInt(-1)};}
bool validPosition(Editor *editor,const QSynedit::CharPos &pos){
    if(pos.line<0||pos.line>=editor->lineCount()||pos.ch<0)return false;
    const auto text=editor->lineText(pos.line);if(pos.ch>text.size())return false;
    return !(pos.ch>0&&pos.ch<text.size()&&text[pos.ch-1].isHighSurrogate()&&text[pos.ch].isLowSurrogate());
}
bool wordCharacter(QChar value){return value.isLetterOrNumber()||value=='_';}
QString contentText(const QJsonValue &value){
    if(value.isString())return value.toString();
    if(value.isArray()){
        QStringList lines;for(const auto &entry:value.toArray())lines.append(contentText(entry));return lines.join('\n');
    }
    if(value.isObject())return value.toObject().value("value").toString();
    return {};
}
}
RustEditorSupport::RustEditorSupport(Editor *editor,RustLspClient *client):QObject(editor),mEditor(editor),mClient(client){
    setObjectName("rustEditorSupport");
    mPopup=new QListWidget(editor);mPopup->setObjectName("rustSemanticCompletion");
    mPopup->setWindowFlags(Qt::ToolTip|Qt::WindowDoesNotAcceptFocus);mPopup->setFocusPolicy(Qt::NoFocus);
    mPopup->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);mPopup->hide();
    mSync.setSingleShot(true);mSync.setInterval(180);
    mComplete.setSingleShot(true);mComplete.setInterval(250);
    mHover.setSingleShot(true);mHover.setInterval(500);
    mSignature.setSingleShot(true);mSignature.setInterval(200);
    editor->installEventFilter(this);editor->viewport()->installEventFilter(this);
    connect(&mSync,&QTimer::timeout,this,[this]{synchronize();});
    connect(&mComplete,&QTimer::timeout,this,[this]{complete(false);});
    connect(&mHover,&QTimer::timeout,this,&RustEditorSupport::hover);
    connect(&mSignature,&QTimer::timeout,this,&RustEditorSupport::signatureHelp);
    connect(mPopup,&QListWidget::itemClicked,this,[this]{acceptCompletion();});
    connect(editor,&QSynedit::QSynEdit::changed,this,[this]{
        ++mRevision;dismiss();QToolTip::hideText();mHover.stop();
        if(mClient){mClient->cancel(mHoverId);mClient->cancel(mDefinitionId);}
        if(mClient)mClient->cancel(mSignatureId);mSignatureId=0;mSignature.stop();
        mHoverId=mDefinitionId=0;
        if(isRust()){
            mEditor->clearSyntaxIssues();mEditor->invalidate();mSync.start();
            if(!mApplying&&mEditor->hasFocus())mComplete.start();
            const auto prefix=mEditor->lineText().left(mEditor->caretX());if(prefix.endsWith('(')||prefix.endsWith(','))mSignature.start();
        }else if(mClient&&!mPath.isEmpty()){mClient->closeDocument(mPath);mPath.clear();}
    });
    connect(editor,&Editor::rustCompletionRequested,this,[this]{complete();});
    connect(editor,&Editor::fileSaved,this,[this]{if(synchronize()&&mClient)mClient->savedDocument(mPath);});
    connect(editor,&Editor::fileRenamed,this,[this]{synchronize();});
    connect(editor,&Editor::fileSaveAsed,this,[this]{synchronize();});
    connect(editor,&Editor::closeOccured,this,[this]{detach();});
    connect(editor,&Editor::aboutToDestroy,this,[this]{detach();},Qt::DirectConnection);
    connect(client,&RustLspClient::result,this,&RustEditorSupport::receive);
    connect(client,&RustLspClient::diagnostics,this,&RustEditorSupport::applyDiagnostics);
    connect(client,&RustLspClient::readyChanged,this,[this](bool ready){
        if(!ready){dismiss();QToolTip::hideText();mHoverId=mDefinitionId=0;}
        else if(mExplicitPending&&mEditor->hasFocus()){mExplicitPending=false;complete();}
    });
    mSync.start(0);
}
RustEditorSupport::~RustEditorSupport(){detach();}
void RustEditorSupport::detach(){
    if(!mEditor)return;
    mEditor->removeEventFilter(this);mEditor->viewport()->removeEventFilter(this);
    disconnect(mEditor,nullptr,this,nullptr);
    if(mClient)disconnect(mClient,nullptr,this,nullptr);
    mSync.stop();mComplete.stop();mHover.stop();mSignature.stop();dismiss();QToolTip::hideText();
    if(mClient&&!mPath.isEmpty())mClient->closeDocument(mPath);
    mPath.clear();mEditor=nullptr;
}
bool RustEditorSupport::isRust()const{return mEditor&&mEditor->syntaxer()&&mEditor->syntaxer()->language()==QSynedit::ProgrammingLanguage::Rust;}
bool RustEditorSupport::synchronize(){
    mSync.stop();
    if(!mClient||!mEditor)return false;
    if(!isRust()||mEditor->isNew()){
        if(!mPath.isEmpty())mClient->closeDocument(mPath);mPath.clear();mVersion=0;return false;
    }
    const auto path=QFileInfo(mEditor->filename()).absoluteFilePath();
    if(path!=mPath){if(!mPath.isEmpty())mClient->closeDocument(mPath);mPath=path;mVersion=0;}
    if(mVersion==0||mSyncedRevision!=mRevision){
        mVersion=mClient->updateDocument(mPath,normalizedText(mEditor));mSyncedRevision=mRevision;
    }
    return mVersion>0;
}
void RustEditorSupport::dismiss(){
    mPopup->hide();mItems={};
    if(mClient){mClient->cancel(mCompletionId);mClient->cancel(mResolveId);}
    mCompletionId=mResolveId=0;mSelected={};
}
void RustEditorSupport::complete(bool explicitRequest){
    mComplete.stop();
    if(!isRust()||mEditor->readOnly()||mEditor->selAvail()||!mEditor->hasFocus()||!mClient)return;
    if(!explicitRequest&&(!pSettings->codeCompletion().enabled()||!pSettings->codeCompletion().showCompletionWhileInput()))return;
    const auto caret=mEditor->caretXY();const auto prefix=mEditor->lineText().left(caret.ch);
    if(!explicitRequest){
        int count=0;for(int i=prefix.size()-1;i>=0&&wordCharacter(prefix[i]);--i)++count;
        if(!prefix.endsWith('.')&&!prefix.endsWith("::")&&count<std::max(1,pSettings->codeCompletion().minCharRequired()))return;
        QSynedit::PTokenAttribute attr;QString token;
        if(caret.ch>0&&mEditor->getTokenAttriAtRowCol({caret.ch-1,caret.line},token,attr)&&attr&&
            (attr->tokenType()==QSynedit::TokenType::Comment||attr->tokenType()==QSynedit::TokenType::String))return;
    }
    dismiss();if(!synchronize())return;
    if(!mClient->ready()){mExplicitPending=explicitRequest;return;}
    mExplicitPending=false;mCompletionRevision=mRevision;mCompletionLine=caret.line;mCompletionCharacter=caret.ch;
    mCompletionId=mClient->requestAt("textDocument/completion",mPath,caret.line,caret.ch);
}
void RustEditorSupport::hover(){
    if(!isRust()||!pSettings->editor().enableTooltips()||!mClient||!mEditor->isVisible()||mPopup->isVisible()||!synchronize()||!mClient->ready())return;
    QSynedit::CharPos pos;
    if(!mEditor->pointToCharLine(mHoverPosition,pos)||!validPosition(mEditor,pos))return;
    if(const auto issue=mEditor->getSyntaxIssueAtPosition(pos)){
        QToolTip::showText(mEditor->mapToGlobal(mHoverPosition),"<pre>"+issue->hint.toHtmlEscaped()+"</pre>",mEditor,{},15000);return;
    }
    mClient->cancel(mHoverId);mHoverRevision=mRevision;mRequestedHover=mHoverPosition;
    mHoverId=mClient->requestAt("textDocument/hover",mPath,pos.line,pos.ch);
}
bool RustEditorSupport::eventFilter(QObject *watched,QEvent *event){
    if(!isRust())return false;
    if(event->type()==QEvent::ShortcutOverride){
        auto key=static_cast<QKeyEvent *>(event);
        if((key->key()==Qt::Key_Space&&key->modifiers()==Qt::ControlModifier)||key->key()==Qt::Key_F12||
            (mPopup->isVisible()&&(key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter||key->key()==Qt::Key_Tab||key->key()==Qt::Key_Escape||key->key()==Qt::Key_Up||key->key()==Qt::Key_Down))){event->accept();return true;}
    }
    if(event->type()==QEvent::KeyPress&&watched==mEditor){
        auto key=static_cast<QKeyEvent *>(event);
        if(key->key()==Qt::Key_Space&&key->modifiers()==Qt::ControlModifier){complete();return true;}
        if(key->key()==Qt::Key_F12&&key->modifiers()==Qt::NoModifier){
            dismiss();if(synchronize()&&mClient&&mClient->ready()){
                mClient->cancel(mDefinitionId);mDefinitionRevision=mRevision;
                mDefinitionLine=mEditor->caretY();mDefinitionCharacter=mEditor->caretX();
                mDefinitionId=mClient->requestAt("textDocument/definition",mPath,mDefinitionLine,mDefinitionCharacter);
            }return true;
        }
        if(mPopup->isVisible()){
            switch(key->key()){
            case Qt::Key_Up:mPopup->setCurrentRow(std::max(0,mPopup->currentRow()-1));return true;
            case Qt::Key_Down:mPopup->setCurrentRow(std::min(mPopup->count()-1,mPopup->currentRow()+1));return true;
            case Qt::Key_Return:case Qt::Key_Enter:case Qt::Key_Tab:acceptCompletion();return true;
            case Qt::Key_Escape:dismiss();return true;
            default:dismiss();break;
            }
        }
    }
    if(event->type()==QEvent::MouseMove){
        auto mouse=static_cast<QMouseEvent *>(event);
        if(mouse->buttons()==Qt::NoButton){
            auto pos=mouse->position().toPoint();if(watched==mEditor->viewport())pos=mEditor->viewport()->mapTo(mEditor,pos);
            if(pos!=mHoverPosition){mHoverPosition=pos;mHover.start();QToolTip::hideText();if(mClient)mClient->cancel(mHoverId);mHoverId=0;}
        }
    }
    if(event->type()==QEvent::ToolTip){
        const auto help=static_cast<QHelpEvent *>(event);mHoverPosition=watched==mEditor->viewport()?mEditor->viewport()->mapTo(mEditor,help->pos()):help->pos();hover();return true;
    }
    if(event->type()==QEvent::FocusOut||event->type()==QEvent::Hide||event->type()==QEvent::Leave){
        if(event->type()!=QEvent::Leave){dismiss();mExplicitPending=false;mComplete.stop();}
        mHover.stop();if(mClient)mClient->cancel(mHoverId);mHoverId=0;QToolTip::hideText();
    }
    return false;
}
void RustEditorSupport::signatureHelp(){
    if(!isRust()||!mClient||!mEditor->hasFocus()||!synchronize()||!mClient->ready())return;
    const auto caret=mEditor->caretXY();mSignatureRevision=mRevision;mSignatureCaret={caret.ch,caret.line};
    mSignatureId=mClient->requestAt("textDocument/signatureHelp",mPath,caret.line,caret.ch);
}
void RustEditorSupport::receive(int id,const QString &method,const QString &path,int version,const QJsonValue &value){
    if(!isRust()||path!=QFileInfo(mPath).absoluteFilePath()||version!=mVersion)return;
    if(id==mCompletionId&&method=="textDocument/completion"){
        mCompletionId=0;
        if(mRevision==mCompletionRevision&&mEditor->hasFocus()&&mEditor->caretY()==mCompletionLine&&mEditor->caretX()==mCompletionCharacter)showCompletions(value);
    }else if(id==mResolveId&&method=="completionItem/resolve"){
        mResolveId=0;
        if(value.isObject())applyCompletion(value.toObject());else dismiss();
    }else if(id==mHoverId&&method=="textDocument/hover"){
        mHoverId=0;
        if(mRevision!=mHoverRevision||mHoverPosition!=mRequestedHover||!mEditor->isVisible()||mPopup->isVisible())return;
        const auto text=contentText(value.toObject().value("contents")).left(16000);
        if(!text.isEmpty())QToolTip::showText(mEditor->mapToGlobal(mRequestedHover),"<pre>"+text.toHtmlEscaped()+"</pre>",mEditor,{},15000);
    }else if(id==mSignatureId&&method=="textDocument/signatureHelp"){
        mSignatureId=0;if(mRevision!=mSignatureRevision||mEditor->caretX()!=mSignatureCaret.x()||mEditor->caretY()!=mSignatureCaret.y()||!mEditor->hasFocus())return;
        const auto signatures=value.toObject().value("signatures").toArray();if(signatures.isEmpty())return;
        const auto index=std::clamp(value.toObject().value("activeSignature").toInt(),0,int(signatures.size())-1);
        const auto signature=signatures[index].toObject();const auto contents=signature.value("label").toString()+"\n"+contentText(signature.value("documentation"));
        const auto point=mEditor->displayCoordToPixels(mEditor->bufferToDisplayPos(mEditor->caretXY()));
        QToolTip::showText(mEditor->mapToGlobal(point+QPoint(0,mEditor->textHeight())),"<pre>"+contents.left(8000).toHtmlEscaped()+"</pre>",mEditor,{},15000);
    }else if(id==mDefinitionId&&method=="textDocument/definition"){
        mDefinitionId=0;
        if(mRevision!=mDefinitionRevision||mEditor->caretY()!=mDefinitionLine||mEditor->caretX()!=mDefinitionCharacter||!mEditor->hasFocus())return;
        const auto array=value.toArray();
        const auto location=value.isArray()?(array.isEmpty()?QJsonObject{}:array.at(0).toObject()):value.toObject();
        const auto url=QUrl(location.value("uri").toString(location.value("targetUri").toString()));
        const auto range=location.value("range").toObject().isEmpty()?location.value("targetSelectionRange").toObject():location.value("range").toObject();
        const auto target=position(range.value("start").toObject());
        if(url.isLocalFile()&&target.line>=0&&target.ch>=0)emit definitionRequested(url.toLocalFile(),target.line,target.ch);
    }
}
void RustEditorSupport::showCompletions(const QJsonValue &value){
    const auto entries=value.isArray()?value.toArray():value.toObject().value("items").toArray();
    mItems={};mPopup->clear();
    for(const auto &entry:entries){
        const auto item=entry.toObject();const auto label=item.value("label").toString();if(label.isEmpty())continue;
        const auto format=item.value("insertTextFormat").toInt(1);
        if(format!=1&&format!=2)continue;
        if(format==2){
            const auto templateText=item.value("textEdit").toObject().value("newText").toString(item.value("insertText").toString());
            if(!RustSnippet::expand(templateText).valid)continue;
        }
        mItems.append(item);auto *row=new QListWidgetItem(label.left(300),mPopup);
        row->setToolTip("<pre>"+(item.value("detail").toString()+"\n"+contentText(item.value("documentation"))).left(4000).toHtmlEscaped()+"</pre>");
        if(mItems.size()>=250)break;
    }
    if(mItems.isEmpty())return;
    mPopup->setFont(mEditor->font());mPopup->setCurrentRow(0);
    mPopup->resize(std::clamp(std::clamp(pSettings->codeCompletion().widthInColumns(),20,120)*std::clamp(mEditor->fontMetrics().averageCharWidth(),2,100),280,650),
        std::clamp(int(mItems.size()),1,12)*(mPopup->fontMetrics().height()+6)+8);
    const auto pixel=mEditor->displayCoordToPixels(mEditor->bufferToDisplayPos(mEditor->caretXY()));
    auto point=mEditor->mapToGlobal(pixel+QPoint(0,mEditor->textHeight()));
    if(auto *screen=QGuiApplication::screenAt(point)){
        const auto rect=screen->availableGeometry();
        if(point.y()+mPopup->height()>rect.bottom())point.setY(mEditor->mapToGlobal(pixel).y()-mPopup->height());
        point.setX(std::clamp(point.x(),rect.left(),std::max(rect.left(),rect.right()-mPopup->width())));
        point.setY(std::max(rect.top(),point.y()));
    }
    QToolTip::hideText();mPopup->move(point);mPopup->show();
}
void RustEditorSupport::acceptCompletion(){
    const auto row=mPopup->currentRow();if(row<0||row>=mItems.size()||!mClient)return;
    mSelected=mItems[row].toObject();mPopup->hide();
    if(mSelected.contains("data")){
        mResolveId=mClient->resolveCompletion(mPath,mSelected);
        if(!mResolveId)dismiss();
    }else applyCompletion(mSelected);
}
void RustEditorSupport::applyCompletion(const QJsonObject &item){
    if(mRevision!=mCompletionRevision||mEditor->readOnly()||mEditor->selAvail()||!mEditor->hasFocus()||
        mEditor->caretY()!=mCompletionLine||mEditor->caretX()!=mCompletionCharacter||item.value("label").toString().isEmpty()){dismiss();return;}
    struct Edit { QSynedit::CharPos begin,end; QString text; qsizetype start,finish; bool primary=false; };
    QVector<Edit> edits;const auto before=normalizedText(mEditor);const auto lines=before.split('\n');
    QVector<qsizetype> offsets;qsizetype offset=0;for(const auto &line:lines){offsets.append(offset);offset+=line.size()+1;}
    auto add=[&](const QJsonObject &edit,bool primary){
        const auto range=edit.value("range").toObject();const auto begin=position(range.value("start").toObject()),end=position(range.value("end").toObject());
        if(!validPosition(mEditor,begin)||!validPosition(mEditor,end)||!edit.value("newText").isString())return false;
        const auto start=offsets[begin.line]+begin.ch,finish=offsets[end.line]+end.ch;
        const auto text=edit.value("newText").toString().replace("\r\n","\n").replace('\r','\n');
        if(finish<start||text.size()>1024*1024)return false;
        edits.append({begin,end,text,start,finish,primary});return true;
    };
    auto primary=item.value("textEdit").toObject();
    if(primary.isEmpty()){
        const auto line=mEditor->lineText();int start=mCompletionCharacter;
        while(start>0&&wordCharacter(line[start-1]))--start;
        primary={{"range",QJsonObject{{"start",QJsonObject{{"line",mCompletionLine},{"character",start}}},
            {"end",QJsonObject{{"line",mCompletionLine},{"character",mCompletionCharacter}}}}},
            {"newText",item.value("insertText").toString(item.value("label").toString())}};
    }
    QString editorTemplate;
    const auto format=item.value("insertTextFormat").toInt(1);
    if(format==2){
        const auto expansion=RustSnippet::expand(primary.value("newText").toString());
        if(!expansion.valid){dismiss();return;}
        primary.insert("newText",expansion.text);editorTemplate=expansion.editorSnippet;
    }else if(format!=1){dismiss();return;}
    if(!add(primary,true)){dismiss();return;}
    const auto requestOffset=offsets[mCompletionLine]+mCompletionCharacter;
    if(edits.first().start>requestOffset||edits.first().finish<requestOffset){dismiss();return;}
    const auto additional=item.value("additionalTextEdits").toArray();
    if(additional.size()>64){dismiss();return;}
    for(const auto &edit:additional)if(!add(edit.toObject(),false)){dismiss();return;}
    std::sort(edits.begin(),edits.end(),[](const Edit &a,const Edit &b){return a.start>b.start;});
    for(int i=1;i<edits.size();++i)if(edits[i].finish>edits[i-1].start||edits[i].start==edits[i-1].start){dismiss();return;}
    qsizetype finalSize=before.size();
    for(const auto &edit:edits)finalSize+=edit.text.size()-(edit.finish-edit.start);
    if(finalSize>4*1024*1024){dismiss();return;}
    Edit primaryEdit{};for(const auto &edit:edits)if(edit.primary)primaryEdit=edit;
    qsizetype start=primaryEdit.start,finish=primaryEdit.finish;
    for(const auto &edit:edits)if(!edit.primary&&edit.finish<=primaryEdit.start){const auto delta=edit.text.size()-(edit.finish-edit.start);start+=delta;finish+=delta;}
    // Validate every edit against the same snapshot before entering a single undo group.
    dismiss();mApplying=true;
    {
        mEditor->beginEditing();const auto cleanup=finally([this]{mEditor->endEditing();mApplying=false;});
        for(const auto &edit:edits)if(!edit.primary){mEditor->setCaretAndSelection(edit.begin,edit.begin,edit.end);mEditor->setSelText(edit.text);}
        const auto text=normalizedText(mEditor);
        const auto at=[&](qsizetype offset){const auto prefix=text.left(offset);return QSynedit::CharPos{int(offset-prefix.lastIndexOf('\n')-1),int(prefix.count('\n'))};};
        const auto begin=at(start),end=at(finish);mEditor->setCaretAndSelection(begin,begin,end);
        if(format==2)mEditor->insertCodeSnippet(editorTemplate);else mEditor->setSelText(primaryEdit.text);
    }
    synchronize();
}
void RustEditorSupport::applyDiagnostics(const QString &path,int version,const QJsonArray &items){
    if(!isRust()||mPath.isEmpty()||QFileInfo(path).absoluteFilePath()!=QFileInfo(mPath).absoluteFilePath()||version!=mVersion||mSyncedRevision!=mRevision)return;
    mEditor->clearSyntaxIssues();int count=0;
    for(const auto &entry:items){
        const auto item=entry.toObject();const auto range=item.value("range").toObject();
        auto begin=position(range.value("start").toObject()),end=position(range.value("end").toObject());
        if(!validPosition(mEditor,begin)||!validPosition(mEditor,end))continue;
        if(end.line<begin.line||(end.line==begin.line&&end.ch<begin.ch))continue;
        const auto severity=item.value("severity").toInt(1);if(severity>2)continue;
        int finish=end.line==begin.line?end.ch:mEditor->lineText(begin.line).size();
        finish=std::max(begin.ch+1,finish);
        mEditor->addSyntaxIssues(begin.line,begin.ch,finish,severity==1?CompileIssueType::Error:CompileIssueType::Warning,
            "rust-analyzer: "+item.value("message").toString().left(8000));
        if(++count>=1000)break;
    }
    mEditor->invalidate();mEditor->invalidateGutter();
}
