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

bool readByte(char& c)
{
    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);

    timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;

    if (select(STDIN_FILENO + 1, &set, nullptr, nullptr, &timeout) > 0)
    {
        return read(STDIN_FILENO, &c, 1) == 1;
    }

    return false;
}

class TextBuffer
{
public:
    std::vector<std::string> lines;
    int cursorLine = 0;
    int cursorColumn = 0;

    void insertChar(char c)
    {
        lines[cursorLine].insert(cursorColumn, 1, c);
        cursorColumn++;
    }

    void backspace()
    {
        std::string& line = lines[cursorLine];

        if (cursorColumn > 0)
        {
            line.erase(cursorColumn - 1, 1);
            cursorColumn--;
        }
    }

    void enter()
    {
        std::string& line = lines[cursorLine];

        std::string newLine = line.substr(cursorColumn);
        line.erase(cursorColumn);

        lines.insert(lines.begin() + cursorLine + 1, newLine);

        cursorLine++;
        cursorColumn = 0;
    }
};

void render(
    const TextBuffer& buffer,
    const std::string& statusMessage,
    int scrollOffset)
{
    struct winsize size;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &size);

    int terminalRows = size.ws_row;
    int terminalCols = size.ws_col;

    if (terminalRows < 5)
        terminalRows = 5;

    if (terminalCols < 20)
        terminalCols = 20;

    int textRows = terminalRows - 4;

    std::cout << "\033[H";

    std::cout << " Lean";

    if (!statusMessage.empty())
    {
        std::cout << "                              "
                  << statusMessage;
    }

    std::cout << "\033[K\n";

    for (int i = 0; i < terminalCols; i++)
        std::cout << "─";

    std::cout << "\n";

    for (int i = 0; i < textRows; i++)
    {
        int lineIndex = scrollOffset + i;

        std::cout << "\033[K";

        if (lineIndex < static_cast<int>(buffer.lines.size()))
        {
            std::cout << lineIndex + 1
                      << " │ "
                      << buffer.lines[lineIndex];
        }

        std::cout << '\n';
    }

    for (int i = 0; i < terminalCols; i++)
        std::cout << "─";

    std::cout << "\033[K";

    std::cout << "\033["
              << terminalRows
              << ";1H";

    std::cout << " Lean"
              << "  Ln "
              << buffer.cursorLine + 1
              << ", Col "
              << buffer.cursorColumn + 1;

    std::cout << "\033["
              << buffer.cursorLine - scrollOffset + 3
              << ";"
              << buffer.cursorColumn + 5
              << "H";

    std::cout << "\033[?25h";
    std::cout.flush();
}

std::string getFilename()
{
    std::string filename;
    char c;

    std::cout << "\nSave as: ";
    std::cout.flush();

    while (true)
    {
        if (read(STDIN_FILENO, &c, 1) != 1)
            continue;

        if (c == '\n')
            break;

        if (c == 127)
        {
            if (!filename.empty())
            {
                filename.pop_back();
                std::cout << "\b \b";
                std::cout.flush();
            }
        }
        else if (c >= 32 && c <= 126)
        {
            filename += c;
            std::cout << c;
            std::cout.flush();
        }
    }

    return filename;
}

std::string saveFile(
    const TextBuffer& buffer,
    const std::string& filename)
{
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
    const std::string& filename)
{
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

bool buildFile(const std::string& filename)
{
    size_t dot = filename.find_last_of('.');

    if (dot == std::string::npos ||
        filename.substr(dot) != ".cpp")
    {
        return false;
    }

    std::string outputName = filename.substr(0, dot);

    std::string command =
        "g++ \"" + filename +
        "\" -o \"" + outputName + "\"";

    return system(command.c_str()) == 0;
}

bool runFile(const std::string& filename)
{
    size_t dot = filename.find_last_of('.');

    if (dot == std::string::npos ||
        filename.substr(dot) != ".cpp")
    {
        return false;
    }

    std::string outputName = filename.substr(0, dot);

    std::string command =
        "xfce4-terminal --hold "
        "--title=\"Lean: " + outputName + "\" "
        "--command=\"./" + outputName + "\"";

    return system(command.c_str()) == 0;
}

int main(int argc, char* argv[])
{
    std::cout << "\033[?1049h";
    std::cout << "\033[?25l";
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

    if (argc > 1)
    {
        if (loadFile(buffer, argv[1]))
        {
            currentFilename = argv[1];
            statusMessage = "Opened: " + currentFilename;
        }
        else
        {
            statusMessage =
                "Could not open: " +
                std::string(argv[1]);
        }
    }

    render(buffer, statusMessage, scrollOffset);

    while (true)
    {
        char c;

        if (read(STDIN_FILENO, &c, 1) != 1)
            continue;

        if (c == 127)
        {
            buffer.backspace();
        }

        if (c >= 32 && c <= 126)
        {
            buffer.insertChar(c);
        }

        if (c == 19)
        {
            if (currentFilename.empty())
                currentFilename = getFilename();

            statusMessage =
                saveFile(buffer, currentFilename);
        }

        if (c == 2)
        {
            if (currentFilename.empty())
            {
                statusMessage = "No file to build";
            }
            else
            {
                statusMessage = "Building...";
                render(buffer, statusMessage, scrollOffset);

                if (buildFile(currentFilename))
                    statusMessage = "Build successful";
                else
                    statusMessage = "Build failed";
            }
        }

        if (c == 18)
        {
            if (currentFilename.empty())
            {
                statusMessage = "No file to run";
            }
            else
            {
                if (runFile(currentFilename))
                    statusMessage = "Program launched";
                else
                    statusMessage = "Could not launch program";
            }
        }

        if (c == 27)
        {
            char sequence[5];

            if (!readByte(sequence[0]))
                continue;

            if (sequence[0] != '[')
                continue;

            if (!readByte(sequence[1]))
                continue;

            if (sequence[1] == 'A' &&
                buffer.cursorLine > 0)
            {
                buffer.cursorLine--;

                if (buffer.cursorColumn >
                    static_cast<int>(
                        buffer.lines[buffer.cursorLine].size()))
                {
                    buffer.cursorColumn =
                        buffer.lines[buffer.cursorLine].size();
                }
            }

            if (sequence[1] == 'B' &&
                buffer.cursorLine <
                static_cast<int>(buffer.lines.size()) - 1)
            {
                buffer.cursorLine++;

                if (buffer.cursorColumn >
                    static_cast<int>(
                        buffer.lines[buffer.cursorLine].size()))
                {
                    buffer.cursorColumn =
                        buffer.lines[buffer.cursorLine].size();
                }
            }

            if (sequence[1] == 'C' &&
                buffer.cursorColumn <
                static_cast<int>(
                    buffer.lines[buffer.cursorLine].size()))
            {
                buffer.cursorColumn++;
            }

            if (sequence[1] == 'D' &&
                buffer.cursorColumn > 0)
            {
                buffer.cursorColumn--;
            }

            if (sequence[1] == '1')
            {
                if (!readByte(sequence[2]))
                    continue;

                if (!readByte(sequence[3]))
                    continue;

                if (!readByte(sequence[4]))
                    continue;

                if (sequence[2] == ';' &&
                    sequence[3] == '5' &&
                    sequence[4] == 'F')
                {
                    break;
                }
            }
        }

        if (c == '\n')
        {
            buffer.enter();
        }

        struct winsize size;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &size);

        int textRows = size.ws_row - 4;

        if (textRows < 1)
            textRows = 1;

        if (buffer.cursorLine < scrollOffset)
        {
            scrollOffset = buffer.cursorLine;
        }

        if (buffer.cursorLine >=
            scrollOffset + textRows)
        {
            scrollOffset =
                buffer.cursorLine - textRows + 1;
        }

        render(buffer, statusMessage, scrollOffset);
    }

    tcsetattr(
        STDIN_FILENO,
        TCSAFLUSH,
        &original);

    std::cout << "\033[?25h";
    std::cout << "\033[?1049l";
    std::cout.flush();

    return 0;
}