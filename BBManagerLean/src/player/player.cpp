/*
  This software and the content provided for use with it is Copyright © 2014-2020 Singular Sound
  BeatBuddy Manager is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2 as published by
    the Free Software Foundation.
   
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
   
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
// Use our wrapper for Qt includes
#include "../QtIncludes.h"

#include <stdint.h>
#include <stdio.h>
#include <QDateTime>
#include <stdexcept>

#ifdef Q_OS_LINUX
#include <sys/sysinfo.h>
#include <malloc.h>
#endif

#include "player.h"
#include "mixer.h"
#include "songPlayer.h"
#include "../model/filegraph/song.h"
#include "soundManager.h"
#include "../../src/workspace/settings.h"

#define TICK_TO_TIME_RATIO(bpm)     ((60.0f / (double)bpm) / 480.0f)
#define PREPARE_STOP_THREASHOLD     (5)
#define MIXER_DEFAULT_LEVEL         (1.0)


// NOTE: these defines can be used due to hardcoded initialization of m_format
// Units: sample/s
#define SAMPLE_PER_SECOND           44100.0f
// Units: ticks/refresh
#define TICKS_PER_REFRESH           5
// Units: byte/sample
#define SAMPLES_TO_BYTES_RATIO_I      (4)
// Units: sample/refresh = ticks/refresh * s/tick * sample/s
#define SAMPLES_PER_REFRESH(bpm)  (TICKS_PER_REFRESH * TICK_TO_TIME_RATIO(bpm) * SAMPLE_PER_SECOND)

// The idea is to process a fixed amount of ticks per update
// In order for player to behave properly (and transition to fit at proper time), the number of ticks needs to be fixed to a value that fits with bar length, etc..
// It was originnaly set to TICKS_PER_EVENT = 20
//    - at 300 bpm, this corresponds to  8.33  ms = (20 ticks/event / 480 ticks/beat / 300 beats/min * 60 sec/min )
//    - at 120 bpm, this corresponds to 20.833 ms = (20 ticks/event / 480 ticks/beat / 120 beats/min * 60 sec/min )
//    - at 100 bpm, this corresponds to 25     ms = (20 ticks/event / 480 ticks/beat / 100 beats/min * 60 sec/min )
//    - at  40 bpm, this corresponds to 62.5   ms = (20 ticks/event / 480 ticks/beat /  40 beats/min * 60 sec/min )
//
// Audio buffer size is configurred to about 50 ms (May change by tuning).
// Audio buffer is actually freed by chunks of 2048 bytes = 11,6 ms
// In order to synchronize with audio card, it seems better to process by chunks of 5

// It is actually set to TICKS_PER_REFRESH = 5
//    - at 300 bpm, this corresponds to  2.0833 ms = (5 ticks/refresh / 480 ticks/beat / 300 beats/min * 60 sec/min )
//    - at 120 bpm, this corresponds to  5.21   ms = (5 ticks/refresh / 480 ticks/beat / 120 beats/min * 60 sec/min )
//    - at 100 bpm, this corresponds to  6.25   ms = (5 ticks/refresh / 480 ticks/beat / 100 beats/min * 60 sec/min )
//    - at  40 bpm, this corresponds to 15.625  ms = (5 ticks/refresh / 480 ticks/beat /  40 beats/min * 60 sec/min )

unsigned int lastPartIndex = -1;

Player::Player(QObject *parent)
    : QThread(parent)
    , m_device(QAudioDeviceInfo::defaultOutputDevice())
    , m_audioOutput(nullptr)
    , m_ioDevice(nullptr)
{
    qDebug() << "Creating Player object";
    m_singleTrack = false;
    m_singleTrackOffset = 0;

    m_bufferTime_ms = Settings::getBufferingTime_ms();
    m_bufferSize_bytes = MIXER_BUFFERRING_TIME_MS_TO_BYTES_STEREO(m_bufferTime_ms);


    m_prevStarted = false;
    m_prevSigNum = 4;
    m_prevBeatInBar = 0;
    m_prevTick = -1;
    m_prevPart = stopped;
}

Player::~Player()
{
    if (isRunning()) {
        stop();
        wait();
    }
    qDebug() << "Deleting Player object";
}

void Player::initAudio(void)
{

    // NOTE: There are defines related to setChannelCount(2) and setSampleSize(16).
    //       Changing these hardcoded values would break these defines and all defines that use them:
    //       - SAMPLE_PER_SECOND
    //       - SAMPLES_TO_BYTES_RATIO_I
    //       Other Mixer defines depend on this
    m_format.setSampleRate(44100);
    m_format.setChannelCount(2);
    m_format.setSampleSize(16);
    m_format.setCodec("audio/pcm");
    m_format.setByteOrder(QAudioFormat::LittleEndian);
    m_format.setSampleType(QAudioFormat::SignedInt);

    m_audioOutput = new QAudioOutput(m_device, m_format, nullptr);

    // NOTE: m_bufferSize_bytes is only computed at start of thread.
    m_bufferSize_bytes = MIXER_BUFFERRING_TIME_MS_TO_BYTES_STEREO(m_bufferTime_ms);
    m_audioOutput->setBufferSize(m_bufferSize_bytes);
    m_ioDevice = m_audioOutput->start();

    // Ajust the actual buffer size with real buffer size
    // m_soundCardLimit is used to prevent buffering too much in case sound card
    // creates a bigger buffer than m_bufferSize_bytes;
    if(m_audioOutput->bufferSize() > m_bufferSize_bytes){
        m_soundCardLimit = m_audioOutput->bufferSize() - m_bufferSize_bytes;
    } else {
        m_soundCardLimit = 0;
    }
}

void Player::initMixer(void)
{
    mixer_init();
    mixer_setOutputLevel(MIXER_DEFAULT_LEVEL);
}

bool Player::checkMemoryAvailability(qint64 requiredBytes)
{
#ifdef Q_OS_LINUX
    // On Linux, check available memory
    struct sysinfo memInfo;
    if (sysinfo(&memInfo) == 0) {
        qint64 availableMemory = memInfo.freeram * memInfo.mem_unit;
        qDebug() << "Available memory: " << availableMemory / (1024*1024) << "MB";
        
        // Require only 2x the file size for processing overhead
        // For small files, require at least 20MB minimum
        qint64 requiredMemory = qMax(requiredBytes * 2, qint64(20 * 1024 * 1024));
        
        if (availableMemory < requiredMemory) {
            qWarning() << "Insufficient memory: Need" << requiredMemory / (1024*1024) 
                       << "MB but only" << availableMemory / (1024*1024) << "MB available";
            return false;
        }
    }
#endif
    return true;
}

void Player::loadDrumset(const QString &filepath)
{
    qDebug() << "Loading drumset " << filepath;

    // Free any potential memory from previous operations
    for (int i = 0; i < MAX_SONG_PARTS; i++) {
        m_effects[i].clear();
        m_effects[i].squeeze();
    }
    m_song.clear();
    m_song.squeeze();
    m_drumset.clear();
    m_drumset.squeeze();

    QFile file(filepath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open drumset file at: " << filepath;
        return;
    }

    try {
        // Check file size before loading
        qint64 fileSize = file.size();
        qDebug() << "Drumset file size: " << fileSize << " bytes";
        
        // Impose a hard limit on file size (100MB)
        const qint64 maxFileSize = 100 * 1024 * 1024; // 100MB
        if (fileSize > maxFileSize) {
            file.close();
            qWarning() << "Drumset file too large: " << fileSize / (1024*1024) << "MB (max " << maxFileSize / (1024*1024) << "MB)";
            throw std::runtime_error("Drumset file too large - maximum supported size is 100MB");
        }
        
        // For large drumsets, reduce memory requirements to 1.1x file size
        const qint64 largeFileCutoff = 50 * 1024 * 1024; // 50MB
        
        if (fileSize > largeFileCutoff) {
            qDebug() << "Large drumset detected - adjusting memory requirements";
            
            // Check with reduced memory requirements (just 10% overhead)
            struct sysinfo memInfo;
            if (sysinfo(&memInfo) == 0) {
                qint64 availableMemory = memInfo.freeram * memInfo.mem_unit;
                qint64 requiredMemory = fileSize * 1.1; // Only 10% overhead for large files
                
                qDebug() << "Available memory: " << availableMemory / (1024*1024) << "MB";
                qDebug() << "Required memory: " << requiredMemory / (1024*1024) << "MB";
                
                if (availableMemory < requiredMemory) {
                    file.close();
                    qWarning() << "Not enough memory for large drumset";
                    throw std::runtime_error("Not enough memory to load large drumset - please free up system memory or use a smaller drumset");
                }
            }
        } else {
            // Standard approach for smaller files
            if (!checkMemoryAvailability(fileSize)) {
                file.close();
                throw std::runtime_error("Not enough available memory to load drumset - try using a smaller drumset file");
            }
        }
        
        // Simply read the entire file in one go
        qDebug() << "Reading drumset file...";
        m_drumset = file.readAll();
        file.close();
        
        qDebug() << "Drumset loaded into memory, initializing sound manager...";
        try {
            SoundManager_init();
            qDebug() << "Sound manager initialized successfully";
        } catch (...) {
            qWarning() << "Failed to initialize sound manager";
            throw std::runtime_error("Failed to initialize sound manager");
        }
        
        qDebug() << "Loading drumset data into sound manager...";
        try {
            // Basic validation - check that we have a non-empty buffer
            if (m_drumset.isEmpty() || m_drumset.size() < 100) {
                qWarning() << "Invalid drumset data - file may be corrupted";
                throw std::runtime_error("Invalid drumset data - file may be corrupted");
            }
            
            // Ensure the pointer is valid
            if (m_drumset.data() == nullptr) {
                qWarning() << "Invalid drumset data pointer";
                throw std::runtime_error("Invalid drumset data pointer");
            }
            
            SoundManager_LoadDrumset(m_drumset.data(), m_drumset.size());
            qDebug() << "Drumset successfully loaded";
        } catch (const std::exception& e) {
            qWarning() << "Failed to load drumset into sound manager:" << e.what();
            throw std::runtime_error(std::string("Failed to load drumset: ") + e.what());
        } catch (...) {
            qWarning() << "Unknown error while loading drumset into sound manager";
            throw std::runtime_error("Failed to load drumset due to an unknown error");
        }
    }
    catch (const std::bad_alloc&) {
        file.close();
        qWarning() << "Memory allocation failed while loading drumset";
        throw std::runtime_error("Not enough memory to load drumset - try using a smaller drumset file");
    }
    catch (const std::exception& e) {
        file.close();
        qWarning() << "Exception while loading drumset: " << e.what();
        throw;
    }
}

void Player::logMemoryUsage() const
{
    qint64 totalMemoryUsed = 0;
    
    // Calculate memory usage from our main buffers
    totalMemoryUsed += m_song.capacity();
    totalMemoryUsed += m_drumset.capacity();
    
    for (int i = 0; i < MAX_SONG_PARTS; i++) {
        totalMemoryUsed += m_effects[i].capacity();
    }
    
    qDebug() << "Memory usage:";
    qDebug() << "  Song buffer: " << m_song.capacity() / (1024.0) << "KB";
    qDebug() << "  Drumset buffer: " << m_drumset.capacity() / (1024.0) << "KB";
    qDebug() << "  Effect buffers: " << (totalMemoryUsed - m_song.capacity() - m_drumset.capacity()) / (1024.0) << "KB";
    qDebug() << "  TOTAL: " << totalMemoryUsed / (1024.0 * 1024.0) << "MB";
    
#ifdef Q_OS_LINUX
    // Get system memory info
    struct sysinfo memInfo;
    if (sysinfo(&memInfo) == 0) {
        qint64 totalMemory = memInfo.totalram * memInfo.mem_unit;
        qint64 availableMemory = memInfo.freeram * memInfo.mem_unit;
        qint64 usedMemory = totalMemory - availableMemory;
        
        qDebug() << "System memory:";
        qDebug() << "  Total: " << totalMemory / (1024.0 * 1024.0) << "MB";
        qDebug() << "  Used: " << usedMemory / (1024.0 * 1024.0) << "MB";
        qDebug() << "  Available: " << availableMemory / (1024.0 * 1024.0) << "MB";
    }
#endif
}

bool Player::loadSong(const QString &filepath)
{
    qDebug() << "Loading song " << filepath;
    
    // Store the filepath for later use
    m_songPath = filepath;
    
    // Free any potential memory from previous operations
    for (int i = 0; i < MAX_SONG_PARTS; i++) {
        m_effects[i].clear();
        m_effects[i].squeeze();
    }
    m_song.clear();
    m_song.squeeze();
    
    // Try to reclaim memory before loading new song
    freeMemoryIfNeeded();
    
    logMemoryUsage();
    
    // First, attempt to load the song without initializing the player
    QFile file(filepath);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open song file at: " << filepath;
        return false;
    }

    try {
        // Check file size before loading
        qint64 fileSize = file.size();
        qDebug() << "Song file size: " << fileSize << " bytes";
        
        // Check if we have enough memory
        if (!checkMemoryAvailability(fileSize)) {
            file.close();
            throw std::runtime_error("Not enough available memory to load song - try using a smaller song file");
        }
        
        // Simply read the entire file
        m_song = file.readAll();
        file.close();
        
        qDebug() << "File read complete. Total song size: " << m_song.size() << " bytes";
        logMemoryUsage();

        // Initialize the Song Player before loading the song
        try {
            qDebug() << "Initializing song player";
            SongPlayer_init();
            qDebug() << "Song player initialized successfully";
        } catch (...) {
            qWarning() << "Error initializing song player";
            throw std::runtime_error("Error initializing song player");
        }

        // Load the song into the player
        try {
            qDebug() << "Loading song data into player";
            if (SongPlayer_loadSong(m_song.data(), m_song.size()) <= 0) {
                qWarning() << "Error loading song data into player";
                throw std::runtime_error("Error loading song data - file may be corrupt");
            }
            qDebug() << "Song loaded into player successfully";
        } catch (...) {
            qWarning() << "Error loading song";
            throw std::runtime_error("Error loading song data into player");
        }

        QString m_effectsPath = "/home/rory/Documents/BBWorkspace/user_lib/projects/BeatBuddy Default Content 2.0 - Project/EFFECTS";

        qDebug() << "Loading effects from " << m_effectsPath;
        for (uint i = 0; i < MAX_SONG_PARTS; i++) {
            char *name = SongPlayer_getSoundEffectName(i);
            if (name) {
                if (*name != '\0') {
                    if (!loadEffect(i, m_effectsPath + "/" + SongPlayer_getSoundEffectName(i))) {
                        return false;
                    }
                } else {
                    clearEffect(i);
                }
            } else {
                clearEffect(i);
            }
        }
        
        logMemoryUsage();
        return true;
    }
    catch (const std::bad_alloc&) {
        file.close();
        qWarning() << "Memory allocation failed while loading song";
        throw std::runtime_error("Not enough memory to load song - try using a smaller song file");
    }
    catch (const std::exception& e) {
        file.close();
        qWarning() << "Exception while loading song: " << e.what();
        throw;
    }
}

bool Player::loadEffect(int part, const QString &filepath)
{
    QFile file(filepath);
    if (!file.open(QIODevice::ReadOnly)){
        qWarning() << "Failed to open effect file at: " << filepath;
        return false;
    }

    try {
        // Check file size before loading
        qint64 fileSize = file.size();
        qDebug() << "Effect file size: " << fileSize << " bytes";
        
        // Check if we have enough memory
        if (!checkMemoryAvailability(fileSize)) {
            file.close();
            throw std::runtime_error("Not enough available memory to load effect - try using a smaller effect file");
        }
        
        // Clear any previously loaded effect
        m_effects[part].clear();
        
        // Simply read the entire file
        m_effects[part] = file.readAll();
        file.close();

        SoundManager_LoadEffect(m_effects[part].data(), part);
        return true;
    }
    catch (const std::bad_alloc&) {
        file.close();
        qWarning() << "Memory allocation failed while loading effect";
        throw std::runtime_error("Not enough memory to load effect - try using a smaller effect file");
    }
    catch (const std::exception& e) {
        file.close();
        qWarning() << "Exception while loading effect: " << e.what();
        throw;
    }
}

void Player::clearEffect(int part)
{
    m_effects[part].clear();
    SoundManager_LoadEffect(nullptr, part);
}

int Player::processTime(int samplesToProcess)
{
    SongPlayer_PlayerStatus currentPlayerStatus;
    unsigned int PartIndex;
    unsigned int DrumfillIndex;

    // Process as many samples as possible, but always by multiples of TICKS_PER_REFRESH
    int updateCount = (int)(samplesToProcess / SAMPLES_PER_REFRESH(m_tempo));

    // Keep track of the actual amount of samples being processed (with fraction being summed until entire value reached)
    m_processedSamples_real += SAMPLES_PER_REFRESH(m_tempo) * updateCount;

    int processedSamples = qFloor(m_processedSamples_real);
    m_processedSamples_real -= (double)processedSamples;

    if(processedSamples > 0){

        if (m_singleTrack){
            SongPlayer_ProcessSingleTrack(TICK_TO_TIME_RATIO(m_tempo), updateCount * TICKS_PER_REFRESH, m_singleTrackOffset);
            SongPlayer_getPlayerStatus( &currentPlayerStatus,
                                        &PartIndex,
                                        &DrumfillIndex);
            if (currentPlayerStatus != m_lastPlayerStatus) {
                switch(currentPlayerStatus){
                case SINGLE_TRACK_PLAYER:   m_prepareStop = 0;  break;
                case STOPPED:               m_prepareStop = 1;  break;
                default:
                    qWarning() << "Player::processTime unhandled status " << currentPlayerStatus;
                    break;
                }
                m_lastPlayerStatus = currentPlayerStatus;
            }

        } else {
            SongPlayer_processSong(TICK_TO_TIME_RATIO(m_tempo), updateCount * TICKS_PER_REFRESH);

            SongPlayer_getPlayerStatus( &currentPlayerStatus,
                                        &PartIndex,
                                        &DrumfillIndex
                                        );
            if (currentPlayerStatus != m_lastPlayerStatus) {
                switch (currentPlayerStatus) {
                case NO_SONG_LOADED:            m_stop = 1;                                     break;
                case STOPPED:                   m_prepareStop = 1;                              break;
                case INTRO:                     emit sigPlayingIntro();                              break;
                case PLAYING_MAIN_TRACK:
                    qDebug() << "whats going on here1" << currentPlayerStatus << m_lastPlayerStatus
                             << PartIndex << lastPartIndex;
                    updateTempo();
                    emit sigPlayingMainTrack(PartIndex);
                    break;
                case OUTRO:                     emit sigPlayingOutro();                              break;
                case TRANFILL_ACTIVE:           emit sigPlayingTranfill(PartIndex);                  break;
                case DRUMFILL_ACTIVE:           emit sigPlayingDrumfill(PartIndex, DrumfillIndex);   break;
                default:
                    qWarning() << "Player::processTime unhandled status " << currentPlayerStatus;
                    break;
                }
                if (currentPlayerStatus != STOPPED) {
                    m_prepareStop = 0;
                    // Make sure song is played at full strength if restarted during fadeout
                    mixer_setOutputLevel(MIXER_DEFAULT_LEVEL);
                }
                m_lastPlayerStatus = currentPlayerStatus;
            }
        }
    }

    return processedSamples;

}

/**
 * NOTE: should only be called if samplesToProcess > 0
 * @brief Player::processAudio
 * @param samplesToProcess
 */
void Player::processAudio(int samplesToProcess)
{


    mixer_ReadOutputStream((short int*)m_buffer,
                           samplesToProcess * m_format.channelCount()); // length is in absolute sample count (regardless of stereo/mono)


    m_ioDevice->write(m_buffer, samplesToProcess * SAMPLES_TO_BYTES_RATIO_I);  // Number of bytes

    /* Stop audio thread if nothing is output after double tap */
    if (m_prepareStop) {
        int i;

        int byteCount = samplesToProcess * SAMPLES_TO_BYTES_RATIO_I;

        // Scan the buffer untile one of the sample (16 bits) is bigger than the threashold,
        for (i=0; i<byteCount; i+=2 ) {
            if (abs(*((short int*)(m_buffer + i))) > PREPARE_STOP_THREASHOLD) break;
        }
        // if none found, stop
        if (i == byteCount) {
            m_stop = 1;
        }

        // Reduce the output level to fade out
        float level = mixer_getOutputLevel();
        mixer_setOutputLevel(0.982 * level);
    }

}

void Player::processEvent(void)
{
    if (m_lock.tryLockForRead()) {
        if (!m_queue.isEmpty()) {
            BUTTON_EVENT event = m_queue.dequeue();

            SongPlayer_ButtonCallback(event, 0);
        }
        m_lock.unlock();
    }
}

void Player::updateTempo()
{
    int bpm = SongPlayer_getTempo();
    qDebug() << "new tempo" << bpm << getTempo();
    if (bpm>0) {
      setTempo(bpm);
      qDebug() << "emiting tempo change signal";
      emit sigTempoChangedBySong(bpm);
    }
}

void Player::updateStatus(bool forceEmit)
{
   //update started
   if( forceEmit || (!m_stop && !m_prevStarted) || (m_stop && m_prevStarted) ){
      m_prevStarted = !m_stop;
      emit sigStartedChanged(m_prevStarted);
   }

   // update currentPart
   partEnum currentPart = m_prevPart;
   unsigned int partIndex;

   if(m_stop){
      currentPart = stopped;
   } else {

      SongPlayer_PlayerStatus status;
      unsigned int drumfillIndex;
      SongPlayer_getPlayerStatus(&status, &partIndex, &drumfillIndex);
      if(m_singleTrack){

         if(status == NO_SONG_LOADED || status == STOPPED){
            // verify status for stopped in order to switch screen color to blue as soon as player stopped
            currentPart = stopped;
         } else {
            switch(m_singleTrackTypeId){
               case MAIN_PART_ID:
                  currentPart = mainLoop;
                  break;
               case DRUM_FILL_ID:
                  currentPart = drumFill;
                  break;
               case TRAN_FILL_ID:
                  currentPart = transFill;
                  break;
               case INTR_FILL_ID:
                  currentPart = intro;
                  break;
               case OUTR_FILL_ID:
                  currentPart = outro;
                  break;
               default:
                  // Do nothing
                  break;
            }
         }

      } else {
         switch(status){
            case NO_SONG_LOADED:
            case STOPPED:
               currentPart = stopped;
               break;

            case PAUSED:
               currentPart = pause;
               break;

            case INTRO :
               currentPart = intro;
               break;
            case PLAYING_MAIN_TRACK:
            case PLAYING_MAIN_TRACK_TO_END:
            case SINGLE_TRACK_PLAYER:
               currentPart = mainLoop;
               break;

            case OUTRO:
               currentPart = outro;
               break;
            case TRANFILL_ACTIVE:
            case TRANFILL_QUITING:
               currentPart = transFill;
               break;
            case DRUMFILL_ACTIVE:
               currentPart = drumFill;
               break;

            default:
               // Do nothing
               break;
         }
      }
   }

   if(forceEmit || (currentPart != m_prevPart)){
      qDebug() << "Part changed!!" << currentPart << m_prevPart << forceEmit;
      m_prevPart = currentPart;
      emit sigPartChanged(m_prevPart);
      updateTempo();
   } else {
       if (lastPartIndex != partIndex && partIndex > 0) {
           qDebug() << "whats going on here2" << lastPartIndex << partIndex << currentPart << m_prevPart;
           lastPartIndex = partIndex;
           updateTempo();
           emit sigPlayingMainTrack(partIndex);
       }
   }

   // update currentSigNum
   int currentSigNum = m_prevSigNum;
   TimeSignature timeSignature;
   if(SongPlayer_getTimeSignature(&timeSignature)){
      currentSigNum = (int) timeSignature.num;
   }

   if(forceEmit || (currentSigNum != m_prevSigNum)){

      m_prevSigNum = currentSigNum;
      emit sigSigNumChanged(m_prevSigNum);
   }

   // update beatInBar
   int unusedStartBeat;
   int currentBeatInBar = SongPlayer_getBeatInbar(&unusedStartBeat);
   if(forceEmit || (currentBeatInBar != m_prevBeatInBar)){
      m_prevBeatInBar = currentBeatInBar;
      emit sigBeatInBarChanged(m_prevBeatInBar);
   }

   // emit MasterTick
   auto currentTick = SongPlayer_getMasterTick();
   if (forceEmit || (currentTick != m_prevTick))
       emit sigPlayerPosition(m_prevTick = currentTick);
}

// Add this method to runtime memory management
void Player::freeMemoryIfNeeded()
{
    // Clear any cached data that's no longer needed
    // This can be called during runtime to free up memory
    
    static QDateTime lastGCTime = QDateTime::currentDateTime();
    QDateTime currentTime = QDateTime::currentDateTime();
    
    // Only run GC once per minute at most
    if (lastGCTime.secsTo(currentTime) > 60) {
        qDebug() << "Running garbage collection to free memory";
        lastGCTime = currentTime;
        
#ifdef Q_OS_LINUX
        // On Linux, we can suggest the OS reclaim memory
        malloc_trim(0);
#endif
    }
}

void Player::run(void)
{
    try {
        initAudio();
        initMixer();
        emit sigPlayerStarted();

        // Load drumset first
        try {
            loadDrumset(m_drumsetPath);
            // After loading a large resource, run GC
            freeMemoryIfNeeded();
        } catch (const std::exception& e) {
            qWarning() << "Failed to load drumset: " << e.what();
            emit sigPlayerError(tr("Failed to load drumset: %1").arg(e.what()));
            emit sigPlayerStopped();
            return;
        }

        if (m_singleTrack) {
            try {
                SongPlayer_init();
                SongPlayer_SetSingleTrack(&mp_singleTrack);
                // calculate Master tick offset
                m_singleTrackOffset = SongPlayer_calculateSingleTrackOffset(mp_singleTrack.nTick, mp_singleTrack.barLength);

                mixer_setOutputLevel(MIXER_DEFAULT_LEVEL);
                m_prepareStop = 0;
                if (m_singleTrackOffset < 0) {
                    qWarning() << "bad midi... stopping player!";
                    emit sigPlayerStopped();
                    return;
                }
            } catch (const std::exception& e) {
                qWarning() << "Failed to set single track: " << e.what();
                emit sigPlayerError(tr("Failed to set single track: %1").arg(e.what()));
                emit sigPlayerStopped();
                return;
            }
        } else {
            try {
                if (!loadSong(m_songPath)) {
                    qWarning() << "Player::run - ERROR - Unable to load song " << m_songPath << " or its Accent Hit";
                    emit sigPlayerStopped();
                    emit sigPlayerError(tr("Unable to load song %1 or its Accent Hit").arg(m_songPath));
                    return;
                }
                SongPlayer_externalStart();
            } catch (const std::exception& e) {
                qWarning() << "Failed to load song: " << e.what();
                emit sigPlayerError(tr("Failed to load song: %1").arg(e.what()));
                emit sigPlayerStopped();
                return;
            }
        }

        updateStatus(true);

        // Set memory usage monitoring variables
        qint64 lastMemoryCheck = 0;
        const qint64 memoryCheckInterval = 5000; // Check memory every 5 seconds
        const qint64 maxMemoryUsage = 500 * 1024 * 1024; // 500MB limit

        while (!m_stop) {
            // Periodically check memory usage
            qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
            if (currentTime - lastMemoryCheck > memoryCheckInterval) {
                lastMemoryCheck = currentTime;
                
                // Get approximate memory usage of our key buffers
                qint64 estimatedMemoryUsage = m_drumset.size() + m_song.size();
                for (const auto& effect : m_effects) {
                    estimatedMemoryUsage += effect.size();
                }
                
                if (estimatedMemoryUsage > maxMemoryUsage) {
                    qWarning() << "Memory usage is too high: " << estimatedMemoryUsage << " bytes";
                    throw std::runtime_error("Memory usage exceeded safe limits");
                }
            }

            // The time is regulated by the amount of free bytes in the buffer
            // The unbreakable unit is the sample = 4 bytes
            int sampleToProcess = 0;
            if (m_audioOutput && m_audioOutput->state() != QAudio::StoppedState) {
                // Sound card limit is used to limit buffering time to smaller values than sound card buffer size.
                int bytesToProcess = m_audioOutput->bytesFree() - m_soundCardLimit; // length in bytes;
                // limit to the buffer size (due to bug on MAC)
                if (bytesToProcess > m_bufferSize_bytes) {
                    m_soundCardLimit += (bytesToProcess - m_bufferSize_bytes);
                    bytesToProcess = m_bufferSize_bytes;
                }

                sampleToProcess = bytesToProcess / SAMPLES_TO_BYTES_RATIO_I;
            }
            // At this point, the sampleToProcess corresponds to the ammount of free space in audio buffer
            if (sampleToProcess > 0) {
                // Require player to create data in mixer
                sampleToProcess = processTime(sampleToProcess);
            }

            // At this point, the sampleToProcess corresponds to the actual ammount of samples ready
            if (sampleToProcess > 0) {
                // Process data from mixer and send to audio card
                processAudio(sampleToProcess);
            }

            // Verify if any pedal were pressed
            processEvent();

            updateStatus(false);

            // NOTE: at 300 BPM, sound processing should be called every 2,083 msec.
            //       On mac mini, buffer was seen to display new free space when there is at least 2048
            //       free bytes. This corresponds to 11.2 ms
            //
            //       On a Windows 7 laptop, buffer was seen to display new free space when there is at
            //       least 1/5 of the buffer free. This is 3528 bytes on a 100 ms buffer which corresponds exactly to 20 ms
            //
            //       5 ms was chosen arbitrarily to sleep long enough but not to exceed threshold too much
            //
            // Only sleep when there is nothing to do
            if (sampleToProcess <= 0) {
                msleep(5);
            }
        }

        // process status one last time to make sure VM is stopped by
        // playback panel stop button press
        updateStatus(false);

        if (!m_singleTrack) {
            SongPlayer_externalStop();
        } else {
            m_singleTrack = false;
        }

        // Free all memory before exiting thread
        m_song.clear();
        m_song.squeeze();
        m_drumset.clear();
        m_drumset.squeeze();
        for (int i = 0; i < MAX_SONG_PARTS; i++) {
            m_effects[i].clear();
            m_effects[i].squeeze();
        }

        emit sigPlayerStopped();
    }
    catch (const std::bad_alloc& e) {
        qWarning() << "Memory allocation failed in player thread: " << e.what();
        emit sigPlayerError(tr("Memory allocation failed: %1").arg(e.what()));
        
        // Clean up resources properly
        if (m_audioOutput) {
            m_audioOutput->stop();
            delete m_audioOutput;
            m_audioOutput = nullptr;
        }
        
        // Free all memory before exiting thread
        m_song.clear();
        m_song.squeeze();
        m_drumset.clear();
        m_drumset.squeeze();
        for (int i = 0; i < MAX_SONG_PARTS; i++) {
            m_effects[i].clear();
            m_effects[i].squeeze();
        }
        
        emit sigPlayerStopped();
    }
    catch (const std::exception& e) {
        qWarning() << "Exception in player thread: " << e.what();
        emit sigPlayerError(tr("Error during playback: %1").arg(e.what()));
        
        // Clean up resources properly
        if (m_audioOutput) {
            m_audioOutput->stop();
            delete m_audioOutput;
            m_audioOutput = nullptr;
        }
        
        // Free all memory before exiting thread
        m_song.clear();
        m_song.squeeze();
        m_drumset.clear();
        m_drumset.squeeze();
        for (int i = 0; i < MAX_SONG_PARTS; i++) {
            m_effects[i].clear();
            m_effects[i].squeeze();
        }
        
        emit sigPlayerStopped();
    }
    catch (...) {
        qWarning() << "Unknown exception in player thread";
        emit sigPlayerError(tr("Unknown error during playback"));
        
        // Clean up resources properly
        if (m_audioOutput) {
            m_audioOutput->stop();
            delete m_audioOutput;
            m_audioOutput = nullptr;
        }
        
        // Free all memory before exiting thread
        m_song.clear();
        m_song.squeeze();
        m_drumset.clear();
        m_drumset.squeeze();
        for (int i = 0; i < MAX_SONG_PARTS; i++) {
            m_effects[i].clear();
            m_effects[i].squeeze();
        }
        
        emit sigPlayerStopped();
    }
}

void Player::play(void)
{
    if (!isRunning()) {
        qDebug() << "Player::play - Starting audio thread";

        m_stop = 0;
        m_prepareStop = 0;

        // Clear any existing audio output in the correct thread
        if (m_audioOutput) {
            QMetaObject::invokeMethod(this, "cleanupAudioOutput", Qt::BlockingQueuedConnection);
        }

        m_ioDevice = nullptr;
        m_drumset.clear();
        m_song.clear();
        m_processedSamples_real = 0;
        m_queue.clear();
        m_lastPlayerStatus = STOPPED;

        start(QThread::TimeCriticalPriority);
    } else {
        // Use safe stopping mechanism
        stop();
        wait();
        play();
    }
}

void Player::stop(void)
{
    if (isRunning()) {
        qDebug() << "Stopping audio thread";
        m_stop = 1;
        
        // Use a timeout when waiting for thread to finish
        constexpr int timeoutMs = 3000; // 3 seconds
        if (!wait(timeoutMs)) {
            qWarning() << "Player thread did not stop within timeout, forcing termination";
            terminate(); // Only as a last resort
            wait();
        }
    }
}

// Add this new method to clean up audio resources in the correct thread
void Player::cleanupAudioOutput()
{
    // This method will be called via QMetaObject::invokeMethod to ensure it runs in the correct thread
    if (m_audioOutput) {
        m_audioOutput->stop();
        delete m_audioOutput;
        m_audioOutput = nullptr;
    }
}

void Player::setDrumset(const QString &path)
{
    qDebug() << "Player: drumset set to " << path;
    m_drumsetPath = path;
}

QString Player::song(void)
{
    return m_songPath;
}

void Player::setSong(const QString &path)
{
    qDebug() << "Player: song set to " << path;
    m_singleTrack = false;
    m_songPath = path;
}

void Player::setSingleTrack(const QByteArray &trackData, int trackIndex, int typeId, int partIndex)
{
    if (trackData.size()) mp_singleTrack = trackData;
    if (trackIndex >= 0) m_singleTrackTrackIndex = trackIndex;
    if (typeId >= 0) m_singleTrackTypeId = typeId;
    if (partIndex >= 0) m_singleTrackPartIndex = partIndex;
    m_singleTrack = true;
}

void Player::setEffectsPath(const QString &path)
{
    qDebug() << "Player: effect path set to " << path;
    m_effectsPath = path;
}

void Player::setAutoPilot(bool autoPilot)
{
    m_AutoPilot = autoPilot;
}

bool Player::getAutoPilot(void){
    return m_AutoPilot;
}

void Player::setTempo(int tempo)
{
    m_tempo = tempo;
}

int Player::getTempo(void){
    return m_tempo;
}

void Player::pedalPress(void)
{
    if (!m_singleTrack){
        m_lock.lockForWrite();
        m_queue.enqueue(BUTTON_EVENT_PEDAL_PRESS);
        m_lock.unlock();
    }
}

void Player::pedalRelease(void)
{
   if (!m_singleTrack){
        m_lock.lockForWrite();
        m_queue.enqueue(BUTTON_EVENT_PEDAL_RELEASE);
        m_lock.unlock();
   }
}


void Player::pedalLongPress(void)
{
    if (!m_singleTrack){
        m_lock.lockForWrite();
        m_queue.enqueue(BUTTON_EVENT_PEDAL_LONG_PRESS);
        m_lock.unlock();
    }
}

void Player::pedalDoubleTap(void)
{
    if (!m_singleTrack){
        m_lock.lockForWrite();
        m_queue.enqueue(BUTTON_EVENT_PEDAL_MULTI_TAP);
        m_lock.unlock();
    }
}

void Player::effect(void)
{
    if (!m_singleTrack){
        m_lock.lockForWrite();
        m_queue.enqueue(BUTTON_EVENT_FOOT_SECONDARY_PRESS);
        m_lock.unlock();
    }
}


void Player::slotSetBufferTime_ms(int time_ms){
   if(time_ms > MIXER_MAX_BUFFERRING_TIME_MS){
      m_bufferTime_ms = MIXER_MAX_BUFFERRING_TIME_MS;
   } else if (time_ms < MIXER_MIN_BUFFERRING_TIME_MS){
      m_bufferTime_ms = MIXER_MIN_BUFFERRING_TIME_MS;
   } else {
      m_bufferTime_ms = time_ms;
   }
}
