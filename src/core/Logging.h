#ifndef LIBMAPA_CORE_LOGGING_H_
#define LIBMAPA_CORE_LOGGING_H_

#include <QLoggingCategory>

// Categorias de log de la libreria. Se activan con QT_LOGGING_RULES, p.ej.:
//   QT_LOGGING_RULES="libmapa.*.debug=true"
Q_DECLARE_LOGGING_CATEGORY(lcMapaDb)
Q_DECLARE_LOGGING_CATEGORY(lcMapaTiles)
Q_DECLARE_LOGGING_CATEGORY(lcMapaRender)
Q_DECLARE_LOGGING_CATEGORY(lcMapaGeo)

#endif // LIBMAPA_CORE_LOGGING_H_
