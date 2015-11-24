#include <QtGui>

QString cache;
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

QDataStream &operator<<(QDataStream &ds, const Color &c)
{
    ds << c.red << c.green << c.blue << c.alpha;
    return ds;
}
QDataStream &operator>>(QDataStream &ds, Color &col)
{
    quint8 tmp;
    ds >> tmp;
    col.red = static_cast<float>(tmp);
    ds >> tmp;
    col.green = static_cast<float>(tmp);
    ds >> tmp;
    col.blue = static_cast<float>(tmp);
    ds >> col.alpha;
    return ds;
}

QByteArray encode(int width, int height, bool allTransparent, const QVector<Color> &cols)
{
    QByteArray ret;
    ret.resize(sizeof(width) + sizeof(height) + sizeof(allTransparent) + (cols.size() * sizeof(Color)));
    char *ptr = ret.data();
    *reinterpret_cast<int*>(ptr) = width;
    *reinterpret_cast<int*>(ptr + sizeof(int)) = height;
    *reinterpret_cast<int*>(ptr + sizeof(int) + sizeof(int)) = allTransparent;
    memcpy(ptr + sizeof(int) + sizeof(int) + sizeof(bool), cols.constData(), sizeof(Color) * cols.size());
    return ret;
}

struct Image {
    Image()
        : width(0), height(0), allTransparent(false)
    {}
    QVector<Color> colors;
    int width, height;
    bool allTransparent;

    Image sub(const QRect &subRect) const
    {
        if (!subRect.isNull() &&
            (subRect.x() != 0 || subRect.y() != 0 || subRect.width() != width || subRect.height() != height)) {
            QVector<Color> cols;
            cols.reserve(subRect.width() * subRect.height());
            bool allTransparent = true;
            for (int y=subRect.y(); y<subRect.height() + subRect.y(); ++y) {
                for (int x=subRect.x(); x<subRect.width() + subRect.x(); ++x) {
                    const Color &c = colors.at((y * width) + x);
                    if (allTransparent)
                        allTransparent = !c.alpha;
                    cols.append(c);
                }
            }
            Image ret;
            ret.colors = cols;
            ret.width = subRect.width();
            ret.height = subRect.height();
            return ret;
        }
        return *this;
    }
};


QDataStream &operator<<(QDataStream &ds, const Image &image)
{
    ds << image.colors << image.width << image.height << image.allTransparent;
    return ds;
}
QDataStream &operator>>(QDataStream &ds, Image &image)
{
    ds >> image.colors >> image.width >> image.height >> image.allTransparent;
    return ds;
}

static Image load(const QString &file, const QRect &subRect)
{
    QString cacheFile;
    if (!cache.isEmpty()) {
        cacheFile = cache + "/" + QFileInfo(file).fileName() + ".cache";
        QFile f(cacheFile);
        if (f.open(QIODevice::ReadOnly)) {
            if (verbose)
                fprintf(stderr, "Read from cache %s\n", qPrintable(cacheFile));
            Image ret;
            QElapsedTimer timer;
            timer.start();
            QDataStream ds(&f);
            ds >> ret;
            qDebug() << timer.elapsed();
            if (!subRect.isNull())
                ret = ret.sub(subRect);
            return ret;
        }
    }
    QImageReader reader(file);
    QImage image;
    reader.read(&image);
    if (image.isNull()) {
        qDebug() << "Couldn't decode" << file;
        return Image();
    }

    if (!cache.isEmpty()) {
        QFile file(cacheFile);
        if (file.open(QIODevice::WriteOnly)) {
            Image ret;
            const int w = image.width();
            const int h = image.height();
            ret.colors.resize(w * h);
            ret.allTransparent = true;
            for (int y=0; y<h; ++y) {
                for (int x=0; x<w; ++x) {
                    Color &c = ret.colors[x + (y * w)];
                    c = QColor::fromRgba(image.pixel(x, y));
                    if (ret.allTransparent)
                        ret.allTransparent = c.alpha == 0;
                }
            }

            ret.width = image.width();
            ret.height = image.height();
            QDataStream ds(&file);
            ds << ret;
            if (!subRect.isNull())
                ret = ret.sub(subRect);
            if (verbose)
                fprintf(stderr, "Wrote to cache %s\n", qPrintable(cacheFile));
            return ret;
        } else {
            qDebug() << "Failed to open" << cacheFile << "for writing";
        }
    }

    if (!subRect.isNull() && subRect.size() != image.size()) {
        image = image.copy(subRect);
    }
    Image ret;
    const int w = image.width();
    const int h = image.height();
    ret.colors.resize(w * h);
    ret.allTransparent = true;
    for (int y=0; y<h; ++y) {
        for (int x=0; x<w; ++x) {
            Color &c = ret.colors[x + (y * w)];
            c = QColor::fromRgba(image.pixel(x, y));
            if (ret.allTransparent)
                ret.allTransparent = c.alpha == 0;
        }
    }

    ret.width = image.width();
    ret.height = image.height();

    return ret;
}

static Image load(const QString &arg)
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

static inline bool compare(const Image &needleData, int needleX, int needleY,
                           const Image &haystackData, int haystackX, int haystackY,
                           float threshold)
{
    const Color &needle = needleData.colors.at(needleX + (needleY * needleData.width));
    const Color &haystack = haystackData.colors.at(haystackX + (haystackY * haystackData.width));
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
            "  --cache=[directory]                Use this directory for caches\n"
            "  --threshold=[threshold]            Set threshold value\n");
}


int main(int argc, char **argv)
{
    QCoreApplication a(argc, argv);
    Image needle, haystack;
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
                threshold *= 100;
                threshold /= 256;
            }
            // if (arg.endsWith("%")) {
            //     qDebug() << "foobar" << arg << threshold;
            // }
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
    if (!needle.colors.size()) {
        fprintf(stderr, "Failed to decode needle\n");
        return 1;
    }
    if (needle.allTransparent) {
        printf("0,0+0x0\n");
        return 0;
    }

    haystack = load(haystackString);
    if (!haystack.colors.size()) {
        fprintf(stderr, "Failed to decode haystack\n");
        return 1;
    }
    if (haystack.allTransparent) {
        return 1;
    }

    if (verbose >= 3) {
        fprintf(stderr, "NEEDLE %dx%d", needle.width, needle.height);
        for (int i=0; i<needle.colors.size(); ++i) {
            if (i % needle.width == 0) {
                fprintf(stderr, "\n");
            } else {
                fprintf(stderr, " ");
            }
            fprintf(stderr, "%s ", qPrintable(needle.colors.at(i).toString()));
        }
        fprintf(stderr, "\nHAYSTACK %dx%d", haystack.width, haystack.height);
        for (int i=0; i<haystack.colors.size(); ++i) {
            if (i % haystack.width == 0) {
                fprintf(stderr, "\n");
            } else {
                fprintf(stderr, " ");
            }
            fprintf(stderr, "%s ", qPrintable(haystack.colors.at(i).toString()));
        }
        fprintf(stderr, "\n");
    }

    const int nw = needle.width;
    const int nh = needle.height;
    const int hw = haystack.width;
    const int hh = haystack.height;
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
    for (int x=0; x<=hw - nw; ++x) {
        // qDebug() << "shit" << x;
        for (int y=0; y<=hh - nh; ++y) {
            // qDebug() << "balls" << y;
            bool ok = true;
            for (int xx=0; xx<nw && ok; ++xx) {
                for (int yy=0; yy<nh; ++yy) {
                    if (!compare(needle, xx, yy,
                                 haystack, x + xx, y + yy,
                                 threshold)) {
                        ok = false;
                        break;
                    }
                }
            }
            if (ok) {
                printf("%d,%d+%dx%d\n", x, y, nw, nh);
                return 0;
            }
        }
    }

    if (verbose)
        fprintf(stderr, "Couldn't find area\n");
    return 1;
}
