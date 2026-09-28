#ifndef LIBMAPA_COMPAT_OPTIONAL_H_
#define LIBMAPA_COMPAT_OPTIONAL_H_

/*!
 * \file Optional.h
 * \brief Un "optional" propio, portable de C++11 en adelante.
 *
 * POR QUE existe: la libreria tiene que compilar tambien con el MinGW que
 * trae Qt 5.7 (GCC 5.3). Ese toolchain no tiene <optional> (llego en C++17 /
 * GCC 7), y ademas el qmake de 5.7 no entiende "CONFIG += c++17", asi que el
 * proyecto se queda en C++11/14. No sirve usar <experimental/optional> como
 * puente porque los compiladores nuevos (GCC 10+) ya lo quitaron: no habria un
 * unico camino valido para todas las versiones.
 *
 * La solucion es un tipo minimo, sin dependencias de la biblioteca estandar,
 * que cubre justo lo que la libreria usa: has_value(), conversion a bool,
 * *opt, opt->, value(), y devolver "vacio" con libmapa::nullopt o {}.
 *
 * Aqui solo se guardan tipos de VALOR por-defecto-construibles (MapFeature,
 * MapTarget, LayerInfo, TileDataset, qint64...), asi que basta con guardar un
 * T y un bool; no hace falta almacenamiento sin inicializar.
 */

namespace libmapa {

//! Marcador para "sin valor": permite `return libmapa::nullopt;`.
struct nullopt_t { };
// const a nivel de espacio de nombres => enlace interno: cada .cpp tiene su
// copia y no hay conflicto de ODR (compatible con C++11/14, sin inline vars).
static const nullopt_t nullopt = nullopt_t();

/*!
 * \brief Contenedor de "cero o un" valor de tipo \a T.
 *
 * Equivalente reducido de std::optional para lo que necesita la libreria.
 */
template <typename T>
class optional
{
public:
    optional() : m_has(false), m_value() {}
    optional(nullopt_t) : m_has(false), m_value() {}
    optional(const T &value) : m_has(true), m_value(value) {}

    optional &operator=(nullopt_t)
    {
        m_has = false;
        m_value = T();
        return *this;
    }
    optional &operator=(const T &value)
    {
        m_has = true;
        m_value = value;
        return *this;
    }

    bool has_value() const { return m_has; }
    explicit operator bool() const { return m_has; }

    const T &value() const { return m_value; }
    T &value() { return m_value; }

    const T &operator*() const { return m_value; }
    T &operator*() { return m_value; }

    const T *operator->() const { return &m_value; }
    T *operator->() { return &m_value; }

private:
    bool m_has;
    T m_value;
};

} // namespace libmapa

#endif // LIBMAPA_COMPAT_OPTIONAL_H_
