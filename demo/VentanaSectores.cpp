// Sectores y circulos con origen en un objetivo o en un punto del usuario.
// Parte de la clase Ventana de la demo (declarada en Ventana.h).
//
// Igual que los anillos, un sector NO es un elemento aparte: cuelga de un objetivo o de un punto
// y se maneja desde su menu del clic derecho. Puede haber varios en el mismo origen, y varios
// origenes a la vez. Un sector es una cuna rellena (area a vigilar); un circulo, una zona
// redonda. Siguen a su dueno si se mueve y se van con el cuando desaparece. El dueno de un
// sector de un punto se guarda en sus atributos, asi que sobrevive al guardado de las entidades;
// el de un objetivo no (los objetivos no se guardan) y esos sectores se quitan al abrir.

#include "Ventana.h"

#include "libmapa/Coordinates.h"

#include <QTimer>

using namespace libmapa;

namespace {

const char *const kCapa = "sectores";
const char *const kAtrPunto = "origen_punto";
const char *const kAtrObjetivo = "origen_objetivo";

// Pide radio y, si no es un circulo, los azimuts. false si se cancela.
bool pedirSector(QWidget *padre, bool circulo, const QString &titulo, double *radioKm, int *ini,
                 int *fin)
{
    QDialog d(padre);
    d.setWindowTitle(titulo);
    auto *f = new QFormLayout(&d);
    auto *radio = new QDoubleSpinBox(&d);
    radio->setRange(0.1, 2000.0);
    radio->setSuffix(QObject::tr(" km"));
    radio->setValue(*radioKm);
    auto *a = new QSpinBox(&d);
    auto *b = new QSpinBox(&d);
    for (QSpinBox *sp : { a, b }) {
        sp->setRange(0, 359);
        sp->setSuffix(QStringLiteral("°"));
    }
    a->setValue(*ini);
    b->setValue(*fin);
    f->addRow(QObject::tr("Radio"), radio);
    if (!circulo) {
        f->addRow(QObject::tr("Desde (azimut, sentido horario desde el norte)"), a);
        f->addRow(QObject::tr("Hasta (azimut)"), b);
    }
    auto *bt = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    f->addRow(bt);
    QObject::connect(bt, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(bt, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted)
        return false;
    *radioKm = radio->value();
    *ini = a->value();
    *fin = b->value();
    return true;
}

} // namespace

// Nombre de un sector para la lista: el del elemento del que cuelga y lo que es.
static QString etiquetaSector(const QString &dueno, bool circulo, double radioKm, int ini, int fin)
{
    return circulo ? QObject::tr("%1 · círculo %2 km").arg(dueno).arg(radioKm, 0, 'f', 0)
                   : QObject::tr("%1 · sector %2–%3 (%4 km)").arg(dueno).arg(ini).arg(fin)
                         .arg(radioKm, 0, 'f', 0);
}

// Ids de los sectores de un objetivo (\a targetId) o de un punto (\a featureId).
QVector<qint64> Ventana::sectoresDe(qint64 targetId, qint64 featureId) const
{
    QVector<qint64> ids;
    for (auto it = m_sectoresDe.constBegin(); it != m_sectoresDe.constEnd(); ++it)
        if ((targetId >= 0 && it->target == targetId)
            || (targetId < 0 && featureId >= 0 && it->target < 0 && it->feature == featureId))
            ids.append(it.key());
    std::sort(ids.begin(), ids.end());
    return ids;
}

// Crea un sector (o un circulo) con origen en un objetivo o un punto y lo anota como suyo.
qint64 Ventana::ponerSector(qint64 targetId, qint64 featureId, bool circulo, double radioKm,
                            double ini, double fin)
{
    const QGeoCoordinate c = posicionDe(targetId, featureId);
    if (!c.isValid())
        return -1;
    static const QColor paleta[] = { QColor(0xc6, 0x28, 0x28), QColor(0x00, 0x83, 0x8f),
                                     QColor(0x6a, 0x1b, 0x9a), QColor(0x55, 0x8b, 0x2f),
                                     QColor(0xef, 0x6c, 0x00) };
    const QColor color = paleta[sectoresDe(targetId, featureId).size() % 5];
    MapFeature f;
    f.kind = circulo ? GeometryKind::Circle : GeometryKind::Arc;
    f.layerId = QString::fromLatin1(kCapa);
    f.type = QStringLiteral("sector");
    f.name = etiquetaSector(targetId >= 0 ? nombreObjetivo(targetId) : nombrePunto(featureId),
                            circulo, radioKm, int(ini), int(fin));
    f.geometry = { c };
    f.radiusMeters = radioKm * 1000.0;
    f.arcStartDeg = ini;
    f.arcEndDeg = fin;
    f.arcSector = !circulo;
    f.style.lineColor = color;
    f.style.lineWidth = 2.0;
    f.style.fillColor = QColor(color.red(), color.green(), color.blue(), 60);
    f.attributes.insert(QString::fromLatin1(targetId >= 0 ? kAtrObjetivo : kAtrPunto),
                        targetId >= 0 ? targetId : featureId);
    const qint64 id = m_mapa->addFeature(f);
    if (id >= 0) {
        m_sectoresDe.insert(id, { targetId, targetId >= 0 ? -1 : featureId });
        marcarSucio();
    }
    return id;
}

// Pide radio y azimuts y pone un sector (o un circulo) al objetivo o punto dado.
void Ventana::ponerSectorPedido(qint64 targetId, qint64 featureId, bool circulo)
{
    double radio = 100.0;
    int ini = 0, fin = 90;
    if (!pedirSector(this, circulo, circulo ? tr("Nuevo círculo") : tr("Nuevo sector"), &radio,
                     &ini, &fin))
        return;
    if (ponerSector(targetId, featureId, circulo, radio, ini, fin) < 0)
        statusBar()->showMessage(tr("No se pudo poner el sector"), 4000);
}

// Pide otro radio y otros azimuts para un sector ya puesto y se los cambia.
void Ventana::cambiarSectorPedido(qint64 sectorId)
{
    auto f = m_mapa->feature(sectorId);
    if (!f)
        return;
    const bool circulo = f->kind == GeometryKind::Circle;
    double radio = f->radiusMeters / 1000.0;
    int ini = int(f->arcStartDeg), fin = int(f->arcEndDeg);
    if (!pedirSector(this, circulo, tr("Cambiar «%1»").arg(f->name), &radio, &ini, &fin))
        return;
    f->radiusMeters = radio * 1000.0;
    f->arcStartDeg = ini;
    f->arcEndDeg = fin;
    m_mapa->updateFeature(*f);
    marcarSucio();
}

// Nombres de los objetivos que caen dentro de un sector o circulo.
QStringList Ventana::objetivosDentroDe(qint64 sectorId) const
{
    QStringList dentro;
    const auto f = m_mapa->feature(sectorId);
    if (!f)
        return dentro;
    for (const MapTarget &t : m_mapa->targets())
        if (f->roundContains(t.position))
            dentro << nombreObjetivo(t.id);
    return dentro;
}

// Quita los sectores que cuelgan de un objetivo o de un punto (cuando este desaparece).
void Ventana::quitarSectoresDe(qint64 targetId, qint64 featureId)
{
    for (const qint64 id : sectoresDe(targetId, featureId)) {
        m_sectoresDe.remove(id);
        m_mapa->removeFeature(id);
    }
}

// Opciones de un sector: lo que tiene dentro, ir a su dueno, mostrarlo u ocultarlo, cambiarlo y
// quitarlo. Con \a cabeza lleva su titulo (si va dentro de un submenu ya lo tiene).
void Ventana::menuSector(QMenu *menu, qint64 sectorId, bool cabeza)
{
    const auto f = m_mapa->feature(sectorId);
    if (!f)
        return;
    if (cabeza) {
        QAction *c = menu->addAction(tr("Sector «%1»").arg(f->name));
        QFont fuente = c->font();
        fuente.setBold(true);
        c->setFont(fuente);
        c->setEnabled(false);
    }
    const QStringList dentro = objetivosDentroDe(sectorId);
    menu->addAction(dentro.isEmpty() ? tr("Ningún objetivo dentro")
                                     : tr("Dentro: %1").arg(dentro.join(QStringLiteral(", "))))
        ->setEnabled(false);
    const DuenoSector dueno = m_sectoresDe.value(sectorId);
    connect(menu->addAction(tr("Ir a su origen")), &QAction::triggered, this, [this, dueno] {
        const QGeoCoordinate p = posicionDe(dueno.target, dueno.feature);
        if (p.isValid())
            m_mapa->setCenter(p);
    });
    QAction *ver = menu->addAction(tr("Mostrar"));
    ver->setCheckable(true);
    ver->setChecked(f->visible);
    connect(ver, &QAction::toggled, this, [this, sectorId](bool on) {
        if (auto g = m_mapa->feature(sectorId)) {
            g->visible = on;
            m_mapa->updateFeature(*g);
        }
    });
    connect(menu->addAction(tr("Cambiar radio y azimuts...")), &QAction::triggered, this,
            [this, sectorId] { cambiarSectorPedido(sectorId); });
    connect(menu->addAction(tr("Eliminar el sector")), &QAction::triggered, this, [this, sectorId] {
        m_sectoresDe.remove(sectorId);
        m_mapa->removeFeature(sectorId);
        marcarSucio();
    });
}

// Submenu «Sectores» de un objetivo o de un punto: agregar un sector o un circulo y, por cada
// uno que ya tiene, su propio submenu. Un origen puede tener varios.
void Ventana::submenuSectores(QMenu *menu, qint64 targetId, qint64 featureId)
{
    const QVector<qint64> ya = sectoresDe(targetId, featureId);
    QMenu *sub = menu->addMenu(tr("Sectores (%1)").arg(ya.size()));
    connect(sub->addAction(tr("Agregar un sector...")), &QAction::triggered, this,
            [this, targetId, featureId] { ponerSectorPedido(targetId, featureId, false); });
    connect(sub->addAction(tr("Agregar un círculo...")), &QAction::triggered, this,
            [this, targetId, featureId] { ponerSectorPedido(targetId, featureId, true); });
    for (const qint64 id : ya)
        if (const auto f = m_mapa->feature(id))
            menuSector(sub->addMenu(f->name), id, false);
    if (!ya.isEmpty()) {
        sub->addSeparator();
        connect(sub->addAction(tr("Quitar todos los sectores")), &QAction::triggered, this,
                [this, targetId, featureId] {
                    quitarSectoresDe(targetId, featureId);
                    marcarSucio();
                });
    }
}

// Recompone quien es el dueno de cada sector tras abrir un fichero: los de un punto vuelven a
// seguir a su punto; los de un objetivo (que ya no existe) se quitan.
void Ventana::reconstruirSectores()
{
    for (const MapFeature &f : m_mapa->featuresOfType(QStringLiteral("sector"))) {
        if (m_sectoresDe.contains(f.id))
            continue;
        if (f.attributes.contains(QString::fromLatin1(kAtrPunto)))
            m_sectoresDe.insert(f.id, { -1, f.attributes.value(QString::fromLatin1(kAtrPunto)).toLongLong() });
        else if (f.attributes.contains(QString::fromLatin1(kAtrObjetivo)))
            m_mapa->removeFeature(f.id);
    }
}

// Paso periodico: cada sector sigue a su dueno (si se movio) y se quita si el dueno ya no existe.
void Ventana::siguenAlDueno()
{
    reconstruirSectores();
    QVector<qint64> huerfanos;
    for (auto it = m_sectoresDe.constBegin(); it != m_sectoresDe.constEnd(); ++it) {
        const auto f = m_mapa->feature(it.key());
        if (!f) {
            huerfanos.append(it.key());
            continue;
        }
        const QGeoCoordinate p = posicionDe(it->target, it->feature);
        if (!p.isValid()) {
            huerfanos.append(it.key());
            continue;
        }
        if (!f->geometry.isEmpty() && f->geometry.first().distanceTo(p) > 1.0) {
            MapFeature g = *f;
            g.geometry = { p };
            m_mapa->updateFeature(g);
        }
    }
    for (const qint64 id : huerfanos) {
        m_sectoresDe.remove(id);
        m_mapa->removeFeature(id);
    }
}
