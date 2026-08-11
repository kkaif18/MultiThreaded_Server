#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

using namespace std;

// Shared task queue: holds client sockets waiting to be handled.
// Protected by queue_mutex, workers sleep on queue_cv until work arrives.
queue<int> task_queue;
mutex queue_mutex;
condition_variable queue_cv;

// Reads a file's full contents into a string. Returns "" if the file
// doesn't exist or can't be opened.
string read_file(const string& path) {
    ifstream file(path);
    if (!file) {
        return "";
    }

    stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

// Handles one client end-to-end: reads the HTTP request, figures out
// which file was asked for, and sends back either the file (200) or
// a 404 response if it doesn't exist.
void handle_client(int client_fd) {
    char buffer[4096] = {0};
    read(client_fd, buffer, sizeof(buffer));

    // Parse just the request line (method, path, version)
    string request(buffer);
    istringstream request_stream(request);

    string method, path, http_version;
    request_stream >> method >> path >> http_version;

    cout << "Method: " << method << ", Path: " << path
         << " | Thread ID: " << this_thread::get_id() << endl;

    // Map the URL path to a file inside public/, defaulting to index.html
    string file_path = "public" + path;
    if (path == "/") {
        file_path = "public/index.html";
    }

    string body = read_file(file_path);
    string response;

    if (body.empty()) {
        string not_found_body = "<html><body><h1>404 Not Found</h1></body></html>";
        response =
            "HTTP/1.1 404 Not Found\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: " + to_string(not_found_body.size()) + "\r\n"
            "\r\n" + not_found_body;
    } else {
        response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html\r\n"
            "Content-Length: " + to_string(body.size()) + "\r\n"
            "\r\n" + body;
    }

    write(client_fd, response.c_str(), response.size());
    close(client_fd);
}

// Runs forever on a pool thread: waits for a client to appear in the
// queue, pops it off, handles it, then goes back to waiting.
void worker_thread() {
    while (true) {
        int client_fd;

        {
            unique_lock<mutex> lock(queue_mutex);
            queue_cv.wait(lock, [] { return !task_queue.empty(); });

            client_fd = task_queue.front();
            task_queue.pop();
        }

        handle_client(client_fd);
    }
}

int main() {
    // --- Socket setup: create, bind to port 8080, start listening ---
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        cerr << "Failed to create socket" << endl;
        return 1;
    }

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == -1) {
        cerr << "Bind failed" << endl;
        return 1;
    }

    if (listen(server_fd, 5) == -1) {
        cerr << "Listen failed" << endl;
        return 1;
    }

    cout << "Server listening on port 8080..." << endl;

    // --- Spin up a fixed pool of worker threads, once, at startup ---
    const int NUM_THREADS = 4;
    vector<thread> workers;
    for (int i = 0; i < NUM_THREADS; i++) {
        workers.emplace_back(worker_thread);
    }

    // --- Main loop: only accepts connections and queues them up ---
    // Workers (above) are the ones actually processing each client.
    while (true) {
        sockaddr_in client_address;
        socklen_t client_len = sizeof(client_address);

        int client_fd = accept(server_fd, (struct sockaddr*)&client_address, &client_len);
        if (client_fd == -1) {
            cerr << "Accept failed" << endl;
            continue;
        }

        {
            lock_guard<mutex> lock(queue_mutex);
            task_queue.push(client_fd);
        }
        queue_cv.notify_one();
    }

    close(server_fd);
    return 0;
}
