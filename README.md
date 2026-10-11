# Red Panda Rust

IDE desktop per Rust derivata da [RedPanda-CPP di Raffaele Mele](https://github.com/lellomele/RedPanda-CPP), con interfaccia Qt e editor QSynEdit originali.

## Installazione e uso

Estrarre il pacchetto Windows e avviare `RedPandaRust.exe`. Rust e Cargo devono essere installati tramite rustup; la IDE cerca Cargo anche in `%USERPROFILE%\.cargo\bin` e `$CARGO_HOME/bin`.

Per un file singolo, aprire o salvare un `.rs` e usare Compila, Controlla o Esegui: la IDE usa direttamente `rustc`, senza richiedere `Cargo.toml`. I binari vengono creati in una cartella temporanea separata dai sorgenti. Sono disponibili anche test e formattazione del file.

I file singoli usano per impostazione predefinita l’edizione Rust 2024, che richiede Rust 1.85 o successivo. L’edizione e i percorsi di Cargo, rustc e rustfmt sono configurabili in **Strumenti → Opzioni → Rust → Strumenti del compilatore**. Lasciare i percorsi vuoti per il rilevamento automatico; **Verifica strumenti** mostra le versioni disponibili. Per dipendenze esterne e Clippy usare un progetto Cargo.

Per un progetto, aprire `Cargo.toml` dal menu **File → Apri progetto Cargo**, oppure creare un progetto con **Nuovo progetto Rust**. La IDE usa Cargo automaticamente per i `.rs` appartenenti a un progetto. Sono disponibili compilazione, controllo, esecuzione, test, Clippy e formattazione. Le diagnostiche appaiono nella tabella originale della IDE; la console accetta input e consente di interrompere il programma.

## Analisi e completamento Rust

Installare i componenti con `rustup component add rust-analyzer rust-src`. Aprendo un file Rust salvato, rust-analyzer analizza anche il testo non ancora salvato: gli errori sono segnalati nell’editor e passando il mouse sui simboli vengono mostrati tipi e documentazione. Il completamento si apre durante la digitazione o con **Ctrl+Spazio**; scegliere con le frecce e confermare con **Invio** o **Tab**, oppure chiudere con **Esc**. Le modifiche del completamento, incluse eventuali importazioni nello stesso documento, sono annullabili con **Ctrl+Z**. **F12** apre la definizione del simbolo.

Sono supportati progetti Cargo e file `.rs` singoli, senza creare `Cargo.toml`. I file nuovi devono essere salvati una prima volta. Il percorso di rust-analyzer è configurabile in **Strumenti → Opzioni → Rust → Strumenti del compilatore**; dopo aver cambiato toolchain, percorsi o edizione dei file singoli usare **Strumenti → Riavvia rust-analyzer**. La barra di stato segnala avvio, indicizzazione ed errori. L’indicizzazione iniziale di progetti grandi può richiedere tempo. Se il server si arresta, la IDE prova a riavviarlo con un’attesa progressiva e sospende i tentativi dopo tre errori ravvicinati; si può continuare a modificare e compilare anche quando l’analisi non è disponibile.

## Requisiti e limiti

Windows 10/11 x64; toolchain Rust funzionante (MSVC con gli strumenti C++ Microsoft, oppure GNU con il relativo linker). I progetti multi-pacchetto usano le impostazioni predefinite di Cargo; la selezione di target, features e argomenti non è ancora disponibile. Il debugger Rust non è integrato in questa anteprima. L’analisi semantica richiede rust-analyzer e, per la libreria standard, rust-src; toolchain e componenti sono installati separatamente tramite rustup. Sono analizzati fino a 128 documenti, con massimo 8 MiB di testo UTF-16 per documento e 64 MiB complessivi; il limite non impedisce di aprire o compilare file più grandi. Non sono supportati snippet LSP, refactoring o modifiche automatiche su più file. Le diagnostiche di rust-analyzer sono separate dalla tabella dei risultati di compilazione; i controlli Cargo si eseguono con Compila/Controlla. La formattazione modifica i file salvati tramite Cargo/rustfmt e viene rilevata dal controllo delle modifiche esterne della IDE. Output massimo 16 MiB per comando e 10.000 righe nella console. La console non è un emulatore di terminale.

## Compilazione

Richiede CMake, Ninja, C++17 e Qt 6.8+ (Core, Concurrent, Gui, LinguistTools, Network, PrintSupport, Svg, Test, Widgets, Xml). Eseguire `tools/build-rust-windows.ps1 -QtRoot <kit-Qt> -CompilerRoot <MinGW>`. Il kit Qt e MinGW devono essere compatibili. La distribuzione delle DLL e delle relative licenze è completata da `tools/package-rust.py`.

## Copyright

Il codice originale RedPanda-CPP/QSynEdit è di Roy Qu (`royqh1979`) e dei contributori. Le modifiche del fork rmele e l’adattamento Rust recano **Copyright © 2026 Ing. prof. Raffaele Mele (lellomele@gmail.com)**. GPL-3.0-or-later. La distinzione tra le parti e le licenze delle dipendenze è documentata in [ATTRIBUTIONS.md](ATTRIBUTIONS.md); gli avvisi originali sono conservati.
