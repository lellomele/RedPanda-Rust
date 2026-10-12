# Red Panda Rust

IDE desktop per Rust derivata da [RedPanda-CPP di Raffaele Mele](https://github.com/lellomele/RedPanda-CPP), con interfaccia Qt e editor QSynEdit. Include compilazione, esecuzione, test, formattazione, analisi semantica, completamento, refactoring, debug e Git.

**1.0.0-beta.3 per Windows x64:** versione per la valutazione su esercizi e progetti reali. Conservare una copia dei propri sorgenti e della cartella `config` prima dell'aggiornamento.

## Installazione

Su Windows estrarre il pacchetto e avviare **RedPandaRust.exe**. Qt e le librerie dell'interfaccia sono inclusi. Le preferenze portabili e il recupero dei documenti risiedono nella cartella `config` accanto all'eseguibile; conservarla durante gli aggiornamenti.

Installare una toolchain Rust funzionante tramite [rustup](https://rustup.rs), con il linker MSVC o GNU corrispondente. Aggiungere i componenti:

```text
rustup component add rust-analyzer rust-src rustfmt clippy
```

Cartella di lavoro e percorsi dei file salvati aperti vengono registrati anche durante la sessione. La riapertura dei file rispetta la preferenza di caricamento automatico dell'ultima sessione; il recupero dei contenuti non salvati resta periodico, secondo l'intervallo configurato. Su Windows la diagnostica dei crash nativi salva `diagnostics.log.native-crash.log` e, quando possibile, `crash-*.dmp` nella cartella `config`; un avviso compare al successivo avvio.

Per il debug installare [CodeLLDB 1.12.3](https://github.com/vadimcn/codelldb/releases/tag/v1.12.3), anche come estensione di Visual Studio Code. Gli strumenti Rust e CodeLLDB sono esterni alla distribuzione. La IDE rileva `%USERPROFILE%\.cargo\bin`, `$CARGO_HOME/bin`, PATH e CodeLLDB nelle estensioni di VS Code. I percorsi, il debugger e l'edizione dei file singoli si configurano in **Strumenti → Opzioni → Rust → Strumenti del compilatore**. **Verifica strumenti** mostra le versioni rilevate.

## Uso

- **File singolo:** aprire o salvare un `.rs`, quindi Compila, Controlla, Esegui o Avvia debug Rust. Viene usato `rustc`, senza richiedere `Cargo.toml`; gli output risiedono in una cartella temporanea. Sono disponibili anche test, rustfmt, assembly e IR LLVM.
- **Progetto Cargo:** aprire `Cargo.toml` con **File → Apri progetto Cargo**, oppure creare un progetto Rust. Sono disponibili build, check, run, test, Clippy, fmt, documentazione, benchmark e download delle dipendenze.
- **Opzioni progetto/file:** selezionare profilo Debug/Release, package, target, feature, piattaforma, argomenti, ambiente e cartella di esecuzione. Per un file singolo si applicano le opzioni rustc, profilo, piattaforma e parametri di esecuzione.
- **Analisi:** rust-analyzer legge anche le modifiche non salvate. **Ctrl+Spazio** apre il completamento; Invio/Tab confermano, Esc chiude, **F12** apre la definizione. Il menu Refactoring offre riferimenti, rinomina con anteprima, azioni Rust ed espansione delle macro; il pannello Struttura mostra i simboli. I suggerimenti sulle chiamate mostrano firma e documentazione.
- **Snippet e template:** **Ctrl+Alt+Spazio** inserisce uno snippet Rust e Tab passa tra i campi. Le raccolte e il template Rust sono modificabili nelle preferenze dell'editor.
- **Debug:** impostare breakpoint con un clic nel margine, dal menu contestuale o con **Ctrl+F4**, poi avviare il debug. **Ctrl+F5 / Esegui fino al cursore** avvia la compilazione e il debug fino alla riga selezionata oppure prosegue una sessione ferma; altri breakpoint possono fermare prima il programma. Le righe senza codice eseguibile possono essere spostate dal debugger alla riga utile successiva o rifiutate, con un messaggio. Dopo avere modificato i sorgenti riavviare il debug per ricompilare. Il pannello Rust offre thread, stack, variabili, watch, modifica dei valori, input del programma, memoria, disassemblato e comandi LLDB. Sono disponibili passi, continua, pausa, breakpoint condizionali, hit condition, logpoint, breakpoint sulle funzioni e watchpoint sulle variabili supportate dal debugger.
- **Git:** configurare Git nelle preferenze Strumenti. Il menu permette repository, staging, commit, branch, merge, cronologia, remoti, fetch, pull e push. Git deve essere installato separatamente.

Le diagnostiche del compilatore sono navigabili nel pannello Problemi; la console accetta input e permette di interrompere i processi. **Progetto Rust → Pulisci artefatti Cargo** mostra la cartella effettiva e sposta gli artefatti nel Cestino dopo conferma; rifiuta cartelle con sorgenti, documenti aperti o collegamenti e cambiamenti successivi all'anteprima. Il recupero periodico si configura nelle preferenze Interfaccia e area di lavoro.

Per il debug passo passo preferire il profilo **Debug**: le ottimizzazioni Release possono eliminare variabili o rendere alcune righe non raggiungibili separatamente.

## Requisiti e limiti

Windows 10 versione 1809 o successiva, oppure Windows 11, x64; Rust 1.85+ per l'edizione 2024. La distribuzione Windows è verificata con Qt 6.11.2, Rust/rust-analyzer 1.98.0 e CodeLLDB 1.12.3. La versione Linux è rimandata; si conservano architettura multipiattaforma e risorse desktop per una futura ripresa.

L'indicizzazione iniziale può richiedere tempo. Se rust-analyzer si arresta, la IDE effettua tentativi di riavvio limitati; **Strumenti → Riavvia rust-analyzer** consente di riprovare. Sono analizzati fino a 128 documenti, 8 MiB di testo UTF-16 per documento e 64 MiB complessivi; questi limiti non impediscono di aprire o compilare file più grandi. Le diagnostiche del server sono nell'editor; build/check si eseguono esplicitamente.

Rinomina e azioni modificano i buffer con anteprima e annullamento; il salvataggio resta esplicito. Le operazioni LSP che creano, rinominano o eliminano file e i comandi arbitrari del server sono rifiutati. Gli snippet LSP supportano campi semplici, valori predefiniti e scelte; trasformazioni, campi annidati e riferimenti ripetuti non sono espansi. La formattazione usa Cargo/rustfmt sui file salvati.

Output massimo 16 MiB per comando e 10.000 righe nella console. L'ispezione delle variabili avviene a pagine di 200 elementi, fino a 20.000 per contenitore; la cache è limitata a 32 MiB. La console di esecuzione non è un emulatore di terminale; il debugger Windows usa ConPTY. Per Rust si usano i test Cargo/rustc; i pannelli per gare C/C++ non sono esposti.

## Compilazione

Richiede CMake, Ninja, C++17 e un kit Qt 6.8+ compatibile con il compilatore C++. Componenti Qt: Core, Concurrent, Gui, LinguistTools, Network, PrintSupport, Svg, Test, Widgets, Xml.

```powershell
tools/build-rust-windows.ps1 -QtRoot <kit-Qt> -CompilerRoot <MinGW>
```

`tools/package-rust.py` completa DLL e licenze. Per eseguire i test configurare CMake con `BUILD_TESTING=ON`, compilare `all-test-targets` ed eseguire `ctest --output-on-failure`. Le prove del debugger richiedono CodeLLDB; `REDPANDA_TEST_CODELLDB` ne può indicare il percorso. `REDPANDA_UI_CHECKS=ON` abilita esclusivamente la build dedicata ai controlli grafici; i pacchetti di produzione lo disabilitano.

## Copyright

Il codice originale RedPanda-CPP/QSynEdit è di Roy Qu (`royqh1979`) e dei contributori. Le modifiche del fork rmele e l'adattamento Rust recano **Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)**. GPL-3.0-or-later. Provenienza, parti riutilizzate e dipendenze sono documentate in [ATTRIBUTIONS.md](ATTRIBUTIONS.md); gli avvisi originali sono conservati.
