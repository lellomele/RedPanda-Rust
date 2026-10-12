# Verifiche Rust

Configurare CMake con `BUILD_TESTING=ON`, compilare `all-test-targets` e usare `ctest --output-on-failure`. Le suite funzionali usano progetti e profili temporanei. Richiedono Rust con rust-src, rust-analyzer, rustfmt e Clippy; le prove DAP richiedono CodeLLDB. `REDPANDA_TEST_CODELLDB` indica l'adapter per i test; `REDPANDA_CODELLDB` lo indica per la finestra.

Le suite coprono Cargo e rustc su file singoli, diagnostiche, input/output Unicode, errori e annullamenti, processi figli e limiti di memoria/output; workspace, target, esempi, test, feature, profili, argomenti e ambiente. Le prove LSP usano un server simulato e rust-analyzer reale: testo non salvato, UTF-16, risposte obsolete, chiusura, riapertura, riavvio e messaggi malformati. Il debugger reale verifica stack, passi, variabili, input, memoria, disassemblato, breakpoint avanzati, destinazioni rifiutate e avvio fino al cursore.

## Interfaccia

La build dedicata `REDPANDA_UI_CHECKS=ON` espone `RedPandaRust.exe --rust-ui-test-output <nuova-cartella>`, con profilo isolato. Il controllo usa eventi Qt di mouse e tastiera per margine, Ctrl+F4 e Ctrl+F5; verifica ripristino dei breakpoint da disco, spostamento con le righe, pannello e rifiuto dei sorgenti modificati. Altri controlli coprono build/run/check, preferenze, analisi e refactoring multi-file con anteprima e undo/redo, Git, pulizia Cargo, salvataggi falliti e conservazione del testo. Produce `checks.json` e immagini, poi chiude la finestra.

La regressione `impl` → `implo` viene verificata sul file grande, con tooltip, Ctrl-hover e 100 cicli di apertura/chiusura. Le suite comuni includono editor, recupero, ricerca, impostazioni, sintassi e undo Unicode.

Eseguire le prove Windows anche nel normale contesto dell'utente. Un ambiente con accesso filesystem limitato può cambiare la risoluzione dei percorsi del debugger e il comportamento dei processi figli; conservare e investigare i risultati differenti.

## Distribuzione

La build da distribuire usa `REDPANDA_UI_CHECKS=OFF`, non contiene comandi QA e non dipende da Qt Test. Controllare DLL e licenze, ZIP, corrispondenza dei sorgenti, checksum e avvio con le DLL del pacchetto. Rigenerare il report sulla nuova release: i risultati precedenti non certificano il nuovo codice.

Linux è rimandato. Le prove locali non sostituiscono il collaudo su sistemi Windows puliti né l'uso indipendente sul campo; fino a tale validazione mantenere la dicitura beta.
