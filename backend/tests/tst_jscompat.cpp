// JavaScript rules the settings validators depend on (SPEC.md ST-4). Expected values are what JS gives.
#include "core/JsCompat.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

class TstJsCompat : public QObject
{
    Q_OBJECT
private slots:
    void number_data()
    {
        QTest::addColumn<QJsonValue>("v");
        QTest::addColumn<double>("want");          // NaN = must be NaN
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        QTest::newRow("undefined") << QJsonValue(QJsonValue::Undefined) << nan;
        QTest::newRow("null") << QJsonValue(QJsonValue::Null) << 0.0;
        QTest::newRow("true") << QJsonValue(true) << 1.0;
        QTest::newRow("false") << QJsonValue(false) << 0.0;
        QTest::newRow("number") << QJsonValue(1.5) << 1.5;
        QTest::newRow("empty string") << QJsonValue(u""_s) << 0.0;
        QTest::newRow("spaces") << QJsonValue(u"  \t\n "_s) << 0.0;
        QTest::newRow("padded") << QJsonValue(u"  42  "_s) << 42.0;
        QTest::newRow("decimal") << QJsonValue(u"0.45"_s) << 0.45;
        QTest::newRow("leading dot") << QJsonValue(u".5"_s) << 0.5;
        QTest::newRow("trailing dot") << QJsonValue(u"5."_s) << 5.0;
        QTest::newRow("plus") << QJsonValue(u"+3"_s) << 3.0;
        QTest::newRow("minus") << QJsonValue(u"-3"_s) << -3.0;
        QTest::newRow("exponent") << QJsonValue(u"1e2"_s) << 100.0;
        QTest::newRow("hex") << QJsonValue(u"0x1F"_s) << 31.0;
        QTest::newRow("signed hex") << QJsonValue(u"-0x1F"_s) << nan;
        QTest::newRow("octal") << QJsonValue(u"0o17"_s) << 15.0;
        QTest::newRow("binary") << QJsonValue(u"0b101"_s) << 5.0;
        QTest::newRow("Infinity") << QJsonValue(u"Infinity"_s) << inf;
        QTest::newRow("-Infinity") << QJsonValue(u"-Infinity"_s) << -inf;
        QTest::newRow("infinity lowercase") << QJsonValue(u"infinity"_s) << nan;
        QTest::newRow("comma decimal") << QJsonValue(u"1,5"_s) << nan;
        // The QML engine's own white space (measured, .dev/settingsdiff.sh): U+0085 yes, U+FEFF no
        QTest::newRow("NEL is space") << QJsonValue(u"\u00859"_s) << 9.0;
        QTest::newRow("BOM is not") << QJsonValue(u"\ufeff8"_s) << nan;
        QTest::newRow("trailing junk") << QJsonValue(u"12px"_s) << nan;
        QTest::newRow("NaN text") << QJsonValue(u"NaN"_s) << nan;
        QTest::newRow("too big") << QJsonValue(u"1e400"_s) << inf;
        QTest::newRow("too small") << QJsonValue(u"1e-400"_s) << 0.0;
        QTest::newRow("empty array") << QJsonValue(QJsonArray()) << 0.0;
        QTest::newRow("one number") << QJsonValue(QJsonArray{ 5 }) << 5.0;
        QTest::newRow("one string") << QJsonValue(QJsonArray{ u" 7 "_s }) << 7.0;
        QTest::newRow("one null") << QJsonValue(QJsonArray{ QJsonValue::Null }) << 0.0;
        QTest::newRow("nested") << QJsonValue(QJsonArray{ QJsonArray{ 3 } }) << 3.0;
        QTest::newRow("two numbers") << QJsonValue(QJsonArray{ 1, 2 }) << nan;
        QTest::newRow("object") << QJsonValue(QJsonObject{ { u"a"_s, 1 } }) << nan;
    }
    void number()
    {
        QFETCH(QJsonValue, v);
        QFETCH(double, want);
        const double got = js::number(v);
        if (std::isnan(want)) QVERIFY2(std::isnan(got), qPrintable(QString::number(got)));
        else QCOMPARE(got, want);
    }

    void round()
    {
        QCOMPARE(js::round(2.5), 3.0);
        QCOMPARE(js::round(-2.5), -2.0);       // JS rounds halves up, std::round away from zero
        QCOMPARE(js::round(0.49), 0.0);
        QCOMPARE(js::round(4.5), 5.0);
        QVERIFY(std::isnan(js::round(std::nan(""))));
    }

    void num()
    {
        QCOMPARE(js::num(QJsonValue(150), 0, 100), 100.0);
        QCOMPARE(js::num(QJsonValue(-5), 0, 100), 0.0);
        QCOMPARE(js::num(QJsonValue(QJsonValue::Null), 70, 100), 70.0);     // null -> 0 -> clamped
        QVERIFY(std::isnan(js::num(QJsonValue(u"Infinity"_s), 0, 100)));  // not finite -> NaN, not clamped
        QVERIFY(std::isnan(js::num(QJsonValue(QJsonValue::Undefined), 0, 1)));
    }

    void truthyAndGet()
    {
        QVERIFY(!js::truthy(QJsonValue(0)));
        QVERIFY(!js::truthy(QJsonValue(u""_s)));
        QVERIFY(js::truthy(QJsonValue(u"0"_s)));
        QVERIFY(js::truthy(QJsonValue(QJsonArray())));
        // p && p.t
        QCOMPARE(js::andGet(QJsonValue::Null, u"t"_s), QJsonValue(QJsonValue::Null));
        QCOMPARE(js::andGet(QJsonValue(0), u"t"_s), QJsonValue(0));
        QCOMPARE(js::andGet(QJsonObject{ { u"t"_s, 40 } }, u"t"_s), QJsonValue(40));
        QCOMPARE(js::andGet(QJsonObject(), u"t"_s).type(), QJsonValue::Undefined);
        QCOMPARE(js::andGet(QJsonValue(5), u"t"_s).type(), QJsonValue::Undefined);
        QCOMPARE(js::andGet(QJsonArray{ 1 }, u"t"_s).type(), QJsonValue::Undefined);
    }

    void trim()
    {
        QCOMPARE(js::trim(u"\u00a0 name \n"_s), u"name"_s);
        QCOMPARE(js::trim(u"\ufeffA\u0085"_s), u"A"_s);          // trim() does remove U+FEFF
        QCOMPARE(js::trim(u"\u200bB"_s), u"\u200bB"_s);          // zero width space isn't white space
    }
};

QTEST_GUILESS_MAIN(TstJsCompat)
#include "tst_jscompat.moc"
