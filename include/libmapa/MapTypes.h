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
    SelectArea,    //!< Marcar un rectangulo SIN hacer zoom: emite areaSelected
                   //!< y deja el recuadro visible (para rellenar teselas, etc.).
    SelectPolygon, //!< Marcar un POLIGONO clic a clic (doble clic / Enter lo
                   //!< cierra): emite polygonSelected y deja el contorno visible,
                   //!< para rellenar solo esa zona.
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

//! Resultado de una medicion sobre el mapa.
struct Measurement {
    QGeoCoordinate from;
    QGeoCoordinate to;
    double distanceMeters = 0.0;
    double azimuthDegrees = 0.0;
};

} // namespace libmapa

Q_DECLARE_METATYPE(libmapa::Measurement)

#endif // LIBMAPA_MAPTYPES_H_
