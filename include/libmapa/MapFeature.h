#ifndef LIBMAPA_MAPFEATURE_H_
#define LIBMAPA_MAPFEATURE_H_

#include <QColor>
#include <QGeoCoordinate>
#include <QMetaType>
#include <QPixmap>
#include <QString>
#include <QVariantMap>
#include <QVector>

namespace libmapa {

/*!
 * \brief Qué forma tiene una entidad. Nada más.
 *
 * Deliberadamente NO hay un tipo por concepto del dominio. En el codigo
 * original habia CPunto, CVehiculo, CAvion, CBarco, con herencia, porque el
 * dominio decia que eran cuatro cosas distintas. Acabo en un CBarco con unos
 * cincuenta getters de AIS que CAvion heredaba sin poder usar, y en una
 * jerarquia que habia que tocar cada vez que aparecia un concepto nuevo.
 *
 * Aqui la libreria solo sabe de tres geometrias. Que una poligonal sea una
 * "zona prohibida", un "area de interes" o el "limite de un sector" lo decide
 * la aplicacion con MapFeature::type y MapFeature::attributes, sin tocar la
 * libreria.
 */
enum class GeometryKind {
    Point,      //!< Un solo vertice.
    Polyline,   //!< Vertices en orden, sin cerrar.
    Polygon     //!< Vertices en orden, cerrado.
};

//! Como se dibuja una entidad.
struct FeatureStyle
{
    QColor lineColor = QColor(0xd3, 0x2f, 0x2f);
    QColor fillColor = QColor(0xd3, 0x2f, 0x2f, 60);
    double lineWidth = 2.0;
    Qt::PenStyle lineStyle = Qt::SolidLine;

    //! Vacio = circulo del color de linea.
    QPixmap icon;

    //! Radio del simbolo en PIXELES: no se deforma con el zoom ni con la
    //! latitud, a diferencia de dibujarlo en grados.
    double pointRadiusPx = 6.0;

    bool labelVisible = true;
    QColor labelColor = Qt::black;

    //! Se dibujan los vertices como tiradores. Lo activa el editor.
    bool verticesVisible = false;
};

/*!
 * \brief Una entidad dibujable sobre el mapa.
 *
 * La libreria dibuja la geometria y la deja editar. Lo que SIGNIFICA cada
 * entidad es cosa de la aplicacion:
 *
 *     MapFeature zona;
 *     zona.kind = GeometryKind::Polygon;
 *     zona.type = "zona_prohibida";              // la libreria no lo
 *     zona.attributes["techo_m"] = 120;          //   interpreta
 *     zona.attributes["vigencia"] = "2026-09-01";
 */
struct MapFeature
{
    qint64 id = -1;              //!< Lo asigna MapWidget al anadirla.
    QString layerId;             //!< Capa a la que pertenece.

    GeometryKind kind = GeometryKind::Point;

    //! Etiqueta del dominio. La libreria no la interpreta: solo la conserva
    //! y permite filtrar por ella.
    QString type;

    QString name;
    QString description;

    //! Un vertice si es Point; varios si es Polyline o Polygon.
    QVector<QGeoCoordinate> geometry;

    FeatureStyle style;

    //! Cualquier dato del dominio. Permite anadir campos sin tocar la
    //! libreria ni migrar nada.
    QVariantMap attributes;

    bool visible = true;
    bool selectable = true;

    QGeoCoordinate position() const
    {
        return geometry.isEmpty() ? QGeoCoordinate() : geometry.first();
    }

    bool isValid() const
    {
        if (geometry.isEmpty())
            return false;
        for (const QGeoCoordinate &c : geometry)
            if (!c.isValid())
                return false;
        switch (kind) {
        case GeometryKind::Point:    return geometry.size() == 1;
        case GeometryKind::Polyline: return geometry.size() >= 2;
        case GeometryKind::Polygon:  return geometry.size() >= 3;
        }
        return false;
    }

    //! Numero minimo de vertices para que la geometria siga siendo valida.
    int minimumVertices() const
    {
        switch (kind) {
        case GeometryKind::Point:    return 1;
        case GeometryKind::Polyline: return 2;
        case GeometryKind::Polygon:  return 3;
        }
        return 1;
    }
};

/*!
 * \brief Capa: una coleccion de entidades con nombre.
 *
 * "Pueden existir varias de cada uno" se resuelve creando las capas que hagan
 * falta y decidiendo que va en cada una. La libreria no impone ninguna.
 */
struct LayerInfo
{
    QString id;
    QString displayName;
    bool visible = true;
    bool editable = true;
    //! Orden de dibujo: mayor se pinta encima.
    int zOrder = 0;
    int featureCount = 0;
};

} // namespace libmapa

Q_DECLARE_METATYPE(libmapa::MapFeature)
Q_DECLARE_METATYPE(libmapa::LayerInfo)
Q_DECLARE_METATYPE(libmapa::FeatureStyle)

#endif // LIBMAPA_MAPFEATURE_H_
