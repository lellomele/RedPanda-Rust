// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "rustdebugpanel.h"
#include <QTreeWidget>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QDialog>
#include <QLabel>
#include <QSettings>
#include <QSignalBlocker>
#include <QHeaderView>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextCursor>
#include <QJsonDocument>
#include <QDialogButtonBox>
#include <QMenu>
namespace {constexpr int Id=Qt::UserRole,Reference=Qt::UserRole+1,Requested=Qt::UserRole+2,ParentReference=Qt::UserRole+3,Name=Qt::UserRole+4,Page=Qt::UserRole+5;}
RustDebugPanel::RustDebugPanel(RustDebugSession *session,const QString &settingsFile,QWidget *parent):QWidget(parent),mSession(session),mSettingsFile(settingsFile){
    setObjectName("rustDebugPanel");auto layout=new QVBoxLayout(this);layout->setContentsMargins(4,4,4,4);
    auto tools=new QHBoxLayout;mThreads=new QComboBox(this);mThreads->setObjectName("rustDebugThreads");
    mMemory=new QPushButton(tr("Memoria"),this);mAssembly=new QPushButton(tr("Disassemblato"),this);
    auto refreshButton=new QPushButton(tr("Aggiorna"),this);tools->addWidget(new QLabel(tr("Thread:"),this));tools->addWidget(mThreads,1);tools->addWidget(refreshButton);tools->addWidget(mMemory);tools->addWidget(mAssembly);layout->addLayout(tools);
    mDataBreakpoint=new QPushButton(tr("Watchpoint"),this);mDataBreakpoint->setObjectName("rustDataBreakpoint");tools->addWidget(mDataBreakpoint);
    mFunctions=new QPushButton(tr("Breakpoint funzioni…"),this);mFunctions->setObjectName("rustFunctionBreakpoints");tools->addWidget(mFunctions);
    auto splitter=new QSplitter(this);mStack=new QTreeWidget(splitter);mStack->setObjectName("rustDebugStack");mStack->setHeaderLabels({tr("Funzione"),tr("File"),tr("Riga")});
    mVariables=new QTreeWidget(splitter);mVariables->setObjectName("rustDebugVariables");mVariables->setHeaderLabels({tr("Variabile"),tr("Valore"),tr("Tipo")});
    auto watchPane=new QWidget(splitter);auto watchLayout=new QVBoxLayout(watchPane);watchLayout->setContentsMargins(0,0,0,0);
    mWatches=new QTreeWidget(watchPane);mWatches->setObjectName("rustDebugWatches");mWatches->setHeaderLabels({tr("Watch"),tr("Valore"),tr("Tipo")});watchLayout->addWidget(mWatches);
    auto watchTools=new QHBoxLayout;mWatchExpression=new QLineEdit(watchPane);mWatchExpression->setPlaceholderText(tr("Espressione da osservare"));
    auto add=new QPushButton("+",watchPane);auto remove=new QPushButton("−",watchPane);watchTools->addWidget(mWatchExpression,1);watchTools->addWidget(add);watchTools->addWidget(remove);watchLayout->addLayout(watchTools);layout->addWidget(splitter,2);
    mConsole=new QPlainTextEdit(this);mConsole->setObjectName("rustDebugConsole");mConsole->setReadOnly(true);mConsole->setMaximumBlockCount(10000);layout->addWidget(mConsole,1);
    mInput=new QLineEdit(this);mInput->setObjectName("rustDebugInput");mInput->setPlaceholderText(tr("Input del programma: scrivi e premi Invio"));layout->addWidget(mInput);
    mEvaluate=new QLineEdit(this);mEvaluate->setPlaceholderText(tr("Comando LLDB o ?espressione: premi Invio quando il programma è in pausa"));layout->addWidget(mEvaluate);
    for(auto tree:{mStack,mVariables,mWatches}){
        tree->setAlternatingRowColors(true);tree->setRootIsDecorated(tree!=mStack);
        tree->header()->setStretchLastSection(false);tree->header()->setMinimumSectionSize(50);
        tree->header()->setSectionResizeMode(0,QHeaderView::Interactive);tree->setColumnWidth(0,160);
        tree->header()->setSectionResizeMode(1,QHeaderView::Stretch);
        tree->header()->setSectionResizeMode(2,QHeaderView::Interactive);tree->setColumnWidth(2,tree==mStack?60:100);
    }
    QSettings settings(mSettingsFile,QSettings::IniFormat);mWatchList=settings.value("RustDebugger/watches").toStringList().mid(0,100);
    const auto savedFunctions=QJsonDocument::fromJson(settings.value("RustDebugger/functions").toByteArray());
    if(savedFunctions.isArray())for(const auto &entry:savedFunctions.array()){
        const auto value=entry.toObject();const auto name=value.value("name").toString();
        if(!name.isEmpty()&&name.size()<=4096&&mFunctionBreakpoints.size()<100)mFunctionBreakpoints.append(QJsonObject{{"name",name}});
    }
    session->setFunctionBreakpoints(mFunctionBreakpoints);
    connect(session,&RustDebugSession::reply,this,&RustDebugPanel::receive);
    connect(session,&RustDebugSession::output,this,&RustDebugPanel::appendOutput);
    connect(session,&RustDebugSession::error,this,[this](const QString &text){appendOutput(tr("Errore: %1\n").arg(text));});
    connect(session,&RustDebugSession::stopped,this,[this](int thread,const QString &reason){mThread=thread;appendOutput(tr("In pausa: %1\n").arg(reason));refresh();});
    connect(session,&RustDebugSession::stateChanged,this,[this](RustDebugSession::State state){
        setMinimumHeight(state==RustDebugSession::State::Idle?0:320);
        const bool stopped=state==RustDebugSession::State::Stopped;
        mThreads->setEnabled(stopped);mEvaluate->setEnabled(stopped);mInput->setEnabled(state==RustDebugSession::State::Running||stopped);
        mMemory->setEnabled(stopped&&mSession->capabilities().value("supportsReadMemoryRequest").toBool());
        mAssembly->setEnabled(stopped&&mSession->capabilities().value("supportsDisassembleRequest").toBool());
        mDataBreakpoint->setEnabled(stopped&&mSession->capabilities().value("supportsDataBreakpoints").toBool());
        mFunctions->setEnabled(state==RustDebugSession::State::Idle||stopped);
        if(!stopped){resetRequests();mStack->clear();mVariables->clear();mWatches->clear();mItems.clear();mFrame=0;mInstructionPointer.clear();
            for(const auto &expression:mWatchList)new QTreeWidgetItem(mWatches,{expression,state==RustDebugSession::State::Running?tr("In esecuzione"):QString(),QString()});}
        if(state==RustDebugSession::State::Starting){mConsole->clear();mEscape.clear();}
        if(state==RustDebugSession::State::Idle)mDataBreakpoints={};
    });
    connect(mThreads,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){if(index>=0&&mSession&&mSession->state()==RustDebugSession::State::Stopped)stack(mThreads->itemData(index).toInt());});
    connect(mStack,&QTreeWidget::currentItemChanged,this,[this](QTreeWidgetItem *item){if(item)selectFrame(item);});
    connect(mVariables,&QTreeWidget::itemExpanded,this,&RustDebugPanel::variables);connect(mWatches,&QTreeWidget::itemExpanded,this,&RustDebugPanel::variables);
    connect(mVariables,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem *item,int column){
        if(item->data(0,Page).toInt()>0){variables(item);return;}
        if(column!=1||!mSession||mSession->state()!=RustDebugSession::State::Stopped||!mSession->capabilities().value("supportsSetVariable").toBool())return;
        const int reference=item->data(0,ParentReference).toInt();if(reference<=0)return;
        bool ok=false;const auto value=QInputDialog::getText(this,tr("Modifica variabile"),tr("Nuovo valore di %1:").arg(item->text(0)),QLineEdit::Normal,item->text(1),&ok);
        if(ok){const auto id=mSession->request("setVariable",{{"variablesReference",reference},{"name",item->data(0,Name).toString()},{"value",value}});if(id)mPending.insert(id,{"setVariable",mEpoch,0,{}});}
    });
    connect(mWatches,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem *item){if(item->data(0,Page).toInt()>0)variables(item);});
    connect(mDataBreakpoint,&QPushButton::clicked,this,&RustDebugPanel::addDataBreakpoint);
    connect(mFunctions,&QPushButton::clicked,this,&RustDebugPanel::configureFunctionBreakpoints);
    mVariables->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(mVariables,&QTreeWidget::customContextMenuRequested,this,[this](const QPoint &point){
        const auto item=mVariables->itemAt(point);if(item)mVariables->setCurrentItem(item);
        QMenu menu(this);auto add=menu.addAction(tr("Interrompi sull'accesso alla variabile…"));add->setEnabled(mDataBreakpoint->isEnabled()&&item&&item->data(0,ParentReference).toInt()>0);
        auto clear=menu.addAction(tr("Rimuovi tutti i watchpoint"));clear->setEnabled(!mDataBreakpoints.isEmpty()&&mSession&&mSession->state()==RustDebugSession::State::Stopped);
        const auto chosen=menu.exec(mVariables->viewport()->mapToGlobal(point));
        if(chosen==add)addDataBreakpoint();else if(chosen==clear){mDataBreakpoints={};const int id=mSession->request("setDataBreakpoints",{{"breakpoints",mDataBreakpoints}});if(id)mPending.insert(id,{"dataSet",mEpoch,0,{}});}
    });
    connect(refreshButton,&QPushButton::clicked,this,&RustDebugPanel::refresh);
    connect(mMemory,&QPushButton::clicked,this,&RustDebugPanel::inspectMemory);connect(mAssembly,&QPushButton::clicked,this,&RustDebugPanel::showAssembly);
    connect(add,&QPushButton::clicked,this,[this]{addWatch(mWatchExpression->text());mWatchExpression->clear();});
    connect(mWatchExpression,&QLineEdit::returnPressed,this,[this]{addWatch(mWatchExpression->text());mWatchExpression->clear();});
    connect(remove,&QPushButton::clicked,this,[this]{
        auto item=mWatches->currentItem();if(!item||item->parent())return;mWatchList.removeAll(item->text(0));saveWatches();refresh();
    });
    connect(mInput,&QLineEdit::returnPressed,this,[this]{if(mSession&&mSession->sendInput(mInput->text()))mInput->clear();else appendOutput(tr("Input non disponibile o coda piena.\n"));});
    connect(mEvaluate,&QLineEdit::returnPressed,this,[this]{
        if(!mSession||mSession->state()!=RustDebugSession::State::Stopped)return;
        const auto expression=mEvaluate->text().trimmed();if(expression.isEmpty())return;
        appendOutput("> "+expression+"\n");const auto id=mSession->request("evaluate",{{"expression",expression},{"frameId",mFrame},{"context","repl"}});
        if(id)mPending.insert(id,{"console",mEpoch,0,{}});mEvaluate->clear();
    });
    mThreads->setEnabled(false);mInput->setEnabled(false);mEvaluate->setEnabled(false);mMemory->setEnabled(false);mAssembly->setEnabled(false);mDataBreakpoint->setEnabled(false);
}
void RustDebugPanel::resetRequests(){++mEpoch;if(mSession)for(const int id:mPending.keys())mSession->cancel(id);mPending.clear();mVariablePages.clear();}
int RustDebugPanel::registerItem(QTreeWidgetItem *item){const int id=mNextItem++;item->setData(0,Id,id);mItems.insert(id,item);return id;}
void RustDebugPanel::forgetItems(QTreeWidgetItem *item){for(int i=0;i<item->childCount();++i)forgetItems(item->child(i));const int id=item->data(0,Id).toInt();mItems.remove(id);mVariablePages.remove(id);}
void RustDebugPanel::refresh(){
    if(!mSession||mSession->state()!=RustDebugSession::State::Stopped)return;resetRequests();
    const int id=mSession->request("threads");if(id)mPending.insert(id,{"threads",mEpoch,0,{}});
}
void RustDebugPanel::stack(int thread){
    if(!mSession||mSession->state()!=RustDebugSession::State::Stopped)return;resetRequests();mThread=thread;mSession->selectThread(thread);
    const int id=mSession->request("stackTrace",{{"threadId",thread},{"startFrame",0},{"levels",100}});if(id)mPending.insert(id,{"stack",mEpoch,0,{}});
}
void RustDebugPanel::selectFrame(QTreeWidgetItem *item){
    if(!mSession||mSession->state()!=RustDebugSession::State::Stopped)return;resetRequests();mFrame=item->data(0,Id).toInt();
    mInstructionPointer=item->data(0,Reference).toString();mVariables->clear();mWatches->clear();mItems.clear();
    const auto file=item->data(1,Name).toString();const int line=item->data(2,Id).toInt()-1;
    if(!file.isEmpty()&&line>=0)emit sourceRequested(file,line,0);
    int id=mSession->request("scopes",{{"frameId",mFrame}});if(id)mPending.insert(id,{"scopes",mEpoch,0,{}});
    for(const auto &expression:mWatchList){auto watch=new QTreeWidgetItem(mWatches,{expression,tr("Valutazione…"),QString()});const int target=registerItem(watch);
        id=mSession->request("evaluate",{{"expression",expression},{"frameId",mFrame},{"context","watch"}});if(id)mPending.insert(id,{"watch",mEpoch,target,{}});}
}
void RustDebugPanel::variables(QTreeWidgetItem *item){
    if(!mSession||mSession->state()!=RustDebugSession::State::Stopped||item->data(0,Requested).toBool())return;
    const int reference=item->data(0,Reference).toInt();if(reference<=0)return;item->setData(0,Requested,true);
    const int start=item->data(0,Page).toInt();const auto parent=start>0?item->parent():item;if(!parent)return;
    const int target=parent->data(0,Id).toInt();
    if(start>0&&mVariablePages.contains(target)){fillVariables(parent,mVariablePages.page(target,start),reference,start);return;}
    const int id=mSession->request("variables",{{"variablesReference",reference}});
    if(id)mPending.insert(id,{"variables",mEpoch,parent->data(0,Id).toInt(),{},start});else item->setData(0,Requested,false);
}
void RustDebugPanel::fillVariables(QTreeWidgetItem *parent,const QJsonArray &values,int parentReference,int start){
    for(int i=parent->childCount()-1;i>=0;--i)if(start==0||parent->child(i)->data(0,Page).toInt()>0){auto child=parent->takeChild(i);forgetItems(child);delete child;}
    int count=0;for(const auto &entry:values){if(++count>200)break;const auto value=entry.toObject();
        auto item=new QTreeWidgetItem(parent,{value.value("name").toString().left(1000),value.value("value").toString().left(4000),value.value("type").toString().left(1000)});
        registerItem(item);item->setData(0,Name,value.value("name").toString());item->setData(0,ParentReference,parentReference);
        item->setToolTip(0,value.value("name").toString().left(4000).toHtmlEscaped());item->setToolTip(2,value.value("type").toString().left(4000).toHtmlEscaped());
        const int reference=value.value("variablesReference").toInt();item->setData(0,Reference,reference);item->setData(0,Requested,false);
        if(reference>0)item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        item->setToolTip(1,value.value("value").toString().left(4000).toHtmlEscaped());
    }
    const int target=parent->data(0,Id).toInt();
    if(mVariablePages.more(target,start+200)){
        auto more=new QTreeWidgetItem(parent,{tr("Carica altre 200 variabili…"),QString(),QString()});
        registerItem(more);more->setData(0,Reference,parentReference);more->setData(0,Page,start+200);
    }else if(mVariablePages.truncated(target)){
        new QTreeWidgetItem(parent,{tr("Limite di 20.000 variabili: usa un'espressione watch"),QString(),QString()});
    }
}
void RustDebugPanel::receive(int id,const QString &,const QJsonObject &body,bool success,const QString &message){
    if(!mPending.contains(id))return;const auto pending=mPending.take(id);if(pending.epoch!=mEpoch)return;
    if(!success){appendOutput(tr("Debugger: %1\n").arg(message));if(pending.kind=="variables")if(auto item=mItems.value(pending.target,nullptr)){item->setData(0,Requested,false);for(int i=0;i<item->childCount();++i)if(item->child(i)->data(0,Page).toInt()>0)item->child(i)->setData(0,Requested,false);}return;}
    if(pending.kind=="threads"){
        QSignalBlocker blocker(mThreads);mThreads->clear();int selected=-1;
        for(const auto &entry:body.value("threads").toArray()){const auto item=entry.toObject();const int thread=item.value("id").toInt();
            mThreads->addItem(item.value("name").toString(),thread);if(thread==mThread)selected=mThreads->count()-1;}
        if(selected<0&&mThreads->count())selected=0;mThreads->setCurrentIndex(selected);
        if(selected>=0)stack(mThreads->itemData(selected).toInt());else stack(mThread);
    }else if(pending.kind=="stack"){
        {QSignalBlocker blocker(mStack);mStack->clear();
            for(const auto &entry:body.value("stackFrames").toArray()){const auto value=entry.toObject();const auto path=value.value("source").toObject().value("path").toString();
                auto item=new QTreeWidgetItem(mStack,{value.value("name").toString(),QFileInfo(path).fileName(),QString::number(value.value("line").toInt())});
                item->setData(0,Id,value.value("id").toInt());item->setData(0,Reference,value.value("instructionPointerReference").toString());item->setData(1,Name,path);item->setData(2,Id,value.value("line").toInt());}
            if(mStack->topLevelItemCount())mStack->setCurrentItem(mStack->topLevelItem(0));}
        if(mStack->topLevelItemCount())selectFrame(mStack->topLevelItem(0));
    }else if(pending.kind=="scopes"){
        for(const auto &entry:body.value("scopes").toArray()){const auto value=entry.toObject();auto item=new QTreeWidgetItem(mVariables,{value.value("name").toString(),QString(),QString()});
            registerItem(item);item->setData(0,Reference,value.value("variablesReference").toInt());item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);item->setExpanded(true);}
    }else if(pending.kind=="variables"){
        if(auto item=mItems.value(pending.target,nullptr)){mVariablePages.store(pending.target,body.value("variables").toArray());fillVariables(item,mVariablePages.page(pending.target,0),item->data(0,Reference).toInt());}
    }else if(pending.kind=="watch"){
        if(auto item=mItems.value(pending.target,nullptr)){item->setText(1,body.value("result").toString().left(4000));item->setText(2,body.value("type").toString());
            item->setData(0,Reference,body.value("variablesReference").toInt());if(body.value("variablesReference").toInt()>0)item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);}
    }else if(pending.kind=="console")appendOutput(body.value("result").toString()+"\n");
    else if(pending.kind=="dataInfo"){
        const auto data=body.value("dataId").toString();if(data.isEmpty()){appendOutput(tr("La variabile non supporta un watchpoint.\n"));return;}
        QStringList access;for(const auto &value:body.value("accessTypes").toArray())if(QSet<QString>{"read","write","readWrite"}.contains(value.toString()))access.append(value.toString());
        if(access.isEmpty())access.append("write");bool ok=false;
        const auto type=QInputDialog::getItem(this,tr("Watchpoint"),tr("Interrompi su:"),access,qMax(0,access.indexOf("write")),false,&ok);
        if(!ok||pending.epoch!=mEpoch||!mSession||mSession->state()!=RustDebugSession::State::Stopped)return;
        QJsonArray next=mDataBreakpoints;bool duplicate=false;for(const auto &value:next)if(value.toObject().value("dataId")==data)duplicate=true;
        if(!duplicate&&next.size()<16)next.append(QJsonObject{{"dataId",data},{"accessType",type}});mDataBreakpoints=next;
        const int id=mSession->request("setDataBreakpoints",{{"breakpoints",next}});if(id)mPending.insert(id,{"dataSet",mEpoch,0,{}});
    }else if(pending.kind=="dataSet"){
        for(const auto &value:body.value("breakpoints").toArray()){const auto bp=value.toObject();appendOutput(bp.value("verified").toBool()?tr("Watchpoint attivo.\n"):tr("Watchpoint non attivo: %1\n").arg(bp.value("message").toString()));}
    }
    else if(pending.kind=="setVariable")refresh();
    else if(pending.kind=="assembly"&&pending.viewer){QStringList lines;for(const auto &entry:body.value("instructions").toArray()){const auto value=entry.toObject();lines.append(value.value("address").toString()+"  "+value.value("instruction").toString());}pending.viewer->setPlainText(lines.join('\n'));}
    else if(pending.kind=="memory"&&pending.viewer){const auto data=QByteArray::fromBase64(body.value("data").toString().toLatin1());QStringList lines;
        for(int offset=0;offset<data.size();offset+=16)lines.append(QString::number(offset,16).rightJustified(4,'0')+"  "+QString::fromLatin1(data.mid(offset,16).toHex(' ')));pending.viewer->setPlainText(body.value("address").toString()+"\n"+lines.join('\n'));}
}
void RustDebugPanel::addWatch(const QString &expression){
    const auto value=expression.trimmed();if(value.isEmpty()||value.size()>4096||mWatchList.size()>=100||mWatchList.contains(value))return;
    mWatchList.append(value);saveWatches();if(mSession&&mSession->state()==RustDebugSession::State::Stopped)refresh();else new QTreeWidgetItem(mWatches,{value,QString(),QString()});
}
void RustDebugPanel::saveWatches(){QSettings settings(mSettingsFile,QSettings::IniFormat);settings.setValue("RustDebugger/watches",mWatchList);settings.sync();if(settings.status()!=QSettings::NoError)appendOutput(tr("Impossibile salvare le watch; restano disponibili nella sessione.\n"));}
void RustDebugPanel::addDataBreakpoint(){
    if(!mSession||mSession->state()!=RustDebugSession::State::Stopped||!mSession->capabilities().value("supportsDataBreakpoints").toBool())return;
    const auto item=mVariables->currentItem();if(!item||item->data(0,ParentReference).toInt()<=0){appendOutput(tr("Seleziona una variabile locale per impostare il watchpoint.\n"));return;}
    const int id=mSession->request("dataBreakpointInfo",{{"variablesReference",item->data(0,ParentReference).toInt()},{"name",item->data(0,Name).toString()},{"frameId",mFrame}});
    if(id)mPending.insert(id,{"dataInfo",mEpoch,item->data(0,Id).toInt(),{}});
}
void RustDebugPanel::configureFunctionBreakpoints(){
    if(!mSession||(mSession->active()&&mSession->state()!=RustDebugSession::State::Stopped))return;
    QDialog dialog(this);dialog.setWindowTitle(tr("Breakpoint sulle funzioni Rust"));dialog.resize(700,450);auto layout=new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Un nome completo per riga, ad esempio mio_crate::modulo::funzione.\nI breakpoint vengono mantenuti tra le sessioni."),&dialog));auto text=new QPlainTextEdit(&dialog);text->setObjectName("rustFunctionNames");QStringList names;for(const auto &entry:mFunctionBreakpoints)names.append(entry.toObject().value("name").toString());text->setPlainText(names.join('\n'));layout->addWidget(text);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return;QJsonArray next;QSet<QString> distinct;
    for(const auto &line:text->toPlainText().split('\n')){const auto name=line.trimmed();if(name.isEmpty()||distinct.contains(name))continue;if(name.size()>4096||next.size()>=100){appendOutput(tr("Usa al massimo 100 nomi di funzione, lunghi fino a 4096 caratteri.\n"));return;}distinct.insert(name);next.append(QJsonObject{{"name",name}});}
    QSettings settings(mSettingsFile,QSettings::IniFormat);settings.setValue("RustDebugger/functions",QJsonDocument(next).toJson(QJsonDocument::Compact));settings.sync();if(settings.status()!=QSettings::NoError){appendOutput(tr("Impossibile salvare i breakpoint sulle funzioni.\n"));return;}
    mFunctionBreakpoints=next;mSession->setFunctionBreakpoints(next);
}
void RustDebugPanel::appendOutput(const QString &text){
    QString plain;
    for(const auto ch:text){
        if(mEscape.isEmpty()){if(ch==QChar(0x1b))mEscape+=ch;else if(ch!='\r')plain+=ch;continue;}
        mEscape+=ch;
        if(mEscape.size()>4096){mEscape.clear();continue;}
        if(mEscape.size()==2&&ch!='['&&ch!=']'){mEscape.clear();continue;}
        if(mEscape.size()>2&&mEscape[1]=='['&&ch.unicode()>=0x40&&ch.unicode()<=0x7e)mEscape.clear();
        else if(mEscape[1]==']'&&(ch==QChar(7)||mEscape.endsWith(QString(QChar(0x1b))+'\\')))mEscape.clear();
    }
    auto cursor=mConsole->textCursor();cursor.movePosition(QTextCursor::End);cursor.insertText(plain);mConsole->setTextCursor(cursor);mConsole->ensureCursorVisible();
}
void RustDebugPanel::showAssembly(){
    if(!mSession||mSession->state()!=RustDebugSession::State::Stopped||mInstructionPointer.isEmpty())return;
    auto dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle(tr("Disassemblato Rust"));dialog->resize(850,500);
    auto layout=new QVBoxLayout(dialog);auto text=new QPlainTextEdit(dialog);text->setReadOnly(true);layout->addWidget(text);dialog->show();
    const int id=mSession->request("disassemble",{{"memoryReference",mInstructionPointer},{"instructionCount",100},{"resolveSymbols",true}});
    if(id)mPending.insert(id,{"assembly",mEpoch,0,text});
}
void RustDebugPanel::inspectMemory(){
    if(!mSession||mSession->state()!=RustDebugSession::State::Stopped)return;bool ok=false;
    const auto address=QInputDialog::getText(this,tr("Memoria"),tr("Indirizzo o riferimento di memoria:"),QLineEdit::Normal,mInstructionPointer,&ok);
    if(!ok||address.trimmed().isEmpty())return;auto dialog=new QDialog(this);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setWindowTitle(tr("Memoria Rust"));dialog->resize(750,500);
    auto layout=new QVBoxLayout(dialog);auto text=new QPlainTextEdit(dialog);text->setReadOnly(true);layout->addWidget(text);dialog->show();
    const int id=mSession->request("readMemory",{{"memoryReference",address.trimmed()},{"count",1024}});if(id)mPending.insert(id,{"memory",mEpoch,0,text});
}
