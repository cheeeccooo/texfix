**Si scrive già stretto.** Una formula in display che supera la larghezza della colonna, o una catena di tre o più
uguaglianze, va subito in `align*`, una relazione per riga allineata su `&=`. Nel dubbio, si spezza.

La matematica nel testo non va mai a capo: una `$...$` che non entra esce dal margine. Va portata in display, o
divisa in più `$...$` separate da parole.

Che cosa funziona, in quest'ordine:

1. riformulare perché la parola che non entrava entri;
2. portare in display una `$...$` lunga;
3. spezzare il paragrafo; da solo non basta quasi mai, va unito alla riformulazione.

Vietati: font più piccoli, `\resizebox` sul testo, `\!` a catena, `\hfuzz`, `\hbadness` e ogni ritocco alle
tolleranze.

**Quando una riga è fuori misura**

- La causa è quasi sempre una formula: l'ultima riga prima di un display viene giustificata come una riga qualsiasi,
  e se è corta si stira. L'avviso si cerca dove il testo incontra una formula, non dove finisce il paragrafo.
- Spezzare una frase densa in paragrafi più corti; altrimenti riscriverla più compatta (sinonimi più corti, struttura
  diversa) senza toccare il significato matematico.
- Fra le varianti che tolgono l'avviso si tiene la più fedele alla fonte.
- Display troppo larghi, in ordine: `align*` su più righe; testo fuori dalla matematica; `\tfrac` al posto di `\frac`
  in tuple e display larghi; `gather*` per formule indipendenti.
