// Piezas comunes de la demo: includes, roles del árbol, la sección plegable
// del panel y los iconos dibujados en código (definidos en Comun.cpp).
#pragma once

#include "libmapa/MapWidget.h"
#include "libmapa/TrackPlayer.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QEvent>
#include <QGroupBox>
#include <QHash>
#include <QMouseEvent>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QProgressDialog>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QCursor>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QListWidget>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSet>
#include <QStatusBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QTranslator>
#include <QLibraryInfo>
#include <QPalette>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <optional>

using namespace libmapa;

//! Roles para distinguir capas de entidades en el arbol.
constexpr int RolCapa = Qt::UserRole;        //!< id de capa (en ambos)
constexpr int RolEntidad = Qt::UserRole + 1; //!< id de entidad (solo hojas)
constexpr int RolBase = Qt::UserRole + 2;    //!< id de capa base (grupo «Base»)

// --- Iconos de objetivos, dibujados en codigo -----------------------------
// La libreria es agnostica del dominio: el JUEGO de iconos lo pone la app. Aqui
// se dibujan a mano (sin ficheros) apuntando al NORTE (arriba); la libreria los
// gira segun el rumbo. Es justo lo que hace un producto real con su simbologia.

// Casco de buque: proa arriba, popa abajo.
QPixmap iconoBuque(const QColor &c);

// Silueta de aeronave: fuselaje, alas y cola, apuntando arriba.
QPixmap iconoAereo(const QColor &c);

// Cuadricoptero: cuatro brazos en X con sus rotores y un nucleo.
QPixmap iconoUav(const QColor &c);

// Sección plegable: una cabecera con flecha (QToolButton) que muestra/oculta su
// contenido. No necesita Q_OBJECT (no define señales propias; connect es estático).
// Se usa en la ficha contextual de la entidad para Atributos/Coordenadas/Estilo.
class SeccionPlegable : public QWidget
{
public:
    explicit SeccionPlegable(const QString &titulo, QWidget *parent = nullptr,
                             bool abierta = false)
        : QWidget(parent)
    {
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(2);

        m_boton = new QToolButton(this);
        m_boton->setText(titulo);
        m_boton->setCheckable(true);
        m_boton->setChecked(abierta);
        m_boton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        m_boton->setArrowType(abierta ? Qt::DownArrow : Qt::RightArrow);
        m_boton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_boton->setObjectName(QStringLiteral("seccionHeader"));
        v->addWidget(m_boton);

        m_cont = new QWidget(this);
        m_cont->setVisible(abierta);
        v->addWidget(m_cont);

        QObject::connect(m_boton, &QToolButton::toggled, m_cont,
                         [this](bool on) {
                             m_cont->setVisible(on);
                             m_boton->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
                         });
    }

    //! Widget donde colocar el contenido (se le pone un layout desde fuera).
    QWidget *contenido() const { return m_cont; }

private:
    QToolButton *m_boton;
    QWidget *m_cont;
};

// Dibuja en código un icono de herramienta (glifo lineal) del color \a c, para
// que se recoloree según el tema (claro/oscuro). Estilo coherente: trazo fino,
// 20×20, sin relleno salvo donde ayuda a leerlo. \a k es la clave de la acción.
QIcon glifo(const QString &k, const QColor &c);
