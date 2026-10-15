// Iconos de la demo dibujados en código: objetivos (buque, aéreo, UAV) y los
// glifos de las herramientas, que se recolorean según el tema.

#include "Comun.h"

// Casco de buque: proa arriba, popa abajo.
QPixmap iconoBuque(const QColor &c)
{
    const int S = 22;
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(Qt::black, 1.0));
    p.setBrush(c);
    QPolygonF casco;
    casco << QPointF(S / 2.0, 2) << QPointF(S - 6, S - 5)
          << QPointF(S / 2.0, S - 2) << QPointF(5, S - 5);
    p.drawPolygon(casco);
    return pm;
}

// Silueta de aeronave: fuselaje, alas y cola, apuntando arriba.
QPixmap iconoAereo(const QColor &c)
{
    const int S = 22;
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(Qt::black, 1.0));
    p.setBrush(c);
    const double cx = S / 2.0;
    QPolygonF av;
    av << QPointF(cx, 1)
       << QPointF(cx + 2, S * 0.45)
       << QPointF(S - 2, S * 0.62)
       << QPointF(cx + 2, S * 0.62)
       << QPointF(cx + 2, S - 4)
       << QPointF(cx + 4, S - 1)
       << QPointF(cx - 4, S - 1)
       << QPointF(cx - 2, S - 4)
       << QPointF(cx - 2, S * 0.62)
       << QPointF(2, S * 0.62)
       << QPointF(cx - 2, S * 0.45);
    p.drawPolygon(av);
    return pm;
}

// Cuadricoptero: cuatro brazos en X con sus rotores y un nucleo.
QPixmap iconoUav(const QColor &c)
{
    const int S = 22;
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const double m = 4, M = S - 4, cx = S / 2.0;
    p.setPen(QPen(c.darker(160), 2.0));
    p.drawLine(QPointF(m, m), QPointF(M, M));
    p.drawLine(QPointF(M, m), QPointF(m, M));
    p.setPen(QPen(Qt::black, 0.8));
    p.setBrush(c);
    for (const QPointF &o : {QPointF(m, m), QPointF(M, m),
                             QPointF(m, M), QPointF(M, M)})
        p.drawEllipse(o, 3.2, 3.2);
    p.setBrush(c.darker(130));
    p.drawEllipse(QPointF(cx, cx), 2.5, 2.5);
    return pm;
}

// Dibuja en código un icono de herramienta (glifo lineal) del color \a c, para
// que se recoloree según el tema (claro/oscuro). Estilo coherente: trazo fino,
// 20×20, sin relleno salvo donde ayuda a leerlo. \a k es la clave de la acción.
QIcon glifo(const QString &k, const QColor &c)
{
    QPixmap pm(20, 20);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(c, 1.7);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    if (k == QLatin1String("navegar")) {               // flecha de cursor
        QPolygonF f({{5, 3}, {5, 16}, {8.5, 12.5}, {11, 17}, {13, 16}, {10.5, 11.5}, {15, 11}});
        p.setBrush(c); p.drawPolygon(f);
    } else if (k == QLatin1String("medir")) {          // regla con marcas
        p.drawRect(QRectF(3, 7, 14, 6));
        for (double x = 5; x < 17; x += 3)
            p.drawLine(QPointF(x, 7), QPointF(x, 10));
    } else if (k == QLatin1String("medirarea")) {      // polígono con regla abajo
        p.drawPolygon(QPolygonF({{4, 3}, {16, 5}, {14, 12}, {5, 11}}));
        p.drawLine(QPointF(3, 16), QPointF(17, 16));
        for (double x = 3; x <= 17; x += 3.5)
            p.drawLine(QPointF(x, 14.5), QPointF(x, 16));
    } else if (k == QLatin1String("zoom")) {           // lupa con +
        p.drawEllipse(QPointF(8.5, 8.5), 5, 5);
        p.drawLine(QPointF(12.5, 12.5), QPointF(17, 17));
        p.drawLine(QPointF(8.5, 6), QPointF(8.5, 11));
        p.drawLine(QPointF(6, 8.5), QPointF(11, 8.5));
    } else if (k == QLatin1String("punto")) {          // punto con anillo
        p.drawEllipse(QPointF(10, 10), 5.5, 5.5);
        p.setBrush(c); p.drawEllipse(QPointF(10, 10), 2.2, 2.2);
    } else if (k == QLatin1String("linea")) {          // polilínea + vértices
        p.drawPolyline(QPolygonF({{3.5, 15}, {9, 6}, {16.5, 13}}));
        p.setBrush(c);
        for (const QPointF &v : {QPointF(3.5, 15), QPointF(9, 6), QPointF(16.5, 13)})
            p.drawEllipse(v, 1.7, 1.7);
    } else if (k == QLatin1String("poligono")) {       // pentágono + vértices
        QPolygonF pg({{10, 3}, {17, 8.5}, {14, 17}, {6, 17}, {3, 8.5}});
        p.drawPolygon(pg);
        p.setBrush(c);
        for (const QPointF &v : pg) p.drawEllipse(v, 1.5, 1.5);
    } else if (k == QLatin1String("objetivo")) {       // cruz de mira
        p.drawEllipse(QPointF(10, 10), 5.5, 5.5);
        p.drawLine(QPointF(10, 1.5), QPointF(10, 5));
        p.drawLine(QPointF(10, 15), QPointF(10, 18.5));
        p.drawLine(QPointF(1.5, 10), QPointF(5, 10));
        p.drawLine(QPointF(15, 10), QPointF(18.5, 10));
        p.setBrush(c); p.drawEllipse(QPointF(10, 10), 1.3, 1.3);
    } else if (k == QLatin1String("area")) {           // pentágono relleno suave
        QPolygonF pg({{10, 3}, {17, 8.5}, {14, 17}, {6, 17}, {3, 8.5}});
        QColor relleno = c; relleno.setAlpha(45);
        p.setBrush(relleno); p.drawPolygon(pg);
        p.setBrush(c); p.drawEllipse(QPointF(10, 3), 2.2, 2.2);   // 1er vértice
    } else if (k == QLatin1String("editar")) {         // lápiz
        QPolygonF cuerpo({{12.5, 3.5}, {16.5, 7.5}, {7.5, 16.5}, {3.5, 12.5}});
        p.drawPolygon(cuerpo);
        p.drawLine(QPointF(11, 5), QPointF(15, 9));     // virola
        p.setBrush(c);                                  // punta de grafito
        p.drawPolygon(QPolygonF({{3.5, 12.5}, {7.5, 16.5}, {2.8, 17.2}}));
    } else if (k == QLatin1String("deshacer") || k == QLatin1String("rehacer")) {
        const bool re = (k == QLatin1String("rehacer"));
        QPainterPath path;                              // flecha curva de retorno
        if (re) { path.moveTo(5, 8); path.cubicTo(7, 4.5, 13, 4.5, 15, 8.5); }
        else    { path.moveTo(15, 8); path.cubicTo(13, 4.5, 7, 4.5, 5, 8.5); }
        p.drawPath(path);
        p.setBrush(c);
        if (re) p.drawPolygon(QPolygonF({{15, 8.5}, {11.5, 9}, {14, 12}}));
        else    p.drawPolygon(QPolygonF({{5, 8.5}, {8.5, 9}, {6, 12}}));
    } else if (k == QLatin1String("borrar")) {         // papelera
        p.drawLine(QPointF(4, 6), QPointF(16, 6));
        p.drawRect(QRectF(5.5, 6, 9, 11));
        p.drawRect(QRectF(8, 3.5, 4, 2.5));
        for (double x = 8; x <= 12; x += 2) p.drawLine(QPointF(x, 8.5), QPointF(x, 14.5));
    } else if (k == QLatin1String("deshacer") || k == QLatin1String("rehacer")) {
        const bool re = (k == QLatin1String("rehacer"));
        QRectF arc(4, 5, 12, 10);
        p.drawArc(arc, 30 * 16, (re ? -200 : 200) * 16);
        const QPointF tip = re ? QPointF(15.5, 7.5) : QPointF(4.5, 7.5);
        const double dx = re ? -1.0 : 1.0;
        p.setBrush(c);
        p.drawPolygon(QPolygonF({tip, {tip.x() + 3.5 * dx, tip.y() - 1},
                                 {tip.x() + 1.5 * dx, tip.y() + 3}}));
    } else if (k == QLatin1String("guardar")) {        // disquete
        QPolygonF cuerpo({{3.5, 3.5}, {14, 3.5}, {16.5, 6}, {16.5, 16.5}, {3.5, 16.5}});
        p.drawPolygon(cuerpo);
        p.drawRect(QRectF(6.5, 3.5, 5, 3.5));
        p.drawRect(QRectF(6, 10.5, 8, 6));
    } else if (k == QLatin1String("abrir")) {          // carpeta
        p.drawPolyline(QPolygonF({{3, 15.5}, {3, 6}, {8, 6}, {9.5, 8}, {17, 8}, {17, 15.5}, {3, 15.5}}));
    } else if (k == QLatin1String("rejilla")) {        // cuadrícula
        p.drawRect(QRectF(4, 4, 12, 12));
        p.drawLine(QPointF(10, 4), QPointF(10, 16));
        p.drawLine(QPointF(4, 10), QPointF(16, 10));
    } else if (k == QLatin1String("cobertura")) {      // mancha / tablero
        QColor relleno = c; relleno.setAlpha(55);
        p.setBrush(relleno);
        p.drawRoundedRect(QRectF(4, 5, 12, 10), 3, 3);
    } else if (k == QLatin1String("geo")) {            // capas apiladas
        p.drawPolygon(QPolygonF({{10, 3.5}, {17, 7}, {10, 10.5}, {3, 7}}));
        p.drawPolyline(QPolygonF({{3, 11}, {10, 14.5}, {17, 11}}));
    } else if (k == QLatin1String("simular")) {        // play
        p.setBrush(c); p.drawPolygon(QPolygonF({{6, 4}, {16, 10}, {6, 16}}));
    } else if (k == QLatin1String("dem")) {            // montaña
        p.drawPolyline(QPolygonF({{3, 16}, {8, 7}, {11, 12}, {13.5, 8.5}, {17, 16}}));
    } else {
        p.drawEllipse(QPointF(10, 10), 5, 5);          // genérico
    }
    p.end();
    return QIcon(pm);
}
