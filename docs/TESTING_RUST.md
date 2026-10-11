# Verifiche Rust

Le suite esistenti coprono editor e workflow di compilazione descritti sotto. L’integrazione LSP introdotta nella versione 0.2.3 è stata compilata; non sono state eseguite nuove prove funzionali LSP e non è coperta da queste suite.

Compilare il target `rust-workflow-checks` con CMake. Richiede Qt Test e una toolchain Cargo funzionante; crea soltanto progetti temporanei senza dipendenze esterne.

La suite copre stato della sintassi (commenti annidati, stringhe raw, lifetime, sorgenti incompleti), Cargo check/build/run/test/clippy/fmt, diagnostiche e percorsi Windows, avvio fallito e ripartenza, input senza newline, interruzione con processi figli e limite di output.

Verifica anche i file Rust singoli con rustc: controllo, compilazione, esecuzione, test, rustfmt, nomi con spazi e accenti, diagnostiche e ripartenza senza eseguire un vecchio binario dopo un errore. Nella prova della finestra vengono attivati i pulsanti Compila, Esegui e Controlla, anche dopo avere aperto un progetto Cargo diverso.

Per le prove della finestra configurare una build separata con `-DREDPANDA_UI_CHECKS=ON`, poi usare `RedPandaRust.exe --rust-ui-test-output <cartella-temporanea-nuova>`. Viene creato un profilo isolato e un progetto Cargo nella cartella indicata. Il controllo verifica identità, icona, evidenziazione Rust, azioni, UTF-8, salvataggi e conservazione del testo dopo un errore, build, run, format, diagnostiche e ripartenza. Le conferme sono gestite automaticamente soltanto durante questo controllo. Produce `checks.json` e uno screenshot, poi chiude la finestra.

Il pacchetto Windows viene inoltre controllato senza Qt e MinGW nel PATH. La versione Linux non è stata compilata o verificata.

La suite copre anche salvataggio e rifiuto dei percorsi non validi, uso dell’edizione selezionata e gestione dello stato del lexer incompatibile. Le prove della finestra includono `implo` alla linea 21 del file grande, tooltip e Ctrl-hover, salvataggio delle impostazioni, verifica delle versioni e contenuti della finestra Informazioni.

