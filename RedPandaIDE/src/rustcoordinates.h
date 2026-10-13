// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RUSTCOORDINATES_H
#define RUSTCOORDINATES_H
#include <QString>
namespace RustCoordinates {
// rustc columns count Unicode scalars from one; Qt editor offsets count UTF-16
// code units from zero. Invalid coordinates must never split a surrogate pair.
inline int scalarColumnToUtf16(const QString &line,int column){
    if(column<1)return -1;int scalar=1,offset=0;
    while(offset<line.size()&&scalar<column){
        const auto c=line[offset];if(c.isHighSurrogate()){if(offset+1>=line.size()||!line[offset+1].isLowSurrogate())return -1;offset+=2;}
        else{if(c.isLowSurrogate())return -1;++offset;}++scalar;
    }
    return scalar==column?offset:-1;
}
}
#endif
