#ifndef BBMANAGERAPPLICATION_H
#define BBMANAGERAPPLICATION_H

#include <QApplication>

class BBManagerApplication : public QApplication
{
    Q_OBJECT
public:
    BBManagerApplication( int &argc, char **argv );
    ~BBManagerApplication();

private slots:
    void slotOpenDefaultProject();

protected:
    bool event(QEvent *ev);
private:
    bool m_projectBeingOpened;
};

#endif // BBMANAGERAPPLICATION_H
