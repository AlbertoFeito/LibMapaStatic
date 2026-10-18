#include "io/PlaceIndex.h"

#include "db/SqliteConnectionPool.h"

#include <QCoreApplication>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>   // Qt5 solo declara QVariant aqui para bindValue/value

#include <atomic>

namespace libmapa {

namespace {

// Id de conexion unico por instancia. No se usa la direccion del objeto: una
// instancia nueva en la misma direccion heredaria la conexion cacheada de otra BD.
QString nextConnId()
{
    static std::atomic<quint32> next{1};
    return QStringLiteral("lugares_%1").arg(next++);
}

// Escapa % _ y \ para usar el texto dentro de un LIKE ... ESCAPE '\'.
QString escapeLike(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        if (c == QLatin1Char('%') || c == QLatin1Char('_') || c == QLatin1Char('\\'))
            out += QLatin1Char('\\');
        out += c;
    }
    return out;
}

} // namespace

// Nombre del tipo en español, para listas y etiquetas.
QString Place::typeLabel() const
{
    struct Par { const char *tipo; const char *texto; };
    static const Par tabla[] = {
        { "city", QT_TRANSLATE_NOOP("libmapa::Place", "ciudad") },
        { "town", QT_TRANSLATE_NOOP("libmapa::Place", "pueblo") },
        { "village", QT_TRANSLATE_NOOP("libmapa::Place", "aldea") },
        { "hamlet", QT_TRANSLATE_NOOP("libmapa::Place", "caserío") },
        { "suburb", QT_TRANSLATE_NOOP("libmapa::Place", "barrio") },
        { "island", QT_TRANSLATE_NOOP("libmapa::Place", "isla") },
        { "islet", QT_TRANSLATE_NOOP("libmapa::Place", "islote") },
        { "locality", QT_TRANSLATE_NOOP("libmapa::Place", "paraje") },
        { "airport", QT_TRANSLATE_NOOP("libmapa::Place", "aeropuerto") },
        { "harbour", QT_TRANSLATE_NOOP("libmapa::Place", "puerto") },
        { "bay", QT_TRANSLATE_NOOP("libmapa::Place", "bahía") },
        { "cape", QT_TRANSLATE_NOOP("libmapa::Place", "cabo") },
        { "peak", QT_TRANSLATE_NOOP("libmapa::Place", "pico") },
        { "strait", QT_TRANSLATE_NOOP("libmapa::Place", "estrecho") },
    };
    for (const Par &p : tabla)
        if (type == QLatin1String(p.tipo))
            return QCoreApplication::translate("libmapa::Place", p.texto);
    return type;
}

// Zoom de encuadre segun el tamaño habitual del lugar.
int Place::suggestedZoom() const
{
    if (type == QLatin1String("island") || type == QLatin1String("strait"))
        return 10;
    if (type == QLatin1String("city") || type == QLatin1String("bay"))
        return 12;
    if (type == QLatin1String("town") || type == QLatin1String("cape"))
        return 13;
    if (type == QLatin1String("airport") || type == QLatin1String("harbour"))
        return 14;
    return 14;     // aldeas, barrios, picos, islotes...
}

// Fija la BD de lugares con un id de conexion propio.
void PlaceIndex::setDatabase(const QString &dbFile)
{
    m_dbFile = dbFile;
    m_connId = dbFile.isEmpty() ? QString() : nextConnId();
}

// Minusculas, sin marcas combinantes (tildes, la tilde de la ene) y espacios simples.
QString PlaceIndex::normalize(const QString &text)
{
    const QString d = text.normalized(QString::NormalizationForm_KD);
    QString out;
    out.reserve(d.size());
    for (const QChar c : d) {
        const QChar::Category cat = c.category();
        if (cat == QChar::Mark_NonSpacing || cat == QChar::Mark_SpacingCombining
            || cat == QChar::Mark_Enclosing)
            continue;
        out += c.toLower();
    }
    return out.simplified();
}

// Abre la BD y comprueba que tenga la tabla `lugar`.
bool PlaceIndex::isUsable() const
{
    if (m_dbFile.isEmpty())
        return false;
    QSqlDatabase db = SqliteConnectionPool::connectionFor(
        m_connId, m_dbFile, SqliteConnectionPool::Mode::ReadOnly);
    if (!db.isOpen())
        return false;
    QSqlQuery q(db);
    return q.exec(QStringLiteral("SELECT 1 FROM lugar LIMIT 1"));
}

// Busca los lugares que contengan todas las palabras. El orden se decide en SQL:
// primero los que empiezan por lo escrito, luego por peso (ciudad > aldea) y
// por nombre mas corto (el lugar «exacto» antes que uno compuesto).
QVector<Place> PlaceIndex::search(const QString &text, int limit) const
{
    QVector<Place> out;
    const QString norm = normalize(text);
    if (norm.isEmpty() || m_dbFile.isEmpty() || limit <= 0)
        return out;

    QSqlDatabase db = SqliteConnectionPool::connectionFor(
        m_connId, m_dbFile, SqliteConnectionPool::Mode::ReadOnly);
    if (!db.isOpen())
        return out;

    const QStringList palabras = norm.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QString sql = QStringLiteral(
        "SELECT id, nombre, tipo, lat, lon, poblacion FROM lugar WHERE ");
    for (int i = 0; i < palabras.size(); ++i) {
        if (i)
            sql += QStringLiteral(" AND ");
        sql += QStringLiteral("busqueda LIKE :w%1 ESCAPE '\\'").arg(i);
    }
    sql += QStringLiteral(" ORDER BY (busqueda LIKE :pre ESCAPE '\\') DESC, "
                          "peso DESC, length(nombre), nombre LIMIT :lim");

    QSqlQuery q(db);
    if (!q.prepare(sql))
        return out;
    for (int i = 0; i < palabras.size(); ++i)
        q.bindValue(QStringLiteral(":w%1").arg(i),
                    QLatin1Char('%') + escapeLike(palabras.at(i)) + QLatin1Char('%'));
    q.bindValue(QStringLiteral(":pre"), escapeLike(norm) + QLatin1Char('%'));
    q.bindValue(QStringLiteral(":lim"), limit);
    if (!q.exec())
        return out;

    while (q.next()) {
        Place p;
        p.id = q.value(0).toLongLong();
        p.name = q.value(1).toString();
        p.type = q.value(2).toString();
        p.coordinate = QGeoCoordinate(q.value(3).toDouble(), q.value(4).toDouble());
        p.population = q.value(5).toLongLong();
        out.push_back(p);
    }
    return out;
}

} // namespace libmapa
