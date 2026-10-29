#include "widget/LabelPlacement.h"

#include <QtTest>

using namespace libmapa;

Q_DECLARE_METATYPE(libmapa::LabelCorner)
Q_DECLARE_METATYPE(libmapa::LabelAnchor)

//! Tests de la colocacion de rotulos (calculos puros, sin ventanas): la esquina
//! de la linea guia segun el desplazamiento y la caja segun el LabelAnchor.
class TstLabelPlacement : public QObject
{
    Q_OBJECT

private slots:
    void cornerFollowsOffsetAzimuth_data();
    void cornerFollowsOffsetAzimuth();
    void cornerPointPicksTheCorner();
    void boxOriginPlacesBoxOnTheRequestedSide_data();
    void boxOriginPlacesBoxOnTheRequestedSide();
};

// Datos de la tabla de esquinas. Las cuatro primeras filas son el caso tipico de
// un formulario a (+-31, +-31) px; las demas, los limites exactos entre sectores.
void TstLabelPlacement::cornerFollowsOffsetAzimuth_data()
{
    QTest::addColumn<QPointF>("offset");
    QTest::addColumn<LabelCorner>("esperada");

    // Cuadrantes: el rotulo queda en el lado opuesto a la esquina elegida.
    QTest::newRow("arriba-izquierda 315") << QPointF(-31, -31) << LabelCorner::BottomRight;
    QTest::newRow("arriba-derecha 45")    << QPointF(31, -31)  << LabelCorner::BottomLeft;
    QTest::newRow("abajo-izquierda 225")  << QPointF(-31, 31)  << LabelCorner::TopRight;
    QTest::newRow("abajo-derecha 135")    << QPointF(31, 31)   << LabelCorner::TopLeft;

    // Limites: 0 y 360 (norte) -> inferior derecha; 90 -> inferior izquierda
    // (cierra el primer sector); 180 -> superior izquierda; 270 -> superior derecha.
    QTest::newRow("norte exacto 0")  << QPointF(0, -40)  << LabelCorner::BottomRight;
    QTest::newRow("este 90")         << QPointF(40, 0)   << LabelCorner::BottomLeft;
    QTest::newRow("sur 180")         << QPointF(0, 40)   << LabelCorner::TopLeft;
    QTest::newRow("oeste 270")       << QPointF(-40, 0)  << LabelCorner::TopRight;

    // Dentro de cada sector, lejos de los limites.
    QTest::newRow("20 grados")  << QPointF(10, -27)  << LabelCorner::BottomLeft;
    QTest::newRow("100 grados") << QPointF(40, 7)    << LabelCorner::TopLeft;
    QTest::newRow("200 grados") << QPointF(-14, 38)  << LabelCorner::TopRight;
    QTest::newRow("300 grados") << QPointF(-35, -20) << LabelCorner::BottomRight;
    QTest::newRow("350 grados") << QPointF(-7, -40)  << LabelCorner::BottomRight;

    // Sin desplazamiento no hay direccion: desempate.
    QTest::newRow("nulo") << QPointF(0, 0) << LabelCorner::BottomRight;
}

// La esquina depende solo del azimut del desplazamiento de pantalla.
void TstLabelPlacement::cornerFollowsOffsetAzimuth()
{
    QFETCH(QPointF, offset);
    QFETCH(LabelCorner, esperada);
    QCOMPARE(labelCornerFor(offset), esperada);
}

// El punto de union es la esquina de la caja que se pidio.
void TstLabelPlacement::cornerPointPicksTheCorner()
{
    const QRectF caja(10, 20, 100, 40);
    QCOMPARE(labelCornerPoint(LabelCorner::TopLeft, caja), QPointF(10, 20));
    QCOMPARE(labelCornerPoint(LabelCorner::TopRight, caja), QPointF(110, 20));
    QCOMPARE(labelCornerPoint(LabelCorner::BottomLeft, caja), QPointF(10, 60));
    QCOMPARE(labelCornerPoint(LabelCorner::BottomRight, caja), QPointF(110, 60));
}

// Origen (esquina superior izquierda) de una caja de 60x20 con hueco de 5 px.
void TstLabelPlacement::boxOriginPlacesBoxOnTheRequestedSide_data()
{
    QTest::addColumn<LabelAnchor>("ancla");
    QTest::addColumn<QPointF>("origen");

    QTest::newRow("Top")         << LabelAnchor::Top         << QPointF(-30, -25);
    QTest::newRow("Bottom")      << LabelAnchor::Bottom      << QPointF(-30, 5);
    QTest::newRow("Left")        << LabelAnchor::Left        << QPointF(-65, -10);
    QTest::newRow("Right")       << LabelAnchor::Right       << QPointF(5, -10);
    QTest::newRow("TopLeft")     << LabelAnchor::TopLeft     << QPointF(-65, -25);
    QTest::newRow("TopRight")    << LabelAnchor::TopRight    << QPointF(5, -25);
    QTest::newRow("BottomLeft")  << LabelAnchor::BottomLeft  << QPointF(-65, 5);
    QTest::newRow("BottomRight") << LabelAnchor::BottomRight << QPointF(5, 5);
    QTest::newRow("Center")      << LabelAnchor::Center      << QPointF(-30, -10);
    QTest::newRow("Auto")        << LabelAnchor::Auto        << QPointF(0, 0);
}

// Cada ancla deja la caja entera en su lado, sin tocar el anclaje (salvo Center).
void TstLabelPlacement::boxOriginPlacesBoxOnTheRequestedSide()
{
    QFETCH(LabelAnchor, ancla);
    QFETCH(QPointF, origen);
    QCOMPARE(labelBoxOrigin(ancla, QSizeF(60, 20), 5.0), origen);
}

QTEST_APPLESS_MAIN(TstLabelPlacement)
#include "tst_labelplacement.moc"
