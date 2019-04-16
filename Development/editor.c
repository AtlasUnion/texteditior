/*** include ***/

#define _DEFAULT_SOURCE // feature test macro
#define _BSD_SOURCE     // feature test macro
#define _GNU_SOURCE     // feature test macro

#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <string.h>

/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f) // mimic that ctrl strips bits 5 and 6
#define ABUF_INIT {NULL,0}
#define EDITOR_VERSION "0.0.1"

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT ,
    ARROW_UP,
    ARROW_DOWN,
    PAGE_UP,
    PAGE_DOWN,
    HOME_KEY,
    END_KEY,
    DEL_KEY
};

/*** data  ***/

/**
 * @brief Editior Config
 * Include cursor position, window size and current terminal mode
 */
typedef struct erow {
    int size;
    char* chars;
} erow;

struct editorConfig {
    int cx, cy;
    int rowoff; // TODO: implement support for vertical scrolling
    int screenRows;
    int screenCols;
    int numrows;
    erow* row;
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

        if (seq[0] == '[') { // Page up, Page down
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] = '~') {
                    switch(seq[1]) {
                        case '1': return HOME_KEY;
                        case '3': return DEL_KEY;
                        case '4': return END_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            } else { // Cursor Key
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == '0') {
            switch (seq[1])
            {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
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

/** row operation **/

/**
 * @brief Append a line to the editor's buffer
 * 
 * @param s 
 * @param len 
 */
void editorAppendRow(char* s, size_t len) {
    E.row = realloc(E.row, sizeof(erow)*(E.numrows + 1));

    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.numrows++;
}

/** file i/o **/
/**
 * @brief Open a file and read line by line into editor
 * 
 * @param filename 
 */
void editorOpen(char* filename) {
    // char* line = "Hello, World!";
    // ssize_t linelen = 13;
    FILE* fp = fopen(filename, "r"); // TODO: current only allow read-only operation
    if (!fp) die("fopen");

    char* line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    
    // let the os allocate space for line by using getline, we must then free the space
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        // do not store newline and carriage return char as each of our erow is defined as new line
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) linelen--; 
        editorAppendRow(line, linelen);
    }
    free(line);
    fclose(fp);
}

/*** output ***/

/**
 * @brief Check the position of the cursor and update the rowoff accordingly
 * 
 */
void editorScroll() {
    if (E.cy  < E.rowoff) { // check if the cursor is above visible window
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenRows) { // check if the cursor is below the visible window
        E.rowoff = E.cy - E.screenRows + 1;
    }
}

/**
 * @brief Draw '~' and welcome message
 * 
 * @param ab 
 */
void editorDrawRows(struct abuf* ab) {
    int y;
    for (y = 0; y < E.screenRows; y++) {
        int filerow = y + E.rowoff; // calculate correct line/row number

        if (filerow >= E.numrows) { // draw version number, welcome string and ~
            if (E.numrows == 0 && y == E.screenRows/3) { // display the welcome message if and only if the user choose not to open a file or type nothing
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
        } else { // draw out user input text
            int len = E.row[filerow].size;
            if (len > E.screenCols) len = E.screenCols;
            abAppend(ab, E.row[filerow].chars, len);
        }

        /*
        ** K -> Erase in line
        */
        abAppend(ab, "\x1b[K", 3);
        if (y < E.screenRows - 1) { // stop screen scroll for last line as '\r\n' will add a new blank line
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
    editorScroll();

    struct abuf ab = ABUF_INIT;

    /*
    ** \x1b escape character, followed by [ to instruct terminal to execute certain commands
    ** 2J: J->Erase in Display, 2->Erase all of the display
    ** H-> Cursor Position, take two arguments: row and column number, default to 1
    ** h, l -> ?25 hide/showing the cursor
    */
    // write(STDOUT_FILENO, "\x1b[2J", 4); 
    // write(STDOUT_FILENO, "\x1b[H", 3);
    abAppend(&ab, "\x1b[?25l",6); // hide cursor
    // abAppend(&ab, "\x1b[2J", 4);
    abAppend(&ab, "\x1b[H", 3);

    editorDrawRows(&ab);

    /*
    ** Move cursor
    */
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, E.cx + 1);
    abAppend(&ab, buf, strlen(buf));

    // write(STDOUT_FILENO, "\x1b[H", 3);
    // abAppend(&ab, "\x1b[H", 3);
    abAppend(&ab, "\x1b[?25h",6); // show cursor

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
            if (E.cy < E.numrows) {
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

        case HOME_KEY:
            E.cx = 0;
            break;

        case END_KEY:
            E.cx = E.screenCols - 1;
            break;
        case PAGE_UP:
        case PAGE_DOWN:
            {
                int times = E.screenRows;
                while (times--)
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
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
    E.rowoff = 0;
    E.numrows = 0;
    E.row = NULL;

    if (getWindowSize(&E.screenRows, &E.screenCols) == -1)
        die("getWindowSize");
}

int main(int argc, char* argv[]) {
    enableRawMode();
    initEditor();
    if (argc >= 2) {
            editorOpen(argv[1]);
    }

    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}