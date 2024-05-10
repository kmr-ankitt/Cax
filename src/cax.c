/*** Headerfiles ***/



#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** Define ***/

#define CTRL_KEY(k) ((k) & 0x1f)

/*** Data***/

/* Here we are configuring the terminal window */
struct editorConfig {
  int screenRows;
  int screenCols;
  struct termios originalTemios;
};

struct editorConfig E;

/*** Terminal ***/

// this kills the editor
void die(const char *s ){

    // clears the screens before exit
    write(STDOUT_FILENO, "\x1b[2J" , 4);

    // repositons the cursor before exit
    write(STDOUT_FILENO, "\x1b[H" , 3);

    perror(s);
    exit(1);
}

// it resets the terminal to original state
void disableRawMode(){
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.originalTemios) == -1)
        die("tcsetattr");
}

// By default terminal is set to canonical mode which send input after pressing enter
void enableRawMode()
{
    if(tcgetattr(STDIN_FILENO, &E.originalTemios) == -1)
        die("tcgetattr");

    atexit(disableRawMode); //it calls disableRawMode function automatically when program exits

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
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN |ISIG); 

    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

// This functions reads the key input 
char editorReadKey(){
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c , 1)) != 1){

        // if no input then die
        if(nread == -1 && errno != EAGAIN)
            die("read");

    }
    return c;
}

int getCursorPosition(int *rows , int *cols){
  char buf[32];
  unsigned int i = 0;

  if(write(STDOUT_FILENO , "\x1b[6n" , 4) != 4)
      return -1;

  while(i < sizeof(buf) - 1){
    if(read(STDIN_FILENO , &buf[i], 1) !=1 )
      break;

    if(buf[i] == 'R')
      break;
    i++;
  }
  buf[i] = '\0';
  
  if(buf[0] != '\x1b' || buf[1] != '[')
    return -1;
  if(sscanf(&buf[2] , "%d;%d" , rows, cols)!= 2)
    return -1;

  return 0;
}

/* Here we are attaining the how many colums and rows high terminal will be using winsize through ioctl  */
int getWindowSize(int *rows , int *cols){
  struct winsize ws;
  
  if(ioctl(STDOUT_FILENO , TIOCGWINSZ , &ws) == -1 || ws.ws_col == 0)
  {
    
   // Since window size may vary from system to system,  we will check it by using position the cursor at the bottom and the tracking its postions using escape sequence

    if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B" , 12) != 12)
      return -1;
    return getCursorPosition(rows , cols);
  }
  else{
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/*** Output ***/

// Drawing ~ 
void editorDrawRows(){
    int y;
    for (y = 0; y < E.screenRows; y++)
    {
        write(STDOUT_FILENO, "~\r\n" , 3);
    }
    
}

// This function clears the whole screen
void editorRefreshScreen(){

    // this escape sequence [2J clears the whole screen. meanwhile [0J clears only upto cursor  
    write(STDOUT_FILENO, "\x1b[2J" , 4);

    // this repostions the cursor
    write(STDOUT_FILENO, "\x1b[H" , 3);

    // draws ~ throughout after refreshing and repositions the cursor
    editorDrawRows();
    write(STDOUT_FILENO, "\x1b[H" , 3);
}

/*** Input ***/

// This function processess the key input
void editorProcessKeypress(){
    char c = editorReadKey();

    switch (c){
        case CTRL_KEY('q'):

            // clears screen before exit
            write(STDOUT_FILENO , "\x1b[2J" , 4);
            
            // repositions cursor before exit
            write(STDOUT_FILENO , "\x1b[H" , 3);
            exit(0);
            break;
    }
}


/*** Init ***/

void initEditor(){
  if(getWindowSize(&E.screenRows, &E.screenCols) == -1) 
    die("getWindowSize");
}

int main()
{
    enableRawMode();
    initEditor();
    // read() returns the number of bytes that it read, and will return 0 when it reaches the end of a file.
    // program quits when 'q' is pressed
    
    // while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q'){
    while (1){

    //     char c = '\0';
    //     if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) die("read");
    //     if(iscntrl(c))
    //         printf("%d\r\n" , c);
    //     else
    //         printf("%d ('%c')\r\n", c, c);
    //     if(c == CTRL_KEY('q')) 
    //         break;

        editorRefreshScreen();
        editorProcessKeypress();

    }
    
    return 0;
}
