#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#endif

using namespace std;
using namespace std::chrono;

const string PLACEHOLDER_ACCOUNT  = "NoAccount";
const string PLACEHOLDER_MAIL     = "NoMail";
const string PLACEHOLDER_USERNAME = "NoUsername";
const string PLACEHOLDER_PASSWORD = "NoPassword";

void copy_to_clipboard(const string& text) {
#ifdef _WIN32
    FILE* pipe = _popen("clip", "w");
    if (pipe) {
        fwrite(text.c_str(), 1, text.size(), pipe);
        fclose(pipe);
    }
#elif defined(__APPLE__)
    FILE* pipe = popen("pbcopy", "w");
    if (pipe) {
        fwrite(text.c_str(), 1, text.size(), pipe);
        pclose(pipe);
    }
#else
    FILE* pipe = popen("xclip -selection clipboard 2>/dev/null", "w");
    if (!pipe) pipe = popen("xsel -ib 2>/dev/null", "w");
    if (pipe) {
        fwrite(text.c_str(), 1, text.size(), pipe);
        pclose(pipe);
    }
#endif
}

enum Key {
    KEY_NONE = 0,
    KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
    KEY_ENTER, KEY_ESC, KEY_BACKSPACE,
    KEY_UNKNOWN = 1000
};

class KeyReader {
#ifdef _WIN32
    HANDLE hIn;
    DWORD mode;
public:
    KeyReader() {
        hIn = GetStdHandle(STD_INPUT_HANDLE);
        GetConsoleMode(hIn, &mode);
        SetConsoleMode(hIn, mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
    }
    ~KeyReader() { SetConsoleMode(hIn, mode); }
    int getch() {
        int c = _getch();
        if (c == 0 || c == 0xE0) {
            int c2 = _getch();
            switch (c2) {
                case 72: return KEY_UP;
                case 80: return KEY_DOWN;
                case 75: return KEY_LEFT;
                case 77: return KEY_RIGHT;
                default: return KEY_UNKNOWN + c2;
            }
        }
        if (c == '\r') return KEY_ENTER;
        if (c == 27)  return KEY_ESC;
        if (c == 8)   return KEY_BACKSPACE;
        if (c >= 32 && c <= 126) return c;
        return KEY_UNKNOWN + c;
    }
#else
    termios oldt, newt;
public:
    KeyReader() {
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        newt.c_cc[VMIN] = 1;
        newt.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    }
    ~KeyReader() { tcsetattr(STDIN_FILENO, TCSANOW, &oldt); }
    int getch() {
        char c;
        if (read(STDIN_FILENO, &c, 1) != 1) return KEY_NONE;
        if (c == '\x1b') {
            struct termios tmp = newt;
            tmp.c_cc[VMIN] = 0;
            tmp.c_cc[VTIME] = 1;
            tcsetattr(STDIN_FILENO, TCSANOW, &tmp);
            char seq[2];
            int n = read(STDIN_FILENO, seq, 2);
            tcsetattr(STDIN_FILENO, TCSANOW, &newt);
            if (n <= 0) return KEY_ESC;
            if (seq[0] == '[') {
                if (n < 2) return KEY_NONE;
                switch (seq[1]) {
                    case 'A': return KEY_UP;
                    case 'B': return KEY_DOWN;
                    case 'C': return KEY_RIGHT;
                    case 'D': return KEY_LEFT;
                }
            }
            return KEY_UNKNOWN + c;
        }
        if (c == '\n' || c == '\r') return KEY_ENTER;
        if (c == 127 || c == '\b') return KEY_BACKSPACE;
        return (unsigned char)c;
    }
#endif

    string read_line(const string& prompt, bool echo = true) {
        cout << prompt << flush;
        string result;
        while (true) {
            int ch = getch();
            if (ch == KEY_ENTER) {
                cout << endl;
                break;
            } else if (ch == KEY_BACKSPACE) {
                if (!result.empty()) {
                    result.pop_back();
                    cout << "\b \b" << flush;
                }
            } else if (ch >= 32 && ch <= 126) {
                result += (char)ch;
                if (echo) cout << (char)ch << flush;
                else cout << '*' << flush;
            }
        }
        return result;
    }
};

unsigned long long calculate_key_value(const string& key) {
    unsigned long long key_value = 1;
    auto char_value = [](char ch) -> unsigned long long {
        ch = tolower((unsigned char)ch);
        if (ch >= 'a' && ch <= 'z') return ch - 'a' + 1;
        if ((unsigned char)ch >= (unsigned char)'ç' && (unsigned char)ch <= (unsigned char)'ü')
            return (unsigned char)ch - (unsigned char)'ç' + 27;
        return 1;
    };
    int len = (int)key.size();
    if (len < 2) {
        for (char ch : key) key_value *= char_value(ch);
    } else {
        for (int i = 0; i < len - 2; ++i) key_value *= char_value(key[i]);
        key_value *= char_value(key[len - 2]) * char_value(key[len - 1]);
    }
    for (char ch : key) {
        if (isdigit(ch) && ch != '0') {
            int digit = ch - '0';
            if (digit) { key_value /= digit; break; }
        }
    }
    for (char ch : key) {
        if (isupper(ch)) {
            int pos = tolower(ch) - 'a' + 1;
            if (pos > 0)
                key_value = (key_value > (unsigned long long)pos) ? key_value - pos : 0;
            break;
        }
    }
    if (key.length() >= 6) key_value += 69;
    return (key_value == 0) ? 1 : key_value;
}

string insert_fake_data(const string& text, int interval, mt19937& rng) {
    if (interval <= 0 || text.empty()) return text;
    uniform_int_distribution<size_t> dist(0, text.size() - 1);
    string result;
    int count = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        result += text[i];
        if (++count == interval) {
            result += text[dist(rng)];
            count = 0;
        }
    }
    return result;
}

string remove_fake_data(const string& text, int interval) {
    if (interval <= 0) return text;
    string result;
    int count = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (count == interval) { count = 0; continue; }
        result += text[i];
        ++count;
    }
    return result;
}

string encode_text(const string& text, unsigned long long key_value, int fake_interval) {
    if (text.empty()) return "";
    vector<char> chars(text.begin(), text.end());
    size_t len = chars.size();
    auto start = steady_clock::now();
    for (unsigned long long n = key_value; n > 0; --n) {
        size_t from = static_cast<size_t>((n - 1) % len);
        size_t to   = (from + n) % len;
        char ch = chars[to];
        chars.erase(chars.begin() + to);
        chars.insert(chars.begin() + from, ch);
        if (n % 1000 == 0 || n < 1000) {
            auto now = steady_clock::now();
            double elapsed = duration_cast<milliseconds>(now - start).count() / 1000.0;
            double processed = key_value - n;
            double rate = processed / (elapsed > 0 ? elapsed : 1);
            double eta = n / (rate > 0 ? rate : 1);
            cerr << "\rEncoding: " << (int)(processed * 100 / key_value)
                 << "%, ETA " << (int)eta << "s, " << (int)rate << " ch/s   " << flush;
        }
    }
    string processed(chars.begin(), chars.end());
    if (fake_interval > 0) {
        mt19937 rng((unsigned int)time(nullptr));
        processed = insert_fake_data(processed, fake_interval, rng);
    }
    cerr << "\rEncoding complete!                            " << endl;
    return processed;
}

string decode_text(const string& text, unsigned long long key_value, int fake_interval) {
    if (text.empty()) return "";
    string without_fake = (fake_interval > 0) ? remove_fake_data(text, fake_interval) : text;
    if (without_fake.empty()) return "";
    vector<char> chars(without_fake.begin(), without_fake.end());
    size_t len = chars.size();
    auto start = steady_clock::now();
    for (unsigned long long n = 1; n <= key_value; ++n) {
        size_t from = static_cast<size_t>((n - 1) % len);
        size_t to   = (from + n) % len;
        char ch = chars[from];
        chars.erase(chars.begin() + from);
        chars.insert(chars.begin() + to, ch);
        if (n % 1000 == 0 || n > key_value - 1000) {
            auto now = steady_clock::now();
            double elapsed = duration_cast<milliseconds>(now - start).count() / 1000.0;
            double processed = n - 1;
            double rate = processed / (elapsed > 0 ? elapsed : 1);
            double eta = (key_value - n + 1) / (rate > 0 ? rate : 1);
            cerr << "\rDecoding: " << (int)(processed * 100 / key_value)
                 << "%, ETA " << (int)eta << "s, " << (int)rate << " ch/s   " << flush;
        }
    }
    cerr << "\rDecoding complete!                            " << endl;
    return string(chars.begin(), chars.end());
}

struct Entry {
    string account, mail, username, password;
};

bool parse_csv_line(const string& line, Entry& out) {
    if (line.empty()) return false;
    istringstream iss(line);
    vector<string> fields;
    string field;
    bool in_quotes = false;

    auto read_next = [&]() -> int {
        field.clear();
        in_quotes = false;
        char c;
        while (iss.get(c)) {
            if (!in_quotes) {
                if (c == '"') { in_quotes = true; continue; }
                if (c == ',') return 1;
                field += c;
            } else {
                if (c == '"') {
                    if (iss.peek() == '"') {
                        field += '"';
                        iss.ignore();
                    } else {
                        in_quotes = false;
                        continue;
                    }
                } else {
                    field += c;
                }
            }
        }
        return 0;
    };

    while (true) {
        int r = read_next();
        fields.push_back(field);
        if (r == 0) break;
    }
    if (fields.size() == 4) {
        out.account  = fields[0];
        out.mail     = fields[1];
        out.username = fields[2];
        out.password = fields[3];
        return true;
    }
    return false;
}

string entry_to_csv(const Entry& e) {
    auto esc = [](const string& s) -> string {
        string r = "\"";
        for (char c : s) {
            if (c == '"') r += "\"\"";
            else r += c;
        }
        r += "\"";
        return r;
    };
    return esc(e.account) + "," + esc(e.mail) + "," +
           esc(e.username) + "," + esc(e.password);
}

void clear_screen() {
    cout << "\033[2J\033[H" << flush;
}

void move_cursor(int row, int col) {
    cout << "\033[" << row << ";" << col << "H" << flush;
}

pair<int,int> term_size() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    return {csbi.srWindow.Bottom - csbi.srWindow.Top + 1,
            csbi.srWindow.Right - csbi.srWindow.Left + 1};
#else
    winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0)
        return {w.ws_row, w.ws_col};
    return {24, 80};
#endif
}

class PasswordManager {
    vector<Entry> entries;
    string file_name;
    unsigned long long key_value;
    int fake_interval;
    mutable mutex mtx;

    void apply_placeholders(Entry& e) {
        if (e.account.empty())  e.account  = PLACEHOLDER_ACCOUNT;
        if (e.mail.empty())     e.mail     = PLACEHOLDER_MAIL;
        if (e.username.empty()) e.username = PLACEHOLDER_USERNAME;
        if (e.password.empty()) e.password = PLACEHOLDER_PASSWORD;
    }

public:
    PasswordManager(const string& fname, const string& key)
        : file_name(fname) {
        key_value = calculate_key_value(key);
        string kv_str = to_string(key_value);
        int half = max(1, (int)kv_str.size() / 2);
        fake_interval = 0;
        for (int i = (int)kv_str.size() - half; i < (int)kv_str.size(); ++i)
            if (isdigit(kv_str[i]))
                fake_interval += kv_str[i] - '0';
    }

    bool load() {
        lock_guard<mutex> lock(mtx);
        ifstream in(file_name, ios::binary);
        if (!in) return true;
        in.seekg(0, ios::end);
        streampos size = in.tellg();
        if (size == 0) return true;
        in.seekg(0, ios::beg);
        string cipher((istreambuf_iterator<char>(in)),
                      istreambuf_iterator<char>());
        string plain = decode_text(cipher, key_value, fake_interval);
        entries.clear();
        istringstream iss(plain);
        string line;
        while (getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            Entry e;
            if (parse_csv_line(line, e))
                entries.push_back(e);
        }
        return true;
    }

    bool save() {
        lock_guard<mutex> lock(mtx);
        string csv;
        for (const auto& e : entries)
            csv += entry_to_csv(e) + "\n";
        string cipher = encode_text(csv, key_value, fake_interval);
        ofstream out(file_name, ios::binary);
        if (!out) return false;
        out.write(cipher.data(), cipher.size());
        return true;
    }

    string export_raw() {
        lock_guard<mutex> lock(mtx);
        string csv;
        for (const auto& e : entries)
            csv += entry_to_csv(e) + "\n";
        return encode_text(csv, key_value, fake_interval);
    }

    bool import_raw(const string& raw_data) {
        string plain = decode_text(raw_data, key_value, fake_interval);
        vector<Entry> new_entries;
        istringstream iss(plain);
        string line;
        while (getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            Entry e;
            if (parse_csv_line(line, e))
                new_entries.push_back(e);
            else
                return false;
        }
        lock_guard<mutex> lock(mtx);
        entries = move(new_entries);
        return true;
    }

    void add(Entry e) {
        lock_guard<mutex> lock(mtx);
        apply_placeholders(e);
        entries.push_back(e);
    }

    void remove(int idx) {
        lock_guard<mutex> lock(mtx);
        if (idx >= 0 && idx < (int)entries.size())
            entries.erase(entries.begin() + idx);
    }

    void update(int idx, Entry e) {
        lock_guard<mutex> lock(mtx);
        if (idx >= 0 && idx < (int)entries.size()) {
            apply_placeholders(e);
            entries[idx] = e;
        }
    }

    int import_file(const string& import_filename) {
        lock_guard<mutex> lock(mtx);
        ifstream fin(import_filename);
        if (!fin) {
            cerr << "Cannot open import file: " << import_filename << endl;
            return 0;
        }
        string line;
        int count = 0;
        while (getline(fin, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            Entry e;
            if (parse_csv_line(line, e)) {
                apply_placeholders(e);
                entries.push_back(e);
                ++count;
            } else {
                cerr << "Skipping malformed line: " << line << endl;
            }
        }
        return count;
    }

    int count() const {
        lock_guard<mutex> lock(mtx);
        return (int)entries.size();
    }

    Entry get(int i) const {
        lock_guard<mutex> lock(mtx);
        if (i >= 0 && i < (int)entries.size())
            return entries[i];
        return Entry();
    }

    void export_plain_csv(const string& filename) {
        lock_guard<mutex> lock(mtx);
        ofstream out(filename);
        if (!out) {
            cerr << "Failed to open " << filename << " for writing" << endl;
            return;
        }
        for (const auto& e : entries) {
            out << entry_to_csv(e) << "\n";
        }
        cout << "Plain CSV exported to " << filename << " (" << entries.size() << " entries)" << endl;
    }
};

class NetworkServer {
    PasswordManager* pm;
    atomic<bool> running;
    thread server_thread;
#ifdef _WIN32
    SOCKET listen_fd;
    vector<SOCKET> clients;
    WSADATA wsaData;
#else
    int listen_fd;
    vector<int> clients;
#endif

    void send_to_client(int fd, const string& msg) {
#ifdef _WIN32
        send(fd, msg.c_str(), msg.size(), 0);
#else
        write(fd, msg.c_str(), msg.size());
#endif
    }

    void handle_client(int fd) {
        char buf[4096];
        string cmd;
        while (running) {
            int n;
#ifdef _WIN32
            n = recv(fd, buf, sizeof(buf)-1, 0);
#else
            n = read(fd, buf, sizeof(buf)-1);
#endif
            if (n <= 0) break;
            buf[n] = '\0';
            cmd += buf;
            size_t pos;
            while ((pos = cmd.find('\n')) != string::npos) {
                string line = cmd.substr(0, pos);
                cmd.erase(0, pos+1);
                if (line == "EXPORT") {
                    string raw = pm->export_raw();
                    string resp = to_string(raw.size()) + "\n" + raw;
                    send_to_client(fd, resp);
                } else if (line == "IMPORT") {
                    if ((pos = cmd.find('\n')) == string::npos) break;
                    string size_str = cmd.substr(0, pos);
                    cmd.erase(0, pos+1);
                    size_t sz = stoull(size_str);
                    if (cmd.size() < sz) break;
                    string raw_data = cmd.substr(0, sz);
                    cmd.erase(0, sz);
                    if (pm->import_raw(raw_data))
                        send_to_client(fd, "OK\n");
                    else
                        send_to_client(fd, "ERROR\n");
                } else if (line == "PING") {
                    send_to_client(fd, "PONG\n");
                } else {
                    send_to_client(fd, "UNKNOWN\n");
                }
            }
        }
#ifdef _WIN32
        closesocket(fd);
#else
        close(fd);
#endif
    }

public:
    NetworkServer(PasswordManager* p) : pm(p), running(false) {}

    bool start() {
#ifdef _WIN32
        if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) return false;
        listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd == INVALID_SOCKET) return false;
#else
        listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd < 0) return false;
#endif
        int opt = 1;
#ifdef _WIN32
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
#else
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(3131);
        if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
#ifdef _WIN32
            closesocket(listen_fd);
            WSACleanup();
#else
            close(listen_fd);
#endif
            return false;
        }
        if (listen(listen_fd, 5) < 0) {
#ifdef _WIN32
            closesocket(listen_fd);
            WSACleanup();
#else
            close(listen_fd);
#endif
            return false;
        }
        running = true;
        server_thread = thread([this]() {
            struct timeval tv;
            fd_set readfds;
            int max_fd;
            while (running) {
                FD_ZERO(&readfds);
                FD_SET(listen_fd, &readfds);
                max_fd = listen_fd;
                for (int fd : clients) {
                    FD_SET(fd, &readfds);
                    if (fd > max_fd) max_fd = fd;
                }
                tv.tv_sec = 5;
                tv.tv_usec = 0;
                int activity;
#ifdef _WIN32
                activity = select(max_fd+1, &readfds, NULL, NULL, &tv);
#else
                activity = select(max_fd+1, &readfds, NULL, NULL, &tv);
#endif
                if (activity < 0) break;
                if (activity == 0) {
                    for (int fd : clients)
                        send_to_client(fd, "PING\n");
                    continue;
                }
                if (FD_ISSET(listen_fd, &readfds)) {
#ifdef _WIN32
                    SOCKET new_fd = accept(listen_fd, NULL, NULL);
#else
                    int new_fd = accept(listen_fd, NULL, NULL);
#endif
                    if (new_fd >= 0) {
                        clients.push_back(new_fd);
                        thread([this, new_fd]() { handle_client(new_fd); }).detach();
                    }
                }
                vector<int> still_alive;
                for (int fd : clients) {
                    int err;
                    socklen_t len = sizeof(err);
#ifdef _WIN32
                    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char*)&err, &len) == 0 && err == 0) {
                        still_alive.push_back(fd);
                    } else {
                        closesocket(fd);
                    }
#else
                    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
                        still_alive.push_back(fd);
                    } else {
                        close(fd);
                    }
#endif
                }
                clients = still_alive;
            }
#ifdef _WIN32
            for (SOCKET fd : clients) closesocket(fd);
            closesocket(listen_fd);
            WSACleanup();
#else
            for (int fd : clients) close(fd);
            close(listen_fd);
#endif
        });
        return true;
    }

    void stop() {
        running = false;
        if (server_thread.joinable())
            server_thread.join();
    }
};

int show_main_menu() {
    KeyReader kr;
    vector<string> options = {
        " View / Manage passwords ",
        " Add new password        ",
        " Import from CSV file    ",
        " Export plain CSV (unencrypted) ",
        " Save and exit           ",
        " Quit without saving     "
    };
    int selected = 0;
    const int cnt = (int)options.size();

    while (true) {
        clear_screen();
        auto [rows, cols] = term_size();
        int start_row = max(1, (rows - cnt - 2) / 2);
        int start_col = max(1, (cols - 30) / 2);

        move_cursor(start_row, start_col);
        cout << "=== Password Manager ===" << endl;
        for (int i = 0; i < cnt; ++i) {
            move_cursor(start_row + 2 + i, start_col);
            if (i == selected) {
                cout << "\033[7m" << options[i] << "\033[0m";
            } else {
                cout << options[i];
            }
            cout << endl;
        }
        move_cursor(start_row + cnt + 3, start_col);
        cout << "Use arrows + Enter, Esc to quit" << flush;

        int key = kr.getch();
        if (key == KEY_UP)           selected = (selected - 1 + cnt) % cnt;
        else if (key == KEY_DOWN)    selected = (selected + 1) % cnt;
        else if (key == KEY_ENTER)   return selected;
        else if (key == KEY_ESC)     return 5;
    }
}

void password_list_view(PasswordManager& pm) {
    KeyReader kr;
    int selected_row = 0;
    int selected_col = 0;
    const vector<string> col_names = {"Account","Mail","Username","Password"};

    auto draw = [&]() {
        clear_screen();
        auto [rows, cols] = term_size();
        int table_start_row = 2;
        int table_start_col = 2;

        move_cursor(1, 2);
        cout << "Entries (" << pm.count() << ")   "
             << "\033[2mArrows:move, Enter:copy, a:add, d:del, e:edit, i:import, Esc:menu\033[0m"
             << endl;

        move_cursor(table_start_row, table_start_col);
        cout << " #  Account      Mail         Username     Password" << endl;
        move_cursor(table_start_row+1, table_start_col);
        cout << "--------------------------------------------------" << endl;

        int visible_rows = min(pm.count(), rows - table_start_row - 5);
        int first = max(0, selected_row - visible_rows / 2);
        if (first + visible_rows > pm.count()) first = max(0, pm.count() - visible_rows);

        for (int i = 0; i < visible_rows; ++i) {
            int idx = first + i;
            Entry e = pm.get(idx);
            int row = table_start_row + 2 + i;
            move_cursor(row, table_start_col);

            cout << (idx == selected_row ? "\033[7m" : "");
            cout << (idx+1 < 10 ? " " : "") << (idx+1) << " ";

            auto print4 = [&](const string& s, int col_index) {
                string show = s.substr(0, 8);
                show.resize(8, ' ');
                if (idx == selected_row && col_index == selected_col)
                    cout << "\033[1;33m";
                cout << show;
                if (idx == selected_row)
                    cout << "\033[0m";
            };

            print4(e.account, 0);   cout << "  ";
            print4(e.mail, 1);      cout << "  ";
            print4(e.username, 2);  cout << "  ";
            if (idx == selected_row && selected_col == 3)
                cout << "\033[1;33m****\033[0m";
            else if (idx == selected_row)
                cout << "****";
            else
                cout << "****";
            if (idx == selected_row) cout << "\033[0m";
        }
        move_cursor(table_start_row + 2 + visible_rows + 1, table_start_col);
        cout << "Selected: " << col_names[selected_col]
             << " (Enter to copy)" << flush;
    };

    draw();
    while (true) {
        int key = kr.getch();
        bool redraw = true;
        switch (key) {
            case KEY_UP:
                if (selected_row > 0) --selected_row;
                break;
            case KEY_DOWN:
                if (selected_row < pm.count()-1) ++selected_row;
                break;
            case KEY_LEFT:
                selected_col = (selected_col - 1 + 4) % 4;
                break;
            case KEY_RIGHT:
                selected_col = (selected_col + 1) % 4;
                break;
            case KEY_ENTER: {
                if (pm.count() > 0 && selected_row < pm.count()) {
                    Entry e = pm.get(selected_row);
                    string val;
                    switch (selected_col) {
                        case 0: val = e.account; break;
                        case 1: val = e.mail; break;
                        case 2: val = e.username; break;
                        case 3: val = e.password; break;
                    }
                    copy_to_clipboard(val);
                    move_cursor(term_size().first, 1);
                    cout << "\033[K\033[1;32mCopied " << col_names[selected_col]
                         << " to clipboard.\033[0m" << flush;
                    this_thread::sleep_for(800ms);
                }
                break;
            }
            case 'a': {
                clear_screen();
                cout << "--- Add new entry ---" << endl;
                Entry e;
                e.account = kr.read_line("Account: ", true);
                e.mail    = kr.read_line("Mail: ", true);
                e.username= kr.read_line("Username: ", true);
                e.password= kr.read_line("Password: ", false);
                pm.add(e);
                if (selected_row >= pm.count()) selected_row = pm.count()-1;
                break;
            }
            case 'd': {
                if (pm.count() > 0) {
                    clear_screen();
                    Entry e = pm.get(selected_row);
                    cout << "Delete entry #" << selected_row+1
                         << " (\"" << e.account.substr(0,10)
                         << "...\")? (y/n) " << flush;
                    int k = kr.getch();
                    if (k == 'y' || k == 'Y') {
                        pm.remove(selected_row);
                        if (selected_row >= pm.count() && selected_row > 0)
                            --selected_row;
                    }
                }
                break;
            }
            case 'e': {
                if (pm.count() > 0) {
                    Entry cur = pm.get(selected_row);
                    clear_screen();
                    cout << "--- Edit entry #" << selected_row+1 << " ---\n"
                         << "Leave blank to set to placeholder (empty), or type new value.\n";

                    string inp = kr.read_line("Account [" + cur.account + "]: ", true);
                    if (!inp.empty()) cur.account = inp;
                    else cur.account = PLACEHOLDER_ACCOUNT;

                    inp = kr.read_line("Mail [" + cur.mail + "]: ", true);
                    if (!inp.empty()) cur.mail = inp;
                    else cur.mail = PLACEHOLDER_MAIL;

                    inp = kr.read_line("Username [" + cur.username + "]: ", true);
                    if (!inp.empty()) cur.username = inp;
                    else cur.username = PLACEHOLDER_USERNAME;

                    string pwd = kr.read_line("New password (blank = NoPassword): ", false);
                    if (!pwd.empty()) cur.password = pwd;
                    else cur.password = PLACEHOLDER_PASSWORD;

                    pm.update(selected_row, cur);
                }
                break;
            }
            case 'i': {
                clear_screen();
                string imp_file = kr.read_line("Enter filename to import (quoted CSV format): ", true);
                int count = pm.import_file(imp_file);
                clear_screen();
                cout << "Imported " << count << " entries." << endl;
                this_thread::sleep_for(1500ms);
                if (selected_row >= pm.count() && pm.count() > 0)
                    selected_row = pm.count() - 1;
                break;
            }
            case KEY_ESC:
                return;
            default:
                redraw = false;
                break;
        }
        if (redraw) draw();
    }
}

int main() {
    cout << "Encoded password file name (with extension): " << flush;
    string file_name;
    getline(cin, file_name);

    KeyReader kr_temp;
    string master_key = kr_temp.read_line("Master password: ", false);
    if (master_key.empty()) {
        cerr << "Password cannot be empty." << endl;
        return 1;
    }

    PasswordManager pm(file_name, master_key);
    if (!pm.load()) {
        cerr << "Failed to decrypt or parse file." << endl;
        return 1;
    }

    NetworkServer net(&pm);
    if (!net.start()) {
        cerr << "Warning: Could not start network server on port 3131" << endl;
    } else {
        cout << "Network server running on port 3131 (EXPORT/IMPORT, PING every 5s)" << endl;
    }

    while (true) {
        int choice = show_main_menu();
        switch (choice) {
            case 0:
                password_list_view(pm);
                break;
            case 1: {
                clear_screen();
                cout << "--- Add new password ---" << endl;
                KeyReader kr;
                Entry e;
                e.account  = kr.read_line("Account: ", true);
                e.mail     = kr.read_line("Mail: ", true);
                e.username = kr.read_line("Username: ", true);
                e.password = kr.read_line("Password: ", false);
                pm.add(e);
                cout << "Entry added." << endl;
                this_thread::sleep_for(800ms);
                break;
            }
            case 2: {
                clear_screen();
                KeyReader kr;
                string imp = kr.read_line("Enter filename to import: ", true);
                int cnt = pm.import_file(imp);
                clear_screen();
                cout << "Imported " << cnt << " entries." << endl;
                this_thread::sleep_for(1500ms);
                break;
            }
            case 3: {
                clear_screen();
                KeyReader kr;
                string export_file = kr.read_line("Enter filename for plain CSV export (e.g., export.txt): ", true);
                pm.export_plain_csv(export_file);
                this_thread::sleep_for(1500ms);
                break;
            }
            case 4: {
                if (pm.save())
                    cout << "Data saved to " << file_name << endl;
                else
                    cerr << "Save failed!" << endl;
                net.stop();
                return 0;
            }
            case 5:
                cout << "Exiting without saving." << endl;
                net.stop();
                return 0;
            default:
                net.stop();
                return 0;
        }
    }
}


























































// 67...🤤

// 69 better.
