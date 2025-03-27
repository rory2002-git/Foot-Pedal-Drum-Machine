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
#include "src/player/player.h"  // Include the Player class header

// Function to check if a key was pressed
bool kbhit() {
    struct termios oldt, newt;
    int ch;
    int oldf;
    
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);
    
    ch = getchar();
    
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);
    if(ch != EOF) {
        ungetc(ch, stdin);
        return true;
    }
    return false;
}

int main(int argc, char *argv[])
{
    // Instantiate the Player object
    Player player;

    // Set default drum set and song
    QString defaultDrumsetPath = "/home/rory/Documents/BBWorkspace/user_lib/drum_sets/Rock Drumset v2.0.DRM";
    QString defaultSongPath = "/home/rory/Documents/BBWorkspace/user_lib/projects/BeatBuddy Default Content 2.0 - Project/SONGS/203A18C4/716D6763.BBS";
    player.setDrumset(defaultDrumsetPath);
    player.setSong(defaultSongPath);

    std::cout << "Press spacebar to control playback. Press 'q' to quit.\n";
    std::cout << "When not playing:\n";
    std::cout << "  1-5: Select drum set\n";
    std::cout << "  6-0: Select song\n";
    
    bool isPlaying = false;
    bool isHolding = false;
    bool wasHolding = false;
    bool holdTransitionTriggered = false;
    auto lastTapTime = std::chrono::steady_clock::now();
    auto holdStartTime = std::chrono::steady_clock::now();
    const auto DOUBLE_TAP_THRESHOLD = std::chrono::milliseconds(300);
    const auto HOLD_MIN_THRESHOLD = std::chrono::milliseconds(500);
    const auto HOLD_MAX_THRESHOLD = std::chrono::milliseconds(1200);

    // Set up terminal for non-blocking input
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    int oldf = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK);

    while (true) {
        char ch;
        if (read(STDIN_FILENO, &ch, 1) > 0) {
            if (ch == ' ') {
                auto currentTime = std::chrono::steady_clock::now();
                auto timeSinceLastTap = std::chrono::duration_cast<std::chrono::milliseconds>(
                    currentTime - lastTapTime).count();

                if (!isPlaying) {
                    // First tap - start playing
                    std::cout << "Starting playback...\n";
                    player.play();
                    isPlaying = true;
                } else if (timeSinceLastTap < DOUBLE_TAP_THRESHOLD.count() && !isHolding) {
                    // Double tap - stop playing
                    std::cout << "Stopping playback...\n";
                    player.pedalDoubleTap();
                    isPlaying = false;
                } else if (!isHolding && !holdTransitionTriggered) {
                    // Single tap during playback - drum fill
                    std::cout << "Playing drum fill...\n";
                    player.pedalPress();
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    player.pedalRelease();
                }
                lastTapTime = currentTime;
                isHolding = true;
                holdStartTime = currentTime;
            } else if (ch == 'q') {
                std::cout << "Exiting...\n";
                break;
            } else if (!isPlaying) {
                // Handle number keys when not playing
                if (ch >= '1' && ch <= '5') {
                    int drumsetSelection = ch - '0';
                    QString drumsetPath;
                    switch (drumsetSelection) {
                        case 1:
                            drumsetPath = "/home/rory/Documents/BBWorkspace/user_lib/drum_sets/Brushes Drumset v2.0.DRM";
                            break;
                        case 2:
                            drumsetPath = "/home/rory/Documents/BBWorkspace/user_lib/drum_sets/Dance Drumset v2.0.DRM";
                            break;
                        case 3:
                            drumsetPath = "/home/rory/Documents/BBWorkspace/user_lib/drum_sets/Rock Drumset v2.0.DRM";
                            break;
                        case 4:
                            drumsetPath = "/home/rory/Documents/BBWorkspace/user_lib/drum_sets/Standard Drumset 2.0.DRM";
                            break;
                        case 5:
                            drumsetPath = "/home/rory/Documents/BBWorkspace/user_lib/drum_sets/Metal Drumset v2.0.DRM";
                            break;
                    }
                    std::cout << "Loading drum set " << drumsetSelection << "...\n";
                    player.setDrumset(drumsetPath);
                } else if (ch >= '6' && ch <= '9') {
                    int songSelection = ch - '0';
                    QString songPath;
                    switch (songSelection) {
                        case 6:
                            songPath = "/home/rory/Documents/BBWorkspace/user_lib/projects/BeatBuddy Default Content 2.0 - Project/SONGS/203A18C4/716D6763.BBS";
                            break;
                        case 7:
                            songPath = "/home/rory/Documents/BBWorkspace/user_lib/projects/BeatBuddy Default Content 2.0 - Project/SONGS/165DA150/0AC5D6C6.BBS";
                            break;
                        case 8:
                            songPath = "/home/rory/Documents/BBWorkspace/user_lib/projects/BeatBuddy Default Content 2.0 - Project/SONGS/165DA150/07E0BFAA.BBS";
                            break;
                        case 9:
                            songPath = "/home/rory/Documents/BBWorkspace/user_lib/projects/BeatBuddy Default Content 2.0 - Project/SONGS/165DA150/07E0BFAA.BBS";
                            break;
                    }
                    std::cout << "Loading song " << songSelection << "...\n";
                    player.setSong(songPath);
                } else if (ch == '0') {
                    QString songPath = "/home/rory/Documents/BBWorkspace/user_lib/projects/BeatBuddy Default Content 2.0 - Project/SONGS/165DA150/07E0BFAA.BBS";
                    std::cout << "Loading song 0...\n";
                    player.setSong(songPath);
                }
            }
        } else {
            // Check for key release
            if (isHolding && !wasHolding) {
                auto currentTime = std::chrono::steady_clock::now();
                auto holdDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    currentTime - holdStartTime).count();
                
                if (holdDuration >= HOLD_MIN_THRESHOLD.count() && holdDuration <= HOLD_MAX_THRESHOLD.count() && !holdTransitionTriggered) {
                    // Transition to next part
                    std::cout << "Transitioning to next part...\n";
                    player.pedalLongPress();
                    holdTransitionTriggered = true;
                }
                isHolding = false;
                holdTransitionTriggered = false;
            }
            wasHolding = isHolding;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Restore terminal settings
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, oldf);

    return 0;
}
