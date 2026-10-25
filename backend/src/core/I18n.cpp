#include "I18n.h"

#include <QFile>
#include <QLocale>

using namespace Qt::StringLiterals;

namespace {

// A minimal reader for the subset of JavaScript translations.js is written in
class Lexer
{
public:
    explicit Lexer(const QString &s) : m_s(s) {}

    enum Kind { End, Ident, String, Punct, Bad };
    struct Token { Kind kind; QString text; };

    Token next()
    {
        skipSpaceAndComments();
        if (m_i >= m_s.size()) return { End, {} };
        const QChar c = m_s[m_i];
        if (c == u'"' || c == u'\'') return readString(c);
        if (c.isLetter() || c == u'_' || c == u'$') {
            const qsizetype a = m_i;
            while (m_i < m_s.size() && (m_s[m_i].isLetterOrNumber() || m_s[m_i] == u'_' || m_s[m_i] == u'$')) ++m_i;
            return { Ident, m_s.mid(a, m_i - a) };
        }
        if (QStringView(u"{}:,;=").contains(c)) { ++m_i; return { Punct, QString(c) }; }
        return { Bad, QString(c) };
    }

    // `.pragma library` (a QML directive, the whole line)
    bool skipPragma()
    {
        skipSpaceAndComments();
        if (!m_s.mid(m_i).startsWith(u".pragma"_s)) return true;
        const qsizetype nl = m_s.indexOf(u'\n', m_i);
        m_i = nl < 0 ? m_s.size() : nl + 1;
        return true;
    }

private:
    void skipSpaceAndComments()
    {
        for (;;) {
            while (m_i < m_s.size() && m_s[m_i].isSpace()) ++m_i;
            if (m_s.mid(m_i, 2) == u"//"_s) {
                const qsizetype nl = m_s.indexOf(u'\n', m_i);
                m_i = nl < 0 ? m_s.size() : nl + 1;
            } else if (m_s.mid(m_i, 2) == u"/*"_s) {
                const qsizetype e = m_s.indexOf(u"*/"_s, m_i + 2);
                m_i = e < 0 ? m_s.size() : e + 2;
            } else {
                return;
            }
        }
    }

    Token readString(QChar quote)
    {
        QString out;
        ++m_i;
        while (m_i < m_s.size()) {
            const QChar c = m_s[m_i++];
            if (c == quote) return { String, out };
            if (c == u'\n') return { Bad, u"new line in a string"_s };
            if (c != u'\\') { out += c; continue; }
            if (m_i >= m_s.size()) break;
            const QChar e = m_s[m_i++];
            switch (e.unicode()) {
            case 'n': out += u'\n'; break;
            case 't': out += u'\t'; break;
            case 'r': out += u'\r'; break;
            case 'b': out += u'\b'; break;
            case 'f': out += u'\f'; break;
            case 'v': out += u'\v'; break;
            case '0': out += QChar(0); break;
            case '\n': break;                                       // line continuation
            case 'x': case 'u': {
                if (e == u'u' && m_i < m_s.size() && m_s[m_i] == u'{') {
                    const qsizetype close = m_s.indexOf(u'}', m_i);
                    bool ok = false;
                    const uint cp = close < 0 ? 0 : m_s.mid(m_i + 1, close - m_i - 1).toUInt(&ok, 16);
                    if (!ok || cp > 0x10FFFF) return { Bad, u"bad \\u{} escape"_s };
                    out += QString::fromUcs4(reinterpret_cast<const char32_t *>(&cp), 1);
                    m_i = close + 1;
                    break;
                }
                const int n = e == u'x' ? 2 : 4;
                bool ok = false;
                const uint v = m_s.mid(m_i, n).toUInt(&ok, 16);
                if (!ok || m_s.mid(m_i, n).size() != n) return { Bad, u"bad escape"_s };
                out += QChar(char16_t(v));
                m_i += n;
                break;
            }
            default: out += e;                                      // \" \' \\ and any other character itself
            }
        }
        return { Bad, u"unterminated string"_s };
    }

    const QString &m_s;
    qsizetype m_i = 0;
};

} // namespace

bool I18n::parse(const QString &js, QHash<QString, QHash<QString, QString>> *dict, QString *why)
{
    Lexer lx(js);
    lx.skipPragma();
    auto fail = [&](const QString &w) { if (why) *why = w; return false; };
    auto expect = [&](Lexer::Kind k, const QString &text) {
        const Lexer::Token t = lx.next();
        return t.kind == k && (text.isEmpty() || t.text == text);
    };
    if (!expect(Lexer::Ident, u"var"_s) || !expect(Lexer::Ident, u"dict"_s) || !expect(Lexer::Punct, u"="_s)
        || !expect(Lexer::Punct, u"{"_s))
        return fail(u"expected `var dict = {`"_s);
    QHash<QString, QHash<QString, QString>> out;
    for (;;) {
        Lexer::Token t = lx.next();
        if (t.kind == Lexer::Punct && t.text == u"}"_s) break;
        if (t.kind != Lexer::Ident && t.kind != Lexer::String) return fail(u"expected a language"_s);
        const QString lang = t.text;
        if (!expect(Lexer::Punct, u":"_s) || !expect(Lexer::Punct, u"{"_s)) return fail(u"expected `: {` after "_s + lang);
        QHash<QString, QString> &d = out[lang];
        for (;;) {
            Lexer::Token k = lx.next();
            if (k.kind == Lexer::Punct && k.text == u"}"_s) break;
            if (k.kind != Lexer::String && k.kind != Lexer::Ident) return fail(u"expected a key in "_s + lang);
            if (!expect(Lexer::Punct, u":"_s)) return fail(u"expected `:` after "_s + k.text);
            const Lexer::Token v = lx.next();
            if (v.kind != Lexer::String) return fail(u"expected a string for "_s + k.text);
            d.insert(k.text, v.text);                           // a key given twice: the last one (as in JS)
            Lexer::Token sep = lx.next();
            if (sep.kind == Lexer::Punct && sep.text == u"}"_s) break;
            if (!(sep.kind == Lexer::Punct && sep.text == u","_s)) return fail(u"expected `,` after "_s + k.text);
        }
        Lexer::Token sep = lx.next();
        if (sep.kind == Lexer::Punct && sep.text == u"}"_s) break;
        if (!(sep.kind == Lexer::Punct && sep.text == u","_s)) return fail(u"expected `,` after language "_s + lang);
    }
    *dict = out;
    return true;
}

I18n::I18n()
{
    QFile f(u":/translations.js"_s);
    QString why;
    if (!f.open(QIODevice::ReadOnly)) qWarning("solwindd: translations.js is not built in: texts stay English");
    else if (!parse(QString::fromUtf8(f.readAll()), &m_dict, &why)) qWarning("solwindd: translations.js: %s", qPrintable(why));
}

QString I18n::lang() const
{
    const QString saved = savedLanguage ? savedLanguage() : QString();
    if (saved == u"tr"_s || saved == u"en"_s) return saved;
    // Qt.locale().name.startsWith("tr"): the same QLocale the QML engine uses
    return QLocale().name().startsWith(u"tr"_s) ? u"tr"_s : u"en"_s;
}

QString I18n::tIn(const QString &lang, const QString &s) const
{
    if (lang == u"en"_s) return s;
    const auto d = m_dict.constFind(lang);
    if (d == m_dict.cend()) return s;
    const auto v = d->constFind(s);
    return v == d->cend() ? s : *v;
}

QString I18n::num(double x, int digits) const
{
    // Number(x).toFixed(digits), with a comma in Turkish
    QString s = QString::number(x, 'f', digits);
    return lang() == u"tr"_s ? s.replace(u'.', u',') : s;
}

QString I18n::languageName(const QString &id)
{
    return id == u"tr"_s ? u"Türkçe"_s : id == u"en"_s ? u"English"_s : id;
}
