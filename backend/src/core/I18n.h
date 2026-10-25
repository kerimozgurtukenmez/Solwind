#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <functional>

// The UI language, as services/I18n.qml does it (SPEC.md I-1): strings in the code are English; translations come
// from the same services/translations.js the QML app uses (built into the daemon), key = the English text.
// Language: the saved choice (settings ui.lang) if any, else the system locale (tr* -> Turkish, else English).
class I18n
{
public:
    I18n();

    // The saved choice ("tr" | "en" | "" = follow the system), read each time a text is made
    std::function<QString()> savedLanguage;

    QString lang() const;                                  // "tr" | "en"
    QString t(const QString &s) const { return tIn(lang(), s); }
    QString tIn(const QString &lang, const QString &s) const;
    QString num(double x, int digits) const;               // Turkish decimal comma: 1,5
    static QStringList languages() { return { QStringLiteral("tr"), QStringLiteral("en") }; }
    static QString languageName(const QString &id);

    // The dictionary of translations.js: language -> (English -> translation). Empty if it couldn't be read.
    const QHash<QString, QHash<QString, QString>> &dict() const { return m_dict; }
    // Parses translations.js (`var dict = { tr: { "key": "value", ... }, ... }`: comments, quoted strings with JS
    // escapes). false + why if the text isn't in that shape.
    static bool parse(const QString &js, QHash<QString, QHash<QString, QString>> *dict, QString *why);

private:
    QHash<QString, QHash<QString, QString>> m_dict;
};
