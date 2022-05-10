/*******************************************************************************************************************
 * 						Basic Shell program
 * 	This program is written featuring some basic Shell's behaviors like executing Shell's built-in commands,
 * executing all executable file programs, and can do piping between commands. The program will prompt the user
 * "sish> " and wait for the user to enter a single command (meaning ";" is not supported in this program). The
 * command can be piped using "|" but not other types of redirection operators like "<", "<<", ">", ">>", and etc.
 * After getting input command from the user, the program will first remove the newline '\n' at the end of the 
 * input line and store the modified input line into an archive called "history". Subsequently, the program will
 * determine if pipeline is needed or not. If it does not need pipeline, it will simply execute either built-in 
 * commands or executable file programs. If it does need pipeline, the program will split the command line
 * by separating the string using a delimiter "|". Following splitting the string, the program will have a 
 * multiple blocks of independents commands, and it allocates pipeline(s) corresponding the number of pipelines
 * it needs. It then create multiple child processes to execute each individual commands from left to right style,
 * and each child process will communicate to each other using pipeline(s). This process will not stop until 
 * there is no more commands to process. Subsequently, the program will not terminate until the user enter 
 * "exit" or "logout" as a command (without piping).
 *
 * Written by Gia Bao Tran, gbt190000, University of Texas at Dallas, starting on March 2nd
 * *******************************************************************************************************************/

#define _GNU_SOURCE
#include <sys/wait.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define MAXLINE 4096

void err_exit(char*);
void rmbksp(char*);
void strsplt(char*[], char*, const char*, char**);
int execbldi(char*[], char*[], char**, int*);
void shist(char*[]);
void addhst(char*[], const char*,  int*);
void clrhist(char*[]);
int isNum(char*);
int countPipe(char*);

int main(int argc, char* argv[]){
	char* iline = NULL;	//Input line getting from the user 
	char* temp = NULL; // temporary pointer
	size_t maxsize = MAXLINE; 	//Max size of the input line
	char* myargs[1000]; 	//Arguments for execute command in shell	
	char* hist[100];	//History of commands with a space of 100 commands
	char* saveptr;	// Save pointer for the tokenization in strsplt
	int histCount = 0;	//Counter to keep track of the current size of history
	int pipeCount = 0;	//Counter for how many pipes needed for allocation

	while(1){
		//Only prompt input when there is no more command line to process
		if(iline == NULL){
			printf("sish> ");			//Prompt shell 
			getline(&iline, &maxsize, stdin);	//Get the input line from the user
			rmbksp(iline);			//Remove the backspace in inputline			
			*(iline + strlen(iline) - 1) = '\0'; 	// Replace the newline by the null-terminator				
			addhst(hist, iline, &histCount);	// Adding the new command line into history
		}


		
		// Get how many pipes do we need to allocate in the future
		pipeCount = countPipe(iline);

		// There is no pipes needed -> single simple command and no piping
		if(pipeCount  == 0){
			// Only need one child process to execute the file program
			pid_t cpid;

			strsplt(myargs, iline, " ", &saveptr);	//Split the input line based on the delimiter and store them into myargs
			
			/*Execute the built-in command if the command is cd exit and history*/
			if(strcmp(myargs[0], "cd") == 0 || strcmp(myargs[0], "exit") == 0 || strcmp(myargs[0], "logout") == 0 ||  strcmp(myargs[0], "history") == 0){
				execbldi(myargs, hist, &temp, &histCount);	// Execute built-in commands
				iline = temp; // Potentially get the command line from history via command format "history -offset"
				temp = NULL; // Resetting the pointer
				// If the history points to null after calling, then history -c must have been invoked
				// If so, reset the counter for history
				// For more details, check execbldi function
				if(hist[0] == NULL) 
					histCount = 0;
			}
			//Otherwise, the command must be from executable file command
			else{
				cpid = fork();		//Creating a child process
			
				if(cpid < 0){		//Prompt error when fork fails
					err_exit("fork failed!");
				}
				//This is for child process
				if(cpid == 0){
					execvp(myargs[0], myargs);	//Execute the executable file command
					printf("Command %s not found!\n",myargs[0]); 	//If the execution failed, prompt error
					exit(EXIT_FAILURE);				//Exit with error code
				}
				//Meanwhile the parent will wait for the child, if the waiting failed -> prompt error and exit
				if(waitpid(cpid, NULL, 0) == -1){
					err_exit("Wait failed!");
				}
				iline = NULL; //Resetting the input line
			}
		}
		// Otherwise, pipeline must be involved. 
		else{
			// Creating the pipeline.
			// The number of pipeline is corresponding to the pipeCount
			int fd[pipeCount][2]; 
			
			int count = pipeCount;	// A temporary counter. Can be used for multiple purposes
			char* my_sub_args[1000]; // Arguments for commands that are separated by piping operators "|"
			
			// An array of child process.
			// The size of the array is corresponding to the (pipeCount + 1) 
			// since we need at least one to write and one to receive.
			pid_t cpid[pipeCount + 1];

			// Piping all the "pipeline" 
			// count is used for looping condition
			while(count > 0){	
				if(pipe(fd[count - 1]) == - 1){  // If the piping failed, prompt error and exit
					err_exit("pipe failed");
				}
				count--;
			}
			
			// Get all commands that are separated by the piping operators "|"
			strsplt(myargs, iline, "|", &saveptr);
			
			// Reseting count for different looping condition
			count = 0;

			// Loop until there is no more command to process
			while(myargs[count] != NULL){
				cpid[count] = fork();	// Creating a child process 
				
				if(cpid[count] < 0){ // If fork failed -> prompt error and exit
					err_exit("fork failed");
				}
				// This is child process
				if(cpid[count] == 0){ 
					// The first child process does not need to read from the pipe
					// When the child process does need to read from the pipe,
					// It will read the (count - 1)th pipe
					if(count != 0 && count <= pipeCount){ 
						if(dup2(fd[count - 1][0], STDIN_FILENO) == -1){ // If dup2 failed -> prompt err and exit
							fprintf(stderr, "dup2 read failed in child %d\n", (count + 1));
							exit(EXIT_FAILURE);
						}
					}
					// The last child process does not need to write to the pipe
					// When the child does need to write to the pipe,
					// It will write to the (count)th pipe
					if(count < pipeCount){
						if(dup2(fd[count][1], STDOUT_FILENO) == -1){ // If dup2 failed -> prompt err and exit
							fprintf(stderr, "dup2 write failed in child %d\n", (count + 1));
							exit(EXIT_FAILURE);
						}
					}
					
					// Closing all the writing ends and reading ends of the pipes
					int pipeid = 0;
					while(pipeid < pipeCount){
						close(fd[pipeid][0]);
						close(fd[pipeid][1]);
						pipeid++;
					}
					
					char* saveptr1;
					// Get all arguments from the command
					strsplt(my_sub_args, myargs[count], " ", &saveptr1);
					
					// Execute all the executable file programs
					execvp(my_sub_args[0], my_sub_args); // Execute the executable file program
					err_exit(my_sub_args[0]); // If execution failed, prompt error and exit
				}

				// From this point, only parent process
				count++;
			}
			
			// After the parent are done looping, close all pipes
			int pipeid = 0;
			while(pipeid < pipeCount){
				close(fd[pipeid][0]);
				close(fd[pipeid][1]);
				pipeid++;
			}
			
			// And in while, wait for all child processes to terminate
			int cpidCount = 0;
			while (cpidCount <= pipeCount){
				if(waitpid(cpid[cpidCount], NULL, 0) == -1){ // If waiting failed -> prompt error and exit
					fprintf(stderr, "wait failed in child %d\n", (cpidCount + 1));
					exit(EXIT_FAILURE);
				}
				cpidCount++;
			}
			iline = NULL; // Resetting the input line
		}

	}
	exit(EXIT_SUCCESS);	

}



// @param:
// 	+ char* msg: a given error message to display
// @ret: none
// @funct: Displaying error message
void err_exit(char* msg){
	perror(msg);
	exit(EXIT_FAILURE);
}


// @param: 
// 	+ char* s: a given string to remove '\b' in the string
// @ret: none
// @funct: Remove backspace from the string
void rmbksp(char* s){
	char* dummy = s;	//Pointer 1
	char* dummy2 = s;	//Pointer 2
	int charCount = 0;	//Counting how many characters in the string
	
	//Loop until the first pointer reaches string null-terminator
	while(*dummy != '\0'){
		/* Every time the first pointer encounters the backspace '\b'
		 * the cursor aka the second pointer will decrement and so as the 
		 * charCount. As long as the charCount is greater than 0, the
		 * second pointer will decrement so that it will not access to
		 * other block of memory*/
		if(*dummy == '\b'){
			if(charCount > 0){	
				dummy2--;
				charCount--;
			}
		}
		/* Otherwise, keep replacing the element of second pointer with 
		 * the element of the first pointer. Increment both pointers and
		 * charCount*/
		else{
			*dummy2 = *dummy;
			dummy2++;
			charCount++;
		}
		dummy++;
	}
	/* The rest of elements of second pointer are "deleted"
	 * So we will replace the rest of elements with null-terminator*/
	while(*dummy2 != '\0'){
		*dummy2 = '\0';
		dummy2++;
	}
	return;
}

// @param: 
// 	+ char* buf[] : a buffer to store the string after splitting
// 	+ char* input : a given string to be splitted by the delim
// 	+ const char* delim: a given delimiter to separate string
// 	+ char** saveptr: a pointer used for strtok_r
// @ret: none	
// @funct: Splitting the string depending on the delimiter
void strsplt(char* buf[], char* input, const char* delim, char** saveptr){
	int index = 0; 		//Index of the buffer

	/* Allocating a temporary string s
	 * The reason for this is because when the argument "input" 
	 * is put into the parameter of strtok_r, the integrity of 
	 * the string "input" will be lost. Therefore, we need a replica
	 * of the input string */
	char* s = (char*) malloc(sizeof(char) * strlen(input) + 1);
	if(s == NULL){ // If the allocation failed -> prompt error and exit
		err_exit("Cannot malloc\n");
	}

	strcpy(s, input); // String s copies string input

	while(1){
		// Store the tokenized string (without the delimiter) in the buffer
		buf[index] = strtok_r(s, delim, saveptr); 
		s = NULL;

		//If there is no more token, exit the loop
		if(buf[index] == NULL){
			index--;
			break;
		}
		index++;
	}

	// Set the last token as the null pointer
	if(strcmp(buf[index], "") == 0)
		buf[index] = NULL;
	
	// Since we do not need the temporary string s anymore, free the memory
	free(s);

	return;
}

// @param:
// 	+ char* args[]: a list of arguments to execute a built-in command
// 	+ char* hist[] : a history aka archive of all entered commands
// 	+ char** temp: temporary pointer to store a command from history
// 	+ int* histCount: a pointer to the address of the counter of history
// @ret: the status of execution (0 for success and -1 for failure)
// @funct: Execute the shell built-in command
int execbldi(char* args[], char* hist[], char** temp, int* histCount){
	if(strcmp(args[0], "cd") == 0){
		// If there is a path of directories (aka args[1] is not null)
		// use the system call chdir to try to change directory
		// If the change failed, prompt error
		if(args[1] != NULL && chdir(args[1]) == -1){
			perror("~bash: No such file or directory!");
			return -1;
		}
		// If there is no arguments -> prompt error
		else if(args[1] == NULL){
			fprintf(stderr,"~bash: Argument needed!\n");
			return -1;
		}
	}
	// Shell will be terminated if the user enter exit or logout
	if(strcmp(args[0], "exit") == 0 || strcmp(args[0], "logout") == 0){
		exit(EXIT_SUCCESS);
	}
	if(strcmp(args[0], "history") == 0){
		// Display history of commands if there is no arguments
		if(args[1] == NULL){
			shist(hist);	//Show history
		}
		// If the argument is -c, then shell will delete all history of commands
		else if (strcmp(args[1], "-c") == 0){	
			clrhist(hist);	//Clear history
		}
		// Otherwise, the command might have this format "history num"
		else{
			// Check if the second argment "num" is an actual number
			if(isNum(args[1]) == 0){
				int argNum = atoi(args[1]); // parse the string into a number
				// History's size is only ranging from 1-100
				// If the counter >= 100 (0-index based) -> prompt error and exit
				if(argNum >= 100) {
					fprintf(stderr,"Index out of bound... in history\n");
					return -1;
				}
				// If there is a valid data at the given index -> display data
				else if(hist[argNum] != NULL) {
					// Get the command line from history
					// If the history is already "full" -> expected the command line needed will be 
					// offset by 1 since all the elements in the history will be pushed up
					// Hence store the command line at offset (argNum - 1) if the history is full
					// or at offset (argNum) otherwise
					*temp = (*histCount == 100) ? hist[argNum - 1] : hist[argNum];
					return 0;
				}
				// Else the given index does not hold a valid data -> prompt error and exit
				else {
					fprintf(stderr, "Index out of bound... in history\n");
					return -1;
				}
			}
			// Otherwise, the argument "num" is not an actual number -> prompt error and exit
			else{
				fprintf(stderr, "-bash: history: %s: numeric argument required\n", args[1]);
				return -1;
			}
		}
	}
	return 0;
}

// @param:
// 	+ char* hist[]: A history aka archive of entered commands
// @ret: none
// @funct: Clear all elements in the history
void clrhist(char* hist[]){
	int index = 0;
	//Loop until there is no more history                                      }
	while(hist[index] != NULL){
		free(hist[index]);	//Free memory of the current history index
		hist[index] = NULL;	//Set the pointer to NULL
		index++;
	}
	return;
}

// @param:
// 	+ char* hist[]: A history aka archive of entered commands
// @ret: none
// @funct: Show all history of commands
void shist(char* hist[]){
	int index = 0;
	//Showing the history of command until there is no more commands to show
	while(hist[index] != NULL){
		printf("%d %s\n", index, hist[index]);
		index++;
	
	}
	return;
}

// @param:
// 	+ char* hist[]: A history aka archive of entered commands
// 	+ const char* cmd: A latest entered command
// 	+ int* count: A pointer to the counter. Used to increment/decrement count through dereference
// @ret: none
// @funct: Add a command to history
void addhst(char* hist[], const char* cmd, int* count){
	//The range of history will be from 0-99
	if(*count == 100){
		// If the size of history is already 100, then free the
		// oldest memory aka hist[0].
		// Push every commands in history up 1 slot
		// And then add the newest command at the bottom
		free(hist[0]);	

		// All commands move up 1 slot
		int index = 0;
		while(index < 99){
			hist[index] = hist[index + 1];
			index++;
		}
		*count = *count - 1;
	}

	//Creating a new block of memory to store the commands history
	char* dst = (char*) malloc(sizeof(char) * strlen(cmd) + 1);
	if(dst == NULL){ // If malloc failed -> prompt error and exit
		err_exit("malloc failed");
	}
	hist[*count] = strcpy(dst, cmd); // Store the entered command into history
	
	//If strcpy failed -> prompt error
	if(hist[*count] == NULL){
		err_exit("strcpy in history failed!");
	}

	*count = *count + 1;
	return;
}

// @param:
// 	+ char* s: A given string s to validate
// @ret: 0 is for number and -1 is for not number
// @funct: Validating the numeric string
int isNum(char* s){
	char* dummy = s; 
	while(*dummy != '\0'){
		if(*dummy > 57 || *dummy < 48) //ASCII value for number 0-9
			return -1;
		dummy++;
	}
	return 0;
}

// @param:
// 	+ char* s: A given string s to count pipe
// @ret: A number of pipes needed
// @funct: Counting a number of pipes needed for allocation
int countPipe(char* s){
	int count = -1;
	char* token = NULL; char* saveptr;
	/* A temporary string
	 * The reason for this is similar to the function strsplt
	 * Declaring the temporary string is for preserving the integrity 
	 * of the input string s*/
	char* temp = (char*) malloc(sizeof(char) * strlen(s) + 1);
	if(temp == NULL){ // If malloc failed -> prompt error and exit
		err_exit("can't malloc");
	}
	
	strcpy(temp, s); // The temporary string copies the input string
	while(1){
		token = strtok_r(temp, "|", &saveptr);
		temp = NULL;
		// If there is no more token -> break
		if(token == NULL)
			break;
		count++;

	}

	// After done tokenizing, free the memory that temporary string holds
	free(temp);

	return count;
}
