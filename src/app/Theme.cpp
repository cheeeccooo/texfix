#include "Theme.h"

#include <QGuiApplication>
#include <QStyleHints>

namespace {
const Theme::Colors LIGHT{
    .window = "#f3f3f1", .panel = "#f8f8f6", .surface = "#ffffff", .border = "#dcdcd8",
    .text = "#1f2328", .textMuted = "#5c6470", .textFaint = "#9aa1ab",
    .accent = "#2f6fde", .onAccent = "#ffffff", .hover = "#e9e9e5", .selected = "#dbe6fb",
    .good = "#2e9e5b", .warn = "#d99a15", .bad = "#d6453d", .idle = "#b4b9c0",
    .currentLine = "#f4f6fa", .selection = "#bcd4fb", .pdfBackdrop = "#e4e4e0",
    .synCommand = "#2458b8", .synEnvironment = "#8a3fb0", .synMath = "#19745a", .synComment = "#8b929c", .synBrace = "#a05a14",
};
const Theme::Colors DARK{
    .window = "#1e1f22", .panel = "#232427", .surface = "#18191b", .border = "#35373c",
    .text = "#e3e5e8", .textMuted = "#a3a9b3", .textFaint = "#6c727c",
    .accent = "#5b93f5", .onAccent = "#0f1114", .hover = "#2c2e33", .selected = "#2b3a57",
    .good = "#4cc27c", .warn = "#e6b043", .bad = "#f0665e", .idle = "#5d636c",
    .currentLine = "#1f2125", .selection = "#2f4a7a", .pdfBackdrop = "#121314",
    .synCommand = "#7fb0ff", .synEnvironment = "#c89bea", .synMath = "#6fd0ac", .synComment = "#737a85", .synBrace = "#e0a45c",
};
}

Theme::Theme()
{
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &Theme::apply);
    apply();
}

Theme *Theme::instance()
{
    static Theme *theme = new Theme;
    return theme;
}

Theme *Theme::create(QQmlEngine *, QJSEngine *)
{
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

void Theme::apply()
{
    const QByteArray forced = qgetenv("TEXFIX_TEMA");
    m_dark = forced == "scuro" || (forced != "chiaro" && QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
    m_colors = m_dark ? DARK : LIGHT;
    emit changed();
}

QColor Theme::readable(const QColor &color) const
{
    float h, s, l, a;
    color.getHslF(&h, &s, &l, &a);
    if (s < 0.08f)                                     // un grigio resta grigio
        return m_dark ? QColor::fromHslF(0, 0, 0.72f) : QColor::fromHslF(0, 0, 0.42f);
    return QColor::fromHslF(h, qMax(s, 0.55f), m_dark ? qMax(l, 0.70f) : qMin(l, 0.36f));
}
