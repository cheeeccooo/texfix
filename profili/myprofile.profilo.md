# myprofile

## Come si scrive

Non è una trascrizione: è lo stesso contenuto, nello stesso ordine, scritto perché uno studente ci studi senza la
fonte davanti. Niente si perde e niente cambia posto; cambia il modo in cui è detto.

**Prosa che spiega**

- Gli appunti telegrafici («→ non si dovrebbe osservare», «quindi $R$ cost.») diventano frasi complete: che cosa si
  fa, perché, che cosa se ne conclude. Frecce, sigle e abbreviazioni si sciolgono in parole.
- Ogni formula è introdotta da una frase e, se non è ovvio, seguita da ciò che dice («cioè: …»).
- Una frase per idea: niente giri di parole, niente tono da manuale, niente ripetere ciò che la formula dice già.

**Ragionamenti chiusi, passaggi tutti scritti**

- Ogni esempio, stima o derivazione finisce con la sua conclusione in una frase: che cosa quel numero o quella
  formula dimostra, anche se la fonte si ferma al risultato.
- Tutti i passaggi della fonte restano; quelli che salta («si vede facilmente», «eliminando $\varphi$…») si scrivono.

**Gerarchia**

Sotto una sottosezione ogni tappa vera di un ragionamento lungo ha un `\paragraph{Titolo.}`: uno per tappa, non uno
per capoverso. Mai `\subsubsection`.

## Riquadri

**Testo prima, riquadri dopo**

Il testo semplice è la forma normale. Un riquadro si apre solo per un'unità con un ruolo preciso; il filo del
discorso, le motivazioni, i raccordi, il commento a un calcolo, i richiami e i riassunti restano testo. Se tutto è
in evidenza, niente risalta.

Si apre un riquadro solo per:

- una definizione, un assioma, un enunciato (teorema, proposizione, lemma, corollario) e la sua dimostrazione;
- un **esempio breve**: un caso concreto che illustra ciò che si è appena detto, in poche righe. Un «per esempio» di
  una frase resta testo;
- un **esercizio svolto**: un problema con i suoi dati e un enunciato proprio («Calcolare…»), risolto fino al
  risultato, con le sue figure. Un calcolo generale della teoria è testo con le sue formule;
- un'**osservazione** che sta in piedi da sola (l'«Oss.» o «N.B.» dell'autore, una conseguenza notevole, un caso
  limite, un legame con qualcosa già visto). Con parsimonia: mai per introdurre, motivare, collegare, commentare,
  richiamare, riassumere;
- un'**avvertenza**: poche e brevi (una o due frasi). Quanto è forte lo decide quanto costa sbagliare.

Nel dubbio fra esempio ed esercizio: con un enunciato proprio e uno svolgimento è un esercizio. Nel dubbio fra
osservazione e testo, o fra esempio e testo: testo. In una lezione di teoria buona parte delle righe è testo fuori
dai riquadri, e le osservazioni sono una ogni qualche pagina.

**Come si usano**

- Titolo opzionale, breve (finisce nell'indice).
- Mai iniziare il corpo di un riquadro con una formula in display: prima una frase che finisce con «:».
- I titoli lunghi vanno a capo da soli: non spezzarli né abbreviarli.

## Controlli

Si rifà ogni conto e si controllano formule note, segni, esponenti e unità.

Ciò che si può aggiungere: solo ciò che serve a capire il passaggio (un'ipotesi sottintesa, perché vale
un'approssimazione, il significato di un simbolo mai definito, il motivo di un limite di validità). Mai nomi, date,
valori o esperimenti che la fonte non riporta; mai risultati nuovi, mai digressioni.

## Notazione

- Le macro dello stile si usano sempre al posto della scrittura a mano (insiemi numerici, norme, operatori, vettori).
- Differenziali con lo spazio sottile prima: `\, dt`, `\, d\theta`.
- Tuple con lo spazio dopo la virgola: `(a\cos(t),\ b\sin(t),\ 0)`.
- Casi con `cases`: `\begin{cases} 1 & \text{se } … \end{cases}`, mai matrici improvvisate.
- Quantificatori staccati: `\quad \forall u \in …`.
- Argomenti delle funzioni trigonometriche fra parentesi: `\sin(t)`, `\cos^2(2t)`.
- Mai `\\` dentro `$...$`: si usa `align*`.
- Se la fonte scrive i vettori con la freccia o la sottolineatura si converte nella notazione dello stile: è
  tipografia, non contenuto.

**Il grassetto è un tipo, non un'enfasi**

Il grassetto dice «questo oggetto vive in $\mathbb{R}^n$, $n \ge 2$»: ogni grandezza vettoriale lo porta (vettori,
punti, curve, campi, matrici, versori), ogni scalare resta magro (componenti, parametri, indici, norme, operatori).

- Coerenza per oggetto: una curva introdotta in grassetto lo resta anche nelle derivate, nell'immagine e nelle norme.
- Le grandezze complesse restano magre anche quando $\mathbb{C}$ è identificato con $\mathbb{R}^2$.

**Formule larghe**

Una formula in display che supera la larghezza della colonna, o una catena di tre o più uguaglianze, va subito in
`align*`, una relazione per riga allineata su `&=`. Nel dubbio, si spezza. La matematica nel testo non va mai a capo:
una `$...$` che non entra esce dal margine. Va portata in display, o divisa in più `$...$` separate da parole.

- Una riga fuori misura viene quasi sempre da una formula: l'ultima riga prima di un display viene giustificata come
  una riga qualsiasi, e se è corta si stira. L'avviso si cerca dove il testo incontra una formula, non dove finisce
  il paragrafo.
- Display troppo larghi, in ordine: `align*` su più righe; testo fuori dalla matematica; `\tfrac` al posto di `\frac`
  in tuple e display larghi; `gather*` per formule indipendenti.

## Figure

- Etichette degli assi in matematica (`$x_1$`, `$t$`), mai sovrapposte a curve o assi.
- Pochi colori, sempre gli stessi: uno per la curva o l'elemento principale, uno per ciò che è secondario, uno per
  ciò che si mette in evidenza, uno per percorsi e risultati.
- Grafici di funzioni con pgfplots (la trigonometria vuole i gradi: `cos(deg(x))`); schizzi a tre dimensioni in
  assonometria.
- Un circuito si disegna con `circuitikz`: fili orizzontali e verticali, componenti centrati sul filo, due schemi
  equivalenti affiancati e della stessa misura. Mai simboli fatti a mano.

## Regole in più
