// Frozen reference from commit 0fa7dc388389a6b4aa593daf1e9baa089b68408c.
// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "rusttextedits.h"
namespace RustTextEditsBaseline {
bool plan(const QString &,const QJsonArray &,QVector<RustTextEdits::Edit> &,QString &);
QString apply(const QString &,const QVector<RustTextEdits::Edit> &);
}
