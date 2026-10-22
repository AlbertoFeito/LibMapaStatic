#ifndef LIBMAPA_VECTOR_VECTORSTYLE_H_
#define LIBMAPA_VECTOR_VECTORSTYLE_H_

#include "vector/MvtTile.h"

#include <QColor>
#include <QJsonObject>
#include <QPair>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace libmapa {

//! Forma del marcador de un punto (\c VectorRule::pointShape): un símbolo distinto
//! por tema permite distinguir de un vistazo hospitales de gasolineras.
enum class PointShape {
    Circle,     //!< círculo (por defecto)
    Square,     //!< cuadrado
    Triangle,   //!< triángulo con la punta arriba
    Diamond,    //!< rombo
    Cross       //!< cruz gruesa (el símbolo de salud)
};

/*!
 * \brief Una regla de dibujo de teselas vectoriales.
 *
 * Elige entidades de UNA capa del MVT (\c layer) por zoom, tipo de geometría y
 * valor de sus propiedades, y dice cómo pintarlas: relleno (polígonos), trazo
 * (líneas y contorno de polígonos) y/o rótulo (puntos). Las reglas se aplican
 * en orden, como capas de pintura: lo de abajo primero.
 */
struct VectorRule
{
    QString layer;                                   //!< capa del MVT: "water", "transportation"…
    //! Grupo al que pertenece ("nombres_calles", "poi"…): la aplicación puede
    //! ocultar un grupo entero (\c VectorTileRenderer::setHiddenGroups). Vacío =
    //! siempre visible (el agua, las vías, el fondo).
    QString group;
    //! Todas deben cumplirse: el valor de la clave está en la lista.
    QVector<QPair<QString, QStringList>> filter;
    //! Ninguna debe cumplirse (p. ej. excluir `brunnel = tunnel`).
    QVector<QPair<QString, QStringList>> exclude;
    int minZoom = 0;                                 //!< zoom lógico mínimo (incluido)
    int maxZoom = 30;                                //!< zoom lógico máximo (incluido)
    //! Tipos de geometría a los que se aplica: bit (1 << GeomType). 0 = todos.
    int geometryMask = 0;

    QColor fill;                                     //!< relleno; inválido = sin relleno
    QColor line;                                     //!< trazo; inválido = sin trazo
    //! Grosor del trazo en píxeles según el zoom: paradas (zoom, px) que se
    //! interpolan linealmente (y se mantienen fuera del rango). Vacío = 1 px.
    QVector<QPointF> width;
    double widthOffset = 0.0;                        //!< se suma al grosor (bordes de vía)
    bool dashed = false;

    //! Propiedades de las que sale el texto, por orden de preferencia
    //! ("name:es", "name", "name:latin"…). Vacía = sin rótulo. En un punto, el
    //! texto va sobre él; en una línea (una calle, un río), A LO LARGO de ella.
    QStringList labelFields;
    QColor textColor = QColor(51, 51, 51);
    QColor haloColor = QColor(255, 255, 255, 220);
    double textSize = 11.0;                          //!< en píxeles
    bool bold = false;
    bool italic = false;
    double pointRadius = 0.0;                        //!< punto dibujado bajo el rótulo (px)
    QColor pointColor = QColor(120, 120, 120);
    //! Forma del punto. Todas menos el círculo llevan un contorno fino del color del
    //! halo, para que se lean sobre cualquier fondo.
    PointShape pointShape = PointShape::Circle;

    //! ¿La regla vale para este zoom y este tipo de geometría?
    bool appliesTo(int zoom, mvt::GeomType type) const;
    //! Grosor del trazo a un zoom dado (paradas interpoladas + widthOffset).
    double widthAt(int zoom) const;
};

/*!
 * \brief Estilo completo para convertir teselas vectoriales en imagen.
 *
 * Trae uno por defecto para el esquema de OpenMapTiles (\c openMapTiles), que es
 * el de los MBTiles de OSM más comunes, y se puede cargar otro de un JSON propio
 * y sencillo (ver \c fromJson y docs/estilo_vectorial.json). NO es el formato de
 * estilos de Mapbox GL: aquel exige expresiones, sprites y fuentes de glifos que
 * no tienen sentido para pintar teselas sin conexión con QPainter.
 */
class VectorStyle
{
public:
    QString name;
    QColor background = QColor(248, 244, 240);     //!< fondo = tierra
    //! Color del mar. Solo se usa con máscara de tierra (\c LandMask): sin ella no se
    //! sabe qué es mar y todo el fondo es \c background.
    QColor sea = QColor(165, 201, 234);
    QVector<VectorRule> rules;
    //! Grupos que se pueden encender y apagar, en el orden en que se ofrecen:
    //! (id, título para la interfaz). Incluye los que solo aparecen en "group"
    //! de alguna regla (con el id como título).
    QVector<QPair<QString, QString>> groups;

    //! Ids de \c groups, en orden.
    QStringList groupIds() const;

    //! Grupos que arrancan APAGADOS ("visible": false en "groups"), salvo que la
    //! configuración diga otra cosa ("hiddenGroups").
    QStringList defaultHidden;

    //! Estilo por defecto para el esquema OpenMapTiles (agua, usos del suelo,
    //! edificios, vías por categoría, límites y nombres de lugares).
    static VectorStyle openMapTiles();

    //! El JSON de \c openMapTiles, tal cual: plantilla para un estilo propio.
    static QByteArray openMapTilesJson();

    /*!
     * \brief Estilo «híbrido», para pintar el MBTiles ENCIMA de otra base (satélite,
     *        relieve…): fondo transparente, vías finas y rótulos blancos con halo.
     */
    static VectorStyle hybrid();
    static QByteArray hybridJson();

    //! \c hybrid si es un overlay, \c openMapTiles si es una capa base.
    static VectorStyle defaultFor(bool overlay);

    /*!
     * \brief Lee un estilo de su JSON:
     * \code
     * { "name": "…", "background": "#f8f4f0",
     *   "rules": [ { "layer": "water", "fill": "#a0c8f0" },
     *              { "layer": "transportation", "filter": { "class": ["motorway","trunk"] },
     *                "line": "#e892a2", "width": [[6,0.5],[16,8]], "minZoom": 5 },
     *              { "layer": "place", "filter": { "class": "city" },
     *                "label": "name:es|name|name:latin", "textSize": 13, "bold": true } ] }
     * \endcode
     * "sea" es el color del mar (solo con máscara de tierra). "groups" lista los grupos
     * que se pueden apagar ([{"id": "poi", "title": "Puntos de interés"}, …]) y cada
     * regla dice el suyo con "group". Otras claves de regla: "exclude", "maxZoom", "geometry" ("point", "line",
     * "polygon" o una lista), "widthOffset", "dash", "textColor", "halo",
     * "italic", "pointRadius", "pointColor", "pointShape" ("circle", "square", "triangle",
     * "diamond" o "cross").
     * \return false si el JSON no tiene reglas válidas; el motivo, en \a error.
     */
    static bool fromJson(const QJsonObject &json, VectorStyle *out, QString *error = nullptr);

    //! Igual que fromJson, leyendo el fichero \a path.
    static bool fromFile(const QString &path, VectorStyle *out, QString *error = nullptr);
};

} // namespace libmapa

#endif // LIBMAPA_VECTOR_VECTORSTYLE_H_
