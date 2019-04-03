/*** include ***/

#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>

/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f) // mimic that ctrl strips bits 5 and 6
#define ABUF_INIT {NULL,0}
#define EDITOR_VERSION "0.0.1"

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT ,
    ARROW_UP,
    ARROW_DOWN
};

/*** data  ***/

/**
 * @brief Editior Config
 * Include cursor position, window size and current terminal mode
 */
struct editorConfig {
    int cx, cy;
    int screenRows;
    int screenCols;
    struct termios orig_termios;
};

struct editorConfig E;

/*** append buffer ***/

/**
 * @brief Dynamic String
 * 
 */
struct abuf{
    char* b;
    int len;
};

/**
 * @brief 
 * 
 * @param ab: original string
 * @param s: string to be appended
 * @param len: length of the string to be appended
 */
void abAppend(struct abuf *ab, const char* s, int len) {
    char* new = realloc(ab->b, ab->len + len);

    if (new == NULL) return; 
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

/**
 * @brief Free dynamic string
 * 
 * @param ab 
 */
void abFree(struct abuf* ab) {
    free(ab->b);
}

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

/**
 * @brief Read a key press from user
 * 
 * @return int
 */
int editorReadKey() {
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    /*
    ** Capture arrow keys
    ** Note arrow key return multiple bytes
    */
    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            switch (seq[1])
            {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
            }
        }
        return '\x1b';
    } else {
        return c;
    }
}

/**
 * @brief Get the Cursor Position
 * 
 * @param rows 
 * @param cols 
 * @return int 0 if no error, else -1
 */
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

/**
 * @brief Get the Window Size
 * 
 * @param rows 
 * @param cols 
 * @return int 0 if no error, else -1
 */
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

/**
 * @brief Draw '~' and welcome message
 * 
 * @param ab 
 */
void editorDrawRows(struct abuf* ab) {
    int y;
    for (y = 0; y < E.screenRows; y++) {
        if (y == E.screenRows/3) {
            char welcome[80];
            int welcomelen = snprintf(welcome, sizeof(welcome), "Editor -- version %s", EDITOR_VERSION);

            if (welcomelen > E.screenCols) welcomelen = E.screenCols;

            /*
            ** Center the welcome string
            */
            int padding = (E.screenCols - welcomelen)/2;
            if (padding) {
                abAppend(ab, "~", 1);
                padding--;
            }
            while(padding--) abAppend(ab, " ", 1);
            abAppend(ab, welcome, welcomelen);
        } else {
            // write(STDOUT_FILENO, "~", 1);
            abAppend(ab, "~", 1);
        }

        /*
        ** K -> Erase in line
        */
        abAppend(ab, "\x1b[K", 3);
        if (y < E.screenRows - 1) {
            // write(STDOUT_FILENO, "\r\n", 2);
            abAppend(ab, "\r\n", 2);
        }
    }
}

/**
 * @brief Refresh the screen -- redraw graphics
 * 
 */
void editorRefreshScreen() {
    struct abuf ab = ABUF_INIT;

    /*
    ** \x1b escape character, followed by [ to instruct terminal to execute certain commands
    ** 2J: J->Erase in Display, 2->Erase all of the display
    ** H-> Cursor Position, take two arguments: row and column number, default to 1
    ** h, l -> ?25 hide/showing the cursor
    */
    // write(STDOUT_FILENO, "\x1b[2J", 4); 
    // write(STDOUT_FILENO, "\x1b[H", 3);
    abAppend(&ab, "\x1b[?25l",6);
    // abAppend(&ab, "\x1b[2J", 4);
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    /*
    ** Move cursor
    */
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    // write(STDOUT_FILENO, "\x1b[H", 3);
    // abAppend(&ab, "\x1b[H", 3);
    abAppend(&ab, "\x1b[?25h",6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/*** input ***/

/**
 * @brief Move the cursor
 * 
 * @param key: user key press
 */
void editorMoveCursor(int key) {
    switch (key)
    {
        case ARROW_LEFT:
            if (E.cx != 0) {
                E.cx--;
            }
            break;
        case ARROW_RIGHT:
            if (E.cx != E.screenCols - 1) {
                E.cx++;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0) {
                E.cy--;
            }
            break;
        case ARROW_DOWN:
            if (E.cy != E.screenRows - 1) {
                E.cy++;
            }
            break;
    }
}

/**
 * @brief User key press process
 * 
 */
void editorProcessKeypress() {
    int c = editorReadKey();

    switch (c)
    {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4); 
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
    }
}

/*** init ***/

/**
 * @brief Initialize Editor
 * Get Window Size, set cursor position
 */
void initEditor() {
    /*
    ** Set cursor position
    */
    E.cx = 0;
    E.cy = 0;

    if (getWindowSize(&E.screenRows, &E.screenCols) == -1)
        die("getWindowSize");
}

int main() {
    enableRawMode();
    initEditor();

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}