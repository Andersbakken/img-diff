#include <QtGui>

void usage(FILE *f)
{
    fprintf(f,
            "img-diff [options...] imga imgb\n"
            "  --verbose|-v                       Be verbose\n"
            "  --threshold=[threshold]            Set threshold value\n");
}

bool cache = false;


struct Color
{
    Color(const QColor &col = QColor())
        : color(col), red(col.red()), green(col.green()), blue(col.blue()), alpha(col.alpha())
    {}

    QString toString() const
    {
        char buf[1024];
        snprintf(buf, sizeof(buf), "%02x%02x%02x%02x",
                 color.red(),
                 color.green(),
                 color.blue(),
                 color.alpha());
        return QString::fromLocal8Bit(buf);
    }

    QColor color;
    float red, green, blue;
    quint8 alpha;
};

struct Image {
    Image()
        : width(0), height(0), allAlpha(false)
    {}
    QVector<Color> colors;
    int width, height;
    bool allAlpha;
};

static void decodeImage(const QImage &image, QVector<Color> &cols, bool &allAlpha)
{
    const int w = image.width();
    const int h = image.height();
    cols.resize(w * h);
    allAlpha = true;
    for (int y=0; y<h; ++y) {
        for (int x=0; x<w; ++x) {
            Color &c = cols[x + (y * w)];
            c = QColor::fromRgba(image.pixel(x, y));
            if (allAlpha)
                allAlpha = c.alpha == 0;
        }
    }
}


static Image loadSubImage(const QString &file, const QRect &subRect)
{
    QImageReader reader(file);
    QImage image;
    reader.read(&image);
    // if (!subRect.isNull())
    //     reader.setClipRect(subRect);

    // QImage image(file);
    // qDebug() << image.pixel(0, 0);
    // qDebug() << QColor(image.pixel(0, 0));
    // qDebug() << image.format();
    if (image.isNull()) {
        qDebug() << "Couldn't decode" << file;
        return Image();
    }
    // QSet<int> seen;
    // for (int x=0; x<image.width(); ++x) {
    //     for (int y=0; y<image.height(); ++y) {
    //         int idx = image.pixelIndex(x, y);
    //         if (!seen.contains(idx)) {
    //             seen.insert(idx);
    //             qDebug() << x << y << idx << image.pixel(x, y) << image.colorTable().at(idx)
    //                      << QColor::fromRgba(image.colorTable().at(idx));
    //         }
    //     }
    // }

    // qDebug() << image.pixel(1, 1) << image.pixelIndex(1, 1) << image.colorTable();

    // if (image.format() != QImage::Format_ARGB32) {
    //     QImage result(image.size(), QImage::Format_ARGB32);
    //     {
    //         QPainter p(&result);
    //         p.fillRect(result.rect(), QColor(Qt::transparent));
    //         p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    //         p.drawImage(0, 0, image);
    //     }
    //     // image.save("/tmp/balls1.png", "PNG");
    //     image = result;
    //     // result.save("/tmp/balls.png", "PNG");
    //     // image = image.convertToFormat(QImage::Format_ARGB32);
    // }

    // qDebug() << image.pixel(1, 1) << QColor(image.pixel(1, 1));
    // qDebug() << image.pixel(0, 0);

    if (!subRect.isNull() && subRect.size() != image.size()) {
        image = image.copy(subRect);
    }
    Image ret;
    decodeImage(image, ret.colors, ret.allAlpha);
    ret.width = image.width();
    ret.height = image.height();

    return ret;
}

static Image loadSubImage(const QString &arg)
{
    QRegExp rx("(.*):([0-9]+),([0-9]+)\\+([0-9]+)x([0-9]+)");
    if (rx.exactMatch(arg)) {
        return loadSubImage(rx.cap(1), QRect(rx.cap(2).toInt(),
                                             rx.cap(3).toInt(),
                                             rx.cap(4).toInt(),
                                             rx.cap(5).toInt()));
    } else {
        return loadSubImage(arg, QRect());
    }
}

int verbose = 0;

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

int main(int argc, char **argv)
{
    // {
    //     Color a(QColor(251, 1, 2, 255));
    //     Color b(QColor(252, 0, 0, 255));
    //     compare(a, b, 0);
    //     return 0;
    // }
    QCoreApplication a(argc, argv);
    Image needle, haystack;
    float threshold = 0;
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
                threshold *= 100;
                threshold /= 256;
            }
            // if (arg.endsWith("%")) {
            //     qDebug() << "foobar" << arg << threshold;
            // }
        } else if (!needle.colors.size()) {
            needle = loadSubImage(arg);
            if (!needle.colors.size()) {
                fprintf(stderr, "Failed to decode needle\n");
                return 1;
            }
            if (needle.allAlpha) {
                printf("0,0+0x0\n");
                return 0;
            }
        } else if (!haystack.colors.size()) {
            haystack = loadSubImage(arg);
            if (!haystack.colors.size()) {
                fprintf(stderr, "Failed to decode haystack\n");
                return 1;
            }
        } else {
            usage(stderr);
            fprintf(stderr, "Too many args\n");
            return 1;
        }
    }
    if (!needle.colors.size() || !haystack.colors.size()) {
        usage(stderr);
        fprintf(stderr, "Not enough args\n");
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
