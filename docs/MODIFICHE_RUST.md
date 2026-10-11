# Adattamento Rust

Base: lellomele/RedPanda-CPP, commit `18e9afbc8a60a4e5568362fc5e8011a8050cc84c`, verificato contro HEAD pubblico l’11 ottobre 2026.

Riutilizzati: finestra Qt Designer, editor QSynEdit, split editor, undo/redo, ricerca, temi, icone, modelli diagnostici, salvataggi atomici, recovery e rilevazione delle modifiche esterne. Conservati i sorgenti originari dei servizi C++ per la compatibilità interna; i comandi non applicabili non sono esposti nella modalità Rust.

Aggiunti: servizio Cargo asincrono, JSON diagnostici ufficiali, protezione Windows Job Object all’avvio, annullamento con processi figli, stdin, limite output, nuovi progetti Cargo, riconoscimento .rs e sintassi Rust con commenti annidati, stringhe raw e lifetime. Identità e configurazione separate. Logo Rust conservato e integrato.

Il componente rust-analyzer è installato localmente ma l’integrazione LSP e il debug Rust restano fuori dal perimetro essenziale dell’anteprima.

I file `.rs` singoli vengono compilati direttamente con rustc (edizione 2024), controllati, eseguiti, testati e formattati senza richiedere Cargo.toml. Gli eseguibili temporanei vengono creati fuori dalla cartella dei sorgenti. Le diagnostiche rustc alimentano la stessa tabella dei progetti Cargo. Un errore di compilazione impedisce di avviare un vecchio eseguibile.

La distribuzione viene compilata con REDPANDA_UI_CHECKS=OFF: le verifiche delle finestre restano disponibili in una build separata per lo sviluppo.

I riferimenti alle icone dell’applicazione ereditati dai file Qt Designer sono collegati alle risorse Rust: barra del titolo, finestra Informazioni e finestre secondarie. La verifica delle icone caricate confronta i pixel dei formati ICO usati nelle finestre.

Il sito dell’applicazione punta al repository dedicato `lellomele/RedPanda-Rust`. La finestra Informazioni distingue la base RedPanda-CPP/QSynEdit di Roy Qu e contributori dalle modifiche del fork e dall’adattamento Rust di Raffaele Mele; `ATTRIBUTIONS.md` documenta la provenienza dei componenti.

Le preferenze includono Rust > Strumenti del compilatore: percorsi Cargo, rustc, rustfmt e rust-analyzer, rilevamento automatico, verifica asincrona delle versioni ed edizione dei file singoli. I percorsi e l’edizione sono salvati nel profilo; i progetti Cargo conservano l’edizione del manifest.

La richiesta di suggerimenti su identificatori Rust incompleti non passa più lo stato del lexer Rust al parser C++. Lo stato del lexer C++ è validato prima dell’uso. La regressione `impl` → `implo` viene verificata sul file di prova completo di oltre 4.500 righe, preservando il testo modificato.
