# Adattamento Rust

Base: lellomele/RedPanda-CPP, commit `18e9afbc8a60a4e5568362fc5e8011a8050cc84c`, verificato contro HEAD pubblico l’11 ottobre 2026.

Riutilizzati: finestra Qt Designer, editor QSynEdit, split editor, undo/redo, ricerca, temi, icone, modelli diagnostici, salvataggi atomici, recovery e rilevazione delle modifiche esterne. Conservati i sorgenti originari dei servizi C++ per la compatibilità interna; i comandi non applicabili non sono esposti nella modalità Rust.

Aggiunti: servizio Cargo asincrono, JSON diagnostici ufficiali, protezione Windows Job Object all’avvio, annullamento con processi figli, stdin, limite output, nuovi progetti Cargo, riconoscimento .rs e sintassi Rust con commenti annidati, stringhe raw e lifetime. Identità e configurazione separate. Logo Rust conservato e integrato.

Il componente rust-analyzer è integrato tramite un client LSP asincrono per diagnostiche, informazioni sui simboli, completamento e navigazione alla definizione. Il debug Rust resta fuori dal perimetro dell’anteprima.

Il client usa un processo separato condiviso, messaggi JSON-RPC su stdin/stdout e posizioni UTF-16. Sincronizza il testo non salvato con un ritardo di 180 ms; il completamento automatico attende 250 ms. Le richieste interattive scadono dopo 10 secondi e quelle superate vengono annullate. Le risposte devono corrispondere alla versione corrente del documento e, per il completamento, alla posizione del cursore. La IDE valida gli intervalli prima di applicare tutte le modifiche nello stesso gruppo undo. Il server non può applicare modifiche al workspace.

Il protocollo gestisce frame frammentati o concatenati, con intestazioni fino a 8 KiB e messaggi fino a 16 MiB. L’invio è regolato in base alla coda di QProcess; la lettura viene suddivisa tra turni dell’event loop. Il caricamento dei progetti Cargo e dei file singoli è configurato con `linkedProjects`, senza generare manifest nella cartella dell’utente. Per i file singoli viene rilevato il sysroot della toolchain e rispettata l’edizione selezionata. La configurazione dei progetti viene inviata soltanto quando cambia.

Il processo rust-analyzer e i suoi figli appartengono a un Windows Job Object chiuso insieme alla IDE. In caso di guasto vengono conservati i documenti aperti, annullate le richieste e risincronizzato il testo dopo il riavvio. I tentativi sono limitati; il comando Strumenti → Riavvia rust-analyzer consente il recupero manuale. Cargo check resta un comando esplicito, evitando che l’analisi provochi una compilazione a ogni salvataggio.

I file `.rs` singoli vengono compilati direttamente con rustc (edizione 2024), controllati, eseguiti, testati e formattati senza richiedere Cargo.toml. Gli eseguibili temporanei vengono creati fuori dalla cartella dei sorgenti. Le diagnostiche rustc alimentano la stessa tabella dei progetti Cargo. Un errore di compilazione impedisce di avviare un vecchio eseguibile.

La distribuzione viene compilata con REDPANDA_UI_CHECKS=OFF: le verifiche delle finestre restano disponibili in una build separata per lo sviluppo.

I riferimenti alle icone dell’applicazione ereditati dai file Qt Designer sono collegati alle risorse Rust: barra del titolo, finestra Informazioni e finestre secondarie. La verifica delle icone caricate confronta i pixel dei formati ICO usati nelle finestre.

Il sito dell’applicazione punta al repository dedicato `lellomele/RedPanda-Rust`. La finestra Informazioni distingue la base RedPanda-CPP/QSynEdit di Roy Qu e contributori dalle modifiche del fork e dall’adattamento Rust di Raffaele Mele; `ATTRIBUTIONS.md` documenta la provenienza dei componenti.

Le preferenze includono Rust > Strumenti del compilatore: percorsi Cargo, rustc, rustfmt e rust-analyzer, rilevamento automatico, verifica asincrona delle versioni ed edizione dei file singoli. I percorsi e l’edizione sono salvati nel profilo; i progetti Cargo conservano l’edizione del manifest.

La richiesta di suggerimenti su identificatori Rust incompleti non passa più lo stato del lexer Rust al parser C++. Lo stato del lexer C++ è validato prima dell’uso. La regressione `impl` → `implo` viene verificata sul file di prova completo di oltre 4.500 righe, preservando il testo modificato.
