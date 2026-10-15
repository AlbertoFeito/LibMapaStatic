#ifndef LIBMAPA_MAPTYPES_H_
#define LIBMAPA_MAPTYPES_H_

#include <QColor>
#include <QGeoCoordinate>
#include <QMetaType>
#include <QPixmap>
#include <QString>
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
 * en la parte de arriba de cada uno. Opcionalmente, radiales cada
 * \a spokeEveryDeg grados con su marcacion ("030°") en el extremo.
 */
struct RangeRings {
    QGeoCoordinate center;          //!< Centro fijo (si \a targetId < 0).
    qint64 targetId = -1;           //!< O centrado en este objetivo, y lo sigue.
    double spacingMeters = 1852.0;  //!< Separacion entre anillos (1 NM por defecto).
    int count = 5;                  //!< Numero de anillos (1..50).
    int spokeEveryDeg = 0;          //!< Radiales cada N grados; 0 = sin radiales.
    QColor color = QColor(0x00, 0x89, 0x7b);
    bool labels = true;             //!< Rotular distancias y marcaciones.
};

/*!
 * \brief Linea de marcacion (EBL) con su rumbo verdadero y distancia rotulados.
 *
 * El origen es un punto fijo o un objetivo (\a fromTarget, al que sigue). El
 * destino, igual: un punto o un objetivo. Sin destino, la linea sale del origen
 * con rumbo \a bearingDeg y longitud \a lengthMeters. El rotulo dice
 * "045° · 5.2 NM" en las unidades de la escala grafica.
 */
struct BearingLine {
    QGeoCoordinate from;            //!< Origen fijo (si \a fromTarget < 0).
    qint64 fromTarget = -1;         //!< O sale de este objetivo.
    QGeoCoordinate to;              //!< Destino fijo (si \a toTarget < 0)...
    qint64 toTarget = -1;           //!< ...o este objetivo.
    //! Sin destino: rumbo (grados verdaderos) y longitud de la linea.
    double bearingDeg = std::numeric_limits<double>::quiet_NaN();
    double lengthMeters = 0.0;
    QColor color = QColor(0x29, 0x62, 0xff);
    bool label = true;
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

#endif // LIBMAPA_MAPTYPES_H_
