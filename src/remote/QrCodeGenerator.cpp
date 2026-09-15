#include "QrCodeGenerator.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace strmqt {

namespace {

// Lightweight QR Code generator implementation based on Project Nayuki (MIT License).
class BitBuffer : public std::vector<bool>
{
public:
    void appendBits(std::uint32_t val, int len)
    {
        assert(len >= 0 && len <= 32);
        assert(len == 32 || (val >> len) == 0);
        for (int i = len - 1; i >= 0; --i)
            push_back(((val >> i) & 1) != 0);
    }
};

class ReedSolomonGenerator
{
public:
    explicit ReedSolomonGenerator(int degree)
    {
        assert(degree >= 1 && degree <= 255);
        coefficients.assign(degree, 0);
        coefficients.back() = 1;
        std::uint8_t root = 1;
        for (int i = 0; i < degree; ++i) {
            for (size_t j = 0; j < coefficients.size(); ++j) {
                coefficients[j] = multiply(coefficients[j], root);
                if (j + 1 < coefficients.size())
                    coefficients[j] ^= coefficients[j + 1];
            }
            root = multiply(root, 0x02);
        }
    }

    std::vector<std::uint8_t> getRemainder(const std::vector<std::uint8_t> &data) const
    {
        std::vector<std::uint8_t> result(coefficients.size(), 0);
        for (std::uint8_t b : data) {
            std::uint8_t factor = b ^ result[0];
            result.erase(result.begin());
            result.push_back(0);
            for (size_t i = 0; i < result.size(); ++i)
                result[i] ^= multiply(coefficients[i], factor);
        }
        return result;
    }

private:
    static std::uint8_t multiply(std::uint8_t x, std::uint8_t y)
    {
        std::uint8_t z = 0;
        for (int i = 7; i >= 0; --i) {
            z = static_cast<std::uint8_t>((z << 1) ^ ((z >> 7) * 0x11D));
            z ^= static_cast<std::uint8_t>(((y >> i) & 1) * x);
        }
        return z;
    }

    std::vector<std::uint8_t> coefficients;
};

// Data capacity table per version (Version 1-10 are sufficient for URLs up to 200+ chars)
// Num ECC codewords per block and number of blocks
struct VersionEcc
{
    int version;
    int dataCodewords;
    int eccCodewordsPerBlock;
    int numBlocks;
};

// Medium ECC table for Versions 1 to 6
static const VersionEcc kVersionEccMedium[] = {
    {1, 16, 10, 1},
    {2, 28, 16, 1},
    {3, 44, 26, 1},
    {4, 64, 18, 2},
    {5, 86, 24, 2},
    {6, 108, 16, 4},
};

static const int kAlignmentPatternBase[] = {
    0, 0, 18, 22, 26, 30, 34
};

} // namespace

std::vector<std::vector<bool>> QrCodeGenerator::encodeText(const QString &text, Ecc ecc)
{
    Q_UNUSED(ecc);
    const QByteArray utf8 = text.toUtf8();
    const int dataLen = utf8.size();

    // Select version
    int version = 1;
    int dataCapacity = 0;
    int eccCodewordsPerBlock = 0;
    int numBlocks = 1;

    for (const auto &v : kVersionEccMedium) {
        // Overhead for Byte mode: 4 bits mode + 8 bits character count indicator
        const int overheadCodewords = (4 + 8 + 7) / 8; // approx 2 bytes
        if (dataLen + overheadCodewords <= v.dataCodewords) {
            version = v.version;
            dataCapacity = v.dataCodewords;
            eccCodewordsPerBlock = v.eccCodewordsPerBlock;
            numBlocks = v.numBlocks;
            break;
        }
    }

    if (version == 1 && dataCapacity == 0) {
        // Text is larger than V6, fallback to V6 clamped or default
        version = 6;
        dataCapacity = kVersionEccMedium[5].dataCodewords;
        eccCodewordsPerBlock = kVersionEccMedium[5].eccCodewordsPerBlock;
        numBlocks = kVersionEccMedium[5].numBlocks;
    }

    // 1. Data encoding (Byte mode = 0100)
    BitBuffer bb;
    bb.appendBits(0b0100, 4); // Mode: Byte
    bb.appendBits(static_cast<std::uint32_t>(dataLen), 8); // Count indicator (8 bits for V1-9)
    for (char c : utf8)
        bb.appendBits(static_cast<std::uint8_t>(c), 8);

    // Terminator (up to 4 zeroes)
    const int totalDataBits = dataCapacity * 8;
    const int terminatorBits = std::min(4, totalDataBits - static_cast<int>(bb.size()));
    bb.appendBits(0, terminatorBits);

    // Pad to byte boundary
    while (bb.size() % 8 != 0)
        bb.push_back(false);

    // Pad bytes: alternating 0xEC and 0x11
    static const std::uint8_t padBytes[2] = {0xEC, 0x11};
    int padIdx = 0;
    while (static_cast<int>(bb.size()) < totalDataBits) {
        bb.appendBits(padBytes[padIdx % 2], 8);
        padIdx++;
    }

    // Convert bits to codewords
    std::vector<std::uint8_t> dataCodewords(dataCapacity, 0);
    for (size_t i = 0; i < bb.size(); ++i) {
        if (bb[i])
            dataCodewords[i / 8] |= (1 << (7 - (i % 8)));
    }

    // 2. Error correction
    ReedSolomonGenerator rs(eccCodewordsPerBlock);
    const int blockLen = dataCapacity / numBlocks;
    std::vector<std::vector<std::uint8_t>> dataBlocks(numBlocks);
    std::vector<std::vector<std::uint8_t>> eccBlocks(numBlocks);

    for (int i = 0; i < numBlocks; ++i) {
        dataBlocks[i].assign(dataCodewords.begin() + i * blockLen,
                             dataCodewords.begin() + (i + 1) * blockLen);
        eccBlocks[i] = rs.getRemainder(dataBlocks[i]);
    }

    // Interleave codewords
    std::vector<std::uint8_t> finalCodewords;
    for (int j = 0; j < blockLen; ++j) {
        for (int i = 0; i < numBlocks; ++i)
            finalCodewords.push_back(dataBlocks[i][j]);
    }
    for (int j = 0; j < eccCodewordsPerBlock; ++j) {
        for (int i = 0; i < numBlocks; ++i)
            finalCodewords.push_back(eccBlocks[i][j]);
    }

    // 3. Construct Matrix
    const int size = 17 + version * 4;
    std::vector<std::vector<int8_t>> grid(size, std::vector<int8_t>(size, -1)); // -1 = unallocated

    // Finder patterns
    const auto drawFinder = [&](int startX, int startY) {
        for (int dy = -1; dy <= 7; ++dy) {
            for (int dx = -1; dx <= 7; ++dx) {
                int x = startX + dx;
                int y = startY + dy;
                if (x >= 0 && x < size && y >= 0 && y < size) {
                    bool dark = (dx >= 0 && dx <= 6 && dy >= 0 && dy <= 6) &&
                                ((dx == 0 || dx == 6 || dy == 0 || dy == 6) ||
                                 (dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4));
                    grid[y][x] = dark ? 1 : 0;
                }
            }
        }
    };
    drawFinder(0, 0);
    drawFinder(size - 7, 0);
    drawFinder(0, size - 7);

    // Timing patterns
    for (int i = 8; i < size - 8; ++i) {
        grid[6][i] = (i % 2 == 0) ? 1 : 0;
        grid[i][6] = (i % 2 == 0) ? 1 : 0;
    }

    // Alignment patterns for version >= 2
    if (version >= 2) {
        int pos = kAlignmentPatternBase[version];
        int coords[2] = {6, pos};
        for (int y : coords) {
            for (int x : coords) {
                if (grid[y][x] != -1)
                    continue;
                for (int dy = -2; dy <= 2; ++dy) {
                    for (int dx = -2; dx <= 2; ++dx) {
                        bool dark = (std::max(std::abs(dx), std::abs(dy)) != 1);
                        grid[y + dy][x + dx] = dark ? 1 : 0;
                    }
                }
            }
        }
    }

    // Reserve format info areas
    for (int i = 0; i < 9; ++i) {
        if (grid[i][8] == -1) grid[i][8] = 0;
        if (grid[8][i] == -1) grid[8][i] = 0;
    }
    for (int i = 0; i < 8; ++i) {
        if (grid[8][size - 1 - i] == -1) grid[8][size - 1 - i] = 0;
        if (grid[size - 1 - i][8] == -1) grid[size - 1 - i][8] = 0;
    }
    grid[size - 8][8] = 1; // Dark module

    // Place codewords into available cells
    size_t bitIndex = 0;
    const size_t totalBits = finalCodewords.size() * 8;
    int right = size - 1;
    while (right > 0) {
        if (right == 6)
            right--; // Skip vertical timing column
        for (int vert = 0; vert < size; ++vert) {
            for (int j = 0; j < 2; ++j) {
                int x = right - j;
                bool upwards = ((right + 1) & 2) == 0;
                int y = upwards ? size - 1 - vert : vert;
                if (grid[y][x] == -1) {
                    bool bit = false;
                    if (bitIndex < totalBits) {
                        bit = ((finalCodewords[bitIndex / 8] >> (7 - (bitIndex % 8))) & 1) != 0;
                        bitIndex++;
                    }
                    // Apply mask 0: (x + y) % 2 == 0
                    if ((x + y) % 2 == 0)
                        bit = !bit;
                    grid[y][x] = bit ? 1 : 0;
                }
            }
        }
        right -= 2;
    }

    // Format bits for Medium ECC (00) and Mask 0 (000) -> 00000 with BCH(15, 5) -> 0x5412
    static const uint16_t formatBits = 0x5412;
    for (int i = 0; i < 6; ++i)
        grid[8][i] = (formatBits >> i) & 1;
    grid[8][7] = (formatBits >> 6) & 1;
    grid[8][8] = (formatBits >> 7) & 1;
    grid[7][8] = (formatBits >> 8) & 1;
    for (int i = 9; i < 15; ++i)
        grid[14 - i][8] = (formatBits >> i) & 1;

    for (int i = 0; i < 8; ++i)
        grid[size - 1 - i][8] = (formatBits >> i) & 1;
    for (int i = 8; i < 15; ++i)
        grid[8][size - 15 + i] = (formatBits >> i) & 1;

    std::vector<std::vector<bool>> result(size, std::vector<bool>(size, false));
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x)
            result[y][x] = (grid[y][x] == 1);
    }
    return result;
}

QString QrCodeGenerator::toSvg(const QString &text, int border,
                               const QString &foreground, const QString &background)
{
    if (text.isEmpty())
        return {};

    const auto grid = encodeText(text, Ecc::Medium);
    const int size = static_cast<int>(grid.size());
    const int totalSize = size + border * 2;

    std::ostringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    ss << "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" viewBox=\"0 0 "
       << totalSize << " " << totalSize << "\" stroke=\"none\">\n";
    ss << "<rect width=\"100%\" height=\"100%\" fill=\"" << background.toStdString() << "\"/>\n";
    ss << "<path fill=\"" << foreground.toStdString() << "\" d=\"";

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (grid[y][x]) {
                ss << "M" << (x + border) << "," << (y + border) << "h1v1h-1z ";
            }
        }
    }
    ss << "\"/>\n</svg>\n";

    return QString::fromStdString(ss.str());
}

} // namespace strmqt
