pragma Singleton
import Quickshell
import QtQuick
import Solwind
import "translations.js" as T

// UI language. Strings in the code are English; translations live in translations.js (key = the English text).
// Usage: I18n.t("Fan %1").arg(n). Bindings that call t() re-evaluate when the language changes,
// because t() reads `lang`.
Singleton {
    id: root

    readonly property var languages: [{ id: "tr", name: "Türkçe" }, { id: "en", name: "English" }]
    // The system language (Turkish if the locale is Turkish, else English)
    readonly property string system: Qt.locale().name.startsWith("tr") ? "tr" : "en"
    // The daemon's: its notifications, messages and profile names are in it too (saved choice, else its system's)
    readonly property string lang: Solwind.connected === true && ["tr", "en"].indexOf(Solwind.daemon.Language) >= 0 ? Solwind.daemon.Language : system

    function t(s) {
        if (lang === "en") return s
        const d = T.dict[lang]
        return d && d[s] !== undefined ? d[s] : s
    }
    // Translation into a specific language (e.g. to accept profile names in any language)
    function tIn(l, s) {
        if (l === "en") return s
        const d = T.dict[l]
        return d && d[s] !== undefined ? d[s] : s
    }
    // Decimal number in the UI language's style (Turkish uses a comma: 1,5)
    function num(x, digits) { const s = Number(x).toFixed(digits); return lang === "tr" ? s.replace(".", ",") : s }
    // Upper case in the UI language's rules (Turkish: i -> İ, ı -> I)
    function upper(s) { return lang === "tr" ? String(s).replace(/i/g, "İ").toUpperCase() : String(s).toUpperCase() }
    function setLang(id) { Settings.setUiLang(id) }
}
