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
    void formatInformationIsWhereAReaderLooks_data();
    void formatInformationIsWhereAReaderLooks();
    void matchesACodeAScannerRead();
    void tooLongIsNoCodeAtAll();
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
    QVERIFY(svg.contains(QStringLiteral("fill=\"#FFFFFF\"")));
    QVERIFY(svg.contains(QStringLiteral("fill=\"#0C0B0A\"")));
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

// The 15 format bits (correction level and mask) as a reader collects them,
// ISO/IEC 18004 §7.9: bit 0 first. `grid` is [row][column].
static int readFormat(const std::vector<std::vector<bool>> &grid, bool secondCopy)
{
    const int size = int(grid.size());
    int bits = 0;
    const auto take = [&](int i, int row, int column) {
        if (grid[size_t(row)][size_t(column)])
            bits |= 1 << i;
    };
    if (!secondCopy) {
        for (int i = 0; i < 6; ++i)
            take(i, i, 8);
        take(6, 7, 8);
        take(7, 8, 8);
        take(8, 8, 7);
        for (int i = 9; i < 15; ++i)
            take(i, 8, 14 - i);
    } else {
        for (int i = 0; i < 8; ++i)
            take(i, 8, size - 1 - i);
        for (int i = 8; i < 15; ++i)
            take(i, size - 15 + i, 8);
    }
    return bits;
}

void QrCodeGeneratorTest::formatInformationIsWhereAReaderLooks_data()
{
    QTest::addColumn<int>("bytes");
    QTest::addColumn<int>("modules");

    // One length in each version the generator can produce.
    QTest::newRow("version 1") << 10 << 21;
    QTest::newRow("version 2") << 25 << 25;
    QTest::newRow("version 3") << 40 << 29;
    QTest::newRow("version 4") << 60 << 33;
    QTest::newRow("version 5") << 80 << 37;
    QTest::newRow("version 6") << 106 << 41;
}

void QrCodeGeneratorTest::formatInformationIsWhereAReaderLooks()
{
    QFETCH(int, bytes);
    QFETCH(int, modules);

    // Every code this generator made was unreadable: the format bits were
    // written with row and column swapped, so a reader saw them reversed and
    // could not tell the mask or the correction level.
    const auto grid = QrCodeGenerator::encodeText(QString(bytes, QLatin1Char('x')));
    QCOMPARE(int(grid.size()), modules);
    // Medium correction, mask 0, with its BCH code and the fixed XOR mask.
    QCOMPARE(readFormat(grid, false), 0x5412);
    QCOMPARE(readFormat(grid, true), 0x5412);
    // The module that is always dark, beside the second copy.
    QVERIFY(grid[size_t(modules - 8)][8]);
}

void QrCodeGeneratorTest::matchesACodeAScannerRead()
{
    // This exact matrix was decoded back to the address by zbar when the
    // format bits were fixed; it is here so a change to data placement,
    // masking or error correction cannot pass unnoticed either.
    static const char *const expected[] = {
        "1111111000111111101111111",
        "1000001010001011101000001",
        "1011101000111010101011101",
        "1011101001011001101011101",
        "1011101010101001101011101",
        "1000001000011111001000001",
        "1111111010101010101111111",
        "0000000000101110100000000",
        "1010101000100111100010010",
        "1100100000001110001100001",
        "1011011000001010100110111",
        "0001010101110101000100010",
        "0010011110001111001101011",
        "0111000011011010001101001",
        "1001101011111110101100111",
        "0101100100001100111010010",
        "1001101011011110111111000",
        "0000000010110010100011011",
        "1111111001000011101011011",
        "1000001001101101100011000",
        "1011101011000110111111000",
        "1011101000000110010111100",
        "1011101011100110100010001",
        "1000001001000110101011010",
        "1111111010000110100100011",
    };
    const auto grid = QrCodeGenerator::encodeText(QStringLiteral("https://192.168.1.50:8337"));
    QCOMPARE(int(grid.size()), 25);
    for (int y = 0; y < 25; ++y) {
        QByteArray row;
        for (int x = 0; x < 25; ++x)
            row.append(grid[size_t(y)][size_t(x)] ? '1' : '0');
        QCOMPARE(row, QByteArray(expected[y]));
    }
}

void QrCodeGeneratorTest::tooLongIsNoCodeAtAll()
{
    // 106 bytes is the most a version 6 code holds here. One more used to
    // write past the end of the data buffer and draw a code for something
    // else; now there is no code, which the caller can see.
    QVERIFY(!QrCodeGenerator::encodeText(QString(106, QLatin1Char('x'))).empty());
    QVERIFY(QrCodeGenerator::encodeText(QString(107, QLatin1Char('x'))).empty());
    QVERIFY(QrCodeGenerator::toSvg(QString(107, QLatin1Char('x'))).isEmpty());
    // Bytes, not characters.
    QVERIFY(QrCodeGenerator::encodeText(QString(54, QChar(0x00E9))).empty());
    QVERIFY(!QrCodeGenerator::encodeText(QString(53, QChar(0x00E9))).empty());
}

QTEST_MAIN(QrCodeGeneratorTest)
#include "tst_qr_code_generator.moc"
