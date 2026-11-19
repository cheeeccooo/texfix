#include "core/Prompts.h"

#include "core/Rules.h"

#include <QFileInfo>

namespace tx::prompts {

QString outline()
{
    return QStringLiteral(
        R"(You plan the conversion of scanned handwritten university lecture notes (Italian) into LaTeX. You get a thumbnail of every page, labelled P1..Pn.
Return only a JSON object:
{"title": "<Italian title for the lecture's \\section, at most 40 characters: the topic, never 'Lezione del ...'>",
 "date": "<the lecture's date as GG/MM/AAAA, from the first date written on the pages; empty if none>",
 "lesson": "<the lecture's number if written on the pages (\"Lezione 3\", \"Lez. 3\"), digits only; empty if none>",
 "skip": [{"page": k, "why": "..."}],
 "chunks": [{"pages": [k, ...], "subsections": ["<titles of the \\subsection that start in this chunk>"]}]}
Rules:
- Chunks are consecutive pages, 3 to 5 pages each (fewer only for the last one).
- Never cut a unit across chunks: a definition or theorem with its proof, an example or exercise with its solution, a figure with the text that explains it. If a unit spans pages, keep those pages in one chunk, even if it grows to 6.
- Every page is in exactly one chunk or in skip. Skip only blank pages.
- Subsection titles: short, Italian, from the notes' headings when present, otherwise descriptive. The first chunk starts a subsection. Lecture dates written on the pages are not subsections, and no title names the lecture itself: titles say the content.)");
}

QString transcribe(const QString &file, const Settings &settings, const Style &style)
{
    const bool faithful = settings.writing == "fedele";
    const QString stem = QFileInfo(file).completeBaseName();
    const QStringList envs = style.boxes();
    const QString figure = style.figureWrapper();

    QString p = faithful
        ? QStringLiteral("You transcribe lecture notes (Italian) into LaTeX. Your output is one fragment of `%1`; the program concatenates "
                         "the fragments after a \\section it writes itself.\n\nThis is a one-to-one transcription: the author's content, "
                         "order and wording, and nothing of yours. Add no explanations, connecting sentences, conclusions, captions, "
                         "references or paragraph titles the source does not have; expand only the abbreviations needed to make a "
                         "grammatical sentence. When a step or a formula of the notes is cut off, complete it with what the mathematics "
                         "on the page requires, and nothing more.\n").arg(file)
        : QStringLiteral("You turn lecture notes (Italian) into LaTeX study notes. Your output is one fragment of `%1`; the program "
                         "concatenates the fragments after a \\section it writes itself.\n\nThe result is not a transcription: it is the "
                         "same content, in the same order, written so that a student can study from it without the notes in hand. The "
                         "EDITORIAL RULES below say how; they are binding.\n").arg(file);
    p += QStringLiteral(R"(
What the program needs (binding):
- Nothing is lost and nothing moves: every formula, step, label and remark of the notes is there, in the author's order. If a symbol is illegible, write your best reading and add a doubt.
- Errors in the notes: when the notes are wrong, write the correct version and right after it \begin{correction} ... \end{correction} (one or two sentences: what the notes say and why it does not hold). A correction you have made is NOT also listed among the doubts.
)");
    if (!faithful)
        p += QStringLiteral("- Added context goes in \\begin{addedtext} ... \\end{addedtext}, one to three sentences. Turning the notes' shorthand "
                            "into sentences is not added context and gets no mark.\n");
    p += QStringLiteral(R"(- Write \subsection{...} exactly for the titles given for your chunk, in order, where each starts. Never write \section or \subsubsection.
- The date that opens the notes is not written (the program writes it with \lezione). Only where another lecture starts in the middle of the notes, write \lezione{}{GG/MM/AAAA} on a line of its own.
- Use only the environments and macros of the DOCUMENT STYLE below; never invent macros. \begin and \end each on a line of their own.
- Labels: \label{<prefix>:%1-c<chunk>-<word>} (prefixes @PREFIXES@: each environment's own, listed in the DOCUMENT STYLE); <chunk> is the chunk number given in the request. Refer only to labels you define in this fragment.
- The author's underlines become \textbf{...} or plain text; never \underline.
- Figures: never draw them. For each drawing or graph write exactly
    \begin{%2} % img F<k>
        \imgslot[<w>\linewidth]{F<k>}{0.6}{<short plain description>}
    \end{%2}
  with k = 1, 2, ... in this fragment and w between 0.5 and 0.95, where the figure belongs.
- No file-header comment.

Output exactly two fenced blocks and nothing else:
```json
{"units": [{"env": "<one of %3, or text>", "title": "...", "page": <k>}],
 "figures": [{"id": "F1", "page": <k>, "desc": "...", "box": [x0, y0, x1, y1]}],
 "doubts": [{"page": <k>, "what": "...", "latex": "...", "readings": [{"text": "...", "latex": "..."}, {"text": "...", "latex": "..."}]}]}
```
```latex
<the fragment>
```
"units": one entry per top-level environment of the list you open (nested ones excluded), plus "text" for significant plain passages, in order. "page" is the P number of the page. "box": the figure's position on its page as fractions 0-1, origin top-left, generous. "doubts": only what a person must decide looking at the source, nothing you could settle from the context or the mathematics. "what": the question, in Italian, at most 15 words. "latex": a short excerpt of your fragment, copied verbatim (at most one line, unique in the fragment), that contains your reading. "readings": 2 or 3 possible readings in Italian, yours first; each with "latex", the same excerpt as it would be written with that reading (for yours, identical to "latex").
)").arg(stem, figure, envs.join(", ")).replace("@PREFIXES@", style.prefixes().join(", "));

    const QStringList sections = faithful ? QStringList{"notazione", "riquadri", "sorgente", "larghezza"}
                                          : QStringList{"scrittura", "aggiunte", "riquadri", "notazione", "rimandi", "sorgente", "larghezza"};
    p += QStringLiteral("\nEDITORIAL RULES\n\n") + rules::forPrompt(sections, settings.rules);
    p += QStringLiteral("\n\nDOCUMENT STYLE\n\n") + style.forPrompt();
    return p;
}

QString verify(const Settings &settings)
{
    QString p = QStringLiteral(
        R"(You check LaTeX study notes written from lecture notes against the source (pages labelled P<k>, or the source text). The LaTeX is not a literal transcription: it says the same things in complete sentences, writes out skipped steps, and may add short clarifications. Figures appear as \imgslot placeholders: only check that every drawing has one.
Report only real problems:
- "missing": content of the source that is not in the LaTeX (a formula, a step, a sentence's meaning, a remark, a whole unit);
- "wrong": something that contradicts the source or is mathematically false: a different formula, sign, index, exponent or condition; a written-out step or a clarification that is incorrect; a `correction` that is itself wrong or that "corrects" something the notes had right; an `addedtext` that states facts, names, dates or numbers not supported by the notes or by the mathematics on the page.
Do not report: rewording, expanded sentences, written-out steps and clarifications that are correct, style, choice of environment, layout, labels and references, content from pages you were told to skip. Each "what" is one short line.
Return only JSON: {"missing": [{"page": k, "what": "..."}], "wrong": [{"page": k, "what": "...", "fix": "..."}]})");
    if (settings.writing == "fedele")
        p += QStringLiteral("\nThis is a strict one-to-one transcription: also report as \"wrong\" any sentence, explanation or caption that is "
                            "not in the source (corrections of errors and completed truncated steps are allowed).");
    return p;
}

}

namespace tx::prompts {

QString outlineTex()
{
    return QStringLiteral(
        R"(You plan the conversion of one lecture of LaTeX study notes, written by another author, into the LaTeX of a notes project. You get the lecture's LaTeX with its line numbers (N lines in all).
Return only a JSON object:
{"title": "<Italian title for the lecture's \\section, at most 40 characters: the topic of the lecture, never 'Lezione del ...'>",
 "chunks": [{"from": <first line>, "to": <last line>, "subsections": ["<titles of the \\subsection that start in this chunk>"]}]}
Rules:
- The chunks are consecutive line ranges that cover every line from 1 to N exactly once, 120 to 300 lines each (fewer only for the last one).
- Cut only between the author's units (between subsections or paragraphs): never inside an environment (a statement with its proof, an example or exercise with its solution, a figure, an align).
- Subsection titles come from the author's \subsection titles, rephrased only when they do not say the content. Titles about the lecture itself ("Riepilogo", "Richiamo", "Lezione del ...") are not subsections: their content becomes text of the neighbouring subsection.)");
}

QString texSource(const Style &style)
{
    return QStringLiteral(R"(

THE SOURCE IS LATEX, NOT A SCAN. These rules override the ones above where they differ.
- The source is LaTeX written by another author: study notes of one lecture, given as SOURCE TEXT. There are no pages and no handwriting: wherever the rules above speak of pages or readings, read the source text; in the inventory every unit has "page": 1.
- Write it in the conventions of this project: the author's theorem-like environments become the matching environments of the DOCUMENT STYLE, the author's macros (listed, commented, at the top of the source) become the notation of this project, the author's references become references to labels you define in your fragment. Never copy the author's preamble definitions.
- The author's errors are corrected and declared as usual. A doubt is about meaning (an ambiguous statement, a formula that cannot be right), never about reading.
- FIGURES (this overrides the figure rule above): keep the author's code. For each figure of the source write
    \begin{%1}
        <the author's tikzpicture or circuitikz environment, the same drawing>
    \end{%1}
  without the author's \begin{figure}, \centering and \caption wrappers (a caption the text needs stays as a caption after the drawing). A circuit declares `% package: circuitikz` on its own line inside the tikzpicture. Do not redraw or simplify the drawing. Never write \imgslot, and leave "figures" empty in the inventory.)").arg(style.figureWrapper());
}

QString texVerify()
{
    return QStringLiteral("\nThe source is the LaTeX text above (another author's notes), not scans: compare the transcription with that text. Its "
                          "figures are kept as the author's code: only check that every figure of the source is there.");
}

}

namespace tx::prompts {
QString title()
{
    return QStringLiteral("You get the beginning of the LaTeX notes of one university lecture (Italian). Return only a JSON object: "
                          "{\"title\": \"<Italian title for the lecture's \\\\section, at most 40 characters: the topic, never 'Lezione del ...'>\"}");
}
}
