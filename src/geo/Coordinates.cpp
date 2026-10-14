#include "libmapa/Coordinates.h"

#include <QRegularExpression>
#include <QtMath>
#include <QStringList>
#include <QVector>

#include <array>
#include <cstddef>
#include <cmath>

namespace libmapa {

namespace {

// --- Elipsoide WGS84 y constantes UTM -----------------------------------------
constexpr double kA = 6378137.0;                 // semieje mayor
constexpr double kF = 1.0 / 298.257223563;       // aplanamiento
constexpr double kK0 = 0.9996;                   // factor de escala en el meridiano central
constexpr double kFalsoEste = 500000.0;
constexpr double kFalsoNorteSur = 10000000.0;
constexpr double kPi = 3.14159265358979323846;

const QString kBandas = QStringLiteral("CDEFGHJKLMNPQRSTUVWX");   // 8° desde 80°S
const QString kFilas = QStringLiteral("ABCDEFGHJKLMNPQRSTUV");    // letras de fila MGRS
const QString kColumnas[3] = { QStringLiteral("ABCDEFGH"), QStringLiteral("JKLMNPQR"),
                               QStringLiteral("STUVWXYZ") };

// Coeficientes de las series de Kruger (Karney 2011, orden 6 en n), calculados
// una vez: alfa para geografica -> UTM, beta para la inversa, y A (radio del
// circulo rectificante).
struct Kruger {
    double e = 0.0;                 // excentricidad
    double A = 0.0;
    std::array<double, 7> alfa{};   // indices 1..6
    std::array<double, 7> beta{};

    // Evalua los polinomios en n = f / (2 - f).
    Kruger()
    {
        const double n = kF / (2.0 - kF);
        const double n2 = n * n, n3 = n2 * n, n4 = n3 * n, n5 = n4 * n, n6 = n5 * n;
        e = std::sqrt(kF * (2.0 - kF));
        A = kA / (1.0 + n) * (1.0 + n2 / 4.0 + n4 / 64.0 + n6 / 256.0);
        alfa[1] = n / 2.0 - 2.0 * n2 / 3.0 + 5.0 * n3 / 16.0 + 41.0 * n4 / 180.0
                  - 127.0 * n5 / 288.0 + 7891.0 * n6 / 37800.0;
        alfa[2] = 13.0 * n2 / 48.0 - 3.0 * n3 / 5.0 + 557.0 * n4 / 1440.0
                  + 281.0 * n5 / 630.0 - 1983433.0 * n6 / 1935360.0;
        alfa[3] = 61.0 * n3 / 240.0 - 103.0 * n4 / 140.0 + 15061.0 * n5 / 26880.0
                  + 167603.0 * n6 / 181440.0;
        alfa[4] = 49561.0 * n4 / 161280.0 - 179.0 * n5 / 168.0 + 6601661.0 * n6 / 7257600.0;
        alfa[5] = 34729.0 * n5 / 80640.0 - 3418889.0 * n6 / 1995840.0;
        alfa[6] = 212378941.0 * n6 / 319334400.0;
        beta[1] = n / 2.0 - 2.0 * n2 / 3.0 + 37.0 * n3 / 96.0 - n4 / 360.0
                  - 81.0 * n5 / 512.0 + 96199.0 * n6 / 604800.0;
        beta[2] = n2 / 48.0 + n3 / 15.0 - 437.0 * n4 / 1440.0 + 46.0 * n5 / 105.0
                  - 1118711.0 * n6 / 3870720.0;
        beta[3] = 17.0 * n3 / 480.0 - 37.0 * n4 / 840.0 - 209.0 * n5 / 4480.0
                  + 5569.0 * n6 / 90720.0;
        beta[4] = 4397.0 * n4 / 161280.0 - 11.0 * n5 / 504.0 - 830251.0 * n6 / 7257600.0;
        beta[5] = 4583.0 * n5 / 161280.0 - 108847.0 * n6 / 3991680.0;
        beta[6] = 20648693.0 * n6 / 638668800.0;
    }
};

// Instancia unica (se construye en el primer uso, sin carreras desde C++11).
const Kruger &kruger()
{
    static const Kruger k;
    return k;
}

double rad(double g) { return g * kPi / 180.0; }
double grados(double r) { return r * 180.0 / kPi; }

// Longitud del meridiano central de una zona, en grados.
double meridianoCentral(int zona) { return -183.0 + 6.0 * zona; }

// Letra de banda de latitud (C..X); X abarca 72..84°N. Nula fuera de UTM.
QChar banda(double lat)
{
    if (lat < -80.0 || lat > 84.0)
        return QChar();
    const int i = qMin(19, int(std::floor((lat + 80.0) / 8.0)));
    return kBandas.at(i);
}

// Geografica -> (x, y) de Transversa de Mercator respecto al meridiano lon0, en
// metros y SIN falsos este/norte (y negativa al sur). Karney 2011, ec. 7-11.
void tmDirecta(double lat, double lon, double lon0, double &x, double &y)
{
    const Kruger &k = kruger();
    const double phi = rad(lat);
    const double lam = rad(lon - lon0);
    const double tau = std::tan(phi);
    const double sigma = std::sinh(k.e * std::atanh(k.e * tau / std::sqrt(1.0 + tau * tau)));
    const double tauP = tau * std::sqrt(1.0 + sigma * sigma) - sigma * std::sqrt(1.0 + tau * tau);
    const double xiP = std::atan2(tauP, std::cos(lam));
    const double etaP = std::asinh(std::sin(lam) / std::sqrt(tauP * tauP + std::cos(lam) * std::cos(lam)));
    double xi = xiP, eta = etaP;
    for (std::size_t j = 1; j <= 6; ++j) {
        const double dj = 2.0 * double(j);
        xi += k.alfa[j] * std::sin(dj * xiP) * std::cosh(dj * etaP);
        eta += k.alfa[j] * std::cos(dj * xiP) * std::sinh(dj * etaP);
    }
    x = kK0 * k.A * eta;
    y = kK0 * k.A * xi;
}

// Inversa de tmDirecta: (x, y) sin falsos -> geografica. La latitud sale de
// iterar Newton sobre tau (converge en 2-3 pasos).
QGeoCoordinate tmInversa(double x, double y, double lon0)
{
    const Kruger &k = kruger();
    const double eta = x / (kK0 * k.A);
    const double xi = y / (kK0 * k.A);
    double xiP = xi, etaP = eta;
    for (std::size_t j = 1; j <= 6; ++j) {
        const double dj = 2.0 * double(j);
        xiP -= k.beta[j] * std::sin(dj * xi) * std::cosh(dj * eta);
        etaP -= k.beta[j] * std::cos(dj * xi) * std::sinh(dj * eta);
    }
    const double sinhEtaP = std::sinh(etaP);
    const double sinXiP = std::sin(xiP), cosXiP = std::cos(xiP);
    const double tauP = sinXiP / std::sqrt(sinhEtaP * sinhEtaP + cosXiP * cosXiP);

    const double e2 = k.e * k.e;
    double tau = tauP;
    for (int i = 0; i < 10; ++i) {
        const double sigma = std::sinh(k.e * std::atanh(k.e * tau / std::sqrt(1.0 + tau * tau)));
        const double tauI = tau * std::sqrt(1.0 + sigma * sigma) - sigma * std::sqrt(1.0 + tau * tau);
        const double d = (tauP - tauI) / std::sqrt(1.0 + tauI * tauI)
                         * (1.0 + (1.0 - e2) * tau * tau)
                         / ((1.0 - e2) * std::sqrt(1.0 + tau * tau));
        tau += d;
        if (std::fabs(d) < 1e-12)
            break;
    }
    const double lam = std::atan2(sinhEtaP, cosXiP);
    return QGeoCoordinate(grados(std::atan(tau)), lon0 + grados(lam));
}

// Juego (1..6) de letras MGRS de una zona: decide columnas y desfase de filas.
int juegoMgrs(int zona)
{
    const int s = zona % 6;
    return s == 0 ? 6 : s;
}

// Numero con relleno de ceros a la izquierda (para los digitos MGRS).
QString digitos(long long v, int ancho)
{
    return QStringLiteral("%1").arg(v, ancho, 10, QLatin1Char('0'));
}

// Grados sexagesimales de un valor absoluto: "23°06'48.96\"" (DMS) o
// "23°06.8160'" (DDM).
QString sexagesimal(double abs, bool conSegundos)
{
    const QChar g(0x00B0);
    if (conSegundos) {
        long long centesimas = std::llround(abs * 360000.0);   // en 1/100 de segundo
        const long long gr = centesimas / 360000;
        centesimas -= gr * 360000;
        const long long mi = centesimas / 6000;
        centesimas -= mi * 6000;
        return QStringLiteral("%1%2%3'%4.%5\"").arg(gr).arg(g).arg(mi, 2, 10, QLatin1Char('0'))
            .arg(centesimas / 100, 2, 10, QLatin1Char('0')).arg(centesimas % 100, 2, 10, QLatin1Char('0'));
    }
    long long diezmil = std::llround(abs * 600000.0);          // en 1/10000 de minuto
    const long long gr = diezmil / 600000;
    diezmil -= gr * 600000;
    return QStringLiteral("%1%2%3.%4'").arg(gr).arg(g).arg(diezmil / 10000, 2, 10, QLatin1Char('0'))
        .arg(diezmil % 10000, 4, 10, QLatin1Char('0'));
}

// Un angulo sexagesimal "23°06'48.96\"N" (minutos y segundos opcionales) ->
// grados con signo. \a eje: true = latitud (N/S), false = longitud (E/W/O).
bool leerSexagesimal(const QRegularExpressionMatch &m, double &valor, bool &esLatitud)
{
    const double g = m.captured(1).toDouble();
    const double mi = m.captured(2).isEmpty() ? 0.0 : m.captured(2).toDouble();
    const double s = m.captured(3).isEmpty() ? 0.0 : m.captured(3).toDouble();
    if (mi >= 60.0 || s >= 60.0)
        return false;
    const QChar h = m.captured(4).toUpper().at(0);
    esLatitud = (h == QLatin1Char('N') || h == QLatin1Char('S'));
    valor = g + mi / 60.0 + s / 3600.0;
    if (h == QLatin1Char('S') || h == QLatin1Char('W') || h == QLatin1Char('O'))
        valor = -valor;
    return true;
}

} // namespace

// Zona natural: franjas de 6° desde 180°W, con las excepciones historicas de
// la costa noruega (32V ensanchada) y Svalbard (31X/33X/35X/37X de 9-12°). El
// meridiano 180 exacto cae en la zona 1, como en GeoTrans (NGA).
int utmZoneFor(const QGeoCoordinate &coord)
{
    if (!coord.isValid())
        return 0;
    const double lat = coord.latitude();
    double lon = coord.longitude();
    if (lat < -80.0 || lat > 84.0)
        return 0;
    if (lon >= 180.0)
        lon -= 360.0;
    int zona = int(std::floor((lon + 180.0) / 6.0)) + 1;
    zona = qBound(1, zona, 60);
    if (lat >= 56.0 && lat < 64.0 && lon >= 3.0 && lon < 12.0)
        zona = 32;
    if (lat >= 72.0) {
        if (lon >= 0.0 && lon < 9.0)        zona = 31;
        else if (lon >= 9.0 && lon < 21.0)  zona = 33;
        else if (lon >= 21.0 && lon < 33.0) zona = 35;
        else if (lon >= 33.0 && lon < 42.0) zona = 37;
    }
    return zona;
}

// Proyecta en la zona natural (o la forzada) y aplica los falsos este/norte.
UtmCoordinate toUtm(const QGeoCoordinate &coord, int forcedZone)
{
    UtmCoordinate u;
    const int zona = (forcedZone >= 1 && forcedZone <= 60) ? forcedZone : utmZoneFor(coord);
    if (zona == 0 || !coord.isValid() || coord.latitude() < -80.0 || coord.latitude() > 84.0)
        return u;
    double x = 0.0, y = 0.0;
    tmDirecta(coord.latitude(), coord.longitude(), meridianoCentral(zona), x, y);
    u.zone = zona;
    u.band = banda(coord.latitude());
    u.north = coord.latitude() >= 0.0;
    u.easting = x + kFalsoEste;
    u.northing = u.north ? y : y + kFalsoNorteSur;
    return u;
}

// Quita los falsos este/norte y desproyecta respecto al meridiano de la zona.
QGeoCoordinate fromUtm(const UtmCoordinate &utm)
{
    if (!utm.isValid())
        return QGeoCoordinate();
    const double y = utm.north ? utm.northing : utm.northing - kFalsoNorteSur;
    return tmInversa(utm.easting - kFalsoEste, y, meridianoCentral(utm.zone));
}

// UTM -> MGRS: columna por el este (juego de letras de la zona), fila por el
// norte modulo 2000 km (desfase de 5 letras en las zonas pares) y los digitos
// del resto, truncados a la precision.
QString toMgrs(const QGeoCoordinate &coord, int precision, bool spaced)
{
    const UtmCoordinate u = toUtm(coord);
    if (!u.isValid())
        return QString();
    const int p = qBound(1, precision, 5);
    const int juego = juegoMgrs(u.zone);

    const int col = int(std::floor(u.easting / 100000.0)) - 1;          // 0..7
    const QString &letrasCol = kColumnas[(juego - 1) % 3];
    if (col < 0 || col >= letrasCol.size())
        return QString();
    const int desfase = (juego % 2 == 0) ? 5 : 0;
    const int fila = (int(std::floor(u.northing / 100000.0)) + desfase) % 20;

    const double divisor = std::pow(10.0, 5 - p);
    const long long e = static_cast<long long>(std::floor(std::fmod(u.easting, 100000.0) / divisor));
    const long long n = static_cast<long long>(std::floor(std::fmod(u.northing, 100000.0) / divisor));

    const QString zona = QString::number(u.zone) + u.band;
    const QString cuadro = QString(letrasCol.at(col)) + kFilas.at(fila);
    if (spaced)
        return zona + QLatin1Char(' ') + cuadro + QLatin1Char(' ') + digitos(e, p)
               + QLatin1Char(' ') + digitos(n, p);
    return zona + cuadro + digitos(e, p) + digitos(n, p);
}

// MGRS -> UTM -> geografica. El norte se reconstruye con la fila (modulo 2000
// km) y la banda: se suman ciclos de 2000 km hasta caer dentro de la banda (una
// banda mide ~900 km, asi que hay una sola solucion).
QGeoCoordinate fromMgrs(const QString &mgrs, bool *ok)
{
    if (ok)
        *ok = false;
    QString s = mgrs.toUpper();
    s.remove(QRegularExpression(QStringLiteral("\\s")));
    static const QRegularExpression re(
        QStringLiteral("^(\\d{1,2})([C-HJ-NP-X])([A-HJ-NP-Z])([A-HJ-NP-V])(\\d*)$"));
    const QRegularExpressionMatch m = re.match(s);
    if (!m.hasMatch())
        return QGeoCoordinate();
    const int zona = m.captured(1).toInt();
    const QString num = m.captured(5);
    if (zona < 1 || zona > 60 || num.size() % 2 != 0 || num.size() > 10)
        return QGeoCoordinate();

    const int juego = juegoMgrs(zona);
    const int col = int(kColumnas[(juego - 1) % 3].indexOf(m.captured(3).at(0)));
    const int filaLetra = int(kFilas.indexOf(m.captured(4).at(0)));
    if (col < 0 || filaLetra < 0)
        return QGeoCoordinate();          // letra que no existe en el juego de la zona

    const int p = int(num.size()) / 2;
    const double mult = std::pow(10.0, 5 - p);
    const double e = p > 0 ? num.left(p).toDouble() * mult : 0.0;
    const double n = p > 0 ? num.mid(p).toDouble() * mult : 0.0;

    const QChar letraBanda = m.captured(2).at(0);
    const int iBanda = int(kBandas.indexOf(letraBanda));
    const bool norte = letraBanda >= QLatin1Char('N');
    const double latMin = -80.0 + 8.0 * iBanda;

    const int desfase = (juego % 2 == 0) ? 5 : 0;
    const int filaBase = (filaLetra - desfase + 20) % 20;
    double northing = filaBase * 100000.0 + n;
    // Norte minimo de la banda (en su meridiano central, donde es menor).
    double x = 0.0, yMin = 0.0;
    tmDirecta(latMin, meridianoCentral(zona), meridianoCentral(zona), x, yMin);
    const double nMin = norte ? yMin : yMin + kFalsoNorteSur;
    while (northing < nMin - 1.0)   // 1 m de holgura por redondeo en el limite
        northing += 2000000.0;

    UtmCoordinate u;
    u.zone = zona;
    u.band = letraBanda;
    u.north = norte;
    u.easting = (col + 1) * 100000.0 + e;
    u.northing = northing;
    const QGeoCoordinate c = fromUtm(u);
    if (ok)
        *ok = c.isValid();
    return c;
}

// Texto en el formato pedido. UTM redondea al metro; MGRS trunca al metro.
QString formatCoordinate(const QGeoCoordinate &coord, CoordinateFormat format)
{
    if (!coord.isValid())
        return QString();
    const double lat = coord.latitude(), lon = coord.longitude();
    switch (format) {
    case CoordinateFormat::Decimal:
        return QStringLiteral("%1, %2").arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6);
    case CoordinateFormat::DMS:
    case CoordinateFormat::DDM: {
        const bool seg = format == CoordinateFormat::DMS;
        return sexagesimal(std::fabs(lat), seg) + (lat >= 0 ? QLatin1Char('N') : QLatin1Char('S'))
               + QLatin1Char(' ')
               + sexagesimal(std::fabs(lon), seg) + (lon >= 0 ? QLatin1Char('E') : QLatin1Char('W'));
    }
    case CoordinateFormat::UTM: {
        const UtmCoordinate u = toUtm(coord);
        if (!u.isValid())
            return QString();
        return QStringLiteral("%1%2 %3 %4").arg(u.zone).arg(u.band)
            .arg(std::llround(u.easting)).arg(std::llround(u.northing));
    }
    case CoordinateFormat::MGRS:
        return toMgrs(coord, 5, true);
    }
    return QString();
}

// Prueba los formatos de mas a menos especifico: MGRS, UTM, sexagesimal con
// hemisferio y, por ultimo, un par de numeros decimales "lat, lon".
QGeoCoordinate parseCoordinate(const QString &text, bool *ok)
{
    if (ok)
        *ok = false;
    const QString t = text.trimmed();
    if (t.isEmpty())
        return QGeoCoordinate();

    bool bien = false;
    QGeoCoordinate c = fromMgrs(t, &bien);
    if (bien) {
        if (ok)
            *ok = true;
        return c;
    }

    static const QRegularExpression reUtm(QStringLiteral(
        "^(\\d{1,2})\\s*([C-HJ-NP-Xc-hj-np-x])\\s+(\\d+(?:\\.\\d+)?)\\s*(?:m?E)?\\s+(\\d+(?:\\.\\d+)?)\\s*(?:m?N)?$"));
    QRegularExpressionMatch m = reUtm.match(t);
    if (m.hasMatch()) {
        UtmCoordinate u;
        u.zone = m.captured(1).toInt();
        u.band = m.captured(2).toUpper().at(0);
        u.north = u.band >= QLatin1Char('N');
        u.easting = m.captured(3).toDouble();
        u.northing = m.captured(4).toDouble();
        c = fromUtm(u);
        if (c.isValid() && ok)
            *ok = true;
        return c;
    }

    static const QRegularExpression reSex(QStringLiteral(
        "(\\d+(?:[.,]\\d+)?)\\s*[\\x{00B0}\\x{00BA}]\\s*"
        "(?:(\\d+(?:[.,]\\d+)?)\\s*['\\x{2019}\\x{2032}]\\s*)?"
        "(?:(\\d+(?:[.,]\\d+)?)\\s*(?:\"|\\x{201D}|\\x{2033}|''))?\\s*([NSEWOnsewo])"));
    // (Simbolos de grado, minuto y segundo como escapes PCRE: el fuente queda
    // en ASCII y compila igual con MSVC sin /utf-8.)
    QString tn = t;
    tn.replace(QLatin1Char(','), QLatin1Char('.'));   // decimales con coma
    auto it = reSex.globalMatch(tn);
    QVector<QRegularExpressionMatch> partes;
    while (it.hasNext())
        partes.append(it.next());
    if (partes.size() == 2) {
        double a = 0.0, b = 0.0;
        bool latA = false, latB = false;
        if (leerSexagesimal(partes[0], a, latA) && leerSexagesimal(partes[1], b, latB)
            && latA != latB) {
            c = latA ? QGeoCoordinate(a, b) : QGeoCoordinate(b, a);
            if (c.isValid() && ok)
                *ok = true;
            return c;
        }
    }

    static const QRegularExpression reDec(QStringLiteral(
        "^([-+]?\\d+(?:\\.\\d+)?)\\s*[,;\\s]\\s*([-+]?\\d+(?:\\.\\d+)?)$"));
    m = reDec.match(t);
    if (m.hasMatch()) {
        c = QGeoCoordinate(m.captured(1).toDouble(), m.captured(2).toDouble());
        if (c.isValid() && ok)
            *ok = true;
        return c;
    }
    return QGeoCoordinate();
}

namespace {

// Funcion q(lat) de la latitud autalica (Snyder, "Map Projections - A Working
// Manual", ec. 3-12) para el elipsoide WGS84.
double qAutalica(double latRad)
{
    constexpr double kE2 = kF * (2.0 - kF);      // primera excentricidad al cuadrado
    const double e = std::sqrt(kE2);
    const double s = std::sin(latRad);
    return (1.0 - kE2) * (s / (1.0 - kE2 * s * s)
                          - std::log((1.0 - e * s) / (1.0 + e * s)) / (2.0 * e));
}

// Seno de la latitud autalica: la latitud de la esfera de igual superficie que
// deja entre el ecuador y ese paralelo la misma area que el elipsoide.
double senoAutalico(double latDeg)
{
    static const double qPolo = qAutalica(kPi / 2.0);
    return qAutalica(qDegreesToRadians(latDeg)) / qPolo;
}

} // namespace

// Area de Chamberlain y Duquette (JPL, 2007) sobre la esfera autalica: suma
// por lados de (lon2 - lon1) * (2 + sin b1 + sin b2), por R^2/2, con b la
// latitud AUTALICA. Asi un trapecio de meridianos y paralelos sale exacto en
// el elipsoide, y cualquier otro poligono queda muy cerca.
double polygonArea(const QVector<QGeoCoordinate> &ring)
{
    // Radio autalico de WGS84: R^2 = a^2 * qPolo / 2.
    static const double kR2 = kA * kA * qAutalica(kPi / 2.0) / 2.0;
    QVector<QGeoCoordinate> v;
    v.reserve(ring.size());
    for (const QGeoCoordinate &c : ring)
        if (c.isValid())
            v.append(c);
    if (v.size() > 1 && v.first().latitude() == v.last().latitude()
        && v.first().longitude() == v.last().longitude())
        v.removeLast();                       // anillo cerrado: sin repetir
    if (v.size() < 3)
        return 0.0;

    double suma = 0.0;
    for (int i = 0, n = int(v.size()); i < n; ++i) {
        const QGeoCoordinate &a = v[i];
        const QGeoCoordinate &b = v[(i + 1) % n];
        suma += qDegreesToRadians(b.longitude() - a.longitude())
              * (2.0 + senoAutalico(a.latitude()) + senoAutalico(b.latitude()));
    }
    return std::abs(suma) * kR2 / 2.0;
}

// Suma de distancias ortodromicas (las de QGeoCoordinate) entre vertices
// validos consecutivos; \a closed anade el lado de cierre.
double pathLength(const QVector<QGeoCoordinate> &path, bool closed)
{
    QVector<QGeoCoordinate> v;
    v.reserve(path.size());
    for (const QGeoCoordinate &c : path)
        if (c.isValid())
            v.append(c);
    double total = 0.0;
    for (int i = 1; i < v.size(); ++i)
        total += v[i - 1].distanceTo(v[i]);
    if (closed && v.size() > 2)
        total += v.last().distanceTo(v.first());
    return total;
}

} // namespace libmapa
