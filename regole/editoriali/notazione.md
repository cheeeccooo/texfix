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
