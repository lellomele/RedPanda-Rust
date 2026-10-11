#ifndef REDPANDA_APPIDENTITY_H
#define REDPANDA_APPIDENTITY_H
#include <QString>
namespace AppIdentity {
inline QString displayName() { return QStringLiteral("Red Panda Rust"); }
inline QString forkCopyright() { return QStringLiteral("Red Panda Rust e modifiche del fork rmele: © 2026 Raffaele Mele.\nIntegrazione Rust/Cargo, sintassi Rust, console, configurazione degli strumenti e identità dell'applicazione."); }
inline QString website() { return QStringLiteral("https://github.com/lellomele/RedPanda-Rust"); }
inline QString internalName() { return QStringLiteral("RedPandaRust"); }
inline QString iconResource() { return QStringLiteral(":/icons/app/red-panda-rust.png"); }
}
#endif
