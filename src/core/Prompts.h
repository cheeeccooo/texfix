#pragma once
#include "core/Settings.h"
#include "core/Style.h"

// I prompt dei worker. Ognuno ha tre parti: quella strutturale (qui: ciò che il programma si aspetta di ricevere),
// le regole editoriali (Preferenze e progetto) e le convenzioni dello stile. Nessuna parte ripete le altre.
namespace tx::prompts {

QString outline();                         // la scaletta di una fonte a pagine
QString transcribe(const QString &file, const Settings &settings, const Style &style);
QString verify(const Settings &settings);

}
