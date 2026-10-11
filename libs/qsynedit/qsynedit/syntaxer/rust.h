// Copyright (C) 2026 Raffaele Mele
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef QSYNEDIT_RUST_SYNTAXER_H
#define QSYNEDIT_RUST_SYNTAXER_H
#include "syntaxer.h"
namespace QSynedit {
class RustSyntaxer final : public Syntaxer {
public:
    struct State : SyntaxState {
        int comments=0, stringMode=0, hashes=0;
        bool equals(const PSyntaxState &other) const override;
    };
    RustSyntaxer();
    bool isCommentNotFinished(const PSyntaxState &s) const override;
    bool isStringNotFinished(const PSyntaxState &s) const override;
    bool eol() const override { return mEnd; }
    PSyntaxState getState() const override { return std::make_shared<State>(mState); }
    QString getToken() const override { return mLine.mid(mStart,mPos-mStart); }
    const PTokenAttribute &getTokenAttribute() const override { return mAttribute; }
    int getTokenPos() override { return mStart; }
    void next() override;
    void setState(const PSyntaxState &s) override;
    void setLine(int,const QString &line,size_t) override;
    void resetState() override { mState=State{}; }
    bool isKeyword(const QString &word) override;
    QSet<QString> keywords() const override;
    QString languageName() const override { return "Rust"; }
    ProgrammingLanguage language() const override { return ProgrammingLanguage::Rust; }
    QString lineCommentSymbol() const override { return "//"; }
    QString blockCommentBeginSymbol() const override { return "/*"; }
    QString blockCommentEndSymbol() const override { return "*/"; }
    bool supportFolding() const override { return true; }
    bool supportBraceLevel() const override { return true; }
    bool needsLineState() const override { return true; }
    PSyntaxer createInstance() const override { return std::make_shared<RustSyntaxer>(); }
private:
    void comment();
    void string();
    State mState;
    QString mLine;
    int mPos=0,mStart=0;
    bool mEnd=false;
    PTokenAttribute mAttribute,mNumber;
};
}
#endif
