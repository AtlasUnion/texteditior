/*** include ***/

#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>

/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f) // mimic that ctrl strips bits 5 and 6

/*** data  ***/
struct editorConfig {
    int screenRows;
    int screenCols;
    struct termios orig_termios;
};

struct editorConfig E;

/*** terminal ***/

/**
 * @brief output error message and exit
 * 
 * @param s 
 */
void die(const char* s) {
    write(STDOUT_FILENO, "\x1b[2J", 4); 
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

/**
 * @brief disable Raw Mode and back to canonical mode
 * 
 */
void disableRawMode() {
    if (tcsetattr(STDERR_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

/**
 * @brief enable Raw Mode
 * 
 */
void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &E.orig_termios)  == -1) // get attributes from terminal associated with fd 0
        die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;

    /*
    ** disable ctrl-q, ctrl-s, carriage return conversion
    ** When ICRNL is on, carriage return get converted to newline character
    ** When IXON is on, ctrl-q, ctrl-s are enabled
    */
    raw.c_iflag &= ~(ICRNL | IXON | BRKINT | INPCK | ISTRIP);

    /*
    ** disable echo, canonical mode, ctrl-c, ctrl-z, ctrl-v
    ** 
    */
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

    /*
    ** Turn off all output processing
    ** Specifically, turn off conversion from '\n' to '\r\n'
    ** '\r' cause the terminal to move the cursor move back to the beginning of the line
    ** '\n' cause the terminal to move the cursor down by one line
    */
    raw.c_oflag &= ~(OPOST); 

    raw.c_cflag |= (CS8); // set charactere size to 8 bits per byte

    /*
    ** VMIN set the number of bytes to read() before return
    ** Set to 0 means the read() return as soon as there is any input
    */
    raw.c_cc[VMIN] = 0; 
    /*
    ** VTIME set the time read() will wait before return
    ** Value in 1/10 of second
    */
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1  ) // set attributes
        die("tcsetattr");
}

char editorReadKey() {
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }
    return c;
}

int getCursorPosition(int* rows, int* cols) {
    char buf[32];
    unsigned int i = 0;
    /*
    ** n -> request status report, arg 6 request cursor position
    */
    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
        return -1;

    char c;
    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

    return 0;
}

int getWindowSize(int* rows, int* cols) {
    struct winsize ws;
    /*
    ** C-> move cursor to the right, B-> move cursor to the left (Cursor won't past the edge of the screen)
    */
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;
        return getCursorPosition(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/*** output ***/

void editorDrawRows() {
    int y;
    for (y = 0; y < E.screenRows; y++) {
        write(STDOUT_FILENO, "~\r\n", 3);
    }
}

void editorRefreshScreen() {
    /*
    ** \x1b escape character, followed by [ to instruct terminal to execute certain commands
    ** 2J: J->Erase in Display, 2->Erase all of the display
    ** H-> Cursor Position, take two arguments: row and column number, default to 1
    */
    write(STDOUT_FILENO, "\x1b[2J", 4); 
    write(STDOUT_FILENO, "\x1b[H", 3);

    editorDrawRows();

    write(STDOUT_FILENO, "\x1b[H", 3);
}

/*** input ***/

void editorProcessKeypress() {
    char c = editiorReadKey();

    switch (c)
    {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4); 
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
    }
}

/*** init ***/

void initEditor() {
    if (getWindowSize(&E.screenRows, &E.screenCols) == -1)
        die("getWindowSize");
}

int main() {
    enableRawMode();
    initEditor();

    while (1) {
        editorProcessKeypress();
    }
    return 0;
}