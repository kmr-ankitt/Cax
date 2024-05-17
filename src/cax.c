/*** Headerfiles ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE


#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*** Define ***/

#define CAX_TAB_STOP 8
#define CTRL_KEY(k) ((k) & 0x1f)
#define CAX_VERSION "0.01"

enum editorKey{
  ARROW_LEFT = 1000 , 
  ARROW_RIGHT , 
  ARROW_UP ,
  ARROW_DOWN, 
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};

/*** Data***/

// It stores a row of text
typedef struct erow{
  int size;
  int rsize;
  char *chars;
  char *render;
}erow;

/* Here we are configuring the terminal window */
struct editorConfig
{
  // We will keep track of mouse cursor using x and y coordinates
  int cx, cy;
  int rx;
  int rowoff;
  int coloff;
  int screenRows;
  int screenCols;
  int numrows;
  erow *row;
  char * filename;
  char statusmsg[80];
  time_t statusmsg_time;
  struct termios originalTemios;
};

struct editorConfig E;

/*** Terminal ***/

// this kills the editor
void die(const char *s)
{

  // clears the screens before exit
  write(STDOUT_FILENO, "\x1b[2J", 4);

  // repositons the cursor before exit
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}

// it resets the terminal to original state
void disableRawMode()
{
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.originalTemios) == -1)
    die("tcsetattr");
}

// By default terminal is set to canonical mode which send input after pressing enter
void enableRawMode()
{
  if (tcgetattr(STDIN_FILENO, &E.originalTemios) == -1)
    die("tcgetattr");

  atexit(disableRawMode); // it calls disableRawMode function automatically when program exits

  struct termios raw = E.originalTemios;

  tcgetattr(STDIN_FILENO, &raw);

  // ICRNL disables ctrl + m, ctrl + j and Enter
  // IXON disables ctrl + s and ctrl + q
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

  // this disables \n\r
  raw.c_iflag &= ~(OPOST);

  raw.c_cflag |= (CS8);

  // ECHO make each keypress to be printed in the terminal, which is not of our use
  // ICANON make the program quit when 'q' is clicked
  // IEXTEN is used to disable ctrl + v
  // ISIG is used to disable ctrl + c and ctrl + z
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

// This functions reads the key input
int editorReadKey()
{
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
  {

    // if no input then die
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }

  if(c == '\x1b'){
    char seq[3];

    if(read(STDIN_FILENO , &seq[0], 1) != 1)
      return '\x1b';
    if(read(STDIN_FILENO , &seq[1], 1) != 1)
      return '\x1b';

    if(seq[0] == '['){
    
      if(seq[1] >= '0' && seq[1] <= '9'){
        if(read(STDIN_FILENO, &seq[2] , 1) != 1)
          return '\x1b';
        if(seq[2] == '~'){
          switch (seq[1]) {
            case '1':
              return HOME_KEY;
            case '3':
              return DEL_KEY;
            case '4':
              return END_KEY;
            case '5':
              return PAGE_UP;
            case '6':
              return PAGE_DOWN;
            case '7':
              return HOME_KEY;
            case '8':
              return END_KEY;
          }
        }
      } else{

        switch (seq[1]) {
          case 'A' : 
            return ARROW_UP;
          case 'B' :
            return ARROW_DOWN;
          case 'C':
            return ARROW_RIGHT;
          case 'D':
            return ARROW_LEFT;
          case 'H':
            return HOME_KEY;
          case 'F':
            return END_KEY;
        }
      }
    } else if(seq[0] == 'O'){
      switch (seq[1]) {
        case 'H': 
          return HOME_KEY;
        case 'F':
          return END_KEY;
      }
    }
  
    return '\x1b';
  }
  else{
    return c;
  }
}

int getCursorPosition(int *rows, int *cols)
{
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;

  while (i < sizeof(buf) - 1)
  {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;

    if (buf[i] == 'R')
      break;
    i++;
  }
  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[')
    return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    return -1;

  return 0;
}

/* Here we are attaining the how many colums and rows high terminal will be using winsize through ioctl  */
int getWindowSize(int *rows, int *cols)
{
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
  {

    // Since window size may vary from system to system,  we will check it by using position the cursor at the bottom and the tracking its postions using escape sequence

    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1;
    return getCursorPosition(rows, cols);
  }
  else
  {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/*** Row operations ***/

int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t')
      rx += (CAX_TAB_STOP - 1) - (rx % CAX_TAB_STOP);
    rx++;
  }
  return rx;
}


void editorUpdateRow(erow *row) {
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++)
    if (row->chars[j] == '\t') tabs++;
  free(row->render);
  row->render = malloc(row->size + tabs*(CAX_TAB_STOP - 1) + 1);
  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % CAX_TAB_STOP != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;
}

void editorAppendRow(char *s, size_t len) {

  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  int at = E.numrows;
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';


  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  editorUpdateRow(&E.row[at]);

  E.numrows++;

}


/*** File I/O ***/


void editorOpen(char *filename) {
  free(E.filename);
  E.filename = strdup(filename);

  FILE *fp = fopen(filename, "r");
  if (!fp) die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 && (line[linelen - 1] == '\n' ||
                           line[linelen - 1] == '\r'))
      linelen--;
    editorAppendRow(line, linelen);
  }
  free(line);
  fclose(fp);
}



/*** Append Buffer ***/

struct abuf
{
  char *b;
  int len;
};

// This is the constructor for our append buffer
#define ABUF_INIT \
  {               \
    NULL, 0       \
  }

void abAppend(struct abuf *ab, const char *s, int len)
{

  // this will reallcoate the buffer space according to the input
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL)
    return;

  // we used memcpy to the append the new input in prev input
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

// this will free the buffer space
void abFree(struct abuf *ab)
{
  free(ab->b);
}

/*** Output ***/


// Vertical and horizontal scroll impelemented here 
void editorScroll() {
  E.rx = 0;

  if (E.cy < E.numrows) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }

  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenRows) {
    E.rowoff = E.cy - E.screenRows + 1;
  }
  
  if (E.rx < E.coloff) {
    E.coloff = E.cx;
  }
  if (E.rx >= E.coloff + E.screenCols) {
    E.coloff = E.rx - E.screenCols + 1;
  }
}

// Drawing ~
void editorDrawRows(struct abuf *ab)
{
  int y;
  for (y = 0; y < E.screenRows; y++)
  {

    // Name printing
    int filerow = y + E.rowoff;
    if(filerow>= E.numrows){
      if(E.numrows == 0 && y == E.screenRows / 3){
        char welcome[80];
        int welcomelen = snprintf(welcome , sizeof(welcome), "Cax editor --version %s", CAX_VERSION);
        if(welcomelen > E.screenCols)
            welcomelen = E.screenCols;

        // centering the welcome message
        // for which we divided the screen width by 2 and subtract string length 
        int padding = (E.screenCols - welcomelen) / 2;
        if(padding){
          abAppend(ab , "~" , 1);
          padding--;
        }
        while(padding--)
          abAppend(ab , " ", 1);

        abAppend(ab , welcome , welcomelen);
      }else {
        abAppend(ab, "~", 1);
      }
    } else {
      int len = E.row[filerow].rsize - E.coloff;
      if(len<0)
        len = 0;
      if(len > E.screenCols) 
        len = E.screenCols;
      abAppend(ab , &E.row[filerow].render[E.coloff] , len);

    } 
    // Clears each line as we withdraw them
    abAppend(ab , "\x1b[K" , 3 );
    // drawing last line
      abAppend(ab, "\r\n", 2);
  }
}

void editorDrawStatusBar(struct abuf *ab) {
  abAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines",
    E.filename ? E.filename : "[No Name]", E.numrows);
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
    E.cy + 1, E.numrows);
  if (len > E.screenCols) len = E.screenCols;
  abAppend(ab, status, len);
  while (len < E.screenCols) {
    if (E.screenCols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab) {
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screenCols) msglen = E.screenCols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
}


// This function clears the whole screen
void editorRefreshScreen() {
  editorScroll();

  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
                                            (E.rx - E.coloff) + 1);
  abAppend(&ab, buf, strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}


void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}


/*** Input ***/

void editorMoveCursor(int key){

  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  switch (key) {

    // left
    case ARROW_LEFT:
      if(E.cx != 0){
        E.cx--;
      } else if (E.cy > 0) {
        E.cy--;
        E.cx = E.row[E.cy].size;
      }
      break;
   
    // right
    case ARROW_RIGHT:
      if (row && E.cx < row->size) {
        E.cx++;
      } else if (row && E.cx == row->size) {
        E.cy++;
        E.cx = 0;
      }
      break;

    // up
    case ARROW_UP:
      if(E.cy != 0){
        E.cy--;
      }
      break;

    // down
    case ARROW_DOWN:
      if(E.cy < E.numrows){
        E.cy++;
      }
      break;
  }
  
  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) {
    E.cx = rowlen;
  }
}

// This function processess the key input
void editorProcessKeypress()
{
  int c = editorReadKey();

  switch (c)
  {
  case CTRL_KEY('q'):

    // clears screen before exit
    write(STDOUT_FILENO, "\x1b[2J", 4);

    // repositions cursor before exit
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;

  case HOME_KEY:
    E.cx = 0;
    break;

  case END_KEY:
    if (E.cy < E.numrows)
      E.cx = E.row[E.cy].size;
    break;

  case PAGE_UP:
  case PAGE_DOWN:
    {
      if (c == PAGE_UP) {
        E.cy = E.rowoff;
      } else if (c == PAGE_DOWN) {
        E.cy = E.rowoff + E.screenRows - 1;
        if (E.cy > E.numrows) E.cy = E.numrows;
      }

      int times = E.screenRows;
      while(times--)
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

/*** Init ***/

void initEditor()
{
  E.cx = 0;
  E.cy = 0;
  E.rx = 0;
  E.rowoff = 0;
  E.numrows = 0;
  E.row = NULL;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;

  if (getWindowSize(&E.screenRows, &E.screenCols) == -1)
    die("getWindowSize");
  E.screenRows -= 2;
}

int main(int argc , char * argv[])
{
  enableRawMode();
  initEditor();
  if(argc >= 2){
    editorOpen(argv[1]);
  }

  editorSetStatusMessage("HELP: Ctrl-Q = quit");

  while (1)
  {

    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}
