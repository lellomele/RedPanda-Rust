// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "rustsnippet.h"
#include <QMap>
#include <QVector>
namespace RustSnippet {
Expansion expand(const QString &source){
    Expansion result;if(source.size()>1024*1024||source.contains("%REPL_BEGIN%")||source.contains("%REPL_END%")||source.contains("%INSERT%"))return result;
    struct Stop {int id;QString value;};QVector<Stop> stops;QString editor,text;
    auto append=[&](const QString &value){text+=value;editor+=value;};
    for(int at=0;at<source.size();){
        const auto ch=source[at++];
        if(ch=='\\'){
            if(at>=source.size())return result;
            const auto escaped=source[at++];if(escaped!='$'&&escaped!='}'&&escaped!='\\'&&escaped!=','&&escaped!='|')return result;
            append(QString(escaped));continue;
        }
        if(ch!='$'){append(QString(ch));continue;}
        if(at>=source.size())return result;
        const bool brace=source[at]=='{';if(brace)++at;
        const int start=at;while(at<source.size()&&source[at].isDigit())++at;
        if(at==start)return result;
        bool ok=false;const int id=source.mid(start,at-start).toInt(&ok);if(!ok||id>1000)return result;
        QString value;
        if(brace){
            if(at>=source.size())return result;
            if(source[at]==':'){
                ++at;bool closed=false;
                while(at<source.size()){
                    const auto current=source[at++];
                    if(current=='}'){closed=true;break;}
                    if(current=='$'||current=='\n'||current=='\r')return result;
                    if(current=='\\'){
                        if(at>=source.size())return result;const auto escaped=source[at++];
                        if(escaped!='}'&&escaped!='$'&&escaped!='\\')return result;value+=escaped;
                    }else value+=current;
                }
                if(!closed)return result;
            }else if(source[at]=='|'){
                ++at;bool closed=false,first=true;
                while(at<source.size()){
                    const auto current=source[at++];
                    if(current=='|'&&at<source.size()&&source[at]=='}'){++at;closed=true;break;}
                    if(current=='\n'||current=='\r'||current=='$')return result;
                    if(current=='\\'){
                        if(at>=source.size())return result;const auto escaped=source[at++];
                        if(escaped!=','&&escaped!='|'&&escaped!='\\')return result;if(first)value+=escaped;
                    }else if(current==',')first=false;else if(first)value+=current;
                }
                if(!closed)return result;
            }else if(source[at]=='}')++at;else return result;
        }
        // The inherited engine visits tab stops in appearance order and cannot
        // mirror edits. Reject reordered or duplicated stops rather than insert
        // a snippet with misleading editing behaviour.
        if(!stops.isEmpty()&&((id!=0&&stops.last().id>=id)||stops.last().id==0))return result;
        stops.append({id,value});text+=value;
        if(id==0)editor+=value.isEmpty()?QString("%INSERT%"):QString("%REPL_BEGIN%")+value+"%REPL_END%%INSERT%";
        else editor+="%REPL_BEGIN%"+value+"%REPL_END%";
    }
    result.text=text;result.editorSnippet=editor;result.valid=true;return result;
}
}
