# Net-enabled Tic Tac Toe game

TicTacToe – MultiPlayer – Stream with Multicast and Failover

This project is based on an in class assignment, and includes the following features:

	• The ability of a client to note that a server is down  
	• The ability of the client to multicast to the server group asking for a new server 
	• The ability of the client to read an ip address from a file when the multicast group fail to respond  
	• The ability of the client to send game information to the server to ‘reconnect’ and ‘resume’ the game from the last play. 
	• The ability of the client to gracefully handle the case when no new servers are available.
	• The ability of the server to handle new or resume games from direct connection or from multicast group
	• For more details, see server README.md
