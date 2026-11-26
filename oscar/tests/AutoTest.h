// From https://qtcreator.blogspot.com/2009/10/running-multiple-unit-tests.html
#ifndef AUTOTEST_H
#define AUTOTEST_H

#include <QTest>
#include <QList>
#include <QString>
#include <QSharedPointer>

namespace AutoTest
{
    typedef QList<QObject*> TestList;

    inline TestList& testList()
    {
        static TestList list;
        return list;
    }

    inline bool findObject(QObject* object)
    {
        TestList& list = testList();
        if (list.contains(object))
        {
            return true;
        }
        foreach (QObject* test, list)
        {
            if (test->objectName() == object->objectName())
            {
                return true;
            }
        }
        return false;
    }

    inline void addTest(QObject* object)
    {
        TestList& list = testList();
        if (!findObject(object))
        {
            list.append(object);
        }
    }

    /*
     * Runs every registered test class in turn, in the static-initialisation order of the
     * DECLARE_TEST objects. Never QSKIP in initTestCase(): QtTest keeps the skip state, and
     * every class that runs after it in this process silently runs nothing (#356). Skip in
     * init() or in each test function instead.
     *
     * Two optional environment variables change a run; with neither set, every class runs
     * with the command line as given:
     *  - OSCAR_TEST_SUITES=A,B runs only the classes with those names.
     *  - OSCAR_TEST_LOGDIR=<dir> writes each class's results to <dir>/<Class>.txt. Without
     *    it, QtTest's -o option keeps only the last class, because each class overwrites
     *    the file. The directory must exist, and other command-line options are ignored.
     */
    inline int run(int argc, char *argv[])
    {
        int ret = 0;

        const QStringList only = qEnvironmentVariable("OSCAR_TEST_SUITES").split(',', Qt::SkipEmptyParts);
        const QString logDir = qEnvironmentVariable("OSCAR_TEST_LOGDIR");
        foreach (QObject* test, testList())
        {
            if (!only.isEmpty() && !only.contains(test->objectName())) continue;
            if (logDir.isEmpty()) {
                ret += QTest::qExec(test, argc, argv);
            } else {
                QStringList args;
                args << QString::fromLocal8Bit(argv[0]) << "-o" << (logDir + "/" + test->objectName() + ".txt,txt");
                ret += QTest::qExec(test, args);
            }
        }

        return ret;
    }
}

template <class T>
class Test
{
public:
    QSharedPointer<T> child;

    Test(const QString& name) : child(new T)
    {
        child->setObjectName(name);
        AutoTest::addTest(child.data());
    }
};

#define DECLARE_TEST(className) static Test<className> t(#className);

#define TEST_MAIN \
    int main(int argc, char *argv[]) \
    { \
        return AutoTest::run(argc, argv); \
    }

#endif // AUTOTEST_H
