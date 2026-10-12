# Confronto C++ RustTextEdits

È stata selezionata la candidata C++: applicazione controllata con costruzione in una passata e percorso specifico per una singola modifica. Il riferimento è la funzione della release 1.0.0-beta.1, conservata soltanto nei test.

## Tempi misurati

Release -O3, Qt 6.11.2, GCC 16.1.0. Tre giri, 7 campioni alternati A/B per giro: 21 campioni per caso e modalità. Dati e piano preparati fuori dalla misura; output consumato per evitare eliminazioni del compilatore. Ogni modalità ha 3 operazioni iniziali di riscaldamento per implementazione. Nessuna compilazione concorrente durante i giri finali.

La colonna percorso completo include validazione JSON, pianificazione e applicazione; esclude rust-analyzer, anteprima e aggiornamento dell’editor. Tempi in microsecondi.

| Caso | Unità UTF-16 / modifiche | Applicazione A / B (µs) | Percorso completo A / B (µs) | Fattore A/B completo |
|---|---:|---:|---:|---:|
| empty-plan | 1024 / 0 | 0.011 / 0.013 | 0.431 / 0.435 | 0.99× |
| small-grow | 64 / 1 | 0.063 / 0.073 | 1.349 / 1.383 | 0.98× |
| small-equal | 64 / 1 | 0.063 / 0.071 | 1.364 / 1.384 | 0.99× |
| small-multiple | 1024 / 10 | 0.926 / 0.291 | 13.522 / 12.546 | 1.08× |
| medium-grow | 262144 / 100 | 10140.700 / 37.000 | 13727.500 / 296.700 | 46.27× |
| original | 225609 / 115 | 9342.100 / 109.400 | 7116.000 / 402.800 | 17.67× |
| large-grow | 262144 / 1000 | 59875.600 / 162.000 | 60910.500 / 1826.200 | 33.35× |
| large-shrink | 262144 / 1000 | 27708.700 / 40.500 | 32131.200 / 1548.200 | 20.75× |
| large-equal | 262144 / 1000 | 73.147 / 38.796 | 1551.750 / 1503.150 | 1.03× |
| limit-single | 4194304 / 1 | 1987.500 / 2078.820 | 3772.450 / 3971.250 | 0.95× |
| ten-thousand | 65536 / 10000 | 34444.200 / 166.600 | 48523.800 / 14661.200 | 3.31× |

## Memoria

Picco di memoria impegnata dell’intero processo Windows (PeakPagefileUsage), misurato in processi nuovi con 12 risultati contemporaneamente mantenuti. Include framework e dati di prova; non è una misura delle sole allocazioni della funzione. Sono state eseguite 3 ripetizioni per ogni coppia caso/implementazione.

| Caso | A (MiB) | B (MiB) |
|---|---:|---:|
| original | 40.43 | 39.65 |
| large-grow | 44.43 | 41.87 |
| large-equal | 41.75 | 41.75 |
| limit-single | 137.98 | 137.97 |
| ten-thousand | 49.01 | 48.77 |

## Robustezza e resilienza

12 controlli del componente superati: 20.000 casi generati con seme riproducibile confrontati con il riferimento, 5.000 rifiuti/retry di piani falsificati, 1.000 verifiche JSON invalido, Unicode, inserimenti nello stesso punto, cancellazione completa, limiti di dimensione e alias tra sorgente/output. Verificata anche l’applicazione su QSynEdit, con undo/redo e rifiuto di un piano invalido senza modifica dell’editor.

Le eccezioni di allocazione sono simulate nei punti di preparazione e applicazione: verificati output precedente conservato, sorgente invariato ed esecuzione successiva riuscita. Non è stato provocato l’esaurimento fisico della memoria del PC. Il confronto non certifica l’assenza di tutti i difetti.

La produzione usa applyChecked: in caso di errore RustNavigation rifiuta l’operazione prima di modificare gli editor. Le nuove verifiche hanno un costo nei casi minimi; i valori e i rallentamenti sono riportati nella tabella e nei campioni grezzi.

## Riproduzione

Compilare rust-text-edit-checks e rust-text-edit-benchmark in Release. Impostare RUST_EDIT_FIXTURE sul file di prova Rust, poi eseguire il benchmark tre volte senza altri test o compilazioni concorrenti. Per la memoria: rust-text-edit-benchmark --memory <caso> baseline oppure candidate, in processi nuovi.

Due giri iniziali su carichi estremi sono stati interrotti, uno dopo un’ispezione con debugger: entrambi esclusi dalle misure pubblicate. Il massimo di 10.000 modifiche e il limite di 4.194.304 unità UTF-16 sono misurati in scenari distinti; la combinazione simultanea dei due massimi non è riportata come benchmark completato.

Non sono stati conteggiati direttamente malloc/realloc e copie interne di Qt. Non è stato misurato in questo confronto il tempo complessivo dell’interfaccia grafica. Questi risultati riguardano questo modulo, hardware e compilatori.
