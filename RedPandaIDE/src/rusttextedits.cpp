// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "rusttextedits.h"
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <new>
#include <QStringView>
namespace RustTextEdits {
namespace {
#ifdef RUST_TEXT_EDITS_TESTING
thread_local bool failAllocation=false;
void allocationPoint(){if(failAllocation){failAllocation=false;throw std::bad_alloc();}}
#else
void allocationPoint(){}
#endif
bool validUtf16(const QString &text){
    for(qsizetype i=0;i<text.size();++i){
        if(text[i].isHighSurrogate()){if(i+1>=text.size()||!text[i+1].isLowSurrogate())return false;++i;}
        else if(text[i].isLowSurrogate())return false;
    }
    return true;
}
bool boundary(const QString &text,qsizetype at){return !(at>0&&at<text.size()&&text[at-1].isHighSurrogate()&&text[at].isLowSurrogate());}
}
#ifdef RUST_TEXT_EDITS_TESTING
void failNextAllocationForTest(){failAllocation=true;}
#endif
bool plan(const QString &source,const QJsonArray &values,QVector<Edit> &result,QString &error)try{
    auto fail=[&](const QString &text){error=text;return false;};
    if(source.size()>MaxTextUnits||values.size()>MaxEdits)return fail("Documento o numero di modifiche oltre il limite.");
    allocationPoint();
    QVector<int> offsets{0};for(int i=0;i<source.size();++i)if(source[i]=='\n')offsets.append(i+1);
    auto offset=[&](const QJsonObject &position,int &result){
        const auto lineValue=position.value("line"),characterValue=position.value("character");
        if(!lineValue.isDouble()||!characterValue.isDouble())return false;
        const auto lineNumber=lineValue.toDouble(),characterNumber=characterValue.toDouble();
        if(lineNumber<0||lineNumber>=offsets.size()||lineNumber!=std::floor(lineNumber)||characterNumber<0||characterNumber>source.size()||characterNumber!=std::floor(characterNumber))return false;
        const int line=int(lineNumber),character=int(characterNumber);const int end=line+1<offsets.size()?offsets[line+1]-1:source.size();
        if(character>end-offsets[line])return false;result=offsets[line]+character;
        return boundary(source,result);
    };
    QVector<Edit> candidate;int order=0;qsizetype finalSize=source.size();
    for(const auto &value:values){
        if(!value.isObject())return fail("Modifica LSP non valida.");const auto object=value.toObject(),range=object.value("range").toObject();Edit edit;edit.order=order++;
        if(!offset(range.value("start").toObject(),edit.start)||!offset(range.value("end").toObject(),edit.end)||edit.end<edit.start||!object.value("newText").isString())return fail("Intervallo di modifica LSP non valido.");
        edit.text=object.value("newText").toString();edit.text.replace("\r\n","\n").replace('\r','\n');if(edit.text.size()>MaxReplacementUnits)return fail("Testo della modifica troppo grande.");
        if(!validUtf16(edit.text))return fail("Testo UTF-16 non valido.");
        finalSize+=edit.text.size()-(edit.end-edit.start);candidate.append(edit);
    }
    if(finalSize>MaxTextUnits)return fail("Il risultato supera il limite del documento.");
    std::stable_sort(candidate.begin(),candidate.end(),[](const Edit &a,const Edit &b){return a.start<b.start;});
    for(int i=1;i<candidate.size();++i){const auto &before=candidate[i-1],&after=candidate[i];
        if(before.end>after.start||(before.start==after.start&&(before.end!=before.start||after.end!=after.start)))return fail("Modifiche LSP sovrapposte.");}
    result=candidate;error.clear();return true;
}catch(const std::bad_alloc &){error=QStringLiteral("Memoria insufficiente per preparare le modifiche; nessun testo modificato.");return false;}
bool applyChecked(const QString &source,const QVector<Edit> &edits,QString &result,QString &error)try{
    const auto fail=[&](const QString &message){error=message;return false;};
    if(source.size()>MaxTextUnits||edits.size()>MaxEdits)return fail(QStringLiteral("Documento o numero di modifiche oltre il limite."));
    qsizetype finalSize=source.size(),cursor=0;int previousStart=-1,previousEnd=-1;
    for(const auto &edit:edits){
        if(edit.start<0||edit.end<edit.start||edit.end>source.size()||edit.start<cursor)
            return fail(QStringLiteral("Piano di modifiche non valido o sovrapposto."));
        if(edit.start==previousStart&&(edit.end!=edit.start||previousEnd!=previousStart))
            return fail(QStringLiteral("Modifiche LSP sovrapposte."));
        if(!boundary(source,edit.start)||!boundary(source,edit.end)||!validUtf16(edit.text))return fail(QStringLiteral("Testo UTF-16 non valido."));
        if(edit.text.size()>MaxReplacementUnits)return fail(QStringLiteral("Testo della modifica troppo grande."));
        finalSize+=edit.text.size()-(qsizetype(edit.end)-edit.start);cursor=edit.end;previousStart=edit.start;previousEnd=edit.end;
    }
    if(finalSize>MaxTextUnits)return fail(QStringLiteral("Il risultato supera il limite del documento."));
    if(edits.isEmpty()){result=source;error.clear();return true;}
    allocationPoint();
    if(edits.size()==1){const auto &edit=edits.front();QString candidate=source;candidate.replace(edit.start,edit.end-edit.start,edit.text);result.swap(candidate);error.clear();return true;}
    QString candidate;candidate.reserve(finalSize);const QStringView original(source);cursor=0;
    for(const auto &edit:edits){candidate.append(original.mid(cursor,edit.start-cursor));candidate.append(edit.text);cursor=edit.end;}
    candidate.append(original.mid(cursor));
    result.swap(candidate);error.clear();return true;
}catch(const std::bad_alloc &){error=QStringLiteral("Memoria insufficiente per applicare le modifiche; nessun testo modificato.");return false;
}
QString apply(const QString &source,const QVector<Edit> &plan){
    QString result,error;return applyChecked(source,plan,result,error)?result:source;
}
}
