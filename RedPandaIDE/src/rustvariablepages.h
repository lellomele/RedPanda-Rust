// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef REDPANDA_RUSTVARIABLEPAGES_H
#define REDPANDA_RUSTVARIABLEPAGES_H
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
// CodeLLDB 1.12.3 returns a whole child list even when DAP start/count are
// supplied. Keep the bounded response and populate Qt tree items on demand.
class RustVariablePages final {
public:
    static constexpr int PageSize=200, MaxValues=20000;
    void clear(){mValues.clear();mBytes.clear();mTruncated.clear();mCacheBytes=0;}
    void remove(int id){mValues.remove(id);mCacheBytes-=mBytes.take(id);mTruncated.remove(id);}
    void store(int id,const QJsonArray &values){
        remove(id);QJsonArray bounded=values;
        if(values.size()>MaxValues){bounded={};for(int i=0;i<MaxValues;++i)bounded.append(values[i]);}
        const auto bytes=QJsonDocument(bounded).toJson(QJsonDocument::Compact).size();
        // Evicted containers can be queried again when expanded. This keeps
        // inspection memory bounded even for deeply nested object graphs.
        if(mCacheBytes+bytes>32*1024*1024)clear();
        if(bytes>32*1024*1024)return;
        mValues.insert(id,bounded);mBytes.insert(id,bytes);mCacheBytes+=bytes;
        if(values.size()>MaxValues)mTruncated.insert(id);
    }
    bool contains(int id)const{return mValues.contains(id);}
    bool truncated(int id)const{return mTruncated.contains(id);}
    bool more(int id,int start)const{return start>=0&&start<qMin(mValues.value(id).size(),MaxValues);}
    QJsonArray page(int id,int start)const{
        QJsonArray result;const auto values=mValues.value(id);
        if(start<0||start>=MaxValues)return result;
        const auto end=qMin(qMin(start+PageSize,MaxValues),values.size());
        for(int i=start;i<end;++i)result.append(values[i]);return result;
    }
private:
    QHash<int,QJsonArray> mValues;
    QHash<int,qsizetype> mBytes;
    QSet<int> mTruncated;
    qsizetype mCacheBytes=0;
};
#endif
