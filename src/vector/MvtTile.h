#ifndef LIBMAPA_VECTOR_MVTTILE_H_
#define LIBMAPA_VECTOR_MVTTILE_H_

#include <QByteArray>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>

namespace libmapa {

/*!
 * \brief Tesela vectorial Mapbox Vector Tile (MVT 2.1), ya decodificada.
 *
 * Es el formato de las teselas `pbf` de un MBTiles vectorial (OpenMapTiles,
 * tippecanoe, planetiler…): un protobuf con capas («water», «transportation»,
 * «place»…), cada una con sus entidades (punto, línea o polígono) en
 * coordenadas enteras de la tesela, de 0 a \c extent (4096 casi siempre), con el
 * eje Y hacia ABAJO como en pantalla.
 *
 * El decodificador es propio y no depende de libprotobuf: el formato solo usa
 * varints, enteros de 32/64 bits fijos y bloques con longitud, y traerse
 * protobuf entero para eso complicaría el despliegue en MinGW. Especificación:
 * https://github.com/mapbox/vector-tile-spec/tree/master/2.1
 */
namespace mvt {

//! Tipo de geometría de una entidad (campo `type` del protobuf).
enum class GeomType {
    Unknown = 0,
    Point = 1,
    LineString = 2,
    Polygon = 3
};

/*!
 * \brief Una entidad de una capa.
 *
 * \c parts son las piezas de la geometría ya en coordenadas absolutas de la
 * tesela: para un punto (o multipunto), una sola pieza con todos los puntos; para
 * una línea, una pieza por tramo; para un polígono, un anillo por pieza (el
 * exterior con área positiva y los huecos negativa, sin repetir el primer punto
 * al final). Las propiedades no se copian a cada entidad: \c tags son índices
 * pares clave/valor a las tablas de su capa (ver \c Layer::value).
 */
struct Feature
{
    quint64 id = 0;
    bool hasId = false;
    GeomType type = GeomType::Unknown;
    QVector<quint32> tags;
    QVector<QVector<QPoint>> parts;
};

//! Una capa de la tesela: nombre, extensión y las tablas de claves y valores.
struct Layer
{
    QString name;
    int version = 1;
    int extent = 4096;
    QStringList keys;
    QVector<QVariant> values;
    QVector<Feature> features;

    //! Índice de una clave en \c keys, o -1 si la capa no la tiene.
    int keyIndex(const QString &key) const { return int(keys.indexOf(key)); }

    //! Valor de la clave número \a keyIdx en la entidad \a f (inválido si no la tiene).
    QVariant value(const Feature &f, int keyIdx) const;

    //! Valor de la clave \a key en la entidad \a f (inválido si no la tiene).
    QVariant value(const Feature &f, const QString &key) const
    {
        return value(f, keyIndex(key));
    }
};

//! Una tesela: sus capas, en el orden en que vienen en el fichero.
struct Tile
{
    QVector<Layer> layers;

    //! La capa de nombre \a name, o null si la tesela no la trae.
    const Layer *layer(const QString &name) const;
};

/*!
 * \brief Decodifica los bytes de una tesela MVT (en claro, gzip o zlib).
 * \return false si no es un MVT válido; el motivo queda en \a error. Una tesela
 *         vacía (cero bytes descomprimidos) es válida y no tiene capas.
 */
bool decode(const QByteArray &bytes, Tile *out, QString *error = nullptr);

/*!
 * \brief Decodifica solo la geometría de una entidad (comandos MoveTo/LineTo/
 *        ClosePath con parámetros en zigzag, §4.3 de la especificación).
 *
 * Lo usa \c decode y se expone para probar los ejemplos de la especificación.
 * \return false si los comandos no son coherentes con \a type.
 */
bool decodeGeometry(const QVector<quint32> &commands, GeomType type,
                    QVector<QVector<QPoint>> *parts);

} // namespace mvt

} // namespace libmapa

#endif // LIBMAPA_VECTOR_MVTTILE_H_
