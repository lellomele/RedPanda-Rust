// Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef REDPANDA_RUSTSNIPPET_H
#define REDPANDA_RUSTSNIPPET_H
#include <QString>
namespace RustSnippet {
struct Expansion {QString text,editorSnippet;bool valid=false;};
// Supported LSP tab stops and single-line placeholders are translated into the
// existing QSynEdit snippet engine. Unsupported constructs are rejected intact.
Expansion expand(const QString &source);
}
#endif
