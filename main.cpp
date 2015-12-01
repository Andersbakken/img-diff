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
        qDebug() << ret->mSize;
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

    std::vector<Chunk> chunks(int count) const
    {
        if (count == 1) {
            std::vector<Chunk> ret;
            ret.push_back(chunk(rect()));
            return ret;
        }
        Q_ASSERT(count > 1);
        std::vector<Chunk> ret(count * 2);
        const int w = width() / count;
        const int wextra = width() - (w * count);
        const int h = height() / count;
        const int hextra = height() - (h * count);
        for (int y=0; y<count; ++y) {
            for (int x=0; x<count; ++x) {
                const QRect r(x * w,
                              y * h,
                              w + (x + 1 == count ? wextra : 0),
                              h + (y + 1 == count ? hextra : 0));
                ret[(y * count) + x] = chunk(r);
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

void usage(FILE *f)
{
    fprintf(f,
            "img-diff [options...] imga imgb\n"
            "  --verbose|-v                       Be verbose\n"
            "  --cache=[directory]                Use this directory for caches (default \"/tmp/img-sub-cache/\") \n"
            "  --threshold=[threshold]            Set threshold value\n");
}

int main(int argc, char **argv)
{
    QCoreApplication a(argc, argv);
    std::shared_ptr<Image> image1, image2;
    float threshold = 0;
    int minSize = 10;
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

    // if (verbose >= 3) {
    //     fprintf(stderr, "NEEDLE %dx%d", needle->width(), needle->height());

    //     int height = needle->height();
    //     int width = needle->width();
    //     for (int y=0; y<height; ++y) {
    //         for (int x=0; x<width; ++x) {
    //             printf("%s ", qPrintable(needle->color(x, y).toString()));
    //         }
    //         printf("\n");
    //     }

    //     fprintf(stderr, "HAYSTACK %dx%d", haystack->width(), haystack->height());

    //     height = haystack->height();
    //     width = haystack->width();
    //     for (int y=0; y<height; ++y) {
    //         for (int x=0; x<width; ++x) {
    //             printf("%s ", qPrintable(haystack->color(x, y).toString()));
    //         }
    //         printf("\n");
    //     }
    // }

    // const int nw = needle->width();
    // const int nh = needle->height();
    // const int hw = haystack->width();
    // const int hh = haystack->height();
    // if (nw > hw) {
    //     usage(stderr);
    //     fprintf(stderr, "Bad rects\n");
    //     return 1;
    // }
    // if (nh > hh) {
    //     usage(stderr);
    //     fprintf(stderr, "Bad rects\n");
    //     return 1;
    // }

    // // qDebug() << nw << nh << hw << hh;
    // auto tryArea = [&](int x, int y) {
    //     for (int xx=0; xx<nw; ++xx) {
    //         for (int yy=0; yy<nh; ++yy) {
    //             if (!compare(needle, xx, yy, haystack, x + xx, y + yy, threshold)) {
    //                 return false;
    //             }
    //         }
    //     }
    //     printf("%d,%d+%dx%d\n", x, y, nw, nh);
    //     return true;
    // };


    // if ((needle->subRect().x() || needle->subRect().y()) && tryArea(needle->subRect().x(), needle->subRect().y())) {
    //     if (verbose) {
    //         qDebug() << "FOUND IN SAME SPOT";
    //     }
    //     // printf("0,0+0x0\n");
    //     return 0;
    // }

    // for (int x=0; x<=hw - nw; ++x) {
    //     // qDebug() << "shit" << x;
    //     for (int y=0; y<=hh - nh; ++y) {
    //         // qDebug() << "balls" << y;
    //         if (tryArea(x, y)) {
    //             return 0;
    //         }
    //     }
    // }

    std::vector<std::pair<Chunk, Chunk> > matches;
    int count = 1;
    while (true) {
        const std::vector<Chunk> chunks1 = image1->chunks(count);
        Q_ASSERT(!chunks1.empty());
        if (chunks1[0].width() < minSize || chunks1[0].height() < minSize)
            break;
        const std::vector<Chunk> chunks2 = image2->chunks(count);
        ++count;
    }

#if 0
    std::function<QRect(const QRect &)> check = [&](const QRect &rect) {
        if (image1->chunk(rect) == image2->chunk(rect)) {
            // need to return info about rect
            return rect;
        }
        qDebug() << "COMPARING" << rect << minSize;
        const int w = rect.width() / 2;
        if (w < minSize)
            return QRect();
        const int wextra = rect.width() - (w * 2);
        const int h = rect.height() / 2;
        if (h < minSize)
            return QRect();
        const int hextra = rect.height() - (h * 2);

        {
            const QRect r = check(QRect(rect.x(), rect.y(), w + wextra, h + hextra));
            if (!r.isNull())
                return r;
        }
        {
            const QRect r = check(QRect(rect.x() + w + wextra, rect.y(), w, h + hextra));
            if (!r.isNull())
                return r;
        }
        {
            const QRect r = check(QRect(rect.x(), rect.y() + h + hextra, w + wextra, h));
            if (!r.isNull())
                return r;
        }
        return check(QRect(rect.x() + w + wextra, rect.y() + h + hextra, w, h));
    };

    QRect rect = check(image1->rect());
    if (rect.isNull()) {
        if (verbose)
            fprintf(stderr, "Couldn't find area\n");
        return 1;
    } else {
        qDebug() << rect;
    }
#endif
    return 0;
}
