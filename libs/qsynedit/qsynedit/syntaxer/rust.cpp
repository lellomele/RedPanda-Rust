// Copyright (C) 2026 Raffaele Mele
// SPDX-License-Identifier: GPL-3.0-or-later
#include "rust.h"
#include "../constants.h"
namespace QSynedit {
static const QSet<QString> rustWords={"as","async","await","break","const","continue","crate","dyn","else","enum","extern","false","fn","for","if","impl","in","let","loop","match","mod","move","mut","pub","ref","return","self","Self","static","struct","super","trait","true","type","unsafe","use","where","while","yield","abstract","become","box","do","final","macro","override","priv","typeof","unsized","virtual","try","gen","bool","char","str","i8","i16","i32","i64","i128","isize","u8","u16","u32","u64","u128","usize","f32","f64"};
RustSyntaxer::RustSyntaxer() {
    mNumber=std::make_shared<TokenAttribute>(SYNS_AttrNumber,TokenType::Number);
    addAttribute(mNumber); mAttribute=mIdentifierAttribute;
}
bool RustSyntaxer::State::equals(const PSyntaxState &other) const {
    auto s=std::dynamic_pointer_cast<State>(other);
    return s && comments==s->comments && stringMode==s->stringMode && hashes==s->hashes && SyntaxState::equals(other);
}
bool RustSyntaxer::isCommentNotFinished(const PSyntaxState &s) const {
    auto v=std::dynamic_pointer_cast<State>(s);return v && v->comments>0;
}
bool RustSyntaxer::isStringNotFinished(const PSyntaxState &s) const {
    auto v=std::dynamic_pointer_cast<State>(s);return v && v->stringMode!=0;
}
void RustSyntaxer::setState(const PSyntaxState &s) {
    auto v=std::dynamic_pointer_cast<State>(s);mState=v?*v:State{};
}
void RustSyntaxer::setLine(int,const QString &line,size_t) {
    mLine=line;mPos=0;mState.blockStarted=0;mState.blockEnded=0;mState.blockEndedLastLine=0;
    mState.hasTrailingSpaces=false;next();
}
bool RustSyntaxer::isKeyword(const QString &word) { return rustWords.contains(word); }
QSet<QString> RustSyntaxer::keywords() const { return rustWords; }
void RustSyntaxer::comment() {
    mAttribute=mCommentAttribute;
    while(mPos<mLine.size()) {
        if(mLine.mid(mPos,2)=="/*"){++mState.comments;mPos+=2;}
        else if(mLine.mid(mPos,2)=="*/"){mPos+=2;if(--mState.comments==0)break;}
        else ++mPos;
    }
}
void RustSyntaxer::string() {
    mAttribute=mStringAttribute;
    while(mPos<mLine.size()) {
        if(mState.stringMode==1 && mLine[mPos]=='\\') {
            mPos=qMin(mPos+2,int(mLine.size()));
        } else if(mLine[mPos]=='"') {
            if(mState.stringMode==1 || mLine.mid(mPos+1,mState.hashes)==QString(mState.hashes,'#')) {
                mPos+=1+(mState.stringMode==2?mState.hashes:0);mState.stringMode=0;break;
            }
            ++mPos;
        } else ++mPos;
    }
}
void RustSyntaxer::next() {
    mStart=mPos;mEnd=mPos>=mLine.size();if(mEnd)return;
    if(mState.comments){comment();return;}
    if(mState.stringMode){string();return;}
    const QChar c=mLine[mPos];
    if(c.isSpace()){
        mAttribute=mWhitespaceAttribute;while(mPos<mLine.size()&&mLine[mPos].isSpace())++mPos;
        mState.hasTrailingSpaces=mPos==mLine.size();return;
    }
    if(mLine.mid(mPos,2)=="//"){mAttribute=mCommentAttribute;mPos=mLine.size();return;}
    if(mLine.mid(mPos,2)=="/*"){comment();return;}
    int raw=mPos;
    if(mLine.mid(raw,2)=="br" || mLine.mid(raw,2)=="cr")raw+=2;
    else if(c=='r')++raw;
    else raw=-1;
    if(raw>=0){
        const int start=raw;while(raw<mLine.size()&&mLine[raw]=='#')++raw;
        if(raw<mLine.size()&&mLine[raw]=='"'){
            mState.hashes=raw-start;mState.stringMode=2;mPos=raw+1;string();return;
        }
    }
    if(c=='"'||((c=='b'||c=='c')&&mLine.mid(mPos+1,1)=="\"")){
        mPos+=(c=='"'?1:2);mState.stringMode=1;string();return;
    }
    // Lifetimes start with apostrophe; character literals have a closing apostrophe.
    int quote=mPos;if(c=='b'&&mLine.mid(mPos+1,1)=="'")++quote;
    if(mLine[quote]=='\''){
        int end=quote+1;
        if(end<mLine.size()&&mLine[end]=='\\'){
            ++end;
            if(end<mLine.size()&&mLine[end]=='u'&&mLine.mid(end+1,1)=="{"){
                const int close=mLine.indexOf('}',end+2);end=close<0?int(mLine.size()):close+1;
            }else if(end<mLine.size()&&mLine[end]=='x')end+=3;
            else ++end;
        }else if(end<mLine.size()){
            if(mLine[end].isHighSurrogate()&&end+1<mLine.size()&&mLine[end+1].isLowSurrogate())++end;
            ++end;
        }
        if(end<mLine.size()&&mLine[end]=='\''){mPos=end+1;mAttribute=mStringAttribute;return;}
    }
    if(c.isDigit()){
        mAttribute=mNumber;++mPos;
        while(mPos<mLine.size()){
            QChar n=mLine[mPos];
            if(n.isLetterOrNumber()||n=='_'){++mPos;continue;}
            if(n=='.'&&mPos+1<mLine.size()&&mLine[mPos+1].isDigit()){++mPos;continue;}
            if((n=='+'||n=='-')&&mPos>0&&(mLine[mPos-1]=='e'||mLine[mPos-1]=='E')){++mPos;continue;}
            break;
        }return;
    }
    if(isIdentStartChar(c)){
        ++mPos;while(mPos<mLine.size()&&isIdentChar(mLine[mPos]))++mPos;
        mAttribute=isKeyword(getToken())?mKeywordAttribute:mIdentifierAttribute;return;
    }
    mAttribute=mSymbolAttribute;++mPos;
    if(c=='{'){++mState.braceLevel;++mState.blockLevel;++mState.blockStarted;}
    else if(c=='}'){
        mState.braceLevel=qMax(0,mState.braceLevel-1);mState.blockLevel=qMax(0,mState.blockLevel-1);++mState.blockEnded;
    }else if(c=='(')++mState.parenthesisLevel;
    else if(c==')')mState.parenthesisLevel=qMax(0,mState.parenthesisLevel-1);
    else if(c=='[')++mState.bracketLevel;
    else if(c==']')mState.bracketLevel=qMax(0,mState.bracketLevel-1);
}
}
