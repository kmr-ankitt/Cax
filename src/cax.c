#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios originalTemios;

// it resets the terminal to original state
void disableRawMode(){
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &originalTemios);
}

// By default terminal is set to canonical mode which send input after pressing enter
void enableRawMode()
{
    tcgetattr(STDIN_FILENO, &originalTemios);
    atexit(disableRawMode); //it calls disableRawMode function automatically when program exits

    struct termios raw = originalTemios;

    tcgetattr(STDIN_FILENO, &raw);

    // ICRNL disables ctrl + m, ctrl + j and Enter 
    // IXON disables ctrl + s and ctrl + q
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

    // this disables \n\r
    raw.c_iflag &= ~(OPOST);
    
    raw.c_cflag |= (CS8);

    // ECHO make each keypress to be printed in the terminal, which is not of our use
    //ICANON make the program quit when 'q' is clicked
    // IEXTEN is used to disable ctrl + v
    //ISIG is used to disable ctrl + c and ctrl + z
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN |ISIG); 

    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main()
{
    enableRawMode();
    char c;

    // read() returns the number of bytes that it read, and will return 0 when it reaches the end of a file.
    // program quits when 'q' is pressed
    
    // while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q'){
    while (1){
        char c = '\0';
        read(STDIN_FILENO , &c , 1);
        if(iscntrl(c))
            printf("%d\r\n" , c);
        
        else
            printf("%d ('%c')\r\n", c, c);

        if(c == 'q')
            break;
    }
    
    return 0;
}