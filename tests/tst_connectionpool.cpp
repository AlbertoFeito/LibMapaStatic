#include "db/SqliteConnectionPool.h"
#include "db/Transaction.h"

// Qt5 solo DECLARA QVariant en qsqlquery.h (Qt6 si lo incluye), asi que
// hay que pedirlo explicitamente: sin esto, bindValue() no compila en
// Qt 5.14 porque QVariant es un tipo incompleto y no hay conversion
// posible desde int, QString ni QByteArray.
#include <QVariant>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

using namespace libmapa;

class TstConnectionPool : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();

    void reusesSameConnection();

    /*! F-3: en el codigo original bd_Sat y bd_Osm se registraban las dos con
     *  el connectionName "Mapas", asi que el segundo addDatabase expulsaba al
     *  primero. Este test demuestra primero el fallo con la API cruda de Qt
     *  y despues que el pool ya no lo tiene. */
    void twoDatasetsDoNotCollide();
    void demonstrateOriginalCollisionBug();

    /*! F-2: removeDatabase("QSQLITE") no borraba nada porque "QSQLITE" es el
     *  driver, no el nombre de conexion. */
    void demonstrateOriginalRemoveDatabaseBug();

    void readOnlyRejectsWrites();
    void missingFileFails();
    void closeAllReleasesConnections();
    void connectionNamesAreThreadSpecific();

    void transactionRollsBackOnScopeExit();
    void transactionCommits();

private:
    QString makeDb(const QString &name);

    QTemporaryDir m_dir;
};

QString TstConnectionPool::makeDb(const QString &name)
{
    const QString path = m_dir.filePath(name);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                    QStringLiteral("setup"));
        db.setDatabaseName(path);
        if (!db.open())
            return {};
        QSqlQuery q(db);
        q.exec(QStringLiteral("CREATE TABLE t (id INTEGER PRIMARY KEY, v TEXT)"));
        q.exec(QStringLiteral("INSERT INTO t (v) VALUES ('%1')").arg(name));
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("setup"));
    return path;
}

void TstConnectionPool::initTestCase()
{
    QVERIFY(m_dir.isValid());
    QVERIFY(QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE")));
}

void TstConnectionPool::cleanup()
{
    SqliteConnectionPool::closeAllForCurrentThread();
}

void TstConnectionPool::reusesSameConnection()
{
    const QString path = makeDb(QStringLiteral("reuse.db"));
    QVERIFY(!path.isEmpty());

    QSqlDatabase a = SqliteConnectionPool::connectionFor(QStringLiteral("ds"), path);
    QVERIFY(a.isOpen());
    QCOMPARE(SqliteConnectionPool::openConnectionCount(), 1);

    // Cien peticiones mas no abren cien conexiones.
    for (int i = 0; i < 100; ++i) {
        QSqlDatabase b = SqliteConnectionPool::connectionFor(QStringLiteral("ds"), path);
        QVERIFY(b.isOpen());
        QCOMPARE(b.connectionName(), a.connectionName());
    }
    QCOMPARE(SqliteConnectionPool::openConnectionCount(), 1);
}

void TstConnectionPool::twoDatasetsDoNotCollide()
{
    const QString osm = makeDb(QStringLiteral("osm.db"));
    const QString sat = makeDb(QStringLiteral("sat.db"));

    QSqlDatabase dbOsm = SqliteConnectionPool::connectionFor(
        QStringLiteral("osm"), osm);
    QSqlDatabase dbSat = SqliteConnectionPool::connectionFor(
        QStringLiteral("satelital"), sat);

    QVERIFY(dbOsm.isOpen());
    QVERIFY(dbSat.isOpen());
    QVERIFY(dbOsm.connectionName() != dbSat.connectionName());
    QCOMPARE(SqliteConnectionPool::openConnectionCount(), 2);

    // Y lo esencial: la primera sigue viva y apuntando a SU fichero despues
    // de abrir la segunda. Esto es justo lo que fallaba al alternar capas.
    QSqlQuery q1(dbOsm);
    QVERIFY(q1.exec(QStringLiteral("SELECT v FROM t")));
    QVERIFY(q1.next());
    QCOMPARE(q1.value(0).toString(), QStringLiteral("osm.db"));

    QSqlQuery q2(dbSat);
    QVERIFY(q2.exec(QStringLiteral("SELECT v FROM t")));
    QVERIFY(q2.next());
    QCOMPARE(q2.value(0).toString(), QStringLiteral("sat.db"));

    // Ida y vuelta varias veces, como al pulsar los botones OSM/Satelital.
    for (int i = 0; i < 5; ++i) {
        QSqlDatabase a = SqliteConnectionPool::connectionFor(QStringLiteral("osm"), osm);
        QSqlDatabase b = SqliteConnectionPool::connectionFor(QStringLiteral("satelital"), sat);
        QVERIFY(a.isOpen());
        QVERIFY(b.isOpen());
    }
    QCOMPARE(SqliteConnectionPool::openConnectionCount(), 2);
}

void TstConnectionPool::demonstrateOriginalCollisionBug()
{
    const QString osm = makeDb(QStringLiteral("bug_osm.db"));
    const QString sat = makeDb(QStringLiteral("bug_sat.db"));

    // Reproduccion literal de CBDatos: ambas conexiones con el nombre "Mapas".
    QSqlDatabase bdSat = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                   QStringLiteral("Mapas"));
    bdSat.setDatabaseName(sat);
    QVERIFY(bdSat.open());
    QCOMPARE(bdSat.connectionName(), QStringLiteral("Mapas"));

    QSqlDatabase bdOsm = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                   QStringLiteral("Mapas"));
    bdOsm.setDatabaseName(osm);
    QVERIFY(bdOsm.open());

    // Resultado, y es peor de lo que parecia: Qt no solo sustituye la
    // conexion, sino que DESACTIVA el objeto anterior. bdSat se queda sin
    // nombre y sin driver util.
    QCOMPARE(bdSat.connectionName(), QString());
    QVERIFY2(!bdSat.isValid(),
             "La primera conexion queda invalidada al reutilizar el nombre");

    // Y el nombre "Mapas" apunta ya solo al segundo fichero.
    QCOMPARE(QSqlDatabase::database(QStringLiteral("Mapas"), false).databaseName(),
             osm);

    // Con lo cual, en el codigo original, tras abrir OSM cualquier consulta
    // hecha "sobre bd_Sat" iba en realidad contra la BD equivocada o fallaba.
    QSqlQuery q(bdSat);
    QVERIFY2(!q.exec(QStringLiteral("SELECT v FROM t")),
             "Consultar por la conexion invalidada debe fallar");

    {
        QSqlDatabase alive = QSqlDatabase::database(QStringLiteral("Mapas"), false);
        if (alive.isOpen())
            alive.close();
    }
    bdOsm = QSqlDatabase();
    QSqlDatabase::removeDatabase(QStringLiteral("Mapas"));
}

void TstConnectionPool::demonstrateOriginalRemoveDatabaseBug()
{
    const QString path = makeDb(QStringLiteral("bug_remove.db"));

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                    QStringLiteral("Mapas"));
        db.setDatabaseName(path);
        QVERIFY(db.open());
    }

    // La linea literal del codigo original. "QSQLITE" es el DRIVER, no el
    // connectionName, asi que no borra absolutamente nada. Segun la version
    // de Qt lo avisa por consola o lo ignora en silencio, que es peor.
    QSqlDatabase::removeDatabase(QStringLiteral("QSQLITE"));

    // Comprobacion de que fue un no-op: la conexion sigue registrada y abierta.
    QVERIFY2(QSqlDatabase::contains(QStringLiteral("Mapas")),
             "removeDatabase(\"QSQLITE\") no borra la conexion \"Mapas\"");
    QVERIFY(QSqlDatabase::database(QStringLiteral("Mapas"), false).isOpen());

    // Asi se hace bien: cerrar, soltar toda referencia y borrar por NOMBRE.
    {
        QSqlDatabase db = QSqlDatabase::database(QStringLiteral("Mapas"), false);
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("Mapas"));
    QVERIFY(!QSqlDatabase::contains(QStringLiteral("Mapas")));
}

void TstConnectionPool::readOnlyRejectsWrites()
{
    const QString path = makeDb(QStringLiteral("ro.db"));

    QSqlDatabase db = SqliteConnectionPool::connectionFor(
        QStringLiteral("ro"), path, SqliteConnectionPool::Mode::ReadOnly);
    QVERIFY(db.isOpen());

    QSqlQuery read(db);
    QVERIFY(read.exec(QStringLiteral("SELECT COUNT(*) FROM t")));

    QSqlQuery write(db);
    QVERIFY2(!write.exec(QStringLiteral("INSERT INTO t (v) VALUES ('x')")),
             "Una conexion de solo lectura no debe permitir escribir");
}

void TstConnectionPool::missingFileFails()
{
    QSqlDatabase db = SqliteConnectionPool::connectionFor(
        QStringLiteral("noexiste"), m_dir.filePath(QStringLiteral("nope.db")));
    QVERIFY(!db.isOpen());
    QCOMPARE(SqliteConnectionPool::openConnectionCount(), 0);
}

void TstConnectionPool::closeAllReleasesConnections()
{
    const QString path = makeDb(QStringLiteral("close.db"));

    SqliteConnectionPool::connectionFor(QStringLiteral("a"), path);
    SqliteConnectionPool::connectionFor(QStringLiteral("b"), path);
    QCOMPARE(SqliteConnectionPool::openConnectionCount(), 2);

    SqliteConnectionPool::closeAllForCurrentThread();
    QCOMPARE(SqliteConnectionPool::openConnectionCount(), 0);

    QVERIFY(!QSqlDatabase::contains(
        SqliteConnectionPool::connectionName(QStringLiteral("a"))));
}

void TstConnectionPool::connectionNamesAreThreadSpecific()
{
    const QString mainName =
        SqliteConnectionPool::connectionName(QStringLiteral("x"));

    QString workerName;
    QThread worker;
    QObject ctx;
    ctx.moveToThread(&worker);
    QObject::connect(&worker, &QThread::started, &ctx, [&] {
        workerName = SqliteConnectionPool::connectionName(QStringLiteral("x"));
        worker.quit();
    });
    worker.start();
    QVERIFY(worker.wait(5000));

    QVERIFY(!workerName.isEmpty());
    QVERIFY2(workerName != mainName,
             "Cada hilo necesita su propia conexion: SQLite no permite "
             "compartirlas entre hilos");
}

void TstConnectionPool::transactionRollsBackOnScopeExit()
{
    const QString path = makeDb(QStringLiteral("tx1.db"));
    QSqlDatabase db = SqliteConnectionPool::connectionFor(
        QStringLiteral("tx1"), path, SqliteConnectionPool::Mode::ReadWrite);
    QVERIFY(db.isOpen());

    int before = 0;
    {
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM t")));
        QVERIFY(q.next());
        before = q.value(0).toInt();
    }

    {
        Transaction tx(db);
        QVERIFY(tx.isActive());
        QSqlQuery q(db);
        QVERIFY(q.exec(QStringLiteral("INSERT INTO t (v) VALUES ('sin commit')")));
        // Se sale del scope SIN llamar a commit: debe deshacerse.
    }

    QSqlQuery q(db);
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM t")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), before);
}

void TstConnectionPool::transactionCommits()
{
    const QString path = makeDb(QStringLiteral("tx2.db"));
    QSqlDatabase db = SqliteConnectionPool::connectionFor(
        QStringLiteral("tx2"), path, SqliteConnectionPool::Mode::ReadWrite);
    QVERIFY(db.isOpen());

    {
        Transaction tx(db);
        QVERIFY(tx.isActive());
        QSqlQuery q(db);
        for (int i = 0; i < 200; ++i) {
            q.prepare(QStringLiteral("INSERT INTO t (v) VALUES (:v)"));
            q.bindValue(QStringLiteral(":v"), QStringLiteral("v%1").arg(i));
            QVERIFY(q.exec());
        }
        QVERIFY(tx.commit());
    }

    QSqlQuery q(db);
    QVERIFY(q.exec(QStringLiteral("SELECT COUNT(*) FROM t")));
    QVERIFY(q.next());
    QCOMPARE(q.value(0).toInt(), 201);   // 1 inicial + 200
}

QTEST_MAIN(TstConnectionPool)
#include "tst_connectionpool.moc"
