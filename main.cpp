#include <QtGui>
#include <memory>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

QString cache = "/tmp/img-sub-cache/";
int verbose = 0;
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

    quint8 red, green, blue, alpha;
};

struct Image
{
    static std::shared_ptr<Image> load(const std::string &fileName, const QRect &subRect)
    {
        int fd = open(fileName.c_str(), O_RDONLY);
        if (fd == -1) {
            return std::shared_ptr<Image>();
        }
        struct stat st;
        if (fstat(fd, &st) == -1) {
            ::close(fd);
            return std::shared_ptr<Image>();
        }
        void *data = mmap(0, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (!data) {
            ::close(fd);
            return std::shared_ptr<Image>();
        }
        std::shared_ptr<Image> ret(new Image);
        ret->mData = data;
        ret->mFD = fd;
        ret->mMappedLength = st.st_size;
        ret->mWidth = ret->read<int>(Width);
        ret->mHeight = ret->read<int>(Height);
        if (!subRect.isNull())
            ret->mSubRect = subRect.intersected(QRect(0, 0, ret->mWidth, ret->mHeight));
        return ret;
    }

    ~Image()
    {
        Q_ASSERT(mData);
        munmap(mData, mMappedLength);
    }

    Color color(int x, int y) const
    {
        if (!mSubRect.isNull()) {
            x += mSubRect.x();
            y += mSubRect.y();
        }
#ifndef QT_NO_DEBUG
        if (x < 0 || x >= mWidth || y < 0 || y >= mHeight) {
            qDebug() << "bad coords" << x << y << mWidth << mHeight << mSubRect;
        }
#endif
        Q_ASSERT(x < mWidth);
        Q_ASSERT(y < mHeight);
        Q_ASSERT(x >= 0);
        Q_ASSERT(y >= 0);
        return read<Color>(Colors + ((x + (y * mWidth)) * sizeof(Color)));
    }

    int width() const { return mSubRect.isNull() ? mWidth : mSubRect.width(); }
    int height() const { return mSubRect.isNull() ? mHeight : mSubRect.height(); }
    QRect subRect() const { return mSubRect; }
    QRect totalRect() const { return QRect(0, 0, mWidth, mHeight); }
private:
    Image()
        : mData(0), mMappedLength(0), mFD(-1), mWidth(0), mHeight(0)
    {}
    enum Offset {
        Width = 0,
        Height = sizeof(int),
        Colors = Height + sizeof(bool)
    };
    template <typename T> T read(int offset) const
    {
        Q_ASSERT(offset < mMappedLength);
        T ret;
        memcpy(&ret, static_cast<const unsigned char *>(mData) + offset, sizeof(T));
        return ret;
    }

    void *mData;
    int mMappedLength;
    int mFD;
    int mWidth, mHeight;
    QRect mSubRect;
};

static inline bool writeColors(const QVector<Color> &cols, FILE *f)
{
    return fwrite(cols.constData(), sizeof(Color) * cols.size(), 1, f);
    // bool first = false;
    // int idx = 0;
    // for (QVector<Color>::const_iterator it = cols.begin(); it != cols.end(); ++it) {
    //     const Color &col = *it;
    //     if (col.alpha && !first) {
    //         first = true;
    //         const unsigned char *shitshit = reinterpret_cast<const unsigned char *>(&col);
    //         for (int i=0; i<8; ++i) {
    //             printf("0x%x ", shitshit[i]);
    //         }
    //         printf("\n");

    //     }
    //     if (!fwrite(&col, sizeof(Color), 1, f))
    //         return false;

    //     ++idx;
    // }
    // return true;
}

static std::shared_ptr<Image> load(const QString &file, const QRect &subRect)
{
    const QString cacheFile = cache + "/" + QFileInfo(file).fileName() + ".cache";
    std::shared_ptr<Image> img = Image::load(cacheFile.toStdString(), subRect);
    if (img)
        return img;
    QImageReader reader(file);
    QImage image;
    reader.read(&image);
    if (image.isNull()) {
        qDebug() << "Couldn't decode" << file;
        return std::shared_ptr<Image>();
    }

    FILE *f = fopen(qPrintable(cacheFile), "w");
    if (!f) {
        qDebug() << "Couldn't open file for writing" << cacheFile;
        return std::shared_ptr<Image>();
    }

    int w = image.width(), h = image.height();
    QVector<Color> colors(w * h);
    for (int y=0; y<h; ++y) {
        for (int x=0; x<w; ++x) {
            Color &c = colors[x + (y * w)];
            c = QColor::fromRgba(image.pixel(x, y));
        }
    }

    if (!fwrite(&w, sizeof(int), 1, f)
        || !fwrite(&h, sizeof(int), 1, f)
        || !writeColors(colors, f)) {
        fclose(f);
        unlink(qPrintable(cacheFile));
        qDebug() << "Failed to write" << cacheFile << errno;
        return std::shared_ptr<Image>();
    }
    fclose(f);

    return Image::load(cacheFile.toStdString(), subRect);
}

static std::shared_ptr<Image> load(const QString &arg)
{
    QRegExp rx("(.*):([0-9]+),([0-9]+)\\+([0-9]+)x([0-9]+)");
    if (rx.exactMatch(arg)) {
        return load(rx.cap(1), QRect(rx.cap(2).toInt(),
                                     rx.cap(3).toInt(),
                                     rx.cap(4).toInt(),
                                     rx.cap(5).toInt()));
    } else {
        return load(arg, QRect());
    }
}

static inline bool compare(const std::shared_ptr<Image> &needleData, int needleX, int needleY,
                           const std::shared_ptr<Image> &haystackData, int haystackX, int haystackY,
                           float threshold)
{
    const Color needle = needleData->color(needleX, needleY);
    const Color haystack = haystackData->color(haystackX, haystackY);
    const float red = powf(haystack.red - needle.red, 2);
    const float green = powf(haystack.green - needle.green, 2);
    const float blue = powf(haystack.blue - needle.blue, 2);

    float distance = sqrtf(red + green + blue);
    const float alphaDistance = std::abs(needle.alpha - haystack.alpha);
    if (verbose >= 2) {
        fprintf(stderr, "%s to %s => %f/%f (%f) at %d,%d (%d,%d)\n",
                qPrintable(needle.toString()),
                qPrintable(haystack.toString()),
                distance,
                alphaDistance,
                threshold,
                needleX, needleY,
                haystackX, haystackY);
    }
    if (alphaDistance > distance)
        distance = alphaDistance;

    static float highest = 0;
    const bool ret = distance <= threshold;
    if (verbose >= 1 && ret && distance > highest) {
        fprintf(stderr, "Allowed %f distance for threshold %f at %d,%d (%d,%d) (%s vs %s)\n",
                distance, threshold,
                needleX, needleY,
                haystackX, haystackY,
                qPrintable(needle.toString()),
                qPrintable(haystack.toString()));
        highest = distance;
    }

    // need to compare alpha
    return ret;
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
    std::shared_ptr<Image> needle, haystack;
    float threshold = 0;
    QString needleString, haystackString;
    for (int i=1; i<argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == "--help" || arg == "-h") {
            usage(stdout);
            return 0;
        } else if (arg == "-v" || arg == "--verbose") {
            ++verbose;
        } else if (arg.startsWith("--cache=")) {
            cache = arg.mid(8);
            if (!cache.isEmpty()) {
                QDir dir;
                dir.mkpath(cache);
            }
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
        } else if (needleString.isEmpty()) {
            needleString = arg;
        } else if (haystackString.isEmpty()) {
            haystackString = arg;
        } else {
            usage(stderr);
            fprintf(stderr, "Too many args\n");
            return 1;
        }
    }
    if (haystackString.isEmpty() || needleString.isEmpty()) {
        usage(stderr);
        fprintf(stderr, "Not enough args\n");
        return 1;
    }

    needle = load(needleString);
    if (!needle) {
        fprintf(stderr, "Failed to decode needle\n");
        return 1;
    }
    haystack = load(haystackString);
    if (!haystack) {
        fprintf(stderr, "Failed to decode haystack\n");
        return 1;
    }
    if (verbose >= 3) {
        fprintf(stderr, "NEEDLE %dx%d", needle->width(), needle->height());

        int height = needle->height();
        int width = needle->width();
        for (int y=0; y<height; ++y) {
            for (int x=0; x<width; ++x) {
                printf("%s ", qPrintable(needle->color(x, y).toString()));
            }
            printf("\n");
        }

        fprintf(stderr, "HAYSTACK %dx%d", haystack->width(), haystack->height());

        height = haystack->height();
        width = haystack->width();
        for (int y=0; y<height; ++y) {
            for (int x=0; x<width; ++x) {
                printf("%s ", qPrintable(haystack->color(x, y).toString()));
            }
            printf("\n");
        }
    }

    const int nw = needle->width();
    const int nh = needle->height();
    const int hw = haystack->width();
    const int hh = haystack->height();
    if (nw > hw) {
        usage(stderr);
        fprintf(stderr, "Bad rects\n");
        return 1;
    }
    if (nh > hh) {
        usage(stderr);
        fprintf(stderr, "Bad rects\n");
        return 1;
    }

    // qDebug() << nw << nh << hw << hh;
    auto tryArea = [&](int x, int y) {
        for (int xx=0; xx<nw; ++xx) {
            for (int yy=0; yy<nh; ++yy) {
                if (!compare(needle, xx, yy, haystack, x + xx, y + yy, threshold)) {
                    return false;
                }
            }
        }
        printf("%d,%d+%dx%d\n", x + haystack->subRect().x(), y + haystack->subRect().y(), nw, nh);
        return true;
    };


    if ((needle->subRect().x() || needle->subRect().y()) && tryArea(needle->subRect().x(), needle->subRect().y())) {
        return 0;
    }

    for (int x=0; x<=hw - nw; ++x) {
        // qDebug() << "shit" << x;
        for (int y=0; y<=hh - nh; ++y) {
            // qDebug() << "balls" << y;
            if (tryArea(x, y)) {
                return 0;
            }
        }
    }

    if (verbose)
        fprintf(stderr, "Couldn't find area\n");
    return 1;
}
