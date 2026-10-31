// OpenRGB/openrgb against a server of the test's own, written from OpenRGB's SDK document: the handshake, the controller
// data of protocols 3 to 5, which device is the keyboard, what is sent. Then the way the core uses it: the light comes
// through KbdIo's detection and KeyboardParse, its writes through KbdIo. MSI/msi-ec-keyboard's light too.
#include "TestEnv.h"
#include "OpenRgbKeyboard.h"
#include "MsiEcKeyboard.h"
#include "core/KbdIo.h"
#include "core/KeyboardParse.h"

#include <QSignalSpy>
#include <QTest>
#include <QtEndian>

#include <arpa/inet.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace {
void u16(QByteArray &d, quint16 v) { char b[2]; qToLittleEndian<quint16>(v, b); d.append(b, 2); }
void u32(QByteArray &d, quint32 v) { char b[4]; qToLittleEndian<quint32>(v, b); d.append(b, 4); }
void text(QByteArray &d, const QByteArray &s) { u16(d, quint16(s.size() + 1)); d += s; d += '\0'; }

// One Device Data block, as the document lays it out for this protocol
QByteArray device(const QByteArray &name, const QByteArray &zone, int leds, quint32 protocol)
{
    QByteArray d;
    u32(d, 5);                            // type
    text(d, name);
    for (const char *s : { "MSI", "desc", "1.0", "serial", "location" }) text(d, s);
    u16(d, 1);                            // one mode
    u32(d, 0);
    text(d, "Direct");
    for (int i = 0; i < 12; ++i) u32(d, 0);
    u16(d, 1);                            // with one color of its own
    u32(d, 0);
    u16(d, 1);                            // one zone
    text(d, zone);
    u32(d, 2);
    for (int i = 0; i < 3; ++i) u32(d, quint32(leds));
    u16(d, 16);                           // a matrix map
    d += QByteArray(16, '\x01');
    if (protocol >= 4) { u16(d, 1); text(d, "seg"); for (int i = 0; i < 3; ++i) u32(d, 1); }
    if (protocol >= 5) u32(d, 0);
    u16(d, quint16(leds));
    for (int i = 0; i < leds; ++i) { text(d, "Key"); u32(d, 0); }
    u16(d, quint16(leds));
    d += QByteArray(leds * 4, '\0');
    QByteArray out;
    u32(out, quint32(d.size() + 4));
    return out + d;
}

class FakeServer
{
public:
    explicit FakeServer(quint32 protocol = 4) : m_protocol(protocol)
    {
        m_listen = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        const int on = 1;
        ::setsockopt(m_listen, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(m_listen, reinterpret_cast<sockaddr *>(&a), sizeof a);
        ::listen(m_listen, 4);
        socklen_t n = sizeof a;
        ::getsockname(m_listen, reinterpret_cast<sockaddr *>(&a), &n);
        port = ntohs(a.sin_port);
        m_thread = std::thread([this] { run(); });
    }
    ~FakeServer()
    {
        m_stop = true;
        ::shutdown(m_listen, SHUT_RDWR);
        dropClient();
        m_thread.join();
        ::close(m_listen);
    }
    void dropClient() { const int c = m_client.exchange(-1); if (c >= 0) ::shutdown(c, SHUT_RDWR); }
    QStringList log() { std::lock_guard l(m_lock); return m_log; }
    QList<std::pair<QByteArray, QByteArray>> devices{ { "Mouse", "Scroll Wheel" }, { "MSI Laptop Keyboard", "Keyboard" } };
    int leds = 3;
    quint16 port = 0;

private:
    bool readN(int fd, char *to, qint64 n)
    {
        for (qint64 got = 0; got < n;) {
            const ssize_t r = ::recv(fd, to + got, size_t(n - got), 0);
            if (r <= 0) return false;
            got += r;
        }
        return true;
    }
    void reply(int fd, quint32 dev, quint32 id, const QByteArray &data)
    {
        QByteArray p("ORGB", 4);
        u32(p, dev); u32(p, id); u32(p, quint32(data.size()));
        p += data;
        ::send(fd, p.constData(), size_t(p.size()), MSG_NOSIGNAL);
    }
    void note(const QString &s) { std::lock_guard l(m_lock); m_log << s; }
    void run()
    {
        while (!m_stop) {
            const int fd = ::accept4(m_listen, nullptr, nullptr, SOCK_CLOEXEC);
            if (fd < 0) return;
            m_client = fd;
            // An unasked packet first, as a server tells of device changes: a client has to skip it
            reply(fd, 0, 100, {});
            for (;;) {
                char h[16];
                if (!readN(fd, h, 16)) break;
                const quint32 dev = qFromLittleEndian<quint32>(h + 4), id = qFromLittleEndian<quint32>(h + 8),
                              size = qFromLittleEndian<quint32>(h + 12);
                QByteArray d(qsizetype(size), '\0');
                if (size && !readN(fd, d.data(), size)) break;
                if (id == 40) { QByteArray v; u32(v, m_protocol); reply(fd, 0, 40, v); }
                else if (id == 50) note(u"name "_s + QString::fromUtf8(d.constData()));
                else if (id == 0) { QByteArray v; u32(v, quint32(devices.size())); reply(fd, 0, 0, v); }
                else if (id == 1) {
                    const quint32 asked = qFromLittleEndian<quint32>(d.constData());
                    reply(fd, dev, 1, device(devices[dev].first, devices[dev].second, leds, asked));
                }
                else if (id == 1100) note(u"direct %1"_s.arg(dev));
                else if (id == 1050) {
                    const quint16 n = qFromLittleEndian<quint16>(d.constData() + 4);
                    note(u"leds %1 n=%2 %3%4%5"_s.arg(dev).arg(n).arg(quint8(d[6]), 2, 16, QChar(u'0'))
                             .arg(quint8(d[7]), 2, 16, QChar(u'0')).arg(quint8(d[8]), 2, 16, QChar(u'0')));
                }
            }
            ::close(fd);
            m_client = -1;
        }
    }
    quint32 m_protocol;
    int m_listen = -1;
    std::atomic<int> m_client{ -1 };
    std::atomic<bool> m_stop{ false };
    std::thread m_thread;
    std::mutex m_lock;
    QStringList m_log;
};
} // namespace

class TstOpenRgb : public QObject
{
    Q_OBJECT
    // What detection gives the core with this plugin
    static KbdFound detect(const Paths &p, const OpenRgbKeyboard &k)
    {
        KbdIo io(p);
        io.setPlugins({ &k });
        QSignalSpy got(&io, &KbdIo::scanned);
        io.scan(1);
        return KeyboardParse::parse(got.at(0).at(1).toString(), QString(), { &k });
    }

private slots:
    void initTestCase() { qputenv("SOLWIND_SYS", "/nonexistent"); }   // (never a real OpenRGB server, if one is installed)
    void parsesEveryProtocol_data()
    {
        QTest::addColumn<quint32>("protocol");
        for (quint32 p : { 3u, 4u, 5u }) QTest::addRow("%u", p) << p;
    }
    void parsesEveryProtocol()
    {
        QFETCH(quint32, protocol);
        const auto c = Orgb::parseController(device("MSI Laptop Keyboard", "Keyboard", 94, protocol).mid(4), protocol);
        QVERIFY(c);
        QCOMPARE(c->name, u"MSI Laptop Keyboard"_s);
        QCOMPARE(c->zones, QStringList{ u"Keyboard"_s });
        QCOMPARE(c->leds, 94);
        QVERIFY(c->isKeyboard());
    }
    void refusesCutData()
    {
        const QByteArray whole = device("K", "Keyboard", 3, 4).mid(4);
        // (up to num_leds, the last thing read: 3 LEDs of 10 bytes, the colors' 14 after them)
        for (qsizetype n = 0; n < whole.size() - 44; ++n) QVERIFY2(!Orgb::parseController(whole.left(n), 4), qPrintable(QString::number(n)));
        QVERIFY(Orgb::parseController(whole.left(whole.size() - 44), 4));
    }
    void keyboardByNameOrZone()
    {
        QVERIFY((Orgb::Controller{ u"MSI 3-Zone Keyboard"_s, {}, 3 }).isKeyboard());
        QVERIFY((Orgb::Controller{ u"Laptop"_s, { u"Keyboard"_s }, 3 }).isKeyboard());
        QVERIFY(!(Orgb::Controller{ u"Mouse"_s, { u"Scroll Wheel"_s }, 3 }).isKeyboard());
    }
    void colorPacket()
    {
        const QByteArray d = Orgb::colorsPacket(2, 1, 2, 3);
        QCOMPARE(d.size(), 4 + 2 + 8);
        QCOMPARE(qFromLittleEndian<quint32>(d.constData()), 14u);
        QCOMPARE(qFromLittleEndian<quint16>(d.constData() + 4), quint16(2));
        QCOMPARE(d.mid(6), QByteArray("\x01\x02\x03\x00\x01\x02\x03\x00", 8));
    }
    void oldServerIsNotUsed()
    {
        FakeServer s(2);
        Orgb::Client c(s.port);
        QVERIFY(!c.open());
        QVERIFY(!c.connected());
    }
    void noServer()
    {
        quint16 unused;
        { FakeServer s; unused = s.port; }
        OpenRgbKeyboard k(unused);
        QCOMPARE(k.externalLights().size(), 0);
        QVERIFY(!k.externalPending());
    }
    void noKeyboardNoLight()
    {
        FakeServer s;
        s.devices = { { "Mouse", "Scroll Wheel" } };
        OpenRgbKeyboard k(s.port);
        QCOMPARE(k.externalLights().size(), 0);
        QVERIFY(k.writeExternal(u"openrgb:0/color"_s, u"ff0000"_s).contains(u"no keyboard"_s));
    }
    void throughDetectionAndWrites()
    {
        FakeServer s;
        OpenRgbKeyboard k(s.port);
        QTemporaryDir root;
        Paths p;
        p.sysRoot = root.path();
        const KbdFound f = detect(p, k);
        QCOMPARE(f.plugin, &k);
        QCOMPARE(f.name, u"MSI Laptop Keyboard"_s);
        QCOMPARE(f.driver, u"openrgb"_s);
        QCOMPARE(f.zones, QStringList{ u"openrgb:1"_s });
        QVERIFY(f.writable);
        QCOMPARE(f.maxBrightness, 255);
        QCOMPARE(f.brightness, 255);
        QVERIFY(s.log().contains(u"name Solwind"_s));
        QVERIFY(!s.log().contains(u"direct 1"_s));            // looking changes nothing on the keyboard

        KbdIo io(p);
        io.setPlugins({ &k });
        QSignalSpy done(&io, &KbdIo::written);
        io.write({ k.colorPath(f.zones[0]), k.colorValue(QColor(u"#ff8000"_s), 1, {}), f.zones[0] + u"/brightness"_s, u"128"_s }, 2);
        QCOMPARE(done.at(0).at(1).toString(), QString());
        QTRY_COMPARE(s.log().count(u"direct 1"_s), 1);        // the keyboard, not the mouse before it, once
        QTRY_VERIFY(s.log().contains(u"leds 1 n=3 ff8000"_s));
        QTRY_VERIFY(s.log().contains(u"leds 1 n=3 804000"_s));   // brightness: the color darker
    }
    void reconnects()
    {
        FakeServer s;
        OpenRgbKeyboard k(s.port);
        QCOMPARE(k.externalLights().size(), 1);
        s.dropClient();
        QTest::qWait(100);
        QCOMPARE(k.writeExternal(u"openrgb:1/color"_s, u"00ff00"_s), QString());
        QTRY_VERIFY(s.log().contains(u"leds 1 n=3 00ff00"_s));
        QVERIFY(s.log().contains(u"direct 1"_s));             // its mode on the new connection
    }
    void aLightThatWentIsLookedForAgain()
    {
        quint16 port;
        std::unique_ptr<OpenRgbKeyboard> k;
        { FakeServer s; port = s.port; k = std::make_unique<OpenRgbKeyboard>(port); QCOMPARE(k->externalLights().size(), 1); QVERIFY(!k->externalPending()); }
        QCOMPARE(k->externalLights().size(), 0);
        QVERIFY(k->externalPending());                        // the core looks again in a moment
    }
    void laptopBeforeAUsbKeyboard()
    {
        FakeServer s;
        s.devices = { { "Corsair Keyboard", "Keyboard" }, { "MSI Laptop Keyboard", "Keyboard" } };
        OpenRgbKeyboard k(s.port);
        QCOMPARE(k.externalLights().value(0).name, u"MSI Laptop Keyboard"_s);
    }
    void refusesWrongValues()
    {
        FakeServer s;
        OpenRgbKeyboard k(s.port);
        k.externalLights();
        QVERIFY(!k.writeExternal(u"openrgb:1/color"_s, u"nope"_s).isEmpty());
        QVERIFY(!k.writeExternal(u"openrgb:1/brightness"_s, u"x"_s).isEmpty());
    }
    void kernelLightComesFirst()
    {
        FakeServer s;
        OpenRgbKeyboard k(s.port);
        MsiEcKeyboard msi;
        QTemporaryDir root;
        FakeWorld::writeAt(root.path() + u"/sys/class/leds/msiacpi::kbd_backlight/brightness"_s, "2\n");
        FakeWorld::writeAt(root.path() + u"/sys/class/leds/msiacpi::kbd_backlight/max_brightness"_s, "3\n");
        Paths p;
        p.sysRoot = root.path();
        KbdIo io(p);
        io.setPlugins({ &msi, &k });
        QSignalSpy got(&io, &KbdIo::scanned);
        io.scan(1);
        const KbdFound f = KeyboardParse::parse(got.at(0).at(1).toString(), QString(), { &msi, &k });
        QCOMPARE(f.plugin, &msi);
        QVERIFY(s.log().isEmpty());                           // OpenRGB wasn't even asked
        QCOMPARE(f.maxBrightness, 3);
        QCOMPARE(f.brightness, 2);
        QVERIFY(f.writable);
        QVERIFY(!f.plugin->rgb());
    }
};

QTEST_GUILESS_MAIN(TstOpenRgb)
#include "tst_openrgb.moc"
