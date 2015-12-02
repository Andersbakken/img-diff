#include <QtGui>
#include <memory>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

int verbose = 0;
float threshold = 0;
struct Color
{
    Color(const QColor &col = QColor())
        : red(col.red()), green(col.green()), blue(col.blue()), alpha(col.alpha())
    {}

    QString toString() const
    {
        char buf[1024];
        snprintf(buf, sizeof(buf), "%02x%02x%02x%02x", red, green, blue, alpha);
        return QString::fromLocal8Bit(buf);
    }

    bool compare(const Color &other) const
    {
        const float r = powf(red - other.red, 2);
        const float g = powf(green - other.green, 2);
        const float b = powf(blue - other.blue, 2);

        float distance = sqrtf(r + g + b);
        const float alphaDistance = std::abs(alpha - other.alpha);
        // if (verbose >= 2) {
        //     fprintf(stderr, "%s to %s => %f/%f (%f) at %d,%d (%d,%d)\n",
        //             qPrintable(toString()),
        //             qPrintable(other.toString()),
        //             distance,
        //             alphaDistance,
        //             threshold,
        //             needleX, needleY,
        //             haystackX, haystackY);
        // }
        if (alphaDistance > distance)
            distance = alphaDistance;

        // static float highest = 0;
        const bool ret = distance <= threshold;
        // if (verbose >= 1 && ret && distance > highest) {
        //     fprintf(stderr, "Allowed %f distance for threshold %f at %d,%d (%d,%d) (%s vs %s)\n",
        //             distance, threshold,
        //             needleX, needleY,
        //             haystackX, haystackY,
        //             qPrintable(needle.toString()),
        //             qPrintable(haystack.toString()));
        //     highest = distance;
        // }

        // need to compare alpha
        return ret;
    }

    bool operator==(const Color &other) const
    {
        return compare(other);
    }
    bool operator!=(const Color &other) const
    {
        return !compare(other);
    }


    quint8 red, green, blue, alpha;
};

class Image;
class Chunk
{
public:
    Chunk(const std::shared_ptr<const Image> &i = std::shared_ptr<const Image>(), const QRect &r = QRect());
    Chunk(const Chunk &other)
        : mImage(other.mImage), mRect(other.mRect)
    {}
    int x() const { return mRect.x(); }
    int y() const { return mRect.y(); }
    int height() const { return mRect.height(); }
    int width() const { return mRect.width(); }
    QSize size() const { return mRect.size(); }
    QRect rect() const { return mRect; }

    inline Color color(int x, int y) const;
    bool compare(const Chunk &other) const;
    bool operator==(const Chunk &other) const { return compare(other); }
    bool operator!=(const Chunk &other) const { return !compare(other); }
    std::shared_ptr<const Image> image() const { return mImage; }
    bool isNull() const { return !mImage; }
    bool isValid() const { return mImage.get(); }
    void adopt(const Chunk &other, Qt::Alignment alignment);
    Qt::Alignment isAligned(const Chunk &other) const;
private:
    std::shared_ptr<const Image> mImage;
    QRect mRect;
};

class Image : public std::enable_shared_from_this<Image>
{
public:
    static std::shared_ptr<Image> load(const QString &fileName)
    {
        QImage image(fileName);
        if (image.isNull())
            return std::shared_ptr<Image>();

        const int w = image.width();
        const int h = image.height();
        std::shared_ptr<Image> ret(new Image);
        ret->mFileName = fileName;
        ret->mSize = image.size();
        ret->mColors.resize(w * h);
        for (int y=0; y<h; ++y) {
            for (int x=0; x<w; ++x) {
                Color &c = ret->mColors[x + (y * w)];
                c = QColor(image.pixel(x, y));
            }
        }
        return ret;
    }

    Chunk chunk(const QRect &rect) const { return Chunk(shared_from_this(), rect); }

    QVector<Chunk> chunks(int count, const QRegion &filter = QRegion()) const
    {
        if (count == 1) {
            Q_ASSERT(filter.isEmpty());
            QVector<Chunk> ret;
            ret.push_back(chunk(rect()));
            return ret;
        }
        Q_ASSERT(count > 1);
        QVector<Chunk> ret(count * count);
        const int w = width() / count;
        // const int wextra = width() - (w * count);
        const int h = height() / count;
        // const int hextra = height() - (h * count);
        for (int y=0; y<count; ++y) {
            for (int x=0; x<count; ++x) {
                const QRect r(x * w, y * h, w, h);
                if (!filter.intersects(r)) {
                    // const QRect r(x * w,
                    //               y * h,
                    //               w + (x + 1 == count ? wextra : 0),
                    //               h + (y + 1 == count ? hextra : 0));
                    ret[(y * count) + x] = chunk(r);
                }
            }
        }
        return ret;
    }

    Color color(int x, int y) const
    {
        Q_ASSERT(x >= 0);
        Q_ASSERT(y >= 0);
        Q_ASSERT(x < mSize.width());
        Q_ASSERT(y < mSize.height());
        return mColors.at((y * mSize.width()) + x);
    }

    QSize size() const { return mSize; }
    int width() const { return mSize.width(); }
    int height() const { return mSize.height(); }
    QString fileName() const { return mFileName; }
    QRect rect() const { return QRect(0, 0, width(), height()); }
private:
    Image()
    {}

    QString mFileName;
    QSize mSize;
    QVector<Color> mColors;

};

QDebug &operator<<(QDebug &debug, const Chunk &chunk)
{
    debug << "Chunk(" << chunk.image()->fileName() << chunk.rect() << ")";
    return debug;
}

Chunk::Chunk(const std::shared_ptr<const Image> &i, const QRect &r)
    : mImage(i), mRect(r)
{
    if (mImage) {
        Q_ASSERT(r.bottom() < i->height());
        Q_ASSERT(r.right() < i->width()); // bottom/right are off-by-one
    }
    Q_ASSERT(!mImage.get() == r.isNull());
}

inline Color Chunk::color(int x, int y) const // x and y is in Chunk coordinates
{
    Q_ASSERT(mImage);
    Q_ASSERT(x < mRect.width());
    Q_ASSERT(y < mRect.height());
    return mImage->color(mRect.x() + x, mRect.y() + y);
}

bool Chunk::compare(const Chunk &other) const
{
    Q_ASSERT(other.mRect.size() == mRect.size());
    const int h = height();
    const int w = width();
    for (int y = 0; y<h; ++y) {
        for (int x = 0; x<w; ++x) {
            if (color(x, y) != other.color(x, y)) {
                return false;
            }
        }
    }
    return true;
}

Qt::Alignment Chunk::isAligned(const Chunk &other) const
{
    Qt::Alignment ret;
    if (height() == other.height()) {
        if (x() + width() == other.x()) {
            ret |= Qt::AlignRight;
        } else if (other.x() + other.width() == x()) {
            ret |= Qt::AlignLeft;
        }
    } else if (width() == other.width()) {
        if (y() + height() == other.y()) {
            ret |= Qt::AlignBottom;
        } else if (other.y() + other.height() == y()) {
            ret |= Qt::AlignTop;
        }
    }
    return ret;
}

void Chunk::adopt(const Chunk &other, Qt::Alignment alignment)
{
    Q_ASSERT(alignment);
    Q_ASSERT(isAligned(other) == alignment);
    QRegion region;
    region |= rect();
    region |= other.rect();
    mRect = region.boundingRect();
    Q_ASSERT(mRect.bottom() < mImage->height());
    Q_ASSERT(mRect.right() < mImage->width()); // bottom/right are off-by-one
}

void usage(FILE *f)
{
    fprintf(f,
            "img-diff [options...] imga imgb\n"
            "  --verbose|-v                       Be verbose\n"
            "  --range=[range]                    The range?\n"
            "  --min-size=[min-size]              The min-size?\n"
            "  --threshold=[threshold]            Set threshold value\n");
}

static void joinChunks(QVector<std::pair<Chunk, Chunk> > &chunks)
{
    bool modified;
    do {
        modified = false;
        for (int i=0; !modified && i<chunks.size(); ++i) {
            Chunk &chunk = chunks[i].first;
            Chunk &otherChunk = chunks[i].second;
            for (int j=i + 1; j<chunks.size(); ++j) {
                const Qt::Alignment aligned = chunk.isAligned(chunks.at(j).first);
                if (verbose >= 2) {
                    qDebug() << "comparing" << chunk.rect() << chunks.at(j).first.rect() << aligned
                             << otherChunk.rect() << chunks.at(j).second.rect()
                             << otherChunk.isAligned(chunks.at(j).second);
                }
                if (aligned && otherChunk.isAligned(chunks.at(j).second) == aligned) {
                    modified = true;
                    chunk.adopt(chunks.at(j).first, aligned);
                    otherChunk.adopt(chunks.at(j).second, aligned);
                    chunks.remove(j, 1);
                    if (verbose)
                        qDebug() << "chunk" << i << chunk.rect() << "is aligned with chunk" << j << chunks.at(j).first.rect();
                    break;
                }
            }
        }
    } while (modified);
}

int main(int argc, char **argv)
{
    QCoreApplication a(argc, argv);
    std::shared_ptr<Image> image1, image2;
    float threshold = 0;
    int minSize = 10;
    int range = 2;
    for (int i=1; i<argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "--help" || arg == "-h") {
            usage(stdout);
            return 0;
        } else if (arg == "-v" || arg == "--verbose") {
            ++verbose;
        } else if (arg.startsWith("--threshold=")) {
            bool ok;
            QString t = arg.mid(12);
            bool percent = false;
            if (t.endsWith("%")) {
                t.chop(1);
                percent = true;
            }
            threshold = t.toFloat(&ok);
            if (!ok || threshold < .0) {
                fprintf(stderr, "Invalid threshold (%s), must be positive float value\n",
                        qPrintable(arg.mid(12)));
                return 1;
            }
            if (percent) {
                threshold /= 100;
                threshold *= 256;
            }
            if (verbose)
                qDebug() << "threshold:" << threshold;
        } else if (arg.startsWith("--min-size=")) {
            bool ok;
            QString t = arg.mid(11);
            minSize = t.toInt(&ok);
            if (!ok || minSize <= 0) {
                fprintf(stderr, "Invalid --min-size (%s), must be positive integer value\n",
                        qPrintable(arg.mid(12)));
                return 1;
            }
            if (verbose)
                qDebug() << "min-size:" << minSize;
        } else if (arg.startsWith("--range=")) {
            bool ok;
            QString t = arg.mid(11);
            range = t.toInt(&ok);
            if (!ok || range <= 0) {
                fprintf(stderr, "Invalid --range (%s), must be positive integer value\n",
                        qPrintable(arg.mid(12)));
                return 1;
            }
            if (verbose)
                qDebug() << "range:" << range;
        } else if (!image1) {
            image1 = Image::load(arg);
            if (!image1) {
                fprintf(stderr, "Failed to decode %s\n", qPrintable(arg));
                return 1;
            }
        } else if (!image2) {
            image2 = Image::load(arg);
            if (!image2) {
                fprintf(stderr, "Failed to decode %s\n", qPrintable(arg));
                return 1;
            }
        } else {
            usage(stderr);
            fprintf(stderr, "Too many args\n");
            return 1;
        }
    }
    if (!image2) {
        usage(stderr);
        fprintf(stderr, "Not enough args\n");
        return 1;
    }

    if (image1->size() != image2->size()) {
        fprintf(stderr, "Images have different sizes: %dx%d vs %dx%d\n",
                image1->width(), image1->height(),
                image2->width(), image2->height());
        return 1;
    }

    auto chunkIndexes = [range](int count, int idx) {
        QVector<int> indexes;
        const int y = (idx / count);
        const int x = idx % count;
        auto add = [&](int xadd, int yadd) {
            const int xx = x + xadd;
            if (xx < 0 || xx >= count)
                return;
            const int yy = y + yadd;
            if (yy < 0 || yy >= count)
                return;
            indexes.push_back((yy * count) + xx);
        };
        for (int y=-range; y<=range; ++y) {
            for (int x=-range; x<=range; ++x) {
                add(x, y);
            }
        }

        return indexes;
    };

    // qDebug() << chunkIndexes(10, 0);
    // return 0;
    QVector<std::pair<Chunk, Chunk> > matches;
    QRegion used;
    int count = 1;
    while (true) {
        const QVector<Chunk> chunks1 = image1->chunks(count, used);
        const QVector<Chunk> chunks2 = image2->chunks(count);
        bool done = false;
        for (int i=0; i<chunks1.size(); ++i) {
            const Chunk &chunk = chunks1.at(i);
            if (chunk.isNull())
                continue;
            if (chunk.width() < minSize || chunk.height() < minSize) {
                done = true;
                break;
            }

            for (int idx : chunkIndexes(count, i)) {
                const Chunk &otherChunk = chunks2.at(idx);
                if (verbose >= 2) {
                    qDebug() << "comparing chunks" << chunk << otherChunk;
                }

                if (chunk == otherChunk) {
                    used |= chunk.rect();
                    matches.push_back(std::make_pair(chunk, otherChunk));
                    break;
                }
            }
        }
        if (done)
            break;

        ++count;
    }
    if (!matches.isEmpty()) {
        joinChunks(matches);
        for (const auto &match : matches) {
            printf("%d,%d+%dx%d %d,%d+%dx%d\n",
                   match.first.x(), match.first.y(), match.first.width(), match.first.height(),
                   match.second.x(), match.second.y(), match.second.width(), match.second.height());
        }
    }

    return 0;
}
