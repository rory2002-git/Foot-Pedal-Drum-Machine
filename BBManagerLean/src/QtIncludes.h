#ifndef QTINCLUDES_H
#define QTINCLUDES_H

// This is a wrapper file for Qt includes to deal with Qt path issues on Ubuntu 18.04

#ifdef __unix__
    // Direct paths for Qt 5.11 on Linux - using correct capitalization
    #include "/opt/qt511/include/QtCore/QThread"
    #include "/opt/qt511/include/QtCore/QFile"
    #include "/opt/qt511/include/QtCore/QDebug"
    #include "/opt/qt511/include/QtCore/QIODevice"
    #include "/opt/qt511/include/QtCore/QTime"
    #include "/opt/qt511/include/QtCore/QString"
    #include "/opt/qt511/include/QtCore/QQueue"
    #include "/opt/qt511/include/QtCore/QReadWriteLock"
    #include "/opt/qt511/include/QtCore/qmath.h"
    #include "/opt/qt511/include/QtMultimedia/QAudioOutput"
    #include "/opt/qt511/include/QtMultimedia/QAudioDeviceInfo"
    #include "/opt/qt511/include/QtMultimedia/QAudioFormat"
#else
    // Standard includes for other platforms
    #include <QThread>
    #include <QFile>
    #include <QDebug>
    #include <QIODevice>
    #include <QTime>
    #include <QString>
    #include <QQueue>
    #include <QReadWriteLock>
    #include <QtCore/qmath.h>
    #include <QAudioOutput>
    #include <QAudioDeviceInfo>
    #include <QAudioFormat>
#endif

#endif // QTINCLUDES_H 