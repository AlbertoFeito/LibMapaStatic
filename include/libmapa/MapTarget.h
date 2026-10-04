#ifndef LIBMAPA_MAPTARGET_H_
#define LIBMAPA_MAPTARGET_H_

#include <QColor>
#include <QGeoCoordinate>
#include <QMetaType>
#include <QString>
#include <QVariant>

namespace libmapa {

/*!
 * \brief Un objetivo movil: posicion, rumbo y una etiqueta de texto.
 *
 * A diferencia de MapFeature (entidades estaticas que se editan a mano), un
 * MapTarget se actualiza en tiempo real desde una fuente externa. La libreria
 * lleva su traza (las ultimas posiciones) y lo dibuja en una capa propia, para
 * que actualizar cientos de objetivos no obligue a redibujar las zonas
 * estaticas.
 *
 * La identidad la pone la aplicacion: suele ser un identificador de pista, un
 * MMSI o similar. Si se deja en -1, MapWidget asigna uno.
 */
struct MapTarget
{
    qint64 id = -1;

    QGeoCoordinate position;
    double headingDeg = 0.0;   //!< 0 = norte, sentido horario. Orienta el simbolo.
    double speed = 0.0;        //!< Informativo; la libreria no lo interpreta.

    QString label;             //!< Texto que se dibuja junto al objetivo.

    //! Clase que la aplicacion asigna al objetivo ("buque", "aeronave", "uav"...).
    //! La libreria no la interpreta; sirve para que la app elija simbolo o filtre.
    QString kind;

    //! Datos libres del objetivo (p.ej. mmsi/imo para AIS, callsign/squawk para
    //! ADS-B, bateria/enlace para un UAV). La libreria los guarda y los devuelve
    //! tal cual, SIN interpretarlos: asi un mismo MapTarget vale para cualquier
    //! dominio sin que la libreria conozca su semantica.
    QVariantMap attributes;

    QColor color = QColor(0xff, 0x8f, 0x00);   //!< Simbolo y traza.
    bool labelVisible = true;
    bool trailVisible = true;

    bool isValid() const { return position.isValid(); }
};

} // namespace libmapa

Q_DECLARE_METATYPE(libmapa::MapTarget)

#endif // LIBMAPA_MAPTARGET_H_
