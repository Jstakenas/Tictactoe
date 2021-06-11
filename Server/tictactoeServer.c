/*************************************************************/
/* This program is a modified version of the original		 */
/* 'pass and play' tictactoe which was developed by Dave Ogle*/
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* This modified version is the *server* half to remote-play */
/* developed by Jordan Stakenas, which allows two players to */
/* match head-to-head in an epic online session of tictactoe.*/
/* Player two must use the *client* half to connect to server*/
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Usage is as follows: 									 */
/*			make											 */
/*			./tictactoeServer <portNumber>					 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*data gram format:											 */
/* |version|command|choice|game#|sequence#|					 */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Known bugs as follows: No bugs currently known			 */
/*************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

//various defines of the tictactoe board game for later use
#define ROWS  3
#define COLUMNS  3
#define STATUSPRAMS 3
#define NEWGAME 0x00
#define MOVE 0x01
#define GAMEOVER 0x02
#define RESUME 0x03
#define VERSION 0x06
#define TIMETOWAIT 30	//timeout for server as a whole
#define TIMEOUT  15 	//timeout for individual game sessions, in seconds
#define OFFSETTO 45		//IF YOU CHANGE THE ABOVE, CHANGE THIS ONE SO THEY SUM TO 60
#define MCTO 5			//timeout for multicast socket
#define MAXGAMES 10

#define MC_PORT 1818			//used for MultiCasting
#define MC_GROUP "239.0.0.1"

/* C language requires that you predefine all the routines you are writing */
int checkWin(char board[ROWS][COLUMNS][MAXGAMES], int gameIndex);
void printBoard(char board[ROWS][COLUMNS][MAXGAMES], int gameIndex);
int tictactoe();
int initSharedState(char board[ROWS][COLUMNS][MAXGAMES], long int gameStatusList[MAXGAMES][STATUSPRAMS]);
int bindSocket(int portNum, int socketType);
int aiChoice(char board[ROWS][COLUMNS][MAXGAMES], int gameIndex, int choice);
int setChoice(char board[ROWS][COLUMNS][MAXGAMES], int gameIndex, int choice, int player);
int resetBoard(char board[ROWS][COLUMNS][MAXGAMES], int gameIndex, long int gameStatusList[MAXGAMES][STATUSPRAMS]);
int checkOpenGame(long int gameStatusList[MAXGAMES][STATUSPRAMS]);
int checkTimeOuts(char board[ROWS][COLUMNS][MAXGAMES], long int gameStatusList[MAXGAMES][STATUSPRAMS]);

int main(int argc, char *argv[]){
	int rc;
	
	if (argc != 2){
		printf ("Usage is \"tictactoeServer <port#>\"\n");
		exit(1);
	}
	
	rc = tictactoe(htons(atoi(argv[1]))); 			// call the 'game' 
	
	if (rc == -1){printf("something REALLY bad happened..");}
	
	return 0; 
}

int tictactoe(int portNum){
	/* this is the meat of the game */
	int checkWVal = -1;					//used to check win conditions
	int choice = 0;  					//used for keeping track of choice users make
	char input[1000];					//for ease in sending a char byte
	int gameIndex;						//used for tracking games
	int skipToListen = 0;
	struct timeval timeStamp;
	//client socket descriptor list[MAXGAMES]={0};  ?NEW
	int clientSDList[MAXGAMES] = {0};
	//fd_set socketFDS, pass to the select; ?new
	fd_set socketFDS;
	int maxSD = 0;
	
	//manages various info about the games
	//IE gameStatusList[0][0] holds the time that game 0 last recvd a datagram
	//gameStatusList[0][1] holds my current sequence number for game 0
	//gameStatusList[0][2] holds my previous position choice for game 0
	long int gameStatusList[MAXGAMES][STATUSPRAMS];
	
	//initialize game boards
	char gameBoards[ROWS][COLUMNS][MAXGAMES];
	initSharedState(gameBoards, gameStatusList);
	
	memset (input, 0, 1000);	//zero'ing out 'buffer'
	
	//initialize socket info
	int rc, sd, MC_sock; 					//socket descriptions and return code
	int connected_sd;
	struct sockaddr_in from_addr;			//setting up obj for client
	socklen_t fromLen;
	from_addr.sin_family = AF_INET;			//server occasionally has a hiccup if this isn't defined
	fromLen = sizeof(from_addr);			//setting fromLen here solves a rare issue with recv later on
	struct ip_mreq mreq;
	
	
	sd = bindSocket(portNum, 0);				//initialize socket before listening
	
	//initialize multicast socket	
	struct sockaddr_in addr;
	socklen_t addrlen;
	int cnt;
	
	MC_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (MC_sock < 0) {
		perror("socket");
		exit(1);
	}
	bzero((char *)&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(MC_PORT);
	addrlen = sizeof(addr); 
	
	//binding MC_sock in a standard fashion
	if (bind(MC_sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {        
		perror("bind");
		exit(1);
	}
	
	//setting up the MC_sock as a multicast socket
	mreq.imr_multiaddr.s_addr = inet_addr(MC_GROUP);         
	mreq.imr_interface.s_addr = htonl(INADDR_ANY);         
	if (setsockopt(MC_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
		perror("ERROR: setsockopt mreq");
		exit(1);
	}
	
	//listen
	rc = listen(sd, 5);
	maxSD = sd;
	
	
	do{	
		printf("\n");
		memset (input, 0, 1000);			//zero'ing out 'buffer'
		
		
		//zero out FD_ZERO(&socketFDS)
		FD_ZERO(&socketFDS);
		//setting the two sockets in the file descriptor set
		FD_SET(sd, &socketFDS);
		FD_SET(MC_sock, &socketFDS);
		
		
		if (MC_sock > maxSD){
			maxSD = MC_sock;
		}
		
		//for loop here to initialize clientSDList
		for(int i=0; i<MAXGAMES; i++){
			if (clientSDList[i]>0){
				FD_SET(clientSDList[i], &socketFDS);
				
				if (clientSDList[i] > maxSD){
					maxSD = clientSDList[i];
				}
			}
		}
		
		
		//block until something arrives
		printf("waiting on input from players. . .\n");
		rc = select(maxSD+1, &socketFDS, NULL, NULL, NULL);
		
		
		//begin multicast section, snippets taken from daves' multicast server code
		if FD_ISSET(MC_sock, &socketFDS){		
			printf("Multicast recieved\n");
			if (checkOpenGame(gameStatusList) != -1){
				cnt = recvfrom(MC_sock, input, sizeof(input), 0, 
					(struct sockaddr *) &addr, &addrlen);
				if (cnt < 0) {
					printf("Got nothing from multicast socket\n");
				} else if (cnt == 0) {
					printf("got message from multicast but it was empty. Did we lose connection?\n");
				} else { //successfully read in from multicast
					printf("@%s: recieved from MC: %i, %i\n", 
						inet_ntoa(addr.sin_addr), input[0], input[1]);
					
					if(input[0] != VERSION){ //check version number
						printf("Message recieved but has incorrect version! ignoring. . .\n");
					
					}
					else{	//valid message recieved from MC
						memset (input, 0, 1000);
						input[0] = VERSION;
						input[1] = (ntohs(portNum) >> 8) & 0xff; 
						input[2] = (ntohs(portNum)) & 0xff;
						//doing some bit-shift magic above in order to get the port number into index1 and 2
						// of the message to send to client.
						//Note that portNum is passed into this main function via htons, but this shifting puts
						//the port number into network-byte order -- with this in mind I wanted to make sure
						//that the port was in host-byte order before shifting
						
						cnt = sendto(MC_sock, input, sizeof(input), 0,
							(struct sockaddr *) &addr, addrlen);
						if (cnt <0){
							perror("sendto MC");
						}
						else {
							printf("successfully sent %i %i", input[0], portNum);
						}
					}
				}
			}
			else{
				printf("Recieved request from MC socket, but no games are available!\nIgnoring. . .\n");
			}
		}
		memset (input, 0, 1000);
		//end multicast
		
		//new stream connection arrived, find it a socket
		if (FD_ISSET(sd, &socketFDS)){
			connected_sd = accept(sd, (struct sockaddr *) &from_addr, &fromLen);
			for (int i=0; i<MAXGAMES; i++){
				if(clientSDList[i] == 0){
					clientSDList[i] = connected_sd;
					break;	//found a socket for new connection
				}
			}
		}
		
		//read all other sockets
		for (int i=0; i<MAXGAMES; i++)
		if(FD_ISSET(clientSDList[i], &socketFDS)){ //check to see if the socket at this index is set
			rc = read(clientSDList[i], &input, 14);	//if so, perform a read on this connection
			if (rc == 0){	//means client lost connection
				printf("Client from board %d has disconnected! Clearing board.\n", i);
				resetBoard(gameBoards, i, gameStatusList);
				close( clientSDList[i]);
				clientSDList[i] = 0;
				skipToListen = 1;
			}			
			else{	//means we got good input, handle as normal
		//player 2 //		
				//error checking below, do we still have the same version?
				//did i get a new game request?
				printf("Recieved %i %i %i %i %i\n", input[0], input[1], input[2], input[3], input[4]);
				if (input[0] != VERSION){
					printf("Request is using incompatible version type. ignoring request. . .\n ");
					skipToListen = 1;
				}
				else if(input[1] == NEWGAME){
					printf("A player 2 is looking for a new game!\n");
					//check for open game, and assign a gameIndex = openGame(gameBoards);
					//if no games are open, set skipToListen and print a statement abt it
					
					int freeGame = checkOpenGame(gameStatusList);
					
					if (freeGame == -1){	//if checkOpenGame returns a -1, all boards are occupied
						printf("No game boards are currently available! Ignoring request.\n");
						skipToListen = 1;
					}
					else{					//otherwise, start a new game at the free space returned by checkOpenGame
						printf("Game board #%d is open! responding accordingly.\n", freeGame);
						gameIndex = freeGame;
						gettimeofday(&timeStamp, NULL);
						gameStatusList[gameIndex][0] = timeStamp.tv_sec;
						choice = 0; //ensure predictable AI
					}
				}
				else if(input[1] == RESUME){
					printf("A player 2 is looking to resume a game!\n");
					//Nearly identical to 'NEWGAME' above, except if an open lobby is found
					//it will manually initiate a gameboard by inserting the given values.
					//Simple as that, AI already chooses a tile based on existing tiles,
					//so no external changes should be needed outside of this else if
					int freeGame = checkOpenGame(gameStatusList);
					
					if (freeGame == -1){	//if checkOpenGame returns a -1, all boards are occupied
						printf("No game boards are currently available! Ignoring request.\n");
						skipToListen = 1;
					}
					else{					//otherwise, resume a new game at the free space returned by checkOpenGame
						printf("Game board #%d is open! responding accordingly.\n", freeGame);
						gameIndex = freeGame;
						gettimeofday(&timeStamp, NULL);
						gameStatusList[gameIndex][0] = timeStamp.tv_sec;
						gameStatusList[gameIndex][1] = input[4];
						choice = input[2]; //ensure predictable AI by using the passed in choice
						int row, column;
						
						for(int i=0;i<9;i++){
							row = (int)(i / ROWS); //doing some pre-emptive math to fill spaces
							column = i % COLUMNS;
							//if this given tile is placed, place it on the board
							if (input[5+i] == 'X' || input[5+i] == 'O'){
								gameBoards[row][column][gameIndex] = input[5+i];
							}		//but otherwise just ignore it incase bad data is sent
						}
						printBoard(gameBoards, gameIndex); //confirm it was imported correctly
					}
				}
				else{
					/*
						By now, we know that the client didn't error out, and 
						the request was not a new game request or a resume game request
					*/
					//retrieve recieved values
					choice = input[2];
					gameIndex = input[3];
					int sequence = input[4];
					
	//TODO: 		Below is a nest of if statements. needs to be cleaned up.
					//Here's a basic flow of whats going on below
					//line 206 -- IS GAMEOVER COMMAND?
					//line 211 -- IF GAME IS ACTIVE
						// 212 -- IF >sequence+1, ignore+move on, do nothing
						// 216 -- IF =sequence+1, we're good! 
							//COMMAND = GAMEOVER?
								//reset gameboard
							//COMMAND = MOVE? 
								//handle as normal (below)
						//ELSE its bad data, ignore move on do nothing
					//else bad data	
					
					if(input[1] == GAMEOVER){
						//redundancy
						//if the command i got was a gameover, i want to make sure the AI
						//doesnt try to play this board
						//Sometimes during the 60s grace period, the lobby is closed
						//but a re-ACK of gameover is recvd. Basically just confirms nothing funky
						//happens
						skipToListen = 1;
					}
					if(gameStatusList[gameIndex][0] != -1){	
						if (sequence > gameStatusList[gameIndex][1]+1){
							printf("recieved a command with sequence beyond scope. Ignoring. . .\n");
							skipToListen = 1;
						}
						else if (sequence == gameStatusList[gameIndex][1]+1){
							//datagram with proper information confirmed!
							//Now is it a GAMEOVER or is it move cmd?
							if(input[1] == GAMEOVER){ //if gamemode, then reset gameboard
								printf("recieved ACK for gameover, ");
								resetBoard(gameBoards, gameIndex, gameStatusList);
								skipToListen = 1;
							}
							else if(input[1] == MOVE){ //otherwise, its a move and handle it as such
								//set these values on the game board
								printf("Player chose %d for board #%d\n", choice, gameIndex);
								setChoice(gameBoards, gameIndex, choice, 2);
								//update 'last active' timeval, in seconds
								gettimeofday(&timeStamp, NULL);
								gameStatusList[gameIndex][0] = timeStamp.tv_sec;
								gameStatusList[gameIndex][1]++;
								
								//see if there was a win or draw
								
								//SEND GAMEOVER HERE
								//HOLD LOBBY OPEN 60S (set timeout = current time +45)
								//LET TIMEOUT HANDLE RESET AFTER 60S
								checkWVal = checkWin(gameBoards, gameIndex);
								if (checkWVal == 1){
									//p2 won
									printBoard(gameBoards, gameIndex);
									printf("==>\aPlayer 2 of board #%d wins\n", gameIndex);
									
									//send gameover ACK
									gameStatusList[gameIndex][1]++;
									input[0] = VERSION;
									input[1] = GAMEOVER;
									input[2] = 0;
									input[3] = gameIndex;
									input[4] = gameStatusList[gameIndex][1];
									
									//rc = sendto(sd, &input, 5, 0, (struct sockaddr *)&from_addr, sizeof(from_addr));			//send that choice to p2
									printf("sending game over %i %i %i %i %i", input[0], input[1], input[2], input[3], input[4]);
									rc = send(clientSDList[i], &input, 5, 0);
									if (rc < 1) { //confirm it sent
										perror("error - sento");
										printf("Terminating. . .\n"); 
										exit(2);
									}
									
									skipToListen = 1;
									
									//this next bit will cause the game to timeout and reset after 60s
									gameStatusList[gameIndex][0] = timeStamp.tv_sec + OFFSETTO;
								}
								else if (checkWVal == 0){
									//game draw
									printBoard(gameBoards, gameIndex);
									printf("==>\aGame board #%d was a draw\n", gameIndex);
									
									//send gameover ACK
									gameStatusList[gameIndex][1]++;
									input[0] = VERSION;
									input[1] = GAMEOVER;
									input[2] = 0;
									input[3] = gameIndex;
									input[4] = gameStatusList[gameIndex][1];
									
									//rc = sendto(sd, &input, 5, 0, (struct sockaddr *)&from_addr, sizeof(from_addr));			//send that choice to p2
									printf("sending game over %i %i %i %i %i", input[0], input[1], input[2], input[3], input[4]);
									rc = send(clientSDList[i], &input, 5, 0);
									if (rc < 1) { //confirm it sent
										perror("error - sento");
										printf("Terminating. . .\n"); 
										exit(2);
									}
									
									skipToListen = 1;
									//reset gameboard
									gameStatusList[gameIndex][0] = timeStamp.tv_sec + OFFSETTO;
								}
							}
							else {
								printf("Did not recognize command! Ignoring request. . .\n");
								skipToListen = 1;
							}						
						}					
					}
					else{
						printf("Recieved a command for a game that is not currently active. Ignoring. . .\n");
						skipToListen = 1;
					}
				}
			}
		//end player 2//
	
		//player 1 AI//
			if (!skipToListen){
				//get AI choice from funct
				choice = aiChoice(gameBoards, gameIndex, choice);
				printf("AI chose number %d for board #%d\n", choice, gameIndex);
				
				//increment the sequence #
				gameStatusList[gameIndex][1]++;
				
				memset (input, 0, 1000);		//zero'ing out 'buffer'
				
				//prepare move msg
				input[0] = VERSION;
				input[1] = MOVE;
				input[2] = choice+0x00;
				input[3] = gameIndex;
				input[4] = gameStatusList[gameIndex][1];
				gameStatusList[gameIndex][2] = choice;
				
				printf("sending %i %i %i %i %i\n", input[0], input[1], input[2], input[3], input[4]);
								
				//with socket, sending input, 5 bytes, flags are 0
				//rc = sendto(sd, &input, 5, 0, (struct sockaddr *)&from_addr, fromLen);			//send that choice to p2
				rc = send(clientSDList[i], &input, 5, 0);
				if (rc < 1) { 						//confirm it sent
					perror("error - sento");
					printf("Terminating. . .\n"); 
					exit(2);
				}
				
				//set the choice that the AI chose
				setChoice(gameBoards, gameIndex, choice, 1);
				
				//will not reset game board here, must wait for ACK
				checkWVal = checkWin(gameBoards, gameIndex);
				
				if (checkWVal == 1){
					//p1 won
					printBoard(gameBoards, gameIndex);
					printf("==>\aPlayer 1 of board #%d wins\n", gameIndex);
					
					//send gameover ACK
					gameStatusList[gameIndex][1]++;
					input[0] = VERSION;
					input[1] = GAMEOVER;
					input[2] = 0;
					input[3] = gameIndex;
					input[4] = gameStatusList[gameIndex][1];
									
					//rc = sendto(sd, &input, 5, 0, (struct sockaddr *)&from_addr, sizeof(from_addr));			//send that choice to p2
					printf("sending game over %i %i %i %i %i", input[0], input[1], input[2], input[3], input[4]);
					rc = send(clientSDList[i], &input, 5, 0);
					if (rc < 1) { //confirm it sent
						perror("error - sento");
						printf("Terminating. . .\n"); 
						exit(2);
					}
				}
				else if (checkWVal == 0){
					//game draw
					printBoard(gameBoards, gameIndex);
					printf("==>\aGame draw\n");
					
					//send gameover ACK
					gameStatusList[gameIndex][1]++;
					input[0] = VERSION;
					input[1] = GAMEOVER;
					input[2] = 0;
					input[3] = gameIndex;
					input[4] = gameStatusList[gameIndex][1];
									
					//rc = sendto(sd, &input, 5, 0, (struct sockaddr *)&from_addr, sizeof(from_addr));			//send that choice to p2
					printf("sending game over %i %i %i %i %i", input[0], input[1], input[2], input[3], input[4]);
					rc = send(clientSDList[i], &input, 5, 0);
					if (rc < 1) { //confirm it sent
						perror("error - sento");
						printf("Terminating. . .\n"); 
						exit(2);
					}
				}
				
			}
			else{
				skipToListen = 0;
			}	
		//end player 1 AI//
	
		}//end for loop	
	}while (1);		 // go forever 
  
	return 0;
}

int checkTimeOuts(char board[ROWS][COLUMNS][MAXGAMES], long int gameStatusList[MAXGAMES][STATUSPRAMS]) {
	
	/***this function is currently unused*/
	
	struct timeval timeStamp; 
	gettimeofday(&timeStamp, NULL);
	int count = 0; 				//returns # of games timed out right now
	
	for (int i = 0; i < MAXGAMES; i++){
		if ((gameStatusList[i][0] != -1) && (timeStamp.tv_sec - gameStatusList[i][0]) > TIMETOWAIT){
			//timeout has occured
			printf("\nServer hasn't recieved activity from board #%d in %d seconds.\n", i, TIMETOWAIT);
			printf("board #%d has timed out, ", i);
			resetBoard(board, i, gameStatusList);
			count++;
		}
	}
	return count;
}

int checkOpenGame(long int gameStatusList[MAXGAMES][STATUSPRAMS]){
	//simply iterate through the games list and look for an open game board
	for (int i = 0; i < MAXGAMES; i++){
		if (gameStatusList[i][0] == -1){ 
			return i; 
		}
	}
	
	return -1; //no free game was found
}

int resetBoard(char board[ROWS][COLUMNS][MAXGAMES], int gameIndex, long int gameStatusList[MAXGAMES][STATUSPRAMS]){
	//resets all tiles to default state
	
	int i, j, count = 1;
	
	printf("resetting gameboard #%d\n", gameIndex);
	
	for (i=0;i<3;i++){	
		for (j=0;j<3;j++){
			board[i][j][gameIndex]= count + '0';
			count++;
		}			
	}
	
	//reset game status vars
	gameStatusList[gameIndex][0] = -1; //-1 in this position indicates an un-used gameboard
	gameStatusList[gameIndex][1] = 0;
	gameStatusList[gameIndex][2] = 0;
	
	//confirm that boards are reset properly
	//printf("board# %i, status of %d", gameIndex, board[ROWS][COLUMNS][gameIndex]);
	//printBoard(board, gameIndex);
	
	return 0;
}

int setChoice(char board[ROWS][COLUMNS][MAXGAMES], int gameIndex, int choice, int player){
	int row, column;
	char mark;      				//either an 'x' or an 'o'
	
	mark = (player == 1) ? 'X' : 'O'; 				//depending on who the player is, either us x or o
		
	/******************************************************************/
	/* little math here. you know the squares are numbered 1-9, but   */
	/* the program is using 3 rows and 3 columns. We have to do some  */
	/* simple math to conver a 1-9 to the right row/column            */
	/******************************************************************/
		
	row = (int)((choice-1) / ROWS); 
	column = (choice-1) % COLUMNS;

	// first check to see if the row/column chosen has the corresponding digit
	// IE if square 8 has and '8' then it is a valid choice
	if (board[row][column][gameIndex] == (choice+'0')){
		board[row][column][gameIndex] = mark;				//then assign the mark to that slot
	}
	else {
		printf("Invalid move ");
	}

	return 1; 
}

int checkWin(char board[ROWS][COLUMNS][MAXGAMES], int gameIndex){
  /************************************************************************/
  /* brute force check to see if someone won, or if there is a draw       */
  /* return 1 if a game is won by either player							  */
  /* return a 0 if the game is 'over' and return -1 if game should go on  */
  /************************************************************************/
	if (board[0][0][gameIndex] == board[0][1][gameIndex] && 
		board[0][1][gameIndex] == board[0][2][gameIndex]) 	{return 1;} 	// row matches
		
	else if (board[1][0][gameIndex] == board[1][1][gameIndex] && 
			board[1][1][gameIndex] == board[1][2][gameIndex]) {return 1;} // row matches

	else if (board[2][0][gameIndex] == board[2][1][gameIndex] && 
			board[2][1][gameIndex] == board[2][2][gameIndex]) {return 1;} // row matches
		
	else if (board[0][0][gameIndex] == board[1][0][gameIndex] && 
			board[1][0][gameIndex] == board[2][0][gameIndex]) {return 1;} // column
		
	else if (board[0][1][gameIndex] == board[1][1][gameIndex] && 
			board[1][1][gameIndex] == board[2][1][gameIndex]) {return 1;} // column
		
	else if (board[0][2][gameIndex] == board[1][2][gameIndex] && 
			board[1][2][gameIndex] == board[2][2][gameIndex]) {return 1;} // column
		
	else if (board[0][0][gameIndex] == board[1][1][gameIndex] && 
			board[1][1][gameIndex] == board[2][2][gameIndex]) {return 1;} // diagonal
		
	else if (board[2][0][gameIndex] == board[1][1][gameIndex] && 
			board[1][1][gameIndex] == board[0][2][gameIndex]) {return 1;} // diagonal
		
	//serious brute force, checking if all tiles are overwritten	
	else if (board[0][0][gameIndex] != '1' &&
			board[0][1][gameIndex] != '2' &&
			board[0][2][gameIndex] != '3' &&
			board[1][0][gameIndex] != '4' &&
			board[1][1][gameIndex] != '5' && 
			board[1][2][gameIndex] != '6' &&
			board[2][0][gameIndex] != '7' &&
			board[2][1][gameIndex] != '8' &&
			board[2][2][gameIndex] != '9') {return 0;} 	// Return of 0 means game over due to draw
	
	else {return  - 1;} 					// return of -1 means no win, no draw, so keep playing
}

void printBoard(char board[ROWS][COLUMNS][MAXGAMES], int gameIndex){
	/*****************************************************************/
	/* brute force print out the board and all the squares/values    */
	/*****************************************************************/

	printf("\n\n\n\tCurrent TicTacToe Game\n\n");

	printf("Player 1 (X)  -  Player 2 (O)\n\n\n");

	printf("     |     |     \n");
	printf("  %c  |  %c  |  %c \n", board[0][0][gameIndex], board[0][1][gameIndex], board[0][2][gameIndex]);

	printf("_____|_____|_____\n");
	printf("     |     |     \n");

	printf("  %c  |  %c  |  %c \n", board[1][0][gameIndex], board[1][1][gameIndex], board[1][2][gameIndex]);

	printf("_____|_____|_____\n");
	printf("     |     |     \n");

	printf("  %c  |  %c  |  %c \n", board[2][0][gameIndex], board[2][1][gameIndex], board[2][2][gameIndex]);

	printf("     |     |     \n\n");
}

int initSharedState(char board[ROWS][COLUMNS][MAXGAMES], long int gameStatusList[MAXGAMES][STATUSPRAMS]){    
	/* this just initializing the shared state aka the board */
	printf ("initializing game boards.\n");
	
	int i, j, k, count = 1;
	for (k=0; k<MAXGAMES; k++){
		
		for (i=0;i<3;i++){
			
			for (j=0;j<3;j++){
				board[i][j][k] = count + '0';
				count++;
			}
			
		}
		
		//init game status vars
		gameStatusList[k][0] = -1; //-1 in this position indicates an un-used gameboard
		gameStatusList[k][1] = 0;
		gameStatusList[k][2] = 0;
		count = 1;
		
		//confirm that boards are initialized properly
		//printf("board# %i, status of %d", k, board[ROWS][COLUMNS][k]);
		//printBoard(board, k);
	}
	
	return 0;
}

int aiChoice(char board[ROWS][COLUMNS][MAXGAMES], int gameIndex, int choice){
	/* Very basic AI
		Basically, its just going to add 1 to whatever
		is in int choice. We'll do some error checking up here so that if
		board slots 1-8 are filled it doesn't print 8 "invalid move" msgs
		and also so that it can't choose >9                               */
	
	int row, column;
	
	choice = choice+1;
			
	if (choice > 9){
		choice = 1;
	}
	/* Dev note: 
		you may notice that this error check below is identical to some code in 
		tictactoe funct. I considered turning that code into a funct for use 
		here as well, however due to the code structure it would have required 
		a significant rewrite and/or passing of a large amount of vars that 
		wouldnt even be used here.
		
		If, in the future, "check board" functionality needs to be re-used in 
		a THIRD place then i'll take the time to refactor this. 
		Until then, this works just as well	without being *too* redundant			
	*/
	row = (int)((choice-1) / ROWS); 
	column = (choice-1) % COLUMNS;
			
	if (board[row][column][gameIndex] == (choice+'0')){
		//nothing happens, the choice was good and 
		//we let the normal procedure occur
	}
	else {
		//the choice was invalid, increment up until valid
		while(board[row][column][gameIndex] != (choice+'0')){
			choice = choice+1;
			
			if (choice > 9){
				choice = 1;
			}
			
			row = (int)((choice-1) / ROWS); 
			column = (choice-1) % COLUMNS;
		}
	}
	
	return choice;
}

int bindSocket(int portNum, int socketType){
	int sd, rc; 				//socket descriptions and return code
	struct sockaddr_in server_addr;		//setting up object for this server
	
	//defining socket description
	if(socketType==0){
		sd = socket(AF_INET, SOCK_STREAM, 0);
	}
	else{
		sd = socket(AF_INET, SOCK_DGRAM, 0);
	}
	
	//defining basic sever address info for the socket
	bzero((char *)&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = portNum;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	//assigning name to socket
	rc = bind(sd, (struct sockaddr *) &server_addr, sizeof(server_addr));
	if (rc == -1){
		printf("Failed to bind socket, try different port?\nterminating. . .\n");
		exit(6);
	}
	
	return sd;
}