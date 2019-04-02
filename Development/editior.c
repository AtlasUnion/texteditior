/*** include ***/

#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>

/*** defines ***/

#define CTRL_KEY(k) ((k) & 0x1f) // mimic that ctrl strips bits 5 and 6

/*** data  ***/

struct termios orig_termios;

/*** terminal ***/

/**
 * @brief output error message and exit
 * 
 * @param s 
 */
void die(const char* s) {
    perror(s);
    exit(1);
}

/**
 * @brief disable Raw Mode and back to canonical mode
 * 
 */
void disableRawMode() {
    if (tcsetattr(STDERR_FILENO, TCSAFLUSH, &orig_termios) == -1)
        die("tcsetattr");
}

/**
 * @brief enable Raw Mode
 * 
 */
void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &orig_termios)  == -1) // get attributes from terminal associated with fd 0
        die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = orig_termios;

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

char editiorReadKey() {
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }
    return c;
}

/*** output ***/

void editiorRefreshScreen() {
    /*
    ** \x1b escape character, followed by [ to instruct terminal to execute certain commands
    */
    write(STDERR_FILENO, "\x1b[2J", 4); 

/*** input ***/

void editiorProcessKeypress() {
    char c = editiorReadKey();

    switch (c)
    {
        case CTRL_KEY('q'):
            exit(0);
            break;
    }
}

/*** init ***/

int main() {
    enableRawMode();

    while (1) {
        editiorProcessKeypress();
    }
    return 0;
}