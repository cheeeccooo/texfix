#pragma once
#include <QByteArray>
#include <QString>

// Letture e scritture dei file del progetto. Ogni scrittura è sicura: o il file nuovo intero, o quello di prima.
namespace tx::files {

QString read(const QString &path, bool *ok = nullptr);
bool write(const QString &path, const QString &text);
bool writeBytes(const QString &path, const QByteArray &data);

// Un nome libero accanto a `path`: «nome.pdf», poi «nome (2).pdf», «nome (3).pdf»…
QString freeName(const QString &path);
// Il nome di una fonte per ritrovarla: senza maiuscole, punteggiatura, estensione e « (2)». «notes_LabSS (1).pdf» e
// «notes_LabSS_1.pdf» sono la stessa. La regola sta solo qui.
QString bare(const QString &name);
// La cartella dei dati del programma (librerie, utilizzo, stime): `TEXFIX_DATI` nelle prove dell'interfaccia.
QString dataDir();
// L'impronta SHA-1 in esadecimale. Un oggetto per thread: QCryptographicHash::hash, con OpenSSL, ricarica il fornitore a
// ogni chiamata e costa cento volte di più (SPEC §3.7 [C]).
QByteArray sha1(const QByteArray &data);

}
