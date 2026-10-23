#ifndef TELEMETRIA_TELEMETRIA_EXPORT_H_
#define TELEMETRIA_TELEMETRIA_EXPORT_H_

#include <QtGlobal>

// Nivel del API: sube cuando se anade algo que una aplicacion puede necesitar. Una aplicacion que exige
// un nivel mas alto del que trae la instalacion (cabeceras viejas, sin reinstalar) lo comprueba con
// #if y da un mensaje claro en vez de un «no esta declarado». Nivel 2: utcDateTime. Nivel 3: los 27 tipos
// AIS de M.1371 (campos nuevos de AisMessage, aisAtonTypeText y aisEpfsTypeText).
#define TELEMETRIA_API_LEVEL 3

#if defined(TELEMETRIA_STATIC)
#  define TELEMETRIA_EXPORT
#elif defined(TELEMETRIA_BUILD)
#  define TELEMETRIA_EXPORT Q_DECL_EXPORT
#else
#  define TELEMETRIA_EXPORT Q_DECL_IMPORT
#endif

#endif // TELEMETRIA_TELEMETRIA_EXPORT_H_
