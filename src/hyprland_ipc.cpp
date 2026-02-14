#include "../include/hyprland_ipc.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <vector>
#include <sstream>
#include <algorithm>

HyprlandIPC::HyprlandIPC() : socket_fd(-1), running(false) {}

HyprlandIPC::~HyprlandIPC() {
    stop_listening();
    if (socket_fd >= 0) {
        close(socket_fd);
    }
}

std::string HyprlandIPC::get_socket_path(bool is_event_socket) {
    const char* sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!sig) {
        return "";
    }
    
    std::string filename = is_event_socket ? ".socket2.sock" : ".socket.sock";
    
    // Try XDG_RUNTIME_DIR first
    const char* xdg = getenv("XDG_RUNTIME_DIR");
    if (xdg) {
        return std::string(xdg) + "/hypr/" + sig + "/" + filename;
    }
    
    return "/tmp/hypr/" + std::string(sig) + "/" + filename;
}

bool HyprlandIPC::connect() {
    //socket connection persistent
    std::string socket_path = get_socket_path(true);
    if (socket_path.empty()) {
        std::cerr << "Could not determine Hyprland socket path" << std::endl;
        return false;
    }
    
    socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return false;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);
    
    if (::connect(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to connect to Hyprland IPC (event socket)" << std::endl;
        close(socket_fd);
        socket_fd = -1;
        return false;
    }
    
    std::cout << "Connected to Hyprland IPC" << std::endl;
    return true;
}

std::string HyprlandIPC::send_command(const std::string& cmd) {
    std::string socket_path = get_socket_path(false); // Command socket
    if (socket_path.empty()) return "";

    int cmd_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (cmd_fd < 0) return "";

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(cmd_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(cmd_fd);
        return "";
    }


    std::string full_cmd = "j/" + cmd;
    if (write(cmd_fd, full_cmd.c_str(), full_cmd.length()) < 0) {
        close(cmd_fd);
        return "";
    }

    // Read response
    std::string response;
    char buffer[4096];
    ssize_t n;
    while ((n = read(cmd_fd, buffer, sizeof(buffer))) > 0) {
        response.append(buffer, n);
    }

    close(cmd_fd);
    return response;
}

// Simple JSON scanner helpers
std::string get_json_value(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    
    pos += search.length();
    
    // Skip whitespace
    while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\r')) pos++;
    
    if (pos >= json.length()) return "";

    if (json[pos] == '"') {

        // String value
        size_t end = json.find('"', pos + 1);
        if (end != std::string::npos) {
            return json.substr(pos + 1, end - pos - 1);
        }
    } else {

        // Number or boolean
        size_t end = pos;
        while (end < json.length() && (isdigit(json[end]) || json[end] == '.' || json[end] == '-')) end++;
        return json.substr(pos, end - pos);
    }
    return "";
}

void HyprlandIPC::listen_events() {
    char buffer[4096];
    std::string pending_data;
    
    while (running) {
        ssize_t n = read(socket_fd, buffer, sizeof(buffer) - 1);
        if (n <= 0) {
            if (running) {
                std::cerr << "Lost connection to Hyprland IPC" << std::endl;
                running = false;
            }
            break;
        }
        
        buffer[n] = '\0';
        pending_data.append(buffer);
        
        bool needs_update = false;
        size_t pos = 0;
        while ((pos = pending_data.find('\n')) != std::string::npos) {
            std::string line = pending_data.substr(0, pos);
            pending_data.erase(0, pos + 1);
            
            
            if (line.find("openwindow>>") == 0 ||
                line.find("closewindow>>") == 0 ||
                line.find("workspace>>") == 0 ||
                line.find("movewindow>>") == 0 ||
                line.find("movewindowv2>>") == 0 ||
                line.find("focusedmon>>") == 0) {
                
                needs_update = true;
            }
        }
        
        // Debounce: Only check once per read chunk if relevant events occurred
        if (needs_update) {
            // Small sleep to allow window state to settle
            usleep(50000); 
            
            bool empty = is_workspace_empty();
            if (on_focus_change) {
                on_focus_change(!empty);
            }
        }
    }
}

bool HyprlandIPC::is_workspace_empty() {
    //Get active workspace ID
    std::string active_ws_json = send_command("activeworkspace");
    if (active_ws_json.empty()) {
        std::cerr << "Failed to get active workspace" << std::endl;
        return true; 
    }
    
    std::string ws_id_str = get_json_value(active_ws_json, "id");
    if (ws_id_str.empty()) {
        std::cerr << "Failed to parse active workspace ID" << std::endl;
        return true;
    }
    
    int active_ws_id = std::atoi(ws_id_str.c_str());
    
    // Get all clients
    std::string clients_json = send_command("clients");
    if (clients_json.empty()) {
        std::cerr << "Failed to get clients" << std::endl;
        return true;
    }
    
    int window_count = 0;
    
    //Parsing
    size_t pos = 0;
    bool in_array = false;
    
    // Find start of array
    pos = clients_json.find('[');
    if (pos != std::string::npos) {
        in_array = true;
        pos++;
    } else {
        return true;
    }
    
    while (pos < clients_json.length()) {
  
        size_t start = clients_json.find('{', pos);
        if (start == std::string::npos) break;
        
        // Find matching closing brace
        int depth = 1;
        size_t current = start + 1;
        bool in_string = false;
        
        while (current < clients_json.length() && depth > 0) {
            char c = clients_json[current];
            if (c == '"' && (current == 0 || clients_json[current-1] != '\\')) {
                in_string = !in_string;
            } else if (!in_string) {
                if (c == '{') depth++;
                else if (c == '}') depth--;
            }
            current++;
        }
        
        if (depth == 0) {

            std::string client_obj = clients_json.substr(start, current - start);
            
            // Check workspace ID
            size_t ws_pos = client_obj.find("\"workspace\":");
            if (ws_pos != std::string::npos) {
                std::string ws_part = client_obj.substr(ws_pos);
                std::string id_val = get_json_value(ws_part, "id");
                
                if (!id_val.empty() && std::atoi(id_val.c_str()) == active_ws_id) {
                  
                    std::string class_val = get_json_value(client_obj, "class");
                    
                    // Filter ignored classes
                    if (class_val != "vidwall" && 
                        class_val != "dunst" && 
                        class_val != "mako" && 
                        class_val != "swaync" && 
                        class_val != "waybar" && 
                        class_val != "eww" &&
                        class_val != "quickshell" && 
                        class_val != "ags") {

                        window_count++;

                    }
                }
            }
            
            pos = current;
        } else {
            break;
        }
    }
    
    std::cout << "Windows on current workspace (" << active_ws_id << "): " << window_count << std::endl;
    return window_count == 0;
}

void HyprlandIPC::start_listening(FocusCallback callback) {
    if (running) return;
    
    on_focus_change = callback;
    running = true;
    
    // Initial check
    bool empty = is_workspace_empty();
    if (on_focus_change) {
        on_focus_change(!empty);
    }
    
    listener_thread = std::thread(&HyprlandIPC::listen_events, this);
}

void HyprlandIPC::stop_listening() {
    if (!running) return;
    
    running = false;
    
    // Close socket to wake up thread
    if (socket_fd >= 0) {
        shutdown(socket_fd, SHUT_RDWR);
        close(socket_fd);
        socket_fd = -1;
    }
    
    if (listener_thread.joinable()) {
        listener_thread.join();
    }
}

