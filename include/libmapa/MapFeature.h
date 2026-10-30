#ifndef LIBMAPA_MAPFEATURE_H_
#define LIBMAPA_MAPFEATURE_H_

#include <QColor>
#include <QGeoCoordinate>
#include <QMetaType>
#include <QPixmap>
#include <QPointF>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <cmath>

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
    Polygon,    //!< Vertices en orden, cerrado.
    //! Circunferencia GEODESICA: un vertice (el centro) y \a MapFeature::radiusMeters.
    //! Se anaden al FINAL del enum porque el tipo se guarda como entero en SQLite.
    Circle,
    //! Arco geodesico: centro, radio y los azimuts de inicio y fin
    //! (\a MapFeature::arcStartDeg y \a arcEndDeg), siempre en sentido horario.
    Arc
};

/*!
 * \brief Donde cuelga el rotulo de una entidad respecto a su punto de anclaje.
 *
 * \a Auto es el comportamiento de siempre (junto a un punto o vertice, arriba a
 * la derecha; en el centro de una linea o un area, desde el propio centro) y es
 * el valor por defecto, asi que nada existente cambia. Los demas valores colocan
 * la caja entera al lado indicado del anclaje; el desplazamiento
 * FeatureStyle::labelOffsetPx se SUMA a esa posicion.
 */
enum class LabelAnchor {
    Auto = 0,    //!< Comportamiento clasico.
    Top,         //!< Encima, centrado.
    Bottom,      //!< Debajo, centrado.
    Left,        //!< A la izquierda, centrado en vertical.
    Right,       //!< A la derecha, centrado en vertical.
    TopLeft,     //!< Arriba a la izquierda.
    TopRight,    //!< Arriba a la derecha.
    BottomLeft,  //!< Abajo a la izquierda.
    BottomRight, //!< Abajo a la derecha.
    Center       //!< Centrado sobre el anclaje.
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

    //! Punto de anclaje del icono, NORMALIZADO [0,1] dentro del pixmap: dice qué
    //! punto de la imagen cae sobre la coordenada. (0.5,0.5) = centro (por
    //! defecto); (0.5,1.0) = pie-centro, para un pin cuya PUNTA marca el lugar.
    QPointF iconAnchor = QPointF(0.5, 0.5);

    //! Radio del simbolo en PIXELES: no se deforma con el zoom ni con la
    //! latitud, a diferencia de dibujarlo en grados.
    double pointRadiusPx = 6.0;

    bool labelVisible = true;
    QColor labelColor = Qt::black;

    //! Desplazamiento de la ETIQUETA respecto a su anclaje, en PIXELES. Como es
    //! relativo al punto, al mover la entidad la etiqueta la sigue (se mueven al
    //! unisono); al arrastrar la etiqueta solo cambia esto. (0,0) = pegada al
    //! anclaje (comportamiento clasico). Cuando no es nula se dibuja una LINEA
    //! GUIA del anclaje a la caja de texto.
    QPointF labelOffsetPx = QPointF(0.0, 0.0);

    //! Vertice al que se ancla la etiqueta: -1 = automatico (el punto, o el
    //! centro de la geometria en lineas/poligonos); >=0 = ese vertice (p. ej. 0
    //! para colgar el rotulo del PRIMER punto de un area).
    int labelAnchorVertex = -1;

    //! Lado del anclaje en el que cuelga la caja del rotulo. \a Auto = como
    //! siempre. Se combina con \a labelOffsetPx (que se suma) y con
    //! \a labelAnchorVertex (que decide A QUE PUNTO se ancla).
    LabelAnchor labelAnchor = LabelAnchor::Auto;

    //! Tamano de la fuente del rotulo, en puntos. 0 = la fuente del mapa. Cambia
    //! el dibujo y la caja que usa el hit-test, que siguen coincidiendo.
    int labelFontSize = 0;

    //! Se dibujan los vertices como tiradores. Lo activa el editor.
    bool verticesVisible = false;
};

/*!
 * \brief Lo que distingue a UNA parte de una entidad multi-parte del resto de la entidad.
 *
 * Una entidad con muchas partes (18 aerodromos como una sola entidad con 18 puntos)
 * comparte \a style, \a type y \a attributes, y cada parte puede ademas tener su
 * propio nombre, su color y el lado donde cuelga su rotulo. Va en
 * MapFeature::partsInfo, paralelo a MapFeature::parts.
 */
struct PartInfo
{
    //! Nombre de la parte; se rotula junto a ella. Vacio = sin rotulo propio.
    QString name;
    //! Color de la parte (linea o simbolo); invalido = el de \a MapFeature::style.
    QColor colorOverride;
    //! Lado donde cuelga su rotulo; \a Auto = el de \a MapFeature::style.
    LabelAnchor labelAnchor = LabelAnchor::Auto;

    bool operator==(const PartInfo &o) const
    {
        return name == o.name && colorOverride == o.colorOverride && labelAnchor == o.labelAnchor;
    }
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

    //! Un vertice si es Point; varios si es Polyline o Polygon. En una entidad
    //! multi-parte es la PRIMERA parte (ver \a parts).
    QVector<QGeoCoordinate> geometry;

    /*!
     * \brief Partes adicionales de una geometria multi-parte.
     *
     * Vacio = entidad de una sola parte (se usa \a geometry). Cuando tiene
     * elementos, la entidad es multi-parte: un fichero .geo entero (varias
     * polilineas o anillos) puede ser UNA sola entidad. Todas las partes
     * comparten \a kind, \a style, \a name y \a attributes. \a geometry
     * coincide con la primera parte.
     */
    QVector<QVector<QGeoCoordinate>> parts;

    /*!
     * \brief Datos propios de cada parte (nombre, color, lado del rotulo).
     *
     * Vacio = las partes no tienen nada propio (lo de siempre). Si tiene elementos,
     * debe tener EXACTAMENTE uno por parte de \a parts, en el mismo orden
     * (\ref isValid lo comprueba). No aplica a un Circle ni a un Arc.
     */
    QVector<PartInfo> partsInfo;

    FeatureStyle style;

    //! Cualquier dato del dominio. Permite anadir campos sin tocar la
    //! libreria ni migrar nada.
    QVariantMap attributes;

    bool visible = true;
    bool selectable = true;

    //! Si es true y es un PUNTO, la herramienta «Navegar» (None) lo arrastra:
    //! al pulsar sobre él empieza el arrastre (en vez de desplazar el mapa) y la
    //! librería emite las señales de arrastre. Pensado para marcadores que el
    //! operador reposiciona (p. ej. los puntos A/B de un análisis). No se
    //! persiste: es una pista de interacción que la app vuelve a poner.
    bool draggable = false;

    //! Radio en metros de un Circle o un Arc (0 en el resto).
    double radiusMeters = 0.0;
    //! Azimuts de inicio y fin de un Arc, en grados desde el norte y en sentido
    //! horario: el arco va de \a arcStartDeg a \a arcEndDeg y da la vuelta si hace
    //! falta (de 315 a 45 son 90 grados). Si son iguales (modulo 360) el arco no es
    //! valido: para una circunferencia entera esta Circle.
    double arcStartDeg = 0.0;
    double arcEndDeg = 0.0;

    bool isMultiPart() const { return !parts.isEmpty(); }

    //! Datos propios de la parte \a i, o null si no tiene (sin \a partsInfo o fuera de rango).
    const PartInfo *partInfo(int i) const
    {
        return (i >= 0 && i < partsInfo.size()) ? &partsInfo.at(i) : nullptr;
    }
    //! Color de la parte \a i: el suyo si lo tiene y, si no, el de la linea del estilo.
    QColor partLineColor(int i) const
    {
        const PartInfo *p = partInfo(i);
        return (p && p->colorOverride.isValid()) ? p->colorOverride : style.lineColor;
    }
    //! Color de relleno de la parte \a i: el del estilo, o el de la parte con la
    //! transparencia del relleno del estilo.
    QColor partFillColor(int i) const
    {
        const PartInfo *p = partInfo(i);
        if (!p || !p->colorOverride.isValid())
            return style.fillColor;
        QColor c = p->colorOverride;
        c.setAlpha(style.fillColor.alpha());
        return c;
    }
    //! Lado del rotulo de la parte \a i: el suyo si lo tiene y, si no, el del estilo.
    LabelAnchor partLabelAnchor(int i) const
    {
        const PartInfo *p = partInfo(i);
        return (p && p->labelAnchor != LabelAnchor::Auto) ? p->labelAnchor : style.labelAnchor;
    }

    //! ¿Es un Circle o un Arc? Su geometria se genera a partir del centro y el radio.
    bool isRound() const { return kind == GeometryKind::Circle || kind == GeometryKind::Arc; }
    //! ¿Se comporta como un area (relleno, cierre, «dentro»)? Poligonos y circulos.
    bool isArea() const { return kind == GeometryKind::Polygon || kind == GeometryKind::Circle; }
    //! ¿Se comporta como una linea? Polilineas y arcos.
    bool isLinear() const { return kind == GeometryKind::Polyline || kind == GeometryKind::Arc; }

    //! Barrido en grados (de 0 a 360, sin llegar a 360) de un arco que va de \a startDeg
    //! a \a endDeg en sentido horario; 0 si no son numeros finitos o si coinciden
    //! (modulo 360). De 315 a 45 son 90; de 45 a 315, 270.
    static double arcSweepDeg(double startDeg, double endDeg)
    {
        if (!std::isfinite(startDeg) || !std::isfinite(endDeg))
            return 0.0;
        double barrido = std::fmod(endDeg - startDeg, 360.0);
        if (barrido < 0.0)
            barrido += 360.0;
        return barrido;
    }

    //! Puntos de un Circle o un Arc, calculados SOBRE EL ELIPSOIDE (una coordenada
    //! cada 2 grados de azimut): en Mercator un circulo de 300 km no es un circulo
    //! en pantalla, y asi sale bien a cualquier latitud y radio. Vacio si no es
    //! redondo o no es valido. El Circle no repite el primer punto (es un area).
    QVector<QGeoCoordinate> roundOutline() const
    {
        QVector<QGeoCoordinate> puntos;
        if (!isRound() || geometry.size() != 1 || !geometry.first().isValid()
            || !(radiusMeters > 0.0) || !std::isfinite(radiusMeters))
            return puntos;
        const bool arco = kind == GeometryKind::Arc;
        const double barrido = arco ? arcSweepDeg(arcStartDeg, arcEndDeg) : 360.0;
        if (!(barrido > 0.0))
            return puntos;
        constexpr double kPasoDeg = 2.0;
        const int tramos = qMax(arco ? 2 : 3, int(std::ceil(barrido / kPasoDeg)));
        const int n = arco ? tramos + 1 : tramos;     // el circulo no repite el primero
        puntos.reserve(n);
        const QGeoCoordinate centro = geometry.first();
        const double inicio = arco ? arcStartDeg : 0.0;
        for (int i = 0; i < n; ++i)
            puntos.append(centro.atDistanceAndAzimuth(radiusMeters,
                                                      inicio + barrido * i / tramos));
        return puntos;
    }

    //! Punto donde se ancla el rotulo: la mitad del recorrido de un Arc y el punto norte del
    //! borde de un Circle (en el centro tapaba lo que hubiera dentro). Invalido si no es redondo.
    QGeoCoordinate roundLabelPoint() const
    {
        if (!isRound() || geometry.isEmpty())
            return QGeoCoordinate();
        if (kind == GeometryKind::Circle)
            return geometry.first().atDistanceAndAzimuth(radiusMeters, 0.0);
        const double barrido = arcSweepDeg(arcStartDeg, arcEndDeg);
        return geometry.first().atDistanceAndAzimuth(radiusMeters, arcStartDeg + barrido / 2.0);
    }

    //! Las partes a dibujar/consultar: \a parts si las hay, si no \a geometry. En un
    //! Circle o un Arc, su contorno ya calculado (\ref roundOutline).
    QVector<QVector<QGeoCoordinate>> outlines() const
    {
        if (isRound()) {
            const QVector<QGeoCoordinate> contorno = roundOutline();
            return contorno.isEmpty() ? QVector<QVector<QGeoCoordinate>>()
                                      : QVector<QVector<QGeoCoordinate>>{ contorno };
        }
        if (!parts.isEmpty())
            return parts;
        if (geometry.isEmpty())
            return {};
        return { geometry };
    }

    //! Lo que se GUARDA de la geometria: las partes si las hay o \a geometry. A
    //! diferencia de \ref outlines, en un Circle o un Arc es solo el centro (el
    //! contorno se vuelve a calcular al leer).
    QVector<QVector<QGeoCoordinate>> storedParts() const
    {
        if (!parts.isEmpty())
            return parts;
        if (geometry.isEmpty())
            return {};
        return { geometry };
    }

    //! Copia con la geometria de siempre (Polygon para un Circle, Polyline para un
    //! Arc, con el contorno ya calculado), para los formatos que no conocen circulos
    //! ni arcos. Guarda el centro, el radio y los azimuts como atributos
    //! (\c radius_m, \c arc_start_deg, \c arc_end_deg, \c center_lat, \c center_lon).
    MapFeature toPlain() const
    {
        if (!isRound())
            return *this;
        MapFeature plana = *this;
        plana.attributes.insert(QStringLiteral("radius_m"), radiusMeters);
        if (!geometry.isEmpty()) {
            plana.attributes.insert(QStringLiteral("center_lat"), geometry.first().latitude());
            plana.attributes.insert(QStringLiteral("center_lon"), geometry.first().longitude());
        }
        if (kind == GeometryKind::Arc) {
            plana.attributes.insert(QStringLiteral("arc_start_deg"), arcStartDeg);
            plana.attributes.insert(QStringLiteral("arc_end_deg"), arcEndDeg);
        }
        plana.geometry = roundOutline();
        plana.kind = kind == GeometryKind::Circle ? GeometryKind::Polygon : GeometryKind::Polyline;
        plana.radiusMeters = 0.0;
        plana.arcStartDeg = 0.0;
        plana.arcEndDeg = 0.0;
        return plana;
    }

    QGeoCoordinate position() const
    {
        return geometry.isEmpty() ? QGeoCoordinate() : geometry.first();
    }

    //! Comprueba una parte suelta contra el tipo de la entidad.
    bool partIsValid(const QVector<QGeoCoordinate> &p) const
    {
        for (const QGeoCoordinate &c : p)
            if (!c.isValid())
                return false;
        switch (kind) {
        case GeometryKind::Point:    return p.size() == 1;
        case GeometryKind::Polyline: return p.size() >= 2;
        case GeometryKind::Polygon:  return p.size() >= 3;
        case GeometryKind::Circle:   return p.size() == 1;
        case GeometryKind::Arc:      return p.size() == 1;
        }
        return false;
    }

    bool isValid() const
    {
        if (isRound()) {
            // Un solo vertice (el centro), sin partes ni datos de partes, radio finito y positivo y, en
            // un arco, un barrido distinto de cero.
            if (!parts.isEmpty() || !partsInfo.isEmpty() || geometry.size() != 1
                || !geometry.first().isValid())
                return false;
            if (!(radiusMeters > 0.0) || !std::isfinite(radiusMeters))
                return false;
            return kind == GeometryKind::Circle || arcSweepDeg(arcStartDeg, arcEndDeg) > 0.0;
        }
        // Los datos de las partes, si los hay, van uno por parte.
        if (!partsInfo.isEmpty() && partsInfo.size() != parts.size())
            return false;
        if (!parts.isEmpty()) {
            for (const QVector<QGeoCoordinate> &p : parts)
                if (!partIsValid(p))
                    return false;
            return true;
        }
        return !geometry.isEmpty() && partIsValid(geometry);
    }

    //! Numero minimo de vertices para que la geometria siga siendo valida.
    int minimumVertices() const
    {
        switch (kind) {
        case GeometryKind::Point:    return 1;
        case GeometryKind::Polyline: return 2;
        case GeometryKind::Polygon:  return 3;
        case GeometryKind::Circle:   return 1;
        case GeometryKind::Arc:      return 1;
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
    //! ¿Se pueden seleccionar con el ratón las entidades de la capa (clic y
    //! rótulo)? Las capas fijas del paquete (overlays) vienen en \c false: son
    //! decoración de fondo, y un polígono grande (una provincia) se llevaba el clic
    //! que era de las rutas o los puntos de encima. Lo demás sigue funcionando
    //! (consultas, exportar, dibujarla): solo no se elige con el ratón.
    bool selectable = true;
    //! Orden de dibujo: mayor se pinta encima.
    int zOrder = 0;
    int featureCount = 0;
    //! Rango de zoom en el que la capa se dibuja (−1 = sin límite). Fuera de
    //! [minZoom, maxZoom] la capa no se pinta ni se puede seleccionar: útil para
    //! capas de detalle (p. ej. curvas de nivel) que estorban a vista general.
    int minZoom = -1;
    int maxZoom = -1;
};

} // namespace libmapa

Q_DECLARE_METATYPE(libmapa::MapFeature)
Q_DECLARE_METATYPE(libmapa::LayerInfo)
Q_DECLARE_METATYPE(libmapa::FeatureStyle)

#endif // LIBMAPA_MAPFEATURE_H_
