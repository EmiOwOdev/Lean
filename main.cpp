#include <iostream>
#include <string>
#include <vector>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <fstream>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <cstdio>

bool readByte(char& c) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);

    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;

    if (select(STDIN_FILENO + 1, &set, nullptr, nullptr, &timeout) > 0)
        return read(STDIN_FILENO, &c, 1) == 1;

    return false;
}

enum class DisplayServer {
    X11,
    Wayland,
    Unknown
};

DisplayServer detectDisplayServer() {
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    const char* x11 = std::getenv("DISPLAY");

    if (wayland && *wayland)
        return DisplayServer::Wayland;

    if (x11 && *x11)
        return DisplayServer::X11;

    return DisplayServer::Unknown;
}

enum class ClipboardBackend {
    WlClipboard,
    Xclip,
    Xsel,
    None
};

bool commandExists(const std::string& command) {
    std::string check = "command -v " + command + " >/dev/null 2>&1";
    return system(check.c_str()) == 0;
}

ClipboardBackend detectClipboardBackend(DisplayServer displayServer) {
    if (displayServer == DisplayServer::Wayland &&
        commandExists("wl-copy") &&
        commandExists("wl-paste")) {
        return ClipboardBackend::WlClipboard;
    }

    if (displayServer == DisplayServer::X11) {
        if (commandExists("xclip"))
            return ClipboardBackend::Xclip;

        if (commandExists("xsel"))
            return ClipboardBackend::Xsel;
    }

    if (commandExists("wl-copy") &&
        commandExists("wl-paste")) {
        return ClipboardBackend::WlClipboard;
    }

    if (commandExists("xclip"))
        return ClipboardBackend::Xclip;

    if (commandExists("xsel"))
        return ClipboardBackend::Xsel;

    return ClipboardBackend::None;
}

bool systemClipboardCopy(
    const std::string& text,
    ClipboardBackend backend
) {
    if (backend == ClipboardBackend::None)
        return false;

    FILE* pipe = nullptr;

    switch (backend) {
        case ClipboardBackend::WlClipboard:
            pipe = popen("wl-copy", "w");
            break;

        case ClipboardBackend::Xclip:
            pipe = popen("xclip -selection clipboard", "w");
            break;

        case ClipboardBackend::Xsel:
            pipe = popen("xsel --clipboard --input", "w");
            break;

        default:
            return false;
    }

    if (!pipe)
        return false;

    fwrite(text.data(), 1, text.size(), pipe);

    return pclose(pipe) == 0;
}

std::string systemClipboardPaste(ClipboardBackend backend) {
    if (backend == ClipboardBackend::None)
        return "";

    FILE* pipe = nullptr;

    switch (backend) {
        case ClipboardBackend::WlClipboard:
            pipe = popen("wl-paste --no-newline", "r");
            break;

        case ClipboardBackend::Xclip:
            pipe = popen("xclip -selection clipboard -o", "r");
            break;

        case ClipboardBackend::Xsel:
            pipe = popen("xsel --clipboard --output", "r");
            break;

        default:
            return "";
    }

    if (!pipe)
        return "";

    std::string result;
    char buffer[4096];

    while (fgets(buffer, sizeof(buffer), pipe))
        result += buffer;

    pclose(pipe);

    return result;
}

class TextBuffer {
public:
    std::vector<std::string> lines;

    int cursorLine = 0;
    int cursorColumn = 0;

    bool selecting = false;
    bool hasSelection = false;

    int selectionStartLine = 0;
    int selectionStartColumn = 0;
    int selectionEndLine = 0;
    int selectionEndColumn = 0;

    std::string clipboard;

    std::vector<std::vector<std::string>> undoStack;
    std::vector<std::vector<std::string>> redoStack;

    void saveUndoState() {
        undoStack.push_back(lines);
        redoStack.clear();
    }

    void undo() {
        if (undoStack.empty())
            return;

        redoStack.push_back(lines);
        lines = undoStack.back();
        undoStack.pop_back();

        if (lines.empty())
            lines.push_back("");

        if (cursorLine >= static_cast<int>(lines.size()))
            cursorLine = static_cast<int>(lines.size()) - 1;

        if (cursorColumn > static_cast<int>(lines[cursorLine].size()))
            cursorColumn = lines[cursorLine].size();

        selecting = false;
        hasSelection = false;
    }

    void redo() {
        if (redoStack.empty())
            return;

        undoStack.push_back(lines);
        lines = redoStack.back();
        redoStack.pop_back();

        if (lines.empty())
            lines.push_back("");

        if (cursorLine >= static_cast<int>(lines.size()))
            cursorLine = static_cast<int>(lines.size()) - 1;

        if (cursorColumn > static_cast<int>(lines[cursorLine].size()))
            cursorColumn = lines[cursorLine].size();

        selecting = false;
        hasSelection = false;
    }

    void deleteSelection() {
        if (!hasSelection)
            return;

        saveUndoState();

        int startLine = selectionStartLine;
        int startColumn = selectionStartColumn;
        int endLine = selectionEndLine;
        int endColumn = selectionEndColumn;

        if (startLine > endLine ||
            (startLine == endLine && startColumn > endColumn)) {
            std::swap(startLine, endLine);
            std::swap(startColumn, endColumn);
        }

        if (startLine == endLine) {
            lines[startLine].erase(
                startColumn,
                endColumn - startColumn
            );
        } else {
            std::string remaining = lines[endLine].substr(endColumn);

            lines[startLine].erase(startColumn);
            lines[startLine] += remaining;

            lines.erase(
                lines.begin() + startLine + 1,
                lines.begin() + endLine + 1
            );
        }

        cursorLine = startLine;
        cursorColumn = startColumn;

        selecting = false;
        hasSelection = false;
    }

    void selectAll() {
        if (lines.empty())
            return;

        selectionStartLine = 0;
        selectionStartColumn = 0;

        selectionEndLine = lines.size() - 1;
        selectionEndColumn = lines.back().size();

        hasSelection = true;
        selecting = false;
    }

    bool isSelected(int line, int column) const {
        if (!hasSelection)
            return false;

        int startLine = selectionStartLine;
        int startColumn = selectionStartColumn;
        int endLine = selectionEndLine;
        int endColumn = selectionEndColumn;

        if (startLine > endLine ||
            (startLine == endLine && startColumn > endColumn)) {
            std::swap(startLine, endLine);
            std::swap(startColumn, endColumn);
        }

        if (line < startLine || line > endLine)
            return false;

        if (startLine == endLine)
            return column >= startColumn && column < endColumn;

        if (line == startLine)
            return column >= startColumn;

        if (line == endLine)
            return column < endColumn;

        return true;
    }

    void insertChar(char c) {
        saveUndoState();

        std::string& line = lines[cursorLine];

        if (c == '{') {
            line.insert(cursorColumn, 1, '{');
            cursorColumn++;

            int newLine = cursorLine + 1;

            lines.insert(
                lines.begin() + newLine,
                ""
            );

            int indentation = calculateIndentation(newLine);

            lines[newLine] = std::string(indentation, ' ');

            lines.insert(
                lines.begin() + newLine + 1,
                std::string(
                    indentation >= 4 ? indentation - 4 : 0,
                    ' '
                ) + "}"
            );

            cursorLine = newLine;
            cursorColumn = indentation;

            return;
        }

        char closing = '\0';

        switch (c) {
            case '(':
                closing = ')';
                break;

            case '[':
                closing = ']';
                break;

            case '"':
                closing = '"';
                break;

            case '\'':
                closing = '\'';
                break;
        }

        if (closing != '\0') {
            line.insert(cursorColumn, 1, c);
            line.insert(cursorColumn + 1, 1, closing);
            cursorColumn++;
            return;
        }

        if ((c == ')' || c == ']' || c == '"' || c == '\'') &&
            cursorColumn < static_cast<int>(line.size()) &&
            line[cursorColumn] == c) {
            cursorColumn++;
            return;
        }

        line.insert(cursorColumn, 1, c);
        cursorColumn++;
    }

    void backspace() {
        saveUndoState();

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

    bool isWordBoundary(char c) const {
        return !std::isalnum(static_cast<unsigned char>(c)) && c != '_';
    }

    bool containsKeyword(
        const std::string& line,
        const std::string& keyword
    ) const {
        size_t pos = line.find(keyword);

        while (pos != std::string::npos) {
            bool leftBoundary =
                pos == 0 || isWordBoundary(line[pos - 1]);

            bool rightBoundary =
                pos + keyword.size() >= line.size() ||
                isWordBoundary(line[pos + keyword.size()]);

            if (leftBoundary && rightBoundary)
                return true;

            pos = line.find(keyword, pos + 1);
        }

        return false;
    }

    int calculateIndentation(int targetLine) const {
        int level = 0;
        bool inBlockComment = false;

        for (int i = 0; i < targetLine; i++) {
            const std::string& line = lines[i];

            bool inString = false;
            bool inChar = false;
            bool escaped = false;

            for (size_t j = 0; j < line.size(); j++) {
                char c = line[j];

                if (inBlockComment) {
                    if (c == '*' &&
                        j + 1 < line.size() &&
                        line[j + 1] == '/') {
                        inBlockComment = false;
                        j++;
                    }

                    continue;
                }

                if (!inString &&
                    !inChar &&
                    c == '/' &&
                    j + 1 < line.size() &&
                    line[j + 1] == '/') {
                    break;
                }

                if (!inString &&
                    !inChar &&
                    c == '/' &&
                    j + 1 < line.size() &&
                    line[j + 1] == '*') {
                    inBlockComment = true;
                    j++;
                    continue;
                }

                if (!inChar && c == '"' && !escaped) {
                    inString = !inString;
                    continue;
                }

                if (!inString && c == '\'' && !escaped) {
                    inChar = !inChar;
                    continue;
                }

                if ((inString || inChar) &&
                    c == '\\' &&
                    !escaped) {
                    escaped = true;
                    continue;
                }

                escaped = false;

                if (inString || inChar)
                    continue;

                if (c == '{')
                    level++;
                else if (c == '}' && level > 0)
                    level--;
            }
        }

        if (targetLine < static_cast<int>(lines.size())) {
            std::string trimmed = lines[targetLine];

            size_t first = trimmed.find_first_not_of(' ');

            if (first != std::string::npos)
                trimmed = trimmed.substr(first);

            if (!trimmed.empty() && trimmed[0] == '}' && level > 0)
                level--;

            if ((containsKeyword(trimmed, "else") ||
                 containsKeyword(trimmed, "catch")) &&
                level > 0) {
                level--;
            }
        }

        return level * 4;
    }

    void enter() {
        saveUndoState();

        std::string& line = lines[cursorLine];

        std::string afterCursor = line.substr(cursorColumn);

        line.erase(cursorColumn);

        size_t firstNonSpace = afterCursor.find_first_not_of(' ');

        if (firstNonSpace == std::string::npos)
            afterCursor.clear();
        else
            afterCursor = afterCursor.substr(firstNonSpace);

        int newLineIndex = cursorLine + 1;

        lines.insert(
            lines.begin() + newLineIndex,
            afterCursor
        );

        cursorLine = newLineIndex;

        int indentation = calculateIndentation(cursorLine);

        lines[cursorLine].insert(
            0,
            indentation,
            ' '
        );

        cursorColumn = indentation;
    }

    void copySelection() {
        if (!hasSelection)
            return;

        int startLine = selectionStartLine;
        int startColumn = selectionStartColumn;
        int endLine = selectionEndLine;
        int endColumn = selectionEndColumn;

        if (startLine > endLine ||
            (startLine == endLine && startColumn > endColumn)) {
            std::swap(startLine, endLine);
            std::swap(startColumn, endColumn);
        }

        clipboard.clear();

        if (startLine == endLine) {
            clipboard = lines[startLine].substr(
                startColumn,
                endColumn - startColumn
            );
            return;
        }

        clipboard = lines[startLine].substr(startColumn);
        clipboard += '\n';

        for (int i = startLine + 1; i < endLine; i++) {
            clipboard += lines[i];
            clipboard += '\n';
        }

        clipboard += lines[endLine].substr(0, endColumn);
    }

    void cutSelection() {
        if (!hasSelection)
            return;

        copySelection();
        deleteSelection();
    }

    void paste() {
        if (clipboard.empty())
            return;

        if (hasSelection)
            deleteSelection();

        std::string before =
            lines[cursorLine].substr(0, cursorColumn);

        std::string after =
            lines[cursorLine].substr(cursorColumn);

        size_t start = 0;
        size_t newline;
        bool firstLine = true;

        while ((newline = clipboard.find('\n', start)) != std::string::npos) {
            std::string part =
                clipboard.substr(start, newline - start);

            if (firstLine) {
                lines[cursorLine] = before + part;
                firstLine = false;
            } else {
                lines.insert(
                    lines.begin() + cursorLine + 1,
                    part
                );
                cursorLine++;
            }

            start = newline + 1;
        }

        std::string lastPart = clipboard.substr(start);

        if (firstLine) {
            lines[cursorLine] =
                before + lastPart + after;

            cursorColumn += lastPart.size();
        } else {
            lines.insert(
                lines.begin() + cursorLine + 1,
                lastPart + after
            );

            cursorLine++;
            cursorColumn = lastPart.size();
        }

        selecting = false;
        hasSelection = false;
    }
};

int lineNumberWidth(const TextBuffer& buffer) {
    int n = std::max(
        1,
        static_cast<int>(buffer.lines.size())
    );

    int width = 1;

    while (n >= 10) {
        n /= 10;
        width++;
    }

    return width;
}

void render(
    const TextBuffer& buffer,
    const std::string& statusMessage,
    int scrollOffset
) {
    struct winsize size;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &size);

    int terminalRows = std::max(5, static_cast<int>(size.ws_row));
    int terminalCols = std::max(20, static_cast<int>(size.ws_col));

    int textRows = terminalRows - 4;
    int gutter = lineNumberWidth(buffer);
    int textStartCol = gutter + 4;

    std::cout << "\033[H";
    std::cout << " Lean";

    if (!statusMessage.empty())
        std::cout << " " << statusMessage;

    std::cout << "\033[K\n";

    for (int i = 0; i < terminalCols; i++)
        std::cout << "─";

    std::cout << "\n";

    for (int i = 0; i < textRows; i++) {
        int lineIndex = scrollOffset + i;

        std::cout << "\033[K";

        if (lineIndex < static_cast<int>(buffer.lines.size())) {
            std::string num =
                std::to_string(lineIndex + 1);

            std::cout
                << std::string(gutter - num.size(), ' ')
                << num
                << " │ ";

            for (
                int c = 0;
                c < static_cast<int>(buffer.lines[lineIndex].size());
                c++
            ) {
                if (buffer.isSelected(lineIndex, c))
                    std::cout << "\033[7m";

                std::cout << buffer.lines[lineIndex][c];

                if (buffer.isSelected(lineIndex, c))
                    std::cout << "\033[0m";
            }
        }

        std::cout << '\n';
    }

    for (int i = 0; i < terminalCols; i++)
        std::cout << "─";

    std::cout << "\033[K";

    std::cout
        << "\033[" << terminalRows << ";1H"
        << " Lean"
        << " Ln " << buffer.cursorLine + 1
        << ", Col " << buffer.cursorColumn + 1;

    std::cout
        << "\033[" << (buffer.cursorLine - scrollOffset + 3)
        << ";"
        << (buffer.cursorColumn + textStartCol)
        << "H";

    std::cout << "\033[?25h";
    std::cout.flush();
}

std::string getFilename() {
    std::string filename;
    char c;

    std::cout << "\nSave as: ";
    std::cout.flush();

    while (true) {
        if (read(STDIN_FILENO, &c, 1) != 1)
            continue;

        if (c == '\n')
            break;

        if (c == 127) {
            if (!filename.empty()) {
                filename.pop_back();
                std::cout << "\b \b";
                std::cout.flush();
            }
        } else if (c >= 32 && c <= 126) {
            filename += c;
            std::cout << c;
            std::cout.flush();
        }
    }

    return filename;
}

std::string saveFile(
    const TextBuffer& buffer,
    const std::string& filename
) {
    if (filename.empty())
        return "Save failed: no filename";

    std::ofstream file(filename);

    if (!file)
        return "Save failed: " + filename;

    for (const std::string& line : buffer.lines)
        file << line << '\n';

    if (!file)
        return "Save failed: " + filename;

    return "Saved: " + filename;
}

bool loadFile(
    TextBuffer& buffer,
    const std::string& filename
) {
    std::ifstream file(filename);

    if (!file)
        return false;

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

    if (dot == std::string::npos ||
        filename.substr(dot) != ".cpp") {
        return false;
    }

    std::string outputName = filename.substr(0, dot);

    std::string command =
        "g++ \"" + filename + "\" -o \"" + outputName + "\"";

    return system(command.c_str()) == 0;
}

bool runFile(const std::string& filename) {
    size_t dot = filename.find_last_of('.');

    if (dot == std::string::npos ||
        filename.substr(dot) != ".cpp") {
        return false;
    }

    std::string outputName = filename.substr(0, dot);

    std::string command =
        "xfce4-terminal --hold "
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

    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_iflag &= ~(IXON | IXOFF);

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    TextBuffer buffer;
    buffer.lines.push_back("");

    std::string statusMessage;
    std::string currentFilename;

    int scrollOffset = 0;

    DisplayServer displayServer = detectDisplayServer();

    ClipboardBackend clipboardBackend =
        detectClipboardBackend(displayServer);

    if (argc > 1) {
        if (loadFile(buffer, argv[1])) {
            currentFilename = argv[1];
            statusMessage = "Opened: " + currentFilename;
        } else {
            statusMessage =
                "Could not open: " +
                std::string(argv[1]);
        }
    }

    render(buffer, statusMessage, scrollOffset);

    while (true) {
        char c;

        if (read(STDIN_FILENO, &c, 1) != 1)
            continue;

        if (c == 127) {
            if (buffer.hasSelection)
                buffer.deleteSelection();
            else
                buffer.backspace();
        }

        if (c >= 32 && c <= 126)
            buffer.insertChar(c);

        if (c == 19) {
            if (currentFilename.empty())
                currentFilename = getFilename();

            statusMessage =
                saveFile(buffer, currentFilename);
        }

        if (c == 2) {
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

        if (c == 18) {
            if (currentFilename.empty()) {
                statusMessage = "No file to run";
            } else if (runFile(currentFilename)) {
                statusMessage = "Program launched";
            } else {
                statusMessage = "Could not launch program";
            }
        }

        if (c == 26)
            buffer.undo();

        if (c == 25)
            buffer.redo();

        if (c == 3) {
            buffer.copySelection();

            if (buffer.hasSelection) {
                systemClipboardCopy(
                    buffer.clipboard,
                    clipboardBackend
                );
            }
        }

        if (c == 24) {
            if (buffer.hasSelection) {
                buffer.copySelection();

                systemClipboardCopy(
                    buffer.clipboard,
                    clipboardBackend
                );

                buffer.deleteSelection();
            }
        }

        if (c == 22) {
            std::string systemClipboard =
                systemClipboardPaste(clipboardBackend);

            if (!systemClipboard.empty())
                buffer.clipboard = systemClipboard;

            buffer.paste();
        }

        if (c == 1)
            buffer.selectAll();

        if (c == 27) {
            char sequence[5];

            if (!readByte(sequence[0]))
                continue;

            if (sequence[0] != '[')
                continue;

            if (!readByte(sequence[1]))
                continue;

            if (sequence[1] == 'M') {
                unsigned char button;
                unsigned char mouseX;
                unsigned char mouseY;

                if (read(STDIN_FILENO, &button, 1) != 1)
                    continue;

                if (read(STDIN_FILENO, &mouseX, 1) != 1)
                    continue;

                if (read(STDIN_FILENO, &mouseY, 1) != 1)
                    continue;

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

                    if (textRows < 1)
                        textRows = 1;

                    int maxScroll = std::max(
                        0,
                        static_cast<int>(buffer.lines.size()) - textRows
                    );

                    if (scrollOffset < maxScroll)
                        ++scrollOffset;

                    render(buffer, statusMessage, scrollOffset);
                    continue;
                }

                int gutter = lineNumberWidth(buffer);
                int textStartCol = gutter + 4;

                int clickedLine =
                    scrollOffset + (y - 3);

                int clickedColumn =
                    x - textStartCol;

                if ((button & 3) == 0) {
                    if (clickedLine < 0 ||
                        clickedLine >= static_cast<int>(buffer.lines.size())) {
                        continue;
                    }

                    if (clickedColumn < 0)
                        clickedColumn = 0;

                    int length =
                        buffer.lines[clickedLine].size();

                    if (clickedColumn > length)
                        clickedColumn = length;

                    buffer.selecting = true;
                    buffer.hasSelection = false;

                    buffer.selectionStartLine = clickedLine;
                    buffer.selectionStartColumn = clickedColumn;

                    buffer.selectionEndLine = clickedLine;
                    buffer.selectionEndColumn = clickedColumn;

                    buffer.cursorLine = clickedLine;
                    buffer.cursorColumn = clickedColumn;
                } else if (button & 32) {
                    if (buffer.selecting) {
                        if (clickedLine >= 0 &&
                            clickedLine < static_cast<int>(buffer.lines.size())) {

                            if (clickedColumn < 0)
                                clickedColumn = 0;

                            int length =
                                buffer.lines[clickedLine].size();

                            if (clickedColumn > length)
                                clickedColumn = length;

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
                } else if ((button & 3) == 3) {
                    buffer.selecting = false;

                    if (buffer.selectionStartLine !=
                            buffer.selectionEndLine ||
                        buffer.selectionStartColumn !=
                            buffer.selectionEndColumn) {
                        buffer.hasSelection = true;
                    }
                }

                render(buffer, statusMessage, scrollOffset);
                continue;
            }

            if (sequence[1] == 'A' &&
                buffer.cursorLine > 0) {

                buffer.cursorLine--;

                if (buffer.cursorColumn >
                    static_cast<int>(
                        buffer.lines[buffer.cursorLine].size()
                    )) {
                    buffer.cursorColumn =
                        buffer.lines[buffer.cursorLine].size();
                }
            }

            if (sequence[1] == 'B' &&
                buffer.cursorLine <
                    static_cast<int>(buffer.lines.size()) - 1) {

                buffer.cursorLine++;

                if (buffer.cursorColumn >
                    static_cast<int>(
                        buffer.lines[buffer.cursorLine].size()
                    )) {
                    buffer.cursorColumn =
                        buffer.lines[buffer.cursorLine].size();
                }
            }

            if (sequence[1] == 'C' &&
                buffer.cursorColumn <
                    static_cast<int>(
                        buffer.lines[buffer.cursorLine].size()
                    )) {
                buffer.cursorColumn++;
            }

            if (sequence[1] == 'D' &&
                buffer.cursorColumn > 0) {
                buffer.cursorColumn--;
            }

            if (sequence[1] == '1') {
                if (read(STDIN_FILENO, &sequence[2], 1) != 1)
                    continue;

                if (read(STDIN_FILENO, &sequence[3], 1) != 1)
                    continue;

                if (read(STDIN_FILENO, &sequence[4], 1) != 1)
                    continue;

                if (sequence[2] == ';' &&
                    sequence[3] == '5' &&
                    sequence[4] == 'F') {
                    break;
                }
            }
        }

        if (c == '\n')
            buffer.enter();

        struct winsize size;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &size);

        int textRows = size.ws_row - 4;

        if (textRows < 1)
            textRows = 1;

        if (buffer.cursorLine < scrollOffset)
            scrollOffset = buffer.cursorLine;

        if (buffer.cursorLine >= scrollOffset + textRows)
            scrollOffset = buffer.cursorLine - textRows + 1;

        render(buffer, statusMessage, scrollOffset);
    }

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);

    std::cout << "\033[?1000l";
    std::cout << "\033[?25h";
    std::cout << "\033[?1049l";
    std::cout.flush();

    return 0;
}
