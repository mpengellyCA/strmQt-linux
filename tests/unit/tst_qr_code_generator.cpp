#include <QtTest>
#include "remote/QrCodeGenerator.h"

using namespace strmqt;

class QrCodeGeneratorTest : public QObject
{
    Q_OBJECT

private slots:
    void generateValidSvg();
    void customColorsAreReflectedInSvg();
    void emptyTextReturnsEmptySvg();
};

void QrCodeGeneratorTest::generateValidSvg()
{
    const QString url = QStringLiteral("https://100.81.48.104:8337");
    const QString svg = QrCodeGenerator::toSvg(url, 4);

    QVERIFY(!svg.isEmpty());
    QVERIFY(svg.contains(QStringLiteral("<svg")));
    QVERIFY(svg.endsWith(QStringLiteral("</svg>\n")));
    QVERIFY(svg.contains(QStringLiteral("viewBox=")));
    QVERIFY(svg.contains(QStringLiteral("<path fill=")));
}

void QrCodeGeneratorTest::customColorsAreReflectedInSvg()
{
    const QString text = QStringLiteral("hello-strmqt");
    const QString fg = QStringLiteral("#123456");
    const QString bg = QStringLiteral("#ABCDEF");

    const QString svg = QrCodeGenerator::toSvg(text, 2, fg, bg);
    QVERIFY(svg.contains(QStringLiteral("fill=\"#123456\"")));
    QVERIFY(svg.contains(QStringLiteral("fill=\"#ABCDEF\"")));
}

void QrCodeGeneratorTest::emptyTextReturnsEmptySvg()
{
    const QString svg = QrCodeGenerator::toSvg(QString());
    QVERIFY(svg.isEmpty());
}

QTEST_MAIN(QrCodeGeneratorTest)
#include "tst_qr_code_generator.moc"
