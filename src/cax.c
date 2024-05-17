/*** Headerfiles ***/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE


#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** Define ***/

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
  char *chars;
}erow;

/* Here we are configuring the terminal window */
struct editorConfig
{
  // We will keep track of mouse cursor using x and y coordinates
  int cx, cy;
  int screenRows;
  int screenCols;
  int numrows;
  erow *row;
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


void editorAppendRow(char *s, size_t len) {

  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  int at = E.numrows;
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';
  E.numrows++;

}


/*** File I/O ***/

void editorOpen(char *filename){
  FILE *fp = fopen(filename , "r");
  if(!fp) 
    die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  linelen = getline(&line , &linecap , fp);

  while((linelen = getline(&line , &linecap , fp)) != -1){
    while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
    { 
      linelen--;
      editorAppendRow(line , linelen);
    } 
    E.row->size = linelen;
    E.row->chars = malloc(linelen + 1);
    memcpy(E.row->chars, line, linelen);
    E.row->chars[linelen] = '\0';
    E.numrows = 1;
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

// Drawing ~
void editorDrawRows(struct abuf *ab)
{
  int y;
  for (y = 0; y < E.screenRows; y++)
  {

    // Name printing
    if(y >= E.numrows){
      if(E.screenRows == 0 && y == E.screenRows / 3){
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
      int len = E.row[y].size;
      if(len > E.screenCols) 
        len = E.screenCols;
      abAppend(ab , E.row[y].chars , len);

    } 
    // Clears each line as we withdraw them
    abAppend(ab , "\x1b[K" , 3 );
    // drawing last line
    if (y < E.screenRows - 1)
    {
      abAppend(ab, "\r\n", 2);
    }
  }
}

// This function clears the whole screen
void editorRefreshScreen()
{

  struct abuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6);

  // abAppend(&ab, "\x1b[2J", 4);

  // this repostions the cursor
  abAppend(&ab, "\x1b[H", 3);

  // draws ~ throughout after refreshing and repositions the cursor
  editorDrawRows(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf) , "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
  abAppend(&ab, buf , strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

/*** Input ***/

void editorMoveCursor(int key){
  switch (key) {

    // left
    case ARROW_LEFT:
      if(E.cx != 0){
        E.cx--;
      }
      break;
   
    // right
    case ARROW_RIGHT:
      if(E.cx != E.screenCols - 1){
        E.cx++;
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
      if(E.cy != E.screenRows - 1){
        E.cy++;
      }
      break;
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
    E.cx = E.screenCols - 1;
    break;

  case PAGE_UP:
  case PAGE_DOWN:
    {
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
  E.numrows = 0;
  E.row = NULL;

  if (getWindowSize(&E.screenRows, &E.screenCols) == -1)
    die("getWindowSize");
}

int main(int argc , char * argv[])
{
  enableRawMode();
  initEditor();
  if(argc >= 2){
    editorOpen(argv[1]);
  }

  while (1)
  {

    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}
