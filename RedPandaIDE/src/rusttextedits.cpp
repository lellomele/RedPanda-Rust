// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "rusttextedits.h"
#include <QJsonObject>
#include <algorithm>
#include <cmath>
namespace RustTextEdits {
bool plan(const QString &source,const QJsonArray &values,QVector<Edit> &result,QString &error){
    auto fail=[&](const QString &text){error=text;return false;};
    if(source.size()>4*1024*1024||values.size()>10000)return fail("Documento o numero di modifiche oltre il limite.");
    QVector<int> offsets{0};for(int i=0;i<source.size();++i)if(source[i]=='\n')offsets.append(i+1);
    auto offset=[&](const QJsonObject &position,int &result){
        const auto lineValue=position.value("line"),characterValue=position.value("character");
        if(!lineValue.isDouble()||!characterValue.isDouble())return false;
        const auto lineNumber=lineValue.toDouble(),characterNumber=characterValue.toDouble();
        if(lineNumber<0||lineNumber>=offsets.size()||lineNumber!=std::floor(lineNumber)||characterNumber<0||characterNumber>source.size()||characterNumber!=std::floor(characterNumber))return false;
        const int line=int(lineNumber),character=int(characterNumber);const int end=line+1<offsets.size()?offsets[line+1]-1:source.size();
        if(character>end-offsets[line])return false;result=offsets[line]+character;
        return !(result>0&&result<source.size()&&source[result-1].isHighSurrogate()&&source[result].isLowSurrogate());
    };
    QVector<Edit> candidate;int order=0;qsizetype finalSize=source.size();
    for(const auto &value:values){
        if(!value.isObject())return fail("Modifica LSP non valida.");const auto object=value.toObject(),range=object.value("range").toObject();Edit edit;edit.order=order++;
        if(!offset(range.value("start").toObject(),edit.start)||!offset(range.value("end").toObject(),edit.end)||edit.end<edit.start||!object.value("newText").isString())return fail("Intervallo di modifica LSP non valido.");
        edit.text=object.value("newText").toString();edit.text.replace("\r\n","\n").replace('\r','\n');if(edit.text.size()>1024*1024)return fail("Testo della modifica troppo grande.");
        for(int i=0;i<edit.text.size();++i){
            if(edit.text[i].isHighSurrogate()){if(i+1>=edit.text.size()||!edit.text[i+1].isLowSurrogate())return fail("Testo UTF-16 non valido.");++i;}
            else if(edit.text[i].isLowSurrogate())return fail("Testo UTF-16 non valido.");
        }
        finalSize+=edit.text.size()-(edit.end-edit.start);candidate.append(edit);
    }
    if(finalSize>4*1024*1024)return fail("Il risultato supera il limite del documento.");
    std::stable_sort(candidate.begin(),candidate.end(),[](const Edit &a,const Edit &b){return a.start<b.start;});
    for(int i=1;i<candidate.size();++i){const auto &before=candidate[i-1],&after=candidate[i];
        if(before.end>after.start||(before.start==after.start&&(before.end!=before.start||after.end!=after.start)))return fail("Modifiche LSP sovrapposte.");}
    result=candidate;error.clear();return true;
}
QString apply(const QString &source,const QVector<Edit> &plan){
    QString result=source;for(auto it=plan.crbegin();it!=plan.crend();++it)result.replace(it->start,it->end-it->start,it->text);return result;
}
}
