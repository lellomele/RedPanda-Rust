# Funzioni della distribuzione Rust

Confronto funzionale con la base RedPanda C++ 3.5.1: si riutilizzano editor QSynEdit, interfaccia Qt e servizi comuni; compilatore, analisi e debugger usano strumenti Rust. Questa matrice descrive il perimetro, non una certificazione di equivalenza completa fra linguaggi.

| Funzione della base | Equivalente Rust | Verifica |
|---|---|---|
| Editing, schede, split, undo/redo, ricerca e sostituzione | Editor QSynEdit riutilizzato, lexer Rust, ricerca in file | Suite editor/search/QSynEdit e controlli della finestra |
| Temi, caratteri, scorciatoie, codifica, icone | Preferenze comuni, UTF-8, icona Rust | Suite impostazioni/icona, controlli GUI |
| Compilazione e avvio file singolo | rustc, test rustc, output temporaneo senza Cargo.toml | Workflow e GUI, errori, annullamento, Unicode |
| Progetti e opzioni compilatore | Cargo workspace/package/target, feature, Debug/Release, ambiente, argomenti | Suite progetto/workflow e dialogo GUI |
| Diagnostiche navigabili | JSON rustc/Cargo e LSP rust-analyzer | Suite workflow/LSP e GUI |
| Analisi, completamento e navigazione | rust-analyzer, hover, definizione, riferimenti, struttura e simboli | Server reale e simulato, GUI su modifiche non salvate |
| Rinomina e refactoring | WorkspaceEdit con anteprima, modifica dei buffer, undo per file, azioni Rust | Suite text-edit/LSP e GUI multi-file |
| Snippet e template | Raccolta Rust modificabile, campi semplici e scelte | Suite snippet e GUI |
| Formattazione e controlli statici | rustfmt e Clippy | Workflow reale e GUI |
| Debug sorgenti | CodeLLDB via DAP, breakpoint, passi, stack, thread, watch, input | Sessioni reali DAP e GUI da mouse/tastiera |
| Debug avanzato | Condizioni, hit count, logpoint, funzioni, watchpoint ove supportati, memoria e disassemblato | Suite debugger e controlli pannello |
| Assembly e rappresentazione intermedia | rustc assembly e IR LLVM | Suite progetto/file singolo |
| Documentazione | Cargo doc / rustdoc | Suite progetto/workflow |
| Test ed esercizi | Cargo test / rustc --test, selezione target ed argomenti | Suite progetto/workflow |
| Gare e casi di input/output C/C++ | Pannelli originari nascosti; usare test Rust | Differenza esplicita, nessun supporto online judge dichiarato |
| Git | Servizi comuni, repository/staging/commit/branch/merge/remoti | GUI con repository temporanei e remoto locale |
| Salvataggi e recupero | QSaveFile e snapshot isolati, errori conservano i buffer | Suite recovery/editor/impostazioni e GUI |
| Pulizia artefatti | Anteprima Cargo, protezione sorgenti e Cestino | Suite clean e GUI con annullamento |
| Console | Input/output dei processi; ConPTY nel debugger Windows | Suite workflow/terminal/debug; nessun emulatore completo dichiarato |
| Distribuzione | ZIP Windows x64 con Qt e attribuzioni | Controllo dipendenze, checksum e prove del pacchetto |
| Linux | Rimandato, preferenza per componenti multipiattaforma | Fuori dal requisito 1.0 Windows |

Limiti: operazioni LSP di creazione/rinomina/eliminazione file e comandi arbitrari rifiutati; snippet complessi non espansi; limiti documenti/output/variabili descritti nel README. Le diagnostiche Rust mantengono il messaggio originale del compilatore; il catalogo GCC italiano della base non si applica a rustc.
