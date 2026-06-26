/*** includes ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>

/*** defines ***/
#define THE_GRIMOIRE_VER "0.0.2" 
#define THE_GRIMOIRE_TAB_STOP 8
#define CTRL_KEY(k) ((k) & 0x1f) // Macro to get the control key equivalent of a character

enum editorKey {
    ARROW_LEFT = 1000, // Define a constant for the left arrow key
    ARROW_RIGHT, // Define a constant for the right arrow key
    ARROW_UP, // Define a constant for the up arrow key
    ARROW_DOWN, // Define a constant for the down arrow key
    DEL_KEY, // Define a constant for the delete key
    HOME_KEY, // Define a constant for the home key
    END_KEY, // Define a constant for the end key
    PAGE_UP, // Define a constant for the page up key
    PAGE_DOWN, // Define a constant for the page down key
};

/*** data ***/
typedef struct erow {
    int size; // Size of the row
    int rsize; //
    char *chars; // Pointer to the characters in the row
    char *render;
} erow;

struct editorConfig {
    int cx, cy; // Cursor x and y position
    int rx;
    int rowoff; // for vertical scrolling
    int coloff; // for horizontal scrolling
    int screenrows; // Number of rows in the terminal
    int screencols; // Number of columns in the terminal
    int numrows; // Number of rows in the editor
    erow *row; // Current row being edited
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
    struct termios orig_termios; // Store original terminal attributes
};

struct editorConfig E; // Global editor configuration

/*** terminal***/

void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4); // Clear the screen
    write(STDOUT_FILENO, "\x1b[H", 3); // Move the cursor to the top-left corner
    
    perror(s); // Print error message
    exit(1); // Exit the program with an error code
}
void disableRawMode() {
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr");
}
void enableRawMode() {
    if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
    atexit(disableRawMode); // Ensure raw mode is disabled on exit
    
    struct termios raw = E.orig_termios; // Copy original terminal attributes to modify
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON); // Disable break condition, carriage return to newline translation, parity checking, stripping of the 8th bit, and software flow control
    raw.c_oflag &= ~(OPOST); // Disable output processing
    raw.c_cflag |= (CS8); // Set character size to 8 bits
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); // Disable echo, canonical mode, Ctrl-V, Ctrl-C and Ctrl-Z
    raw.c_cc[VMIN] = 0; // Minimum number of bytes of input before read() returns
    raw.c_cc[VTIME] = 1; // Maximum time to wait before read
    
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr"); // Set terminal to raw mode
}
int editorReadKey() {
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if(nread == -1 && errno != EAGAIN) die("read"); // Read a single character from standard input
    }
    
    if(c == '\x1b') { // If the character is an escape sequence
        char seq[3];
        if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b'; // Read the next character
        if(read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b'; // Read the next character
        
        if(seq[0] == '[') {
            if(seq[1] >= '0' && seq[1] <= '9') {
                if(read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b'; // Read the next character
                if(seq[2] == '~') {
                    switch(seq[1]) {
                        case '1': return HOME_KEY; // Home key
                        case '4': return END_KEY; // End key
                        case '3': return DEL_KEY; // Delete key
                        case '5': return PAGE_UP; // Page up key
                        case '6': return PAGE_DOWN; // Page down key
                        case '7': return HOME_KEY; // Home key
                        case '8': return END_KEY; // End key
                    }
                }
            } else {
                switch(seq[1]) {
                    case 'A': return ARROW_UP; // Up arrow key
                    case 'B': return ARROW_DOWN; // Down arrow key
                    case 'C': return ARROW_RIGHT; // Right arrow key
                    case 'D': return ARROW_LEFT; // Left arrow key
                    case 'H': return HOME_KEY; // Home key
                    case 'F': return END_KEY; // End key
                }
            }
        } else if(seq[0] == 'O') {
            switch(seq[1]) {
                case 'H': return HOME_KEY; // Home key
                case 'F': return END_KEY; // End key
            }
        }

        return '\x1b'; // Return escape character if no valid sequence is found
    } else {
        return c; // Return the read character
    }
    
    return c; // Return the read character
}

int getCursorPosition(int *rows, int *cols) {
    char buf[32];
    unsigned int i = 0;
    
    if(write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1; // Request cursor position report

    while(i < sizeof(buf)-1) {
        if(read(STDIN_FILENO, &buf[i], 1) != 1) break; // Read the response from the terminal
        if(buf[i] == 'R') break; // Stop reading when 'R' is encountered
        i++;
    }
    buf[i] = '\0'; // Null-terminate the buffer

    if(buf[0] != '\x1b' || buf[1] != '[') return -1; // Check if the response is valid
    if(sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1; // Parse the row and column values from the response

    return 0; // Return success
}

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;

    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1; // Move the cursor to the bottom-right corner
        return getCursorPosition(rows, cols); // Get the cursor position to determine the window size
    } else {
        *cols = ws.ws_col; // Set the number of columns
        *rows = ws.ws_row; // Set the number of rows
        return 0; // Return success
    }
}

/** row operations ***/

int editorRowCxToRx(erow *row, int cx){
    int rx=0;
    int j;
    for(j=0; j<cx; j++){
        if(row->chars[j]== '\t')
            rx += (THE_GRIMOIRE_TAB_STOP - 1) - (rx % THE_GRIMOIRE_TAB_STOP);
        rx++;
    }
    return rx;
}

void editorUpdateRow(erow * row){
    int tabs = 0;
    int j;
    for(j=0; j < row->size; j++){
        if(row->chars[j]=='\t') tabs++;
    }
    
    free(row->render);
    row->render = malloc(row->size + tabs*(THE_GRIMOIRE_TAB_STOP - 1) + 1);

    int idx = 0;
    for(j=0; j < row->size; j++){
        if(row->chars[j] == '\t'){
            row->render[idx++] = ' ';
            while(idx % THE_GRIMOIRE_TAB_STOP != 0) row->render[idx++] = ' ';
        }else{
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx]= '\0';
    row->rsize = idx;
}

void editorAppendRow(char *s, size_t len) {
    E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1)); // Reallocate memory for the rows in the editor
    
    int at = E.numrows; // Get the index of the new row
    E.row[at].size = len; // Set the size of the new row
    E.row[at].chars = malloc(len + 1); // Allocate memory for the characters in the new row
    memcpy(E.row[at].chars, s, len); // Copy the characters from the input string to the new row
    E.row[at].chars[len] = '\0'; // Null-terminate the string
    
    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    editorUpdateRow(&E.row[at]);
    
    E.numrows++; // Increment the number of rows in the editor
}

/*** file i/o ***/

void editorOpen(char *filename) {
    free(E.filename);
    E.filename = strdup(filename);
    
    FILE *fp = fopen(filename, "r"); // Open the file for reading
    if(!fp) die("fopen"); // If the file cannot be opened, print an error message and exit

    char *line = NULL; // Pointer to hold the line read from the file
    size_t linecap = 0; // Variable to hold the capacity of the line buffer
    ssize_t linelen; // Variable to hold the length of the line read
    while((linelen = getline(&line, &linecap, fp)) !=-1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
            linelen--; // Remove trailing newline and carriage return characters
        }

        editorAppendRow(line, linelen); // Append the line to the editor's row
    }
    free(line); // Free the memory allocated for the line buffer
    fclose(fp); // Close the file
}

//*** append buffer ***/

struct abuf {
    char *b; // Pointer to the buffer
    int len; // Length of the buffer
};

#define ABUF_INIT {NULL, 0} // Macro to initialize an abuf structure

void abAppend(struct abuf *ab, const char *s, int len) {
    char *new = realloc(ab->b, ab->len + len); // Reallocate memory for the buffer
    
    if(new == NULL) return; // Return if memory allocation fails
    memcpy(&new[ab->len], s, len); // Copy the new string to the end of the buffer
    ab->b = new; // Update the buffer pointer
    ab->len += len; // Update the length of the buffer
}

void abFree(struct abuf *ab) {
    free(ab->b); // Free the memory allocated for the buffer
}

/*** input ***/

void editorMoveCursor(int key) {
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    
    switch(key) {
        case ARROW_LEFT:
            if(E.cx != 0){ //Move cursor right
                E.cx--;
            }else if(E.cy >0 ){ //moving left at the start of a line (press left at the beginning of the line to move to the end of the previous line)
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_RIGHT:
            if(row && E.cx < row->size){
                E.cx++; // Move cursor right
            }else if(row && E.cx == row->size){ //moving right at the end of a line will go to the beginning of the next line
                E.cy++;
                E.cx=0;
            }
            break;
        case ARROW_UP:
            if(E.cy != 0) E.cy--; // Move cursor up
            break;
        case ARROW_DOWN:
            if(E.cy < E.numrows){
                E.cy++;
            }
            break;
    }

    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row -> size : 0;
    if(E.cx > rowlen){
        E.cx = rowlen;
    }
}

void editorProcessKeypress() {
    int c = editorReadKey(); // Read a key press
    
    switch(c) {
        case CTRL_KEY('q'):
        write(STDOUT_FILENO, "\x1b[2J", 4); 
        write(STDOUT_FILENO, "\x1b[H", 3); 
            exit(0); // Exit the program if 'q' is pressed
            break;

        case HOME_KEY:
            E.cx = 0; // Move cursor to the beginning of the line
            break;
        case END_KEY:
            if(E.cy < E.numrows)
                E.cx = E.row[E.cy].size; // Move cursor to the end of the line
            break;
        case PAGE_UP:
        case PAGE_DOWN:
            {   
                if(c == PAGE_UP){
                    E.cy = E.rowoff;
                }else if(c == PAGE_DOWN){
                    E.cy = E.rowoff + E.screenrows - 1;
                    if(E.cy > E.numrows) E.cy = E.numrows;
                }
                int times = E.screenrows; // Number of times to move the cursor
                while(times--) {
                    editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN); // Move the cursor up or down based on the key pressed
                }
            }
            break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c); // Move the cursor based on the key pressed
            break;
    }
}

/*** output ***/

void editorScroll(){
    E.rx = 0;
    if(E.cy < E.numrows){
        E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
    }
    if(E.cy < E.rowoff){
        E.rowoff = E.cy;
    }
    if(E.cy>= E.rowoff + E.screenrows){
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if(E.rx < E.coloff){
        E.coloff = E.rx;
    }
    if(E.rx >= E.coloff + E.screencols){
        E.coloff = E.rx - E.screencols+1;
    }
}

void editorDrawRows(struct abuf *ab) {
    int y;
    for (y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff;

        if(filerow >= E.numrows) {
            if(E.numrows == 0 && y == E.screenrows / 3) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome), "THE GRIMOIRE editor -- version %s", THE_GRIMOIRE_VER); // Create a welcome message
                if(welcomelen > E.screencols) welcomelen = E.screencols; // Truncate the message if it exceeds the screen width
                int padding = (E.screencols - welcomelen) / 2; // Calculate padding to center the message
                if(padding) {
                    abAppend(ab, "~", 1); // Append a tilde for the left padding
                    padding--;
                }
                while(padding--) abAppend(ab, " ", 1); // Append spaces for the remaining padding
                abAppend(ab, welcome, welcomelen); // Append the welcome message
            } else {
            abAppend(ab, "~", 1); // Append a tilde for each row to indicate empty lines
            }
        } else {
            int len = E.row[filerow].rsize - E.coloff; // Get the length of the current row
            if(len < 0) len = 0;
            if(len > E.screencols) len = E.screencols; // Truncate the row if it exceeds the screen width
            abAppend(ab, &E.row[filerow].render[E.coloff], len); // Append the characters of the current row
        }

        abAppend(ab, "\x1b[K", 3); // Clear the line after the tilde
        abAppend(ab, "\r\n", 2); // Append a newline for all rows except the last one
    }
}

void editorDrawStatusBar(struct abuf *ab){
    abAppend(ab, "\x1b[7m", 4);
    
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines", E.filename ? E.filename : "[No Name]", E.numrows);
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);
    if(len > E.screencols) len = E.screencols;
    abAppend(ab, status, len);
    
    while(len < E.screencols) {
        if(E.screencols - len == rlen){
            abAppend(ab, rstatus, rlen);
            break;
        }else{
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab){
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);
    if(msglen > E.screencols) msglen = E.screencols;
    if(msglen && time(NULL) - E.statusmsg_time < 5)
        abAppend(ab, E.statusmsg, msglen); 
}

void editorRefreshScreen() {
    editorScroll();

    struct abuf ab = ABUF_INIT; // Initialize an append buffer

    abAppend(&ab, "\x1b[?25l", 6); // Hide the cursor
    abAppend(&ab, "\x1b[H", 3); // Move the cursor to the top-left corner

    editorDrawRows(&ab); // Draw the rows of the editor
    editorDrawStatusBar(&ab); //draw the status bar
    editorDrawMessageBar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff)+ 1, (E.rx - E.coloff) + 1); // Move the cursor to the current position
    abAppend(&ab, buf, strlen(buf)); // Append the cursor position command to the buffer

    abAppend(&ab, "\x1b[?25h", 6); // Show the cursor

    write(STDOUT_FILENO, ab.b, ab.len); // Write the contents of the buffer to standard output
    abFree(&ab); // Free the memory allocated for the buffer
}

void editorSetStatusMessage(const char *fmt, ...){
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

/*** init ***/

void initEditor() {
    E.cx = 0; // Initialize cursor x position
    E.cy = 0; // Initialize cursor y position
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0; // Initialize number of rows in the editor
    E.row = NULL; // Initialize the current row to NULL
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    
    if(getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize"); // Get the window size of the terminal
    E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();
    if(argc >= 2) {
        editorOpen(argv[1]); // Open the file specified as a command-line argument
    }

    editorSetStatusMessage("HELP: Ctrl-Q = quit");

    while(1){
        editorRefreshScreen(); // Refresh the screen
        editorProcessKeypress(); // Continuously process key presses
    }
    return 0;
}