# Red Panda Rust 1.0 — perimetro e criteri di consegna

La distribuzione 1.0 è dedicata a Windows x64. Linux è accantonato per il momento: non costituisce un requisito di rilascio né una piattaforma dichiarata verificata. Si preferiscono C++/Qt, CMake, protocolli LSP/DAP e strumenti Rust disponibili su più piattaforme. Il codice specifico di Windows resta isolato nei servizi per processi, terminale e Cestino, conservando risorse desktop e percorsi di compilazione per una futura ripresa Linux.

Priorità: robustezza, resilienza, velocità, in quest'ordine. Le prove devono esercitare i comandi effettivamente usati nell'interfaccia, comprese azioni di mouse e tastiera; una chiamata interna non sostituisce tale copertura.

## Criteri

- Breakpoint da margine, menu e tastiera, rimozione, proprietà, persistenza e spostamento con le righe.
- Esecuzione fino al cursore da sessione ferma e avvio diretto; verifica della destinazione accettata dal debugger; gestione di destinazioni rifiutate, richieste scadute e sorgenti modificati.
- Debug di file singoli e Cargo: stack, thread, variabili, watch, modifica valori, input, passi, pausa, continua, arresto e riavvio; condizioni, logpoint, funzioni, watchpoint ove supportati.
- Adattamento Rust dell'interfaccia, configurazione strumenti e messaggi comprensibili.
- Salvataggi atomici, recupero, refactoring annullabile, errori e annullamenti che conservano i documenti.
- Cargo workspace, più crate e target, esempi, test, feature, profili, percorsi Unicode e con spazi.
- Analisi e completamento rust-analyzer anche su testo non salvato, cambio progetto e riavvio del server.
- Test di guasto, file grandi, ripetizione e prestazioni misurate.
- Pacchetto di produzione con runtime proprio, attribuzioni, archivio sorgenti, checksum e istruzioni utente.
- Matrice delle funzioni rispetto alla base RedPanda C++ 3.5.1, con differenze e limiti Rust espliciti.

La dicitura **beta** resta appropriata fino alla validazione su sistemi Windows puliti e a una prova sul campo indipendente su esercizi e progetti reali. I risultati delle prove locali non equivalgono a tali verifiche.
