// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QElapsedTimer>
#include <QTextStream>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "rusttextedits.h"
#include "rusttextedits-baseline.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif
using Edit=RustTextEdits::Edit;
struct Case{QString name,source;QVector<Edit> edits;QJsonArray json;};
static volatile qsizetype sink=0;
static Case makeCase(const QString &name){
    Case c;c.name=name;int size=64,count=1,width=1;QString replacement="replacement";
    if(name=="small-equal"){replacement="R";}
    else if(name=="small-multiple"){size=1024;count=10;}
    else if(name=="medium-grow"){size=256*1024;count=100;}
    else if(name.startsWith("large-")){size=256*1024;count=1000;if(name=="large-equal")replacement="R";if(name=="large-shrink"){width=4;replacement.clear();}}
    else if(name=="limit-single"){size=RustTextEdits::MaxTextUnits;count=1;replacement="R";}
    else if(name=="ten-thousand"){size=64*1024;count=10000;replacement="RR";}
    else if(name=="empty-plan"){size=1024;count=0;}
    else if(name=="original"){
        QFile file(qEnvironmentVariable("RUST_EDIT_FIXTURE"));if(!file.open(QIODevice::ReadOnly))std::exit(2);
        c.source=QString::fromUtf8(file.readAll()).replace("\r\n","\n").replace('\r','\n');
        int at=0;while((at=c.source.indexOf("TestError",at))>=0){c.edits.append({at,at+9,"TestErrorRenamed",int(c.edits.size())});at+=9;}
    }
    if(c.source.isEmpty()){
        c.source=QString(size,'x');for(int i=0;i<count;++i){int at=(i+1)*(size/(count+1));c.edits.append({at,at+width,replacement,i});}
    }
    QVector<int> lines{0};for(int i=0;i<c.source.size();++i)if(c.source[i]=='\n')lines.append(i+1);
    const auto position=[&](int at){const int line=int(std::upper_bound(lines.cbegin(),lines.cend(),at)-lines.cbegin())-1;return QJsonObject{{"line",line},{"character",at-lines[line]}};};
    for(const auto &e:c.edits)c.json.append(QJsonObject{{"range",QJsonObject{{"start",position(e.start)},{"end",position(e.end)}}},{"newText",e.text}});
    return c;
}
static QString run(const Case &c,bool candidate,bool pipeline){
    const QVector<Edit> *edits=&c.edits;QVector<Edit> planned;QString error;
    if(pipeline){bool ok=candidate?RustTextEdits::plan(c.source,c.json,planned,error):RustTextEditsBaseline::plan(c.source,c.json,planned,error);if(!ok)std::exit(3);edits=&planned;}
    if(!candidate)return RustTextEditsBaseline::apply(c.source,*edits);
    QString result;if(!RustTextEdits::applyChecked(c.source,*edits,result,error))std::exit(4);return result;
}
static double sample(const Case &c,bool candidate,bool pipeline,int repetitions){
    QElapsedTimer clock;clock.start();for(int i=0;i<repetitions;++i){const auto result=run(c,candidate,pipeline);sink=result.size()+(result.isEmpty()?0:result.front().unicode());}
    return double(clock.nsecsElapsed())/repetitions;
}
static QJsonObject stats(QVector<double> values){QJsonArray raw;for(double value:values)raw.append(value);std::sort(values.begin(),values.end());return {{"medianNs",values[values.size()/2]},{"p95Ns",values[qMin(values.size()-1,qsizetype(std::ceil(values.size()*0.95))-1)]},{"minNs",values.front()},{"maxNs",values.back()},{"samples",values.size()},{"rawNs",raw}};}
static QJsonObject memory(){
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS data{};data.cb=sizeof(data);if(GetProcessMemoryInfo(GetCurrentProcess(),&data,sizeof(data)))return {{"peakCommittedBytes",double(data.PeakPagefileUsage)},{"peakWorkingSetBytes",double(data.PeakWorkingSetSize)},{"currentCommittedBytes",double(data.PagefileUsage)}};
#endif
    return {};
}
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);const auto args=app.arguments();
    if(args.size()>3&&args[1]=="--memory"){
        const auto c=makeCase(args[2]);const bool candidate=args[3]=="candidate";QVector<QString> outputs;const auto before=memory();
        for(int i=0;i<12;++i)outputs.append(run(c,candidate,false));
        auto data=memory();data.insert("before",before);data.insert("outputUnits",outputs.last().size());data.insert("liveOutputs",outputs.size());data.insert("implementation",args[3]);data.insert("case",args[2]);QTextStream(stdout)<<QJsonDocument(data).toJson(QJsonDocument::Compact)<<"\n";return 0;
    }
    QJsonArray cases;const QStringList names={"empty-plan","small-grow","small-equal","small-multiple","medium-grow","original","large-grow","large-shrink","large-equal","limit-single","ten-thousand"};
    for(const auto &name:names){const auto c=makeCase(name);const auto expected=run(c,false,false);if(run(c,true,false)!=expected||run(c,true,true)!=expected)return 5;
        QJsonObject item{{"case",name},{"sourceUnits",c.source.size()},{"editCount",c.edits.size()},{"outputUnits",expected.size()}};
        for(bool pipeline:{false,true}){
            for(int i=0;i<3;++i){sample(c,false,pipeline,1);sample(c,true,pipeline,1);}
            const auto estimate=qMax(sample(c,false,pipeline,1),sample(c,true,pipeline,1));const int repetitions=qBound(1,int(10000000.0/qMax(estimate,1.0)),100000);
            QVector<double> a,b;for(int round=0;round<7;++round){if(round%2){b.append(sample(c,true,pipeline,repetitions));a.append(sample(c,false,pipeline,repetitions));}else{a.append(sample(c,false,pipeline,repetitions));b.append(sample(c,true,pipeline,repetitions));}}
            item.insert(pipeline?"pipeline":"apply",QJsonObject{{"baseline",stats(a)},{"candidate",stats(b)},{"repetitionsPerSample",repetitions}});
        }
        cases.append(item);QTextStream progress(stderr);progress<<"Benchmarked "<<name<<"\n";progress.flush();std::fflush(stderr);
    }
    QTextStream(stdout)<<QJsonDocument(QJsonObject{{"qt",QT_VERSION_STR},{"compiler",__VERSION__},{"build","Release"},{"timingUnit","nanoseconds per operation"},{"cases",cases}}).toJson();return 0;
}
