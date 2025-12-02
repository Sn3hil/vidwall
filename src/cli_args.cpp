#include "../include/cli_args.h"
#include <iostream>
#include <cstring>
#include <unistd.h>

CliArgs CliArgs::parse(int argc, char** argv) {
    CliArgs args;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            args.show_help = true;
            return args;
        }
        else if (arg == "--no-mute" || arg == "-u") {
            args.mute = false;
        }
        else if (arg == "--no-loop" || arg == "-l") {
            args.loop = false;
        }
        else if (arg == "--no-pause" || arg == "-p") {
            args.auto_pause = false;
        }
        else if (arg == "--mute" || arg == "-m") {
            args.mute = true;
        }
        else if (arg == "--no-downscale" || arg == "-n") {
            args.no_downscale = true;
        }
        else if (arg == "--no-hwdec" || arg == "-H") {
            args.no_hwdec = true;
        }
        else if (arg[0] == '-') {
            std::cerr << "Unknown option: " << arg << std::endl;
            std::cerr << "Use --help for usage information" << std::endl;
            args.show_help = true;
            return args;
        }
        else {
            // First non-option argument is the video path
            if (args.video_path.empty()) {
                args.video_path = arg;
            } else {
                std::cerr << "Multiple video paths specified. Only one video supported currently." << std::endl;
                args.show_help = true;
                return args;
            }
        }
    }
    
    return args;
}

void CliArgs::print_help(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS] <video-file>\n\n";
    std::cout << "Play a video as an animated wallpaper on Hyprland\n\n";
    std::cout << "Options:\n";
    std::cout << "  -h, --help        Show this help message\n";
    std::cout << "  -m, --mute        Mute audio (default: on)\n";
    std::cout << "  -u, --no-mute     Enable audio\n";
    std::cout << "  -l, --no-loop     Don't loop the video\n";
    std::cout << "  -p, --no-pause    Disable auto-pause on window focus\n";
    std::cout << "  -n, --no-downscale Disable 4K downscaling (higher quality, more CPU)\n";
    std::cout << "  -H, --no-hwdec    Disable hardware decoding (use if crashing)\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program_name << " video.mp4\n";
    std::cout << "  " << program_name << " --no-mute video.mp4\n";
    std::cout << "  " << program_name << " --no-pause video.mp4\n";
    std::cout << "\n";
    std::cout << "Features:\n";
    std::cout << "  • Hardware-accelerated playback\n";
    std::cout << "  • Automatic 4K downscaling for performance\n";
    std::cout << "  • Auto-pause when windows are focused (Hyprland)\n";
    std::cout << "  • Runs on background layer (behind all windows)\n";
    std::cout << "\n";
}

bool CliArgs::is_valid() const {
    if (show_help) return true;
    
    if (video_path.empty()) {
        std::cerr << "Error: No video file specified\n";
        return false;
    }
    
    // Check if file exists
    if (access(video_path.c_str(), F_OK) != 0) {
        std::cerr << "Error: Video file does not exist: " << video_path << std::endl;
        return false;
    }
    
    return true;
}
