// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef REDPANDA_RUSTTEXTEDITS_H
#define REDPANDA_RUSTTEXTEDITS_H
#include <QString>
#include <QVector>
#include <QJsonArray>
namespace RustTextEdits {
struct Edit {int start=0,end=0;QString text;int order=0;};
bool plan(const QString &source,const QJsonArray &edits,QVector<Edit> &result,QString &error);
QString apply(const QString &source,const QVector<Edit> &plan);
}
#endif
