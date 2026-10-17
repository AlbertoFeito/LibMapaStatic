#ifndef LIBMAPA_DEM_GEOTIFF_H_
#define LIBMAPA_DEM_GEOTIFF_H_

#include <QString>

#include <memory>

namespace libmapa {

/*!
 * \brief Lector mínimo de imágenes GeoTIFF, sin GDAL ni librerías externas.
 *
 * Lee la PRIMERA imagen del fichero (la de resolución completa; los
 * sobremuestreos de un COG se ignoran) y la deja consultable píxel a píxel.
 * Está pensado para modelos de elevación (DEM), pero lee cualquier banda
 * numérica, así que sirve también para ortofotos de 8 bits.
 *
 * Soporta:
 *  - TIFF clásico y BigTIFF, en cualquier orden de bytes.
 *  - Organización por franjas (strips) o por teselas (tiles), con muestras
 *    entrelazadas o por planos.
 *  - Compresión: ninguna, LZW, Deflate (Adobe y PKZIP) y PackBits, con los
 *    predictores 1 (nada), 2 (diferencias horizontales) y 3 (coma flotante).
 *  - Muestras de 8/16/32 bits enteras (con y sin signo) y de 32/64 bits en
 *    coma flotante.
 *  - Georreferencia por ModelPixelScale+ModelTiepoint o por
 *    ModelTransformation sin rotación; PixelIsArea y PixelIsPoint.
 *  - Sistemas de coordenadas geográficos (grados, cualquier datum) y Web
 *    Mercator (EPSG:3857). Cualquier otro se rechaza con un mensaje que dice
 *    cuál es: hay que reproyectarlo a EPSG:4326 antes.
 *  - El valor «sin dato» de GDAL (etiqueta GDAL_NODATA).
 *
 * No carga la imagen entera: descodifica bajo demanda los bloques que se
 * consultan y guarda los últimos en una caché acotada, así que un DEM de varios
 * GB no ocupa memoria. Es seguro para varios hilos.
 */
class GeoTiffImage
{
public:
    //! Sistema de coordenadas en el que está georreferenciada la imagen.
    enum class Crs {
        Geographic,    //!< x = longitud, y = latitud, en grados
        WebMercator    //!< EPSG:3857, en metros
    };

    GeoTiffImage();
    ~GeoTiffImage();
    GeoTiffImage(const GeoTiffImage &) = delete;
    GeoTiffImage &operator=(const GeoTiffImage &) = delete;

    /*!
     * \brief Abre el fichero y lee su cabecera (no los píxeles).
     * \return false si no es un TIFF legible o usa algo no soportado; el motivo
     *         queda en \a error.
     */
    bool open(const QString &path, QString *error = nullptr);

    bool isOpen() const;
    QString path() const;

    int width() const;
    int height() const;
    int bands() const;           //!< muestras por píxel
    Crs crs() const;

    //! Extensión geográfica de la imagen, en grados (en Web Mercator se convierte).
    double lonMin() const;
    double lonMax() const;
    double latMin() const;
    double latMax() const;

    //! ¿Hay un valor «sin dato» declarado en el fichero?
    bool hasNoData() const;
    double noData() const;

    /*!
     * \brief Valor de la banda \a band en el píxel (\a col, \a row).
     *
     * \a row 0 es la fila superior del fichero. NaN si está fuera de la imagen,
     * si es el valor «sin dato» o si el bloque no se pudo leer/descodificar.
     */
    double sample(int col, int row, int band = 0) const;

    /*!
     * \brief Posición continua en la imagen (en píxeles) de una coordenada.
     *
     * (\a col, \a row) son fraccionarios y se miden respecto al CENTRO del píxel
     * (0,0), de modo que un valor entero cae justo sobre una muestra. Devuelve
     * false si la coordenada queda fuera de la imagen.
     */
    bool locate(double lon, double lat, double *col, double *row) const;

    //! Cota (o valor de la banda \a band) interpolada bilinealmente en la
    //! coordenada; NaN si queda fuera o algún vecino es «sin dato».
    double interpolate(double lon, double lat, int band = 0) const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace libmapa

#endif // LIBMAPA_DEM_GEOTIFF_H_
