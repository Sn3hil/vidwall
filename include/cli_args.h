#pragma once
#include <string>
#include <vector>

struct CliArgs {
    std::string video_path;
    bool mute = true;              
    bool loop = true;              
    bool auto_pause = true;        
    bool no_downscale = false;     
    bool no_hwdec = false;        
    bool show_help = false;
    
   
    static CliArgs parse(int argc, char** argv);
    
    static void print_help(const char* program_name);

    bool is_valid() const;
    
};
