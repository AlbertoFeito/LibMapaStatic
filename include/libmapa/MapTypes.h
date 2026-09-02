#ifndef LIBMAPA_MAPTYPES_H_
#define LIBMAPA_MAPTYPES_H_

#include <QColor>
#include <QGeoCoordinate>
#include <QMetaType>
#include <QPixmap>
#include <QString>
#include <QVector>

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
    PickPoint,     //!< Devolver la coordenada del siguiente clic.

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

/*!
 * \brief Un punto sobre el mapa.
 *
 * Es un tipo de VALOR, no un puntero a una jerarquia. El codigo original
 * manejaba QList<CPunto*> y QList<void*>, con castes a mano en cada uso
 * (fallas F-12 y F-7 del analisis). Aqui la identidad es un entero que
 * devuelve MapWidget al anadirlo.
 */
struct MapPoint {
    qint64 id = -1;              //!< Lo asigna MapWidget; -1 si aun no se anadio.
    QString name;
    QString description;
    QGeoCoordinate position;
    QPixmap icon;                //!< Vacio = simbolo por defecto.
    QColor color = Qt::red;
    bool labelVisible = true;
    double altitude = 0.0;
};

//! Clase de vehiculo. Sustituye a la jerarquia CVehiculo/CAvion/CBarco.
enum class VehicleKind {
    Land,
    Aerial,
    Naval
};

//! Datos AIS. Van por COMPOSICION, no por herencia: en el codigo original
//! CBarco anadia unos cincuenta getters que CAvion no tenia ni podia usar.
struct AisData {
    QString mmsi;
    QString imo;
    QString callSign;
    QString shipName;
    QString shipType;
    QString flag;
    QString destination;
    QString navigationStatus;
    double length = 0.0;
    double beam = 0.0;
    double draught = 0.0;
    double heading = 0.0;
    double course = 0.0;
    qint64 lastUpdateUtcMs = 0;
    bool isValid() const { return !mmsi.isEmpty(); }
};

struct MapVehicle {
    qint64 id = -1;
    QString name;
    VehicleKind kind = VehicleKind::Land;
    QGeoCoordinate position;
    double heading = 0.0;        //!< Grados desde el norte.
    double speed = 0.0;
    double altitude = 0.0;
    QPixmap icon;
    QColor trackColor = Qt::yellow;
    bool trackVisible = true;
    int trackMaxPoints = 500;
    AisData ais;                 //!< Solo relevante en los navales.
};

//! Una muestra de trayectoria.
struct TrackSample {
    QGeoCoordinate position;
    qint64 timeUtcMs = 0;      //!< Epoch en ms, no texto: comparable en SQL.
    double heading = 0.0;
    double speed = 0.0;
    double altitude = 0.0;
};

struct MapPolygon {
    qint64 id = -1;
    QString name;
    QVector<QGeoCoordinate> vertices;
    QColor lineColor = Qt::darkGreen;
    QColor fillColor = QColor(0, 128, 0, 60);
    bool filled = true;
};

struct MapRoutePoint {
    QGeoCoordinate position;
    QString description;
    int priority = 0;
    double approachRadiusMeters = 20.0;
};

struct MapRoute {
    qint64 id = -1;
    QString name;
    QVector<MapRoutePoint> points;
    QColor color = Qt::blue;
};

//! Resultado de una medicion sobre el mapa.
struct Measurement {
    QGeoCoordinate from;
    QGeoCoordinate to;
    double distanceMeters = 0.0;
    double azimuthDegrees = 0.0;
};

} // namespace libmapa

Q_DECLARE_METATYPE(libmapa::MapPoint)
Q_DECLARE_METATYPE(libmapa::MapVehicle)
Q_DECLARE_METATYPE(libmapa::MapPolygon)
Q_DECLARE_METATYPE(libmapa::MapRoute)
Q_DECLARE_METATYPE(libmapa::Measurement)
Q_DECLARE_METATYPE(libmapa::TrackSample)

#endif // LIBMAPA_MAPTYPES_H_
