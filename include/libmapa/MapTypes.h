#ifndef LIBMAPA_MAPTYPES_H_
#define LIBMAPA_MAPTYPES_H_

#include <QColor>
#include <QGeoCoordinate>
#include <QPoint>
#include <QMetaType>
#include <QPixmap>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include <limits>

namespace libmapa {

//! Capa base del mapa. Los identificadores salen del datasets.json.
enum class BaseLayer {
    OSM,
    Satellite
};

//! Herramienta activa sobre el mapa.
enum class MapTool {
    None,          //!< Solo navegar.
    Measure,       //!< Medir distancia y marcacion entre dos puntos.
    AreaZoom,      //!< Ampliar arrastrando un rectangulo.
    SelectArea,    //!< Marcar un rectangulo SIN hacer zoom: emite areaSelected
                   //!< y deja el recuadro visible (para rellenar teselas, etc.).
    SelectPolygon, //!< Marcar un POLIGONO clic a clic (doble clic / Enter lo
                   //!< cierra): emite polygonSelected y deja el contorno visible,
                   //!< para rellenar solo esa zona.
    PickPoint,     //!< Devolver la coordenada del siguiente clic.
    MeasureArea,   //!< Medir SUPERFICIE y perimetro de un poligono marcado clic a
                   //!< clic (en vivo mientras se marca; doble clic / Enter /
                   //!< clic derecho lo cierra y emite areaMeasured).

    // --- Creacion de entidades -------------------------------------------
    // Se dibujan sobre la capa activa (MapWidget::setActiveFeatureLayer) con
    // el estilo por defecto (setDraftStyle). Escape cancela lo que se lleve
    // trazado.
    DrawPoint,     //!< Un clic crea un punto.
    DrawPolyline,  //!< Clic a clic; doble clic o clic derecho lo cierra.
    DrawPolygon,   //!< Igual, pero la geometria se cierra sola.

    /*!
     * \brief Seleccionar y editar lo ya dibujado.
     *
     * - clic sobre una entidad: la selecciona
     * - arrastrar un tirador de vertice: lo mueve
     * - arrastrar el interior: mueve la entidad entera
     * - doble clic sobre un lado: inserta un vertice ahi
     * - Supr: borra el vertice bajo el cursor, o la entidad si no hay ninguno
     */
    EditFeature
};

//! Informacion de una capa base disponible, para poblar un menu.
struct BaseLayerInfo {
    QString id;            //!< "osm", "satelital"
    QString displayName;   //!< Texto para la interfaz.
    int minZoom = 0;
    int maxZoom = 0;       //!< El recomendado, no el ultimo con teselas sueltas.
    bool available = false;
};

//! Una entidad del mapa vectorial bajo un punto (MapWidget::vectorFeaturesAt).
struct VectorFeatureInfo {
    QString layer;          //!< Capa del MBTiles: "transportation", "poi", "building"...
    QString geometry;       //!< "point", "line" o "polygon".
    double distanceM = 0.0; //!< Distancia al punto; 0 si cae dentro del poligono.
    QVariantMap attributes; //!< Propiedades: "class", "name", "surface"...
    bool fromOverlay = false; //!< true si viene del overlay (mapa hibrido), no de la base.

    //! Nombre legible si lo tiene ("name", si no "name:latin" o "name_en"), o vacio.
    QString name() const
    {
        for (const char *k : { "name", "name:latin", "name_en", "ref" })
            if (!attributes.value(QLatin1String(k)).toString().isEmpty())
                return attributes.value(QLatin1String(k)).toString();
        return QString();
    }
};

//! Un grupo de una capa base VECTORIAL que se puede encender o apagar (nombres de
//! calles, puntos de interes, edificios...). Ver MapWidget::baseLayerGroups.
struct BaseLayerGroup {
    QString id;            //!< "nombres_calles", "poi"...
    QString title;         //!< Texto para la interfaz: "Nombres de calles".
    bool visible = true;
};

//! Unidades de la escala grafica (MapWidget::setScaleBarUnits).
enum class ScaleUnits {
    Metric,     //!< metros / kilometros
    Nautical    //!< millas nauticas (1 NM = 1852 m), para uso naval/aereo
};

//! Que cuadricula dibuja MapWidget::setGraticuleVisible.
enum class GraticuleMode {
    Geographic, //!< meridianos y paralelos (grados, minutos, segundos)
    Utm         //!< lineas de este/norte UTM constantes, por zona (en metros/km)
};

/*!
 * \brief Anillos de distancia (VRM) alrededor de un punto o de un objetivo.
 *
 * \a count anillos cada \a spacingMeters, geodesicos (a la distancia real,
 * aunque en Mercator se estiren hacia los polos), con su distancia rotulada
 * en la parte de arriba de cada uno (o, si esa parte queda fuera de la vista,
 * en el punto visible mas cercano). Opcionalmente, radiales cada
 * \a spokeEveryDeg grados con su marcacion ("030°") en el extremo.
 *
 * El centro es un punto fijo (\a center, con su \a name rotulado al lado y un
 * cuadrado), un objetivo movil (\a targetId) o una entidad punto del mapa
 * (\a featureId: un radar u observador colocado por el usuario). En los dos
 * ultimos los anillos siguen a su dueño si se mueve, y no se dibuja marca de
 * centro: el propio simbolo del objetivo o de la entidad queda a la vista
 * (los radiales arrancan a unos pixeles del centro para no taparlo).
 *
 * Con \a spacingMeters = 0 la separacion es AUTOMATICA: se ajusta al zoom
 * para que los \a count anillos llenen casi la vista, con un valor redondo
 * en las unidades de la escala (1, 2, 2.5 o 5 por potencia de 10: 500 m,
 * 2.5 km, 5 NM...). Ver MapWidget::rangeRingsSpacing.
 */
struct RangeRings {
    QGeoCoordinate center;          //!< Centro fijo (si \a targetId < 0).
    qint64 targetId = -1;           //!< O centrado en este objetivo, y lo sigue.
    qint64 featureId = -1;          //!< O en esta entidad punto (si targetId < 0), y la sigue.
    double spacingMeters = 1852.0;  //!< Separacion entre anillos; 0 = automatica (segun el zoom).
    int count = 5;                  //!< Numero de anillos (1..100). Solo cuenta anillos, no radiales.
    int spokeEveryDeg = 0;          //!< Radiales cada N grados (5..180); 0 = sin radiales.
    QColor color = QColor(0x00, 0x89, 0x7b);
    //! Enfasis para leer de un vistazo una escala: uno de cada \a majorRingEvery
    //! anillos (el 5.o, el 10.o... con 5) y uno de cada \a majorSpokeEveryDeg
    //! grados de radial (0, 30, 60... con 30) se dibujan con \a majorColor y algo
    //! mas gruesos. 0 = sin enfasis (como siempre).
    int majorRingEvery = 0;
    int majorSpokeEveryDeg = 0;
    //! Color del enfasis; invalido = el de \a color mas oscuro.
    QColor majorColor;
    //! Color de los radiales (los anillos siguen con \a color); invalido = \a color.
    QColor spokeColor;
    bool labels = true;             //!< Rotular distancias y marcaciones.
    QString name;                   //!< Rotulo junto al centro ("Radar Mariel"); vacio = ninguno.
};

/*!
 * \brief Lo que hay bajo un punto de la pantalla (MapWidget::hitTest).
 *
 * Cada campo se rellena por separado (un objetivo puede estar ENCIMA de una
 * entidad y dentro de unos anillos a la vez); la app decide la prioridad. Es
 * lo que lleva MapWidget::contextMenuRequested para montar un menu segun lo
 * que haya bajo el clic derecho.
 */
struct MapHit {
    QPoint pixel;                       //!< En coordenadas del MapWidget.
    QGeoCoordinate coordinate;          //!< Geografica bajo el pixel.
    qint64 targetId = -1;               //!< Objetivo suelto (visible, no agrupado).
    QVector<qint64> clusterTargets;     //!< Miembros, si cae en un grupo de objetivos.
    qint64 featureId = -1;              //!< Entidad de arriba bajo el cursor.
    //! Parte de la entidad (\a featureId) que esta bajo el cursor, si la entidad tiene
    //! varias (MapFeature::parts); -1 si es de una sola parte o no hay entidad.
    int subIndex = -1;
    qint64 rangeMarkId = -1;            //!< Anillos (centro o un anillo) o marcacion.
    //! Tirador de vertice bajo el pixel en la entidad SELECCIONADA (\a vertexFeature),
    //! o -1: sirve para ofrecer «borrar este vertice».
    qint64 vertexFeature = -1;
    int vertexIndex = -1;
    //! Nada bajo el cursor: solo el mapa.
    bool isEmpty() const
    {
        return targetId < 0 && clusterTargets.isEmpty() && featureId < 0 && rangeMarkId < 0;
    }
};

/*!
 * \brief Linea de marcacion (EBL) con su rumbo verdadero y distancia rotulados.
 *
 * El origen es un punto fijo, un objetivo (\a fromTarget) o una entidad punto
 * del mapa (\a fromFeature: un radar, un observador); en los dos ultimos la
 * linea sigue a su extremo si se mueve. El destino, igual (\a toTarget,
 * \a toFeature o \a to). Sin destino, la linea sale del origen
 * con rumbo \a bearingDeg y longitud \a lengthMeters. El rotulo dice
 * "045° · 5.2 NM" en las unidades de la escala grafica.
 */
struct BearingLine {
    QGeoCoordinate from;            //!< Origen fijo (si no hay \a fromTarget ni \a fromFeature).
    qint64 fromTarget = -1;         //!< O sale de este objetivo.
    qint64 fromFeature = -1;        //!< O de esta entidad punto (si \a fromTarget < 0).
    QGeoCoordinate to;              //!< Destino fijo (si no hay \a toTarget ni \a toFeature)...
    qint64 toTarget = -1;           //!< ...o este objetivo...
    qint64 toFeature = -1;          //!< ...o esta entidad punto (si \a toTarget < 0).
    //! Sin destino: rumbo (grados verdaderos) y longitud de la linea.
    double bearingDeg = std::numeric_limits<double>::quiet_NaN();
    double lengthMeters = 0.0;
    QColor color = QColor(0x29, 0x62, 0xff);
    bool label = true;
    //! Desplazamiento del rotulo («045° · 5.2 NM») respecto al centro de la linea,
    //! en pixeles. Se cambia arrastrandolo con el raton (o con
    //! MapWidget::setBearingLabelOffset); si es distinto de cero, una linea guia
    //! lo une a la marcacion.
    QPointF labelOffsetPx;
};

//! Que lineas marcan su primer y su ultimo punto (MapWidget::setRouteEndpoints).
enum class RouteEndpoints {
    Hidden,     //!< Ninguna.
    Selected,   //!< Solo la linea seleccionada (con la hora, si la tiene).
    Routes,     //!< Ademas, siempre, las rutas y tracks leidos de GPX (por defecto).
    AllLines    //!< Todas las lineas.
};

/*!
 * \brief Como se exporta la vista del mapa (MapWidget::exportView).
 *
 * El mapa sale tal cual se ve (base, entidades, objetivos, escala,
 * cuadricula...). Encima puede ir un TITULO y debajo un PIE con la extension
 * de la vista, la fecha (UTC), la escala de impresion (solo PDF), las notas y
 * la atribucion de los datos.
 */
struct MapExportOptions {
    QString title;              //!< Encabezado; vacio = sin encabezado.
    QString notes;              //!< Texto libre en el pie (p. ej. "Ejercicio 3").
    QString attribution;        //!< Atribucion de los datos (p. ej. "Imagen: Esri").
    bool footer = true;         //!< Pie con extension, fecha y (en PDF) escala.
    //! Solo imagen: factor de resolucion. 2 = el doble de pixeles por lado,
    //! con lineas y textos nitidos (las teselas se escalan).
    double imageScale = 1.0;
    //! Solo PDF: tamaño de la hoja en mm (A4 por defecto). La orientacion se
    //! elige sola para que el mapa aproveche la hoja.
    double pageWidthMm = 210.0;
    double pageHeightMm = 297.0;
    double marginMm = 10.0;     //!< Solo PDF: margen de la hoja.
};

//! Resultado de medir una superficie (MapTool::MeasureArea).
struct AreaMeasurement {
    QVector<QGeoCoordinate> polygon;   //!< Vertices marcados, sin repetir el primero.
    double areaSquareMeters = 0.0;     //!< Sobre el elipsoide (polygonArea).
    double perimeterMeters = 0.0;      //!< Ortodromicas, incluido el lado de cierre.
};

//! Resultado de una medicion sobre el mapa.
struct Measurement {
    QGeoCoordinate from;
    QGeoCoordinate to;
    double distanceMeters = 0.0;
    double azimuthDegrees = 0.0;
};

} // namespace libmapa

Q_DECLARE_METATYPE(libmapa::Measurement)
Q_DECLARE_METATYPE(libmapa::AreaMeasurement)
Q_DECLARE_METATYPE(libmapa::MapHit)

#endif // LIBMAPA_MAPTYPES_H_
