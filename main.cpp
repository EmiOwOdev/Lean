// lean v0.1.2
// Fixed mouse-wheel scrolling + consistent line-number gutter

#include <iostream>
#include <string>
#include <vector>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <fstream>
#include <cstdlib>
#include <cerrno>
#include <algorithm>   

bool readByte(char& c) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);

    timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;

    if (select(STDIN_FILENO + 1, &set, nullptr, nullptr, &timeout) > 0) {
        return read(STDIN_FILENO, &c, 1) == 1;
    }
    return false;
}

class TextBuffer {
public:
    std::vector<std::string> lines;
    int cursorLine = 0;
    int cursorColumn = 0;

    bool selecting = false;
    int selectionStartLine = 0;
    int selectionStartColumn = 0;
    int selectionEndLine = 0;
    int selectionEndColumn = 0;
    bool hasSelection = false;

    void deleteSelection() {
        int startLine = selectionStartLine;
        int startColumn = selectionStartColumn;
        int endLine = selectionEndLine;
        int endColumn = selectionEndColumn;

        if (startLine > endLine || (startLine == endLine && startColumn > endColumn)) {
            std::swap(startLine, endLine);
            std::swap(startColumn, endColumn);
        }

        if (startLine == endLine) {
            lines[startLine].erase(startColumn, endColumn - startColumn);
        } else {
            std::string remaining = lines[endLine].substr(endColumn);
            lines[startLine].erase(startColumn);
            lines[startLine] += remaining;
            lines.erase(lines.begin() + startLine + 1, lines.begin() + endLine + 1);
        }

        cursorLine = startLine;
        cursorColumn = startColumn;
        selecting = false;
        hasSelection = false;
    }

    bool isSelected(int line, int column) const {
        if (!hasSelection) return false;

        int startLine = selectionStartLine;
        int startColumn = selectionStartColumn;
        int endLine = selectionEndLine;
        int endColumn = selectionEndColumn;

        if (startLine > endLine || (startLine == endLine && startColumn > endColumn)) {
            std::swap(startLine, endLine);
            std::swap(startColumn, endColumn);
        }

        if (line < startLine || line > endLine) return false;

        if (startLine == endLine) {
            return column >= startColumn && column < endColumn;
        }

        if (line == startLine) return column >= startColumn;
        if (line == endLine) return column < endColumn;

        return true;
    }

    void insertChar(char c) {
        lines[cursorLine].insert(cursorColumn, 1, c);
        cursorColumn++;
    }

    void backspace() {
        std::string& line = lines[cursorLine];

        if (cursorColumn > 0) {
            line.erase(cursorColumn - 1, 1);
            cursorColumn--;
            return;
        }

        if (cursorLine > 0) {
            int previousLength = lines[cursorLine - 1].size();
            lines[cursorLine - 1] += line;
            lines.erase(lines.begin() + cursorLine);
            cursorLine--;
            cursorColumn = previousLength;
        }
    }

    void enter() {
        std::string& line = lines[cursorLine];
        std::string newLine = line.substr(cursorColumn);
        line.erase(cursorColumn);
        lines.insert(lines.begin() + cursorLine + 1, newLine);
        cursorLine++;
        cursorColumn = 0;
    }
};


int lineNumberWidth(const TextBuffer& buffer) {
    int n = std::max(1, static_cast<int>(buffer.lines.size()));
    int w = 1;
    while (n >= 10) {
        n /= 10;
        ++w;
    }
    return w;
}

void render(const TextBuffer& buffer, const std::string& statusMessage, int scrollOffset) {
    struct winsize size;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &size);

    int terminalRows = size.ws_row;
    int terminalCols = size.ws_col;

    if (terminalRows < 5) terminalRows = 5;
    if (terminalCols < 20) terminalCols = 20;

    int textRows = terminalRows - 4;
    int gutter = lineNumberWidth(buffer);          
    int textStartCol = gutter + 4;                

    std::cout << "\033[H";
    std::cout << " Lean";
    if (!statusMessage.empty()) {
        std::cout << " " << statusMessage;
    }
    std::cout << "\033[K\n";

    for (int i = 0; i < terminalCols; i++) std::cout << "─";
    std::cout << "\n";

    for (int i = 0; i < textRows; i++) {
        int lineIndex = scrollOffset + i;
        std::cout << "\033[K";

        if (lineIndex < static_cast<int>(buffer.lines.size())) {
    
            std::string num = std::to_string(lineIndex + 1);
            std::cout << std::string(gutter - num.size(), ' ') << num << " │ ";

            for (int c = 0; c < static_cast<int>(buffer.lines[lineIndex].size()); c++) {
                if (buffer.isSelected(lineIndex, c)) {
                    std::cout << "\033[7m";
                }
                std::cout << buffer.lines[lineIndex][c];
                if (buffer.isSelected(lineIndex, c)) {
                    std::cout << "\033[0m";
                }
            }
        }
        std::cout << '\n';
    }

    for (int i = 0; i < terminalCols; i++) std::cout << "─";
    std::cout << "\033[K";

    std::cout << "\033[" << terminalRows << ";1H";
    std::cout << " Lean"
              << " Ln " << buffer.cursorLine + 1
              << ", Col " << buffer.cursorColumn + 1;

    
    std::cout << "\033[" << (buffer.cursorLine - scrollOffset + 3) << ";"
              << (buffer.cursorColumn + textStartCol) << "H";
    std::cout << "\033[?25h";
    std::cout.flush();
}

std::string getFilename() {
    std::string filename;
    char c;

    std::cout << "\nSave as: ";
    std::cout.flush();

    while (true) {
        if (read(STDIN_FILENO, &c, 1) != 1) continue;

        if (c == '\n') break;

        if (c == 127) {
            if (!filename.empty()) {
                filename.pop_back();
                std::cout << "\b \b";
                std::cout.flush();
            }
        }
        else if (c >= 32 && c <= 126) {
            filename += c;
            std::cout << c;
            std::cout.flush();
        }
    }

    return filename;
}

std::string saveFile(const TextBuffer& buffer, const std::string& filename) {
    if (filename.empty()) return "Save failed: no filename";

    std::ofstream file(filename);
    if (!file) return "Save failed: " + filename;

    for (const std::string& line : buffer.lines)
        file << line << '\n';

    if (!file) return "Save failed: " + filename;

    return "Saved: " + filename;
}

bool loadFile(TextBuffer& buffer, const std::string& filename) {
    std::ifstream file(filename);
    if (!file) return false;

    buffer.lines.clear();
    std::string line;

    while (std::getline(file, line))
        buffer.lines.push_back(line);

    if (buffer.lines.empty())
        buffer.lines.push_back("");

    buffer.cursorLine = 0;
    buffer.cursorColumn = 0;
    return true;
}

bool buildFile(const std::string& filename) {
    size_t dot = filename.find_last_of('.');
    if (dot == std::string::npos || filename.substr(dot) != ".cpp") {
        return false;
    }

    std::string outputName = filename.substr(0, dot);
    std::string command = "g++ \"" + filename + "\" -o \"" + outputName + "\"";

    return system(command.c_str()) == 0;
}

bool runFile(const std::string& filename) {
    size_t dot = filename.find_last_of('.');
    if (dot == std::string::npos || filename.substr(dot) != ".cpp") {
        return false;
    }

    std::string outputName = filename.substr(0, dot);
    std::string command = "xfce4-terminal --hold "
                          "--title=\"Lean: " + outputName + "\" "
                          "--command=\"./" + outputName + "\"";

    return system(command.c_str()) == 0;
}

int main(int argc, char* argv[]) {
    std::cout << "\033[?1049h";
    std::cout << "\033[?25l";
    std::cout << "\033[?1000h";
    std::cout.flush();

    termios original;
    tcgetattr(STDIN_FILENO, &original);

    termios raw = original;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_iflag &= ~(IXON | IXOFF);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    TextBuffer buffer;
    buffer.lines.push_back("");

    std::string statusMessage;
    std::string currentFilename;
    int scrollOffset = 0;

    if (argc > 1) {
        if (loadFile(buffer, argv[1])) {
            currentFilename = argv[1];
            statusMessage = "Opened: " + currentFilename;
        } else {
            statusMessage = "Could not open: " + std::string(argv[1]);
        }
    }

    render(buffer, statusMessage, scrollOffset);

    while (true) {
        char c;
        if (read(STDIN_FILENO, &c, 1) != 1) continue;

        if (c == 127) {
            if (buffer.hasSelection) {
                buffer.deleteSelection();
            } else {
                buffer.backspace();
            }
        }

        if (c >= 32 && c <= 126) {
            buffer.insertChar(c);
        }

        if (c == 19) {  // Ctrl+S
            if (currentFilename.empty())
                currentFilename = getFilename();
            statusMessage = saveFile(buffer, currentFilename);
        }

        if (c == 2) {   // Ctrl+B
            if (currentFilename.empty()) {
                statusMessage = "No file to build";
            } else {
                statusMessage = "Building...";
                render(buffer, statusMessage, scrollOffset);

                if (buildFile(currentFilename))
                    statusMessage = "Build successful";
                else
                    statusMessage = "Build failed";
            }
        }

        if (c == 18) {  // Ctrl+R
            if (currentFilename.empty()) {
                statusMessage = "No file to run";
            } else {
                if (runFile(currentFilename))
                    statusMessage = "Program launched";
                else
                    statusMessage = "Could not launch program";
            }
        }

        if (c == 27) {
            char sequence[5];
            if (!readByte(sequence[0])) continue;
            if (sequence[0] != '[') continue;
            if (!readByte(sequence[1])) continue;

            if (sequence[1] == 'M') {
                unsigned char button;
                unsigned char mouseX;
                unsigned char mouseY;

                if (read(STDIN_FILENO, &button, 1) != 1) continue;
                if (read(STDIN_FILENO, &mouseX, 1) != 1) continue;
                if (read(STDIN_FILENO, &mouseY, 1) != 1) continue;

                int x = mouseX - 32;
                int y = mouseY - 32;

             
                if (button == 96) {          
                    if (scrollOffset > 0)
                        --scrollOffset;
                    render(buffer, statusMessage, scrollOffset);
                    continue;
                }
                if (button == 97) {          
                    struct winsize size;
                    ioctl(STDOUT_FILENO, TIOCGWINSZ, &size);
                    int textRows = size.ws_row - 4;
                    if (textRows < 1) textRows = 1;
                    int maxScroll = std::max(0, static_cast<int>(buffer.lines.size()) - textRows);
                    if (scrollOffset < maxScroll)
                        ++scrollOffset;
                    render(buffer, statusMessage, scrollOffset);
                    continue;
                }

                
                int gutter = lineNumberWidth(buffer);
                int textStartCol = gutter + 4;

                int clickedLine = scrollOffset + (y - 3);
                int clickedColumn = x - textStartCol;

                if ((button & 3) == 0) {                
                    if (clickedLine < 0 || clickedLine >= static_cast<int>(buffer.lines.size()))
                        continue;

                    if (clickedColumn < 0) clickedColumn = 0;
                    int length = buffer.lines[clickedLine].size();
                    if (clickedColumn > length) clickedColumn = length;

                    buffer.selecting = true;
                    buffer.hasSelection = false;
                    buffer.selectionStartLine = clickedLine;
                    buffer.selectionStartColumn = clickedColumn;
                    buffer.selectionEndLine = clickedLine;
                    buffer.selectionEndColumn = clickedColumn;
                    buffer.cursorLine = clickedLine;
                    buffer.cursorColumn = clickedColumn;
                }
                else if (button & 32) {                
                    if (buffer.selecting) {
                        if (clickedLine >= 0 && clickedLine < static_cast<int>(buffer.lines.size())) {
                            if (clickedColumn < 0) clickedColumn = 0;
                            int length = buffer.lines[clickedLine].size();
                            if (clickedColumn > length) clickedColumn = length;

                            buffer.selectionEndLine = clickedLine;
                            buffer.selectionEndColumn = clickedColumn;
                            buffer.cursorLine = clickedLine;
                            buffer.cursorColumn = clickedColumn;

                            if (clickedLine != buffer.selectionStartLine ||
                                clickedColumn != buffer.selectionStartColumn) {
                                buffer.hasSelection = true;
                            }
                        }
                    }
                }
                else if ((button & 3) == 3) {           
                    buffer.selecting = false;
                    if (buffer.selectionStartLine != buffer.selectionEndLine ||
                        buffer.selectionStartColumn != buffer.selectionEndColumn) {
                        buffer.hasSelection = true;
                    }
                }

                render(buffer, statusMessage, scrollOffset);
                continue;
            }

            // Arrow keys
            if (sequence[1] == 'A' && buffer.cursorLine > 0) {
                buffer.cursorLine--;
                if (buffer.cursorColumn > static_cast<int>(buffer.lines[buffer.cursorLine].size())) {
                    buffer.cursorColumn = buffer.lines[buffer.cursorLine].size();
                }
            }

            if (sequence[1] == 'B' && buffer.cursorLine < static_cast<int>(buffer.lines.size()) - 1) {
                buffer.cursorLine++;
                if (buffer.cursorColumn > static_cast<int>(buffer.lines[buffer.cursorLine].size())) {
                    buffer.cursorColumn = buffer.lines[buffer.cursorLine].size();
                }
            }

            if (sequence[1] == 'C' && buffer.cursorColumn < static_cast<int>(buffer.lines[buffer.cursorLine].size())) {
                buffer.cursorColumn++;
            }

            if (sequence[1] == 'D' && buffer.cursorColumn > 0) {
                buffer.cursorColumn--;
            }

            // Ctrl+End 
            if (sequence[1] == '1') {
                if (read(STDIN_FILENO, &sequence[2], 1) != 1) continue;
                if (read(STDIN_FILENO, &sequence[3], 1) != 1) continue;
                if (read(STDIN_FILENO, &sequence[4], 1) != 1) continue;

                if (sequence[2] == ';' && sequence[3] == '5' && sequence[4] == 'F') {
                    break;
                }
            }
        }

        if (c == '\n') {
            buffer.enter();
        }

        
        struct winsize size;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &size);
        int textRows = size.ws_row - 4;
        if (textRows < 1) textRows = 1;

        if (buffer.cursorLine < scrollOffset) {
            scrollOffset = buffer.cursorLine;
        }
        if (buffer.cursorLine >= scrollOffset + textRows) {
            scrollOffset = buffer.cursorLine - textRows + 1;
        }

        render(buffer, statusMessage, scrollOffset);
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
    std::cout << "\033[?1000l";
    std::cout << "\033[?25h";
    std::cout << "\033[?1049l";
    std::cout.flush();

    return 0;
}