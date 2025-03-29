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
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QThread>
#include <cstring>
#include <stdexcept>
#include <signal.h>
#include "src/player/player.h"  // Include the Player class header

// Default paths for the demo content
const QString DEFAULT_DRUMSET_PATH = "/home/rory/Documents/BBWorkspace/user_lib/drum_sets/Indie Drumset v2.0.DRM"; // Using a smaller drumset as default
const QString DEFAULT_SONG_PATH = "/home/rory/Documents/BBWorkspace/user_lib/projects/BeatBuddy Default Content 2.0 - Project/SONGS/203A18C4/716D6763.BBS";

// List of available drum sets for testing (smaller to larger size)
const QStringList AVAILABLE_DRUMSETS = {
    "/home/rory/Documents/BBWorkspace/user_lib/drum_sets/Indie Drumset v2.0.DRM",      // Option 1 - Smallest
    "/home/rory/Documents/BBWorkspace/user_lib/drum_sets/Jazz Drumset v2.0.DRM",      // Option 2 - Small
    "/home/rory/Documents/BBWorkspace/user_lib/drum_sets/Dance Drumset v2.0.DRM",     // Option 3 - Medium
    "/home/rory/Documents/BBWorkspace/user_lib/drum_sets/Metal Drumset v2.0.DRM",     // Option 4 - Large
    "/home/rory/Documents/BBWorkspace/user_lib/drum_sets/Rock Drumset v2.0.DRM"      // Option 5 - Largest
};

// List of available songs for testing
const QStringList AVAILABLE_SONGS = {
    "/home/rory/Documents/BBWorkspace/user_lib/projects/BeatBuddy Default Content 2.0 - Project/SONGS/203A18C4/716D6763.BBS", // Option 6
    "/home/rory/Documents/BBWorkspace/user_lib/projects/BeatBuddy Default Content 2.0 - Project/SONGS/203A18C4/781A9FDF.BBS", // Option 7
    "/home/rory/Documents/BBWorkspace/user_lib/projects/BeatBuddy Default Content 2.0 - Project/SONGS/1F3C0EE8/6B87F13A.BBS", // Option 8
    "/home/rory/Documents/BBWorkspace/user_lib/projects/BeatBuddy Default Content 2.0 - Project/SONGS/165DA150/07E0BFAA.BBS", // Option 9
    "/home/rory/Documents/BBWorkspace/user_lib/projects/BeatBuddy Default Content 2.0 - Project/SONGS/165DA150/3BACBCA4.BBS"  // Option 0
};

Player* gPlayerPtr = nullptr;

// Signal handler for clean shutdown
void signalHandler(int signal) {
    if (gPlayerPtr) {
        qDebug() << "Received signal" << signal << ", stopping player";
        gPlayerPtr->stop();
    }
    QCoreApplication::quit();
}

// Helper function to handle keyboard input
void processKeyboardInput(Player* player) {
    QThread inputThread;
    QObject::connect(&inputThread, &QThread::started, [player]() {
        try {
            char input;
            while (std::cin.get(input)) {
                if (input == ' ') {
                    // Toggle playback on spacebar
                    if (player->started()) {
                        std::cout << "Stopping playback..." << std::endl;
                        player->stop();
                    } else {
                        std::cout << "Starting playback..." << std::endl;
                        player->play();
                    }
                } else if (input == 'q' || input == 'Q') {
                    // Quit on 'q'
                    std::cout << "Quitting..." << std::endl;
                    player->stop();
                    QCoreApplication::quit();
                    break;
                } else if (input >= '1' && input <= '5') {
                    // Select drum set
                    int index = input - '1';
                    if (index >= 0 && index < AVAILABLE_DRUMSETS.size()) {
                        std::cout << "Loading drum set " << (index + 1) << "..." << std::endl;
                        player->setDrumset(AVAILABLE_DRUMSETS[index]);
                    }
                } else if (input >= '6' && input <= '0' + AVAILABLE_SONGS.size() - 5) {
                    // Select song (6-0 map to indices 0-4)
                    int index = (input == '0') ? 4 : (input - '6');
                    if (index >= 0 && index < AVAILABLE_SONGS.size()) {
                        std::cout << "Loading song " << (index + 6) << "..." << std::endl;
                        player->setSong(AVAILABLE_SONGS[index]);
                    }
                }
            }
        } catch (const std::exception& e) {
            qCritical() << "Error in input thread:" << e.what();
        } catch (...) {
            qCritical() << "Unknown error in input thread";
        }
    });
    
    inputThread.start();
}

int main(int argc, char *argv[])
{
    try {
        QCoreApplication a(argc, argv);
        
        // Set signal handlers for graceful shutdown
        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);
        
        // Create the player instance
        Player player;
        gPlayerPtr = &player;
        
        // Set initial files
        player.setDrumset(DEFAULT_DRUMSET_PATH);  // Start with a smaller drumset by default
        player.setSong(DEFAULT_SONG_PATH);
        
        // Display instructions
        std::cout << "Press spacebar to control playback. Press 'q' to quit." << std::endl;
        std::cout << "When not playing:" << std::endl;
        std::cout << "  1-5: Select drum set" << std::endl;
        std::cout << "  6-0: Select song" << std::endl;
        
        // Start keyboard input processing in another thread
        processKeyboardInput(&player);
        
        // Start the application event loop
        return a.exec();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown fatal error" << std::endl;
        return 1;
    }
}
