// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "rustoutlinemodel.h"
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QThread>
#include <QUrl>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

struct RustOutlineModel::Node {
    quintptr id=0;Node *parent=nullptr;QVector<Node *> children,ordered;
    QString name,detail;int kind=0,line=0,character=0,visibility=0,row=-1;
    bool associated=false,trait=false,visible=true;Style style;
};
struct RustOutlineModel::Snapshot {
    Node root;std::vector<std::unique_ptr<Node>> owned;QHash<quintptr,Node *> ids;
    QString file,source;
};
namespace {
bool integer(const QJsonValue &v,int &value){
    if(!v.isDouble())return false;const double n=v.toDouble();
    if(!std::isfinite(n)||n<0||n>std::numeric_limits<int>::max()||std::floor(n)!=n)return false;
    value=int(n);return true;
}
bool unicode(const QString &s){
    for(qsizetype i=0;i<s.size();++i){if(s[i].isHighSurrogate()){if(++i>=s.size()||!s[i].isLowSurrogate())return false;}else if(s[i].isLowSurrogate())return false;}return true;
}
int rank(int kind,bool associated){
    switch(kind){case 1:case 2:case 3:case 4:return 0;case 5:case 23:case 19:return 1;case 11:case 26:return 2;case 10:case 22:return 3;case 9:return 4;case 12:return associated?5:7;case 6:case 25:return 6;case 7:case 8:return 8;case 14:return 9;case 13:return 10;default:return 11;}
}
QString bounded(const QString &s,int limit){int n=qMin(limit,int(s.size()));if(n<s.size()&&n>0&&s[n-1].isHighSurrogate())--n;return s.left(n);}
}
RustOutlineModel::RustOutlineModel(QObject *parent):QAbstractItemModel(parent),mData(std::make_shared<Snapshot>()){}
RustOutlineModel::~RustOutlineModel()=default;
const RustOutlineModel::Node *RustOutlineModel::node(const QModelIndex &i,const std::shared_ptr<Snapshot> &s) const{
    if(!i.isValid())return &s->root;
    if(i.model()!=this||i.column()!=0)return nullptr;
    const auto n=s->ids.value(i.internalId(),nullptr);
    return n&&n->visible&&n->row==i.row()?n:nullptr;
}
QModelIndex RustOutlineModel::nodeIndex(const Node *n) const{return n&&n!=&mData->root&&n->visible?createIndex(n->row,0,n->id):QModelIndex{};}
std::shared_ptr<RustOutlineModel::Snapshot> RustOutlineModel::cloneSnapshot() const{
    const auto old=mData;auto next=std::make_shared<Snapshot>();next->file=old->file;next->source=old->source;
    QHash<quintptr,Node *> nodes;nodes.insert(0,&next->root);
    for(const auto &n:old->owned){auto copy=std::make_unique<Node>(*n);nodes.insert(copy->id,copy.get());if(old->ids.contains(copy->id))next->ids.insert(copy->id,copy.get());next->owned.push_back(std::move(copy));}
    next->root.children=old->root.children;next->root.ordered=old->root.ordered;
    const auto remap=[&](Node *n){n->parent=n->parent?nodes.value(n->parent->id):nullptr;for(auto &child:n->children)child=nodes.value(child->id);for(auto &child:n->ordered)child=nodes.value(child->id);};
    remap(&next->root);for(const auto &n:next->owned)remap(n.get());return next;
}
QModelIndex RustOutlineModel::index(int row,int column,const QModelIndex &p) const{
    const auto s=mData;const auto n=node(p,s);if(!n||column!=0||row<0||row>=n->ordered.size())return {};return createIndex(row,column,n->ordered[row]->id);
}
QModelIndex RustOutlineModel::parent(const QModelIndex &i) const{const auto s=mData;const auto n=i.isValid()?node(i,s):nullptr;return n?nodeIndex(n->parent):QModelIndex{};}
int RustOutlineModel::rowCount(const QModelIndex &p) const{const auto s=mData;const auto n=node(p,s);return n?int(n->ordered.size()):0;}
int RustOutlineModel::columnCount(const QModelIndex &) const{return 1;}
QVariant RustOutlineModel::headerData(int s,Qt::Orientation o,int role) const{return s==0&&o==Qt::Horizontal&&role==Qt::DisplayRole?QVariant(tr("Simbolo Rust")):QVariant{};}
QVariant RustOutlineModel::data(const QModelIndex &i,int role) const{
    const auto s=mData;const auto n=i.isValid()?node(i,s):nullptr;if(!n)return {};
    switch(role){
    case Qt::DisplayRole:{auto detail=n->detail;if(detail.startsWith("fn("))return n->name+detail.mid(2);return detail.isEmpty()?n->name:n->name+" : "+detail;}
    case Qt::DecorationRole:return n->style.icon;
    case Qt::ForegroundRole:return n->style.color.isValid()?QVariant(n->style.color):QVariant{};
    case Qt::ToolTipRole:return "<pre>"+(n->name+"\n"+n->detail).toHtmlEscaped()+"</pre>";
    case NameRole:return n->name;case KindRole:return n->kind;case VisibilityRole:return n->visibility;case AssociatedRole:return n->associated;
    default:return {};
    }
}
RustOutlineModel::Location RustOutlineModel::location(const QModelIndex &i) const{
    const auto s=mData;const auto n=i.isValid()?node(i,s):nullptr;if(!n)return {};return {s->file,s->source,n->line,n->character,true};
}
QString RustOutlineModel::file() const{return mData->file;}
void RustOutlineModel::arrange(Node *p,bool visible){
    p->visible=visible;p->ordered.clear();
    for(auto child:p->children){const bool shown=visible&&(mShowTraits||!child->trait);child->row=-1;arrange(child,shown);if(shown)p->ordered.append(child);}
    std::stable_sort(p->ordered.begin(),p->ordered.end(),[this](const Node *a,const Node *b){
        if(mByKind&&rank(a->kind,a->associated)!=rank(b->kind,b->associated))return rank(a->kind,a->associated)<rank(b->kind,b->associated);
        if(mByName){const auto c=QString::compare(a->name,b->name,Qt::CaseInsensitive);if(c)return c<0;}
        if(a->line!=b->line)return a->line<b->line;if(a->character!=b->character)return a->character<b->character;return a->id<b->id;
    });
    for(int i=0;i<p->ordered.size();++i)p->ordered[i]->row=i;
}
void RustOutlineModel::finishChange(){
    mChanging=false;
    if(!mQueued&&(mPending||mPendingOptions||mPendingStyler)){mQueued=true;QTimer::singleShot(0,this,&RustOutlineModel::drainPending);}
}
void RustOutlineModel::drainPending(){
    mQueued=false;if(mChanging)return;
    const QPointer<RustOutlineModel> self(this);
    if(mPendingOptions){const auto options=*mPendingOptions;mPendingOptions.reset();setOptions(std::get<0>(options),std::get<1>(options),std::get<2>(options));if(!self)return;}
    if(mPendingStyler){auto style=std::move(*mPendingStyler);mPendingStyler.reset();setStyler(std::move(style));if(!self)return;}
    if(mPending){auto next=std::move(*mPending);mPending.reset();if(next.clear)clear();else {QString error;if(!setSymbols(next.symbols,next.file,next.source,error)&&self)emit rejected(error);}}
}
bool RustOutlineModel::setSymbols(const QJsonArray &symbols,const QString &file,const QString &source,QString &error){
    if(QThread::currentThread()!=thread()){error=QStringLiteral("La struttura Rust deve essere aggiornata nel thread proprietario.");return false;}
    if(mChanging){mPending=Update{symbols,file,source,false};error.clear();return true;}
    if(source.size()>4*1024*1024||file.size()>4096){error=tr("Struttura Rust oltre i limiti di dimensione.");return false;}
    const QPointer<RustOutlineModel> self(this);mChanging=true;
    const auto finish=[self]{if(self)self->finishChange();};
    try{
        auto next=std::make_shared<Snapshot>();next->file=file;next->source=source;
        QVector<qsizetype> starts{0};for(qsizetype at=source.indexOf('\n');at>=0;at=source.indexOf('\n',at+1))starts.append(at+1);
        const auto length=[&](int line){return (line+1<starts.size()?starts[line+1]-1:source.size())-starts[line];};
        quintptr nextId=mNextId;int count=0;QHash<int,Style> styles;
        const auto point=[&](const QJsonValue &value,int &line,int &column){
            const auto p=value.toObject();if(!integer(p.value("line"),line)||!integer(p.value("character"),column)||line>=starts.size()||column>length(line))return false;
            const auto at=starts[line]+column;return !(column>0&&column<length(line)&&source[at-1].isHighSurrogate()&&source[at].isLowSurrogate());
        };
        std::function<bool(const QJsonArray &,Node *,int)> build;
        build=[&](const QJsonArray &entries,Node *parent,int depth){
            if(depth>24&&!entries.isEmpty()){error=tr("Struttura Rust troppo annidata.");return false;}
            for(const auto &value:entries){
                if(++count>10000||nextId==std::numeric_limits<quintptr>::max()){error=tr("Troppi simboli Rust.");return false;}
                if(!value.isObject()){error=tr("Simbolo Rust non valido.");return false;}
                const auto entry=value.toObject();auto n=std::make_unique<Node>();n->parent=parent;n->id=nextId++;
                const auto name=entry.value("name"),detail=entry.value("detail");
                if(!name.isString()||name.toString().isEmpty()||!unicode(name.toString())||(!detail.isUndefined()&&(!detail.isString()||!unicode(detail.toString())))||!integer(entry.value("kind"),n->kind)||n->kind<1||n->kind>26){error=tr("Dati del simbolo Rust non validi.");return false;}
                n->name=bounded(name.toString(),300);n->detail=bounded(detail.toString(),512);
                auto range=entry.value("selectionRange").toObject();if(range.isEmpty())range=entry.value("location").toObject().value("range").toObject();
                int endLine,endColumn;if(!point(range.value("start"),n->line,n->character)||!point(range.value("end"),endLine,endColumn)||endLine<n->line||(endLine==n->line&&endColumn<n->character)){error=tr("Posizione del simbolo Rust non valida.");return false;}
                if(entry.contains("location")){
                    const QUrl uri(entry.value("location").toObject().value("uri").toString());
#ifdef Q_OS_WIN
                    const auto sensitivity=Qt::CaseInsensitive;
#else
                    const auto sensitivity=Qt::CaseSensitive;
#endif
                    if(!uri.isLocalFile()||QFileInfo(uri.toLocalFile()).absoluteFilePath().compare(QFileInfo(file).absoluteFilePath(),sensitivity)!=0){error=tr("Simbolo Rust riferito a un altro documento.");return false;}
                }
                n->associated=parent->kind==19||parent->kind==23||parent->kind==5||parent->kind==11;
                n->trait=n->kind==19&&n->name.startsWith("impl ")&&n->name.contains(" for ");
                int prefixStart=qMax(0,n->character-4096),rangeLine,rangeColumn;
                const auto declaration=entry.value("range").toObject().value("start").toObject();
                if(integer(declaration.value("line"),rangeLine)&&integer(declaration.value("character"),rangeColumn)&&rangeLine==n->line&&rangeColumn<=n->character&&n->character-rangeColumn<=4096)prefixStart=rangeColumn;
                auto prefix=source.mid(starts[n->line]+prefixStart,n->character-prefixStart).trimmed();
                static const QRegularExpression multilinePub("^pub\\([^)]*\\)$"),visibility("^pub\\b\\s*(\\([^)]*\\))?");
                if(n->line>0&&prefix.startsWith("fn ")&&length(n->line-1)<=256){const auto before=source.mid(starts[n->line-1],length(n->line-1)).trimmed();if(before=="pub"||multilinePub.match(before).hasMatch())prefix=before+" "+prefix;}
                const auto access=visibility.match(prefix);
                n->visibility=access.hasMatch()?(access.captured(1).isEmpty()?2:1):((parent->kind==11||parent->trait)?2:0);
                if(mStyler){const int key=n->kind*8+n->visibility*2+int(n->associated);if(!styles.contains(key)){const auto style=mStyler;const auto rendered=style(n->kind,n->visibility,n->associated);if(!self)return false;styles.insert(key,rendered);}n->style=styles.value(key);}
                auto raw=n.get();next->ids.insert(n->id,raw);parent->children.append(raw);next->owned.push_back(std::move(n));
                if(entry.contains("children")&&!entry.value("children").isArray()){error=tr("Figli del simbolo Rust non validi.");return false;}
                if(!build(entry.value("children").toArray(),raw,depth+1))return false;
            }
            // Join only unambiguous, simple inherent impls in the same scope.
            // Generic and trait impls retain their explicit Rust grouping.
            const auto children=parent->children;
            for(auto impl:children)if(impl->kind==19&&!impl->trait&&impl->name.startsWith("impl ")){
                const auto type=impl->name.mid(5);Node *target=nullptr;int matches=0;
                for(auto sibling:children)if((sibling->kind==23||sibling->kind==5||sibling->kind==10)&&sibling->name==type){target=sibling;++matches;}
                if(matches==1){for(auto member:impl->children){member->parent=target;target->children.append(member);}impl->children.clear();parent->children.removeOne(impl);impl->visible=false;next->ids.remove(impl->id);}
            }
            return true;
        };
        if(!build(symbols,&next->root,0)){finish();return false;}
        if(!self)return false;
        arrange(&next->root,true); // All owned allocations precede model publication.
        beginResetModel();if(!self)return false;
        mData=std::move(next);mNextId=nextId;endResetModel();
        if(!self)return false;error.clear();finish();return true;
    }catch(const std::bad_alloc &){error=QStringLiteral("Memoria insufficiente per aggiornare la struttura Rust.");finish();return false;}
}
void RustOutlineModel::clear(){
    if(QThread::currentThread()!=thread())return;
    if(mChanging){mPending=Update{{},{},{},true};return;}
    if(mData->file.isEmpty()&&mData->owned.empty())return;
    auto empty=std::make_shared<Snapshot>();const QPointer<RustOutlineModel> self(this);mChanging=true;
    beginResetModel();if(!self)return;mData=std::move(empty);endResetModel();if(self)finishChange();
}
void RustOutlineModel::setOptions(bool name,bool kind,bool traits){
    if(QThread::currentThread()!=thread())return;
    if(mChanging){mPendingOptions=std::make_tuple(name,kind,traits);return;}
    if(mByName==name&&mByKind==kind&&mShowTraits==traits)return;
    const QPointer<RustOutlineModel> self(this);mChanging=true;const bool reset=mShowTraits!=traits;
    const auto previousFlags=std::make_tuple(mByName,mByKind,mShowTraits);
    bool announced=false,ended=false,committed=false;
    try{
        auto next=cloneSnapshot();mByName=name;mByKind=kind;mShowTraits=traits;arrange(&next->root,true);
        // Notify observers with the old options and coherent old snapshot.
        std::tie(mByName,mByKind,mShowTraits)=previousFlags;
        if(reset)beginResetModel();else emit layoutAboutToBeChanged({},QAbstractItemModel::VerticalSortHint);
        if(!self)return;announced=true;const auto previous=persistentIndexList();
        QModelIndexList current;
        for(const auto &index:previous){const auto n=next->ids.value(index.internalId(),nullptr);current.append(n&&n->visible?createIndex(n->row,0,n->id):QModelIndex{});}
        mData=std::move(next);mByName=name;mByKind=kind;mShowTraits=traits;committed=true;ended=true;
        if(reset)endResetModel();else {changePersistentIndexList(previous,current);emit layoutChanged({},QAbstractItemModel::VerticalSortHint);}
        if(self)finishChange();
    }catch(const std::bad_alloc &){if(self){if(!committed)std::tie(mByName,mByKind,mShowTraits)=previousFlags;if(announced&&!ended){if(reset)endResetModel();else emit layoutChanged({},QAbstractItemModel::VerticalSortHint);}if(self){finishChange();emit rejected(tr("Memoria insufficiente per ordinare la struttura Rust."));}}}
}
void RustOutlineModel::setStyler(Styler style){
    if(QThread::currentThread()!=thread())return;
    if(mChanging){mPendingStyler=std::move(style);return;}
    const QPointer<RustOutlineModel> self(this);mChanging=true;
    try{
        auto next=cloneSnapshot();
        for(const auto &n:next->owned){n->style=style?style(n->kind,n->visibility,n->associated):Style{};if(!self)return;}
        mStyler=std::move(style);mData=std::move(next);
        if(rowCount())emit dataChanged(index(0,0),index(rowCount()-1,0),{Qt::DecorationRole,Qt::ForegroundRole});
        if(self)finishChange();
    }catch(const std::bad_alloc &){if(self){finishChange();emit rejected(tr("Memoria insufficiente per le icone della struttura Rust."));}}
}
