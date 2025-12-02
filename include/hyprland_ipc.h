#pragma once
#include <string>
#include <functional>
#include <thread>
#include <atomic>

class HyprlandIPC {
public:
    using FocusCallback = std::function<void(bool has_focus)>;
    
    HyprlandIPC();
    ~HyprlandIPC();
    
    bool connect();
    void start_listening(FocusCallback callback);
    void stop_listening();
    bool is_workspace_empty();
    
private:
    int socket_fd;
    std::thread listener_thread;
    std::atomic<bool> running;
    FocusCallback on_focus_change;
    
    std::string get_socket_path(bool is_event_socket);
    std::string send_command(const std::string& cmd);
    void listen_events();
};
