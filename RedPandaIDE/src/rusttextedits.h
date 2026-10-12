// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef REDPANDA_RUSTTEXTEDITS_H
#define REDPANDA_RUSTTEXTEDITS_H
#include <QString>
#include <QVector>
#include <QJsonArray>
namespace RustTextEdits {
inline constexpr qsizetype MaxTextUnits=4*1024*1024,MaxReplacementUnits=1024*1024,MaxEdits=10000;
struct Edit {int start=0,end=0;QString text;int order=0;};
bool plan(const QString &source,const QJsonArray &edits,QVector<Edit> &result,QString &error);
// On failure, result is unchanged. source/result may alias; error must be separate.
bool applyChecked(const QString &source,const QVector<Edit> &plan,QString &result,QString &error);
// Compatibility facade: returns the original source if the plan is invalid.
QString apply(const QString &source,const QVector<Edit> &plan);
#ifdef RUST_TEXT_EDITS_TESTING
void failNextAllocationForTest();
#endif
}
#endif
