// leia_track_app — Standalone Leia eye tracking to OpenTrack UDP converter
// Reads Leia SDK eye positions, filters with One-Euro, maps lean to rotation,
// sends as OpenTrack UDP to VRto3D. Console app for parameter tuning.
//
// Usage: Run with VRto3D (use_open_track=true, port 4242) + SteamVR active.
//        Lean head to rotate camera. Use Ctrl+key shortcuts to tune parameters.

#define NOMINMAX
#include <winsock2.h>  // Must be before windows.h
#include <windows.h>

#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <conio.h>
#include <cstdio>
#include <algorithm>

#include "sr/management/srcontext.h"
#include "sr/utility/exception.h"
#include "sr/sense/eyetracker/predictingeyetracker.h"
#include "sr/sense/eyetracker/eyepair.h"
#include "sr/sense/core/inputstream.h"

#include "one_euro_filter.h"
#include "track_pipeline.h"
#include "opentrack_udp.h"

// Ctrl+letter codes returned by _getch() on Windows
#define CTRL_A  1
#define CTRL_B  2
#define CTRL_C  3
#define CTRL_D  4
#define CTRL_E  5
#define CTRL_F  6
#define CTRL_G  7
#define CTRL_H  8
#define CTRL_J  10
#define CTRL_K  11
#define CTRL_L  12
#define CTRL_M  13
#define CTRL_N  14
#define CTRL_P  16
#define CTRL_Q  17
#define CTRL_R  18
#define CTRL_S  19

// --- Eye data listener ---

class EyeListener : public SR::EyePairListener {
    std::mutex mutex_;
    float left_[3]  = {0.0f, 0.0f, 600.0f};
    float right_[3] = {65.0f, 0.0f, 600.0f};
    uint64_t frame_time_ = 0;
    bool has_data_ = false;

public:
    SR::InputStream<SR::EyePairStream> stream;

    void accept(const SR_eyePair& frame) override {
        std::lock_guard<std::mutex> lock(mutex_);
        left_[0]  = static_cast<float>(frame.left.x);
        left_[1]  = static_cast<float>(frame.left.y);
        left_[2]  = static_cast<float>(frame.left.z);
        right_[0] = static_cast<float>(frame.right.x);
        right_[1] = static_cast<float>(frame.right.y);
        right_[2] = static_cast<float>(frame.right.z);
        frame_time_ = frame.time;
        has_data_ = true;
    }

    bool get(float left[3], float right[3], uint64_t& time_us) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!has_data_) return false;
        left[0] = left_[0]; left[1] = left_[1]; left[2] = left_[2];
        right[0] = right_[0]; right[1] = right_[1]; right[2] = right_[2];
        time_us = frame_time_;
        return true;
    }
};

// --- Config file (Steam/config/vrto3d/) ---

static std::string get_config_path() {
    // Config lives in Steam/config/vrto3d/ alongside VRto3D's config
    char steam_path[MAX_PATH] = {};
    DWORD size = sizeof(steam_path);
    HKEY key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Valve\\Steam", 0, KEY_READ, &key) == ERROR_SUCCESS ||
        RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Valve\\Steam", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        RegQueryValueExA(key, "InstallPath", nullptr, nullptr, reinterpret_cast<LPBYTE>(steam_path), &size);
        RegCloseKey(key);
    }

    if (steam_path[0] != '\0') {
        std::string dir = std::string(steam_path) + "\\config\\vrto3d";
        CreateDirectoryA((std::string(steam_path) + "\\config").c_str(), nullptr);
        CreateDirectoryA(dir.c_str(), nullptr);
        return dir + "\\leia_track_config.txt";
    }

    // Fallback: next to the exe
    char exe_path[MAX_PATH];
    GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    std::string path(exe_path);
    auto last_slash = path.find_last_of("\\/");
    if (last_slash != std::string::npos) path = path.substr(0, last_slash + 1);
    return path + "leia_track_config.txt";
}

static bool save_config(const TrackConfig& cfg) {
    std::string path = get_config_path();
    std::ofstream f(path);
    if (!f.is_open()) return false;

    f << "# Leia Track App — Settings\n";
    f << "# Edit values or tune in-app with Ctrl+key and press Ctrl+S to save.\n\n";
    f << "filter_mincutoff = " << cfg.filter_mincutoff << "\n";
    f << "filter_beta = " << cfg.filter_beta << "\n";
    f << "sens_yaw = " << cfg.sens_yaw << "\n";
    f << "sens_pitch = " << cfg.sens_pitch << "\n";
    f << "curve_power = " << cfg.curve_power << "\n";
    f << "mag_strength = " << cfg.mag_strength << "\n";
    f << "mag_radius = " << cfg.mag_radius << "\n";
    f << "dead_zone_cm = " << cfg.dead_zone_cm << "\n";
    f << "max_yaw = " << cfg.max_yaw << "\n";
    f << "max_pitch = " << cfg.max_pitch << "\n";

    f.close();
    return true;
}

static bool load_config(TrackConfig& cfg) {
    std::string path = get_config_path();
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        auto trim = [](std::string& s) {
            while (!s.empty() && s.front() == ' ') s.erase(s.begin());
            while (!s.empty() && s.back() == ' ') s.pop_back();
        };
        trim(key);
        trim(val);

        try {
            float v = std::stof(val);
            if (!std::isfinite(v)) continue;

            if (key == "filter_mincutoff") cfg.filter_mincutoff = v;
            else if (key == "filter_beta") cfg.filter_beta = v;
            else if (key == "sens_yaw") cfg.sens_yaw = v;
            else if (key == "sens_pitch") cfg.sens_pitch = v;
            else if (key == "curve_power") cfg.curve_power = v;
            else if (key == "mag_strength") cfg.mag_strength = v;
            else if (key == "mag_radius") cfg.mag_radius = v;
            else if (key == "dead_zone_cm") cfg.dead_zone_cm = v;
            else if (key == "max_yaw") cfg.max_yaw = v;
            else if (key == "max_pitch") cfg.max_pitch = v;
        } catch (...) {
            continue;
        }
    }
    return true;
}

// --- Console helpers ---

static void print_help() {
    std::printf("\n========================================================\n");
    std::printf("  Leia Track App — Head Tracking to OpenTrack UDP\n");
    std::printf("========================================================\n");
    std::printf("  Tracks head lean via eye positions, filters noise,\n");
    std::printf("  converts to camera rotation, sends to VRto3D.\n");
    std::printf("  All hotkeys use Ctrl+ to avoid game conflicts.\n");
    std::printf("\n");
    std::printf("  CALIBRATION\n");
    std::printf("    Ctrl+C = Set current head position as center\n");
    std::printf("             (look straight at the screen, sit naturally)\n");
    std::printf("\n");
    std::printf("  SMOOTHING (One-Euro Filter)\n");
    std::printf("    Ctrl+A = More smooth at rest    (min_cutoff down)\n");
    std::printf("    Ctrl+D = Less smooth at rest    (min_cutoff up)\n");
    std::printf("             Low = silky but laggy. High = responsive but jittery.\n");
    std::printf("    Ctrl+E = Slower response        (beta down)\n");
    std::printf("    Ctrl+F = Faster response        (beta up)\n");
    std::printf("             Controls how quickly the filter reacts to fast moves.\n");
    std::printf("\n");
    std::printf("  SENSITIVITY\n");
    std::printf("    Ctrl+J = Less yaw  (left/right)\n");
    std::printf("    Ctrl+K = More yaw  (left/right)\n");
    std::printf("    Ctrl+N = Less pitch (up/down)\n");
    std::printf("    Ctrl+M = More pitch (up/down)\n");
    std::printf("             Degrees of camera rotation per cm of head lean.\n");
    std::printf("\n");
    std::printf("  CURVE\n");
    std::printf("    Ctrl+G = More linear (equal response everywhere)\n");
    std::printf("    Ctrl+B = More curved (gentle center, aggressive edges)\n");
    std::printf("             1.0 = perfectly linear. 2.0+ = TrackIR-style curve.\n");
    std::printf("\n");
    std::printf("  OTHER\n");
    std::printf("    Ctrl+S = Save settings to file (persists across sessions)\n");
    std::printf("    Ctrl+L = Load settings from file\n");
    std::printf("    Ctrl+P = Print current settings\n");
    std::printf("    Ctrl+H = Show this help\n");
    std::printf("    Ctrl+Q = Quit (sends camera back to center first)\n");
    std::printf("========================================================\n");
    std::printf("  Status: Lean=position offset | Filt=after filter\n");
    std::printf("  Out=degrees sent | UDP=packets sent | FAIL=send errors\n");
    std::printf("========================================================\n\n");
}

static void print_config(const TrackConfig& cfg) {
    std::printf("\n========================================================\n");
    std::printf("  Current Settings                       Leia Track App\n");
    std::printf("========================================================\n");
    std::printf("\n  Smoothing (One-Euro Filter)\n");
    std::printf("    min_cutoff  = %.4f   How smooth at rest (Ctrl+A/D)\n", cfg.filter_mincutoff);
    std::printf("                          Lower = silkier, higher = snappier\n");
    std::printf("    beta        = %.4f   How fast it reacts to movement (Ctrl+E/F)\n", cfg.filter_beta);
    std::printf("                          Lower = smoother turns, higher = instant\n");
    std::printf("\n  Sensitivity\n");
    std::printf("    yaw         = %.1f      Degrees per cm of left/right lean (Ctrl+J/K)\n", cfg.sens_yaw);
    std::printf("    pitch       = %.1f      Degrees per cm of up/down lean (Ctrl+N/M)\n", cfg.sens_pitch);
    std::printf("\n  Response Curve\n");
    std::printf("    curve_power = %.2f     Shape of the response (Ctrl+G/B)\n", cfg.curve_power);
    std::printf("                          1.0 = linear, 2.0+ = gentle center, fast edges\n");
    std::printf("\n  Center Feel\n");
    std::printf("    mag_strength = %.2f    How strongly it pulls back to center\n", cfg.mag_strength);
    std::printf("    mag_radius   = %.1f cm  How far the pull reaches\n", cfg.mag_radius);
    std::printf("    dead_zone    = %.1f cm  Ignore tiny movements below this\n", cfg.dead_zone_cm);
    std::printf("\n  Limits\n");
    std::printf("    max_yaw      = %.0f deg  Maximum left/right rotation\n", cfg.max_yaw);
    std::printf("    max_pitch    = %.0f deg  Maximum up/down rotation\n", cfg.max_pitch);
    std::printf("========================================================\n\n");
}

// --- Main ---

int main() {
    // Disable default Ctrl+C handler so it doesn't kill the app
    SetConsoleCtrlHandler(nullptr, TRUE);

    std::printf("========================================================\n");
    std::printf("  Leia Track App v0.1              by evilkermitreturns\n");
    std::printf("========================================================\n");
    std::printf("  Leia eye tracking -> One-Euro filter -> OpenTrack UDP\n");
    std::printf("  Standalone head tracking for any SteamVR game.\n");
    std::printf("  Requires: Leia display + VRto3D (use_open_track=true)\n");
    std::printf("========================================================\n\n");

    // 1. Init UDP sender
    OpenTrackSender sender;
    if (!sender.init("127.0.0.1", 4242)) {
        std::printf("ERROR: Failed to initialize UDP socket.\n");
        return 1;
    }
    std::printf("[OK] UDP sender ready (localhost:4242)\n");

    // 2. Init Leia SDK
    SR::SRContext* context = nullptr;
    SR::PredictingEyeTracker* tracker = nullptr;
    EyeListener listener;

    try {
        context = new SR::SRContext();
        std::printf("[OK] SR Context created\n");
    } catch (SR::ServerNotAvailableException&) {
        std::printf("ERROR: Leia SR Service not available.\n");
        std::printf("       Make sure the SR Platform is installed and running.\n");
        return 1;
    } catch (std::runtime_error& e) {
        std::printf("ERROR: SR Context failed: %s\n", e.what());
        return 1;
    }

    try {
        tracker = SR::PredictingEyeTracker::create(*context);
        listener.stream.set(tracker->openEyePairStream(&listener));
        context->initialize();
        std::printf("[OK] Eye tracker started\n");
    } catch (std::exception& e) {
        std::printf("ERROR: Eye tracker init failed: %s\n", e.what());
        delete context;
        return 1;
    }

    // 3. Create pipeline (auto-load saved settings if available)
    TrackConfig cfg;
    if (load_config(cfg)) {
        std::printf("[OK] Settings loaded from: %s\n", get_config_path().c_str());
    }
    TrackPipeline pipeline(cfg);

    // 4. Wait for first eye data, then calibrate
    std::printf("\nWaiting for eye data...\n");
    float left[3], right[3];
    uint64_t time_us;
    bool got_first = false;

    for (int i = 0; i < 300 && !got_first; ++i) {
        tracker->predict(80);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        if (listener.get(left, right, time_us)) {
            float ipd = std::fabs(right[0] - left[0]);
            if (ipd > 10.0f) got_first = true;
        }
    }

    if (!got_first) {
        std::printf("WARNING: No face detected after 5 seconds. Calibrating with defaults.\n");
        left[0] = -32.5f; left[1] = 0.0f; left[2] = 600.0f;
        right[0] = 32.5f; right[1] = 0.0f; right[2] = 600.0f;
    } else {
        std::printf("[OK] Face detected. IPD = %.1f mm\n", std::fabs(right[0] - left[0]));
    }

    pipeline.calibrate(left, right);
    std::printf("[OK] Center calibrated.\n");
    print_help();

    // 5. Main loop
    auto start_time = std::chrono::high_resolution_clock::now();
    int frame_count = 0;
    int udp_sent_count = 0;
    int udp_fail_count = 0;
    bool running = true;
    bool face_lost_announced = false;

    while (running) {
        auto loop_start = std::chrono::high_resolution_clock::now();

        tracker->predict(80);

        if (listener.get(left, right, time_us)) {
            auto now = std::chrono::high_resolution_clock::now();
            float timestamp_sec = std::chrono::duration<float>(now - start_time).count();

            TrackResult r = pipeline.process(left, right, timestamp_sec);

            if (r.valid) {
                bool sent_ok = sender.send(r.yaw_deg, r.pitch_deg, r.roll_deg);
                if (sent_ok) udp_sent_count++; else udp_fail_count++;
                face_lost_announced = false;

                if (frame_count % 30 == 0) {
                    std::printf("\r  Lean: X=%+5.1f Y=%+5.1f | Filt: X=%+5.1f Y=%+5.1f | Out: Yaw=%+6.1f Pitch=%+5.1f | UDP:%d",
                        r.lean_x_cm, r.lean_y_cm,
                        r.filt_x_cm, r.filt_y_cm,
                        r.yaw_deg, r.pitch_deg,
                        udp_sent_count);
                    if (udp_fail_count > 0) std::printf(" FAIL:%d", udp_fail_count);
                    std::printf("   ");
                    std::fflush(stdout);
                }
            } else {
                sender.sendIdentity();
                if (!face_lost_announced) {
                    std::printf("\n  [FACE LOST] Sending identity rotation\n");
                    face_lost_announced = true;
                }
            }
        }

        while (_kbhit()) {
            int key = _getch();
            switch (key) {
                case CTRL_Q:
                    running = false;
                    break;
                case CTRL_C:
                    if (listener.get(left, right, time_us)) {
                        pipeline.calibrate(left, right);
                        std::printf("\n  [CALIBRATED] Center reset.\n");
                    }
                    break;

                // Smoothing: Ctrl+A/D = min_cutoff down/up
                case CTRL_A:
                    cfg.filter_mincutoff = std::max(0.001f, cfg.filter_mincutoff / 2.0f);
                    pipeline.config().filter_mincutoff = cfg.filter_mincutoff;
                    pipeline.reset_filters();
                    std::printf("\n  min_cutoff = %.4f (smoother)\n", cfg.filter_mincutoff);
                    break;
                case CTRL_D:
                    cfg.filter_mincutoff = std::min(10.0f, cfg.filter_mincutoff * 2.0f);
                    pipeline.config().filter_mincutoff = cfg.filter_mincutoff;
                    pipeline.reset_filters();
                    std::printf("\n  min_cutoff = %.4f (less smooth)\n", cfg.filter_mincutoff);
                    break;

                // Response: Ctrl+E/F = beta down/up
                case CTRL_E:
                    cfg.filter_beta = std::max(0.001f, cfg.filter_beta / 10.0f);
                    pipeline.config().filter_beta = cfg.filter_beta;
                    pipeline.reset_filters();
                    std::printf("\n  beta = %.4f (less responsive)\n", cfg.filter_beta);
                    break;
                case CTRL_F:
                    cfg.filter_beta = std::min(100.0f, cfg.filter_beta * 10.0f);
                    pipeline.config().filter_beta = cfg.filter_beta;
                    pipeline.reset_filters();
                    std::printf("\n  beta = %.4f (more responsive)\n", cfg.filter_beta);
                    break;

                // Yaw sensitivity: Ctrl+J/K
                case CTRL_J:
                    cfg.sens_yaw = std::max(0.5f, cfg.sens_yaw - 0.5f);
                    pipeline.config().sens_yaw = cfg.sens_yaw;
                    std::printf("\n  sens_yaw = %.1f\n", cfg.sens_yaw);
                    break;
                case CTRL_K:
                    cfg.sens_yaw = std::min(20.0f, cfg.sens_yaw + 0.5f);
                    pipeline.config().sens_yaw = cfg.sens_yaw;
                    std::printf("\n  sens_yaw = %.1f\n", cfg.sens_yaw);
                    break;

                // Pitch sensitivity: Ctrl+N/M
                case CTRL_N:
                    cfg.sens_pitch = std::max(0.5f, cfg.sens_pitch - 0.5f);
                    pipeline.config().sens_pitch = cfg.sens_pitch;
                    std::printf("\n  sens_pitch = %.1f\n", cfg.sens_pitch);
                    break;
                case CTRL_M:
                    cfg.sens_pitch = std::min(20.0f, cfg.sens_pitch + 0.5f);
                    pipeline.config().sens_pitch = cfg.sens_pitch;
                    std::printf("\n  sens_pitch = %.1f\n", cfg.sens_pitch);
                    break;

                // Curve: Ctrl+G/B = more linear / more curved
                case CTRL_G:
                    cfg.curve_power = std::max(1.0f, cfg.curve_power - 0.1f);
                    pipeline.config().curve_power = cfg.curve_power;
                    std::printf("\n  curve_power = %.2f\n", cfg.curve_power);
                    break;
                case CTRL_B:
                    cfg.curve_power = std::min(3.0f, cfg.curve_power + 0.1f);
                    pipeline.config().curve_power = cfg.curve_power;
                    std::printf("\n  curve_power = %.2f\n", cfg.curve_power);
                    break;

                // Save/Load
                case CTRL_S:
                    if (save_config(cfg)) {
                        std::printf("\n  [SAVED] %s\n", get_config_path().c_str());
                    } else {
                        std::printf("\n  [ERROR] Could not save config.\n");
                    }
                    break;
                case CTRL_L: {
                    TrackConfig loaded;
                    if (load_config(loaded)) {
                        cfg = loaded;
                        pipeline.config() = cfg;
                        pipeline.reset_filters();
                        std::printf("\n  [LOADED] Settings restored from file.\n");
                    } else {
                        std::printf("\n  [ERROR] No saved config found.\n");
                    }
                    break;
                }

                case CTRL_P:
                    print_config(cfg);
                    break;
                case CTRL_H:
                    print_help();
                    break;
            }
        }

        frame_count++;

        auto loop_end = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(loop_end - loop_start);
        auto remaining = std::chrono::milliseconds(8) - elapsed;
        if (remaining.count() > 0) {
            std::this_thread::sleep_for(remaining);
        }
    }

    std::printf("\n\nShutting down...\n");
    sender.shutdown();
    delete context;
    std::printf("Done.\n");
    return 0;
}
