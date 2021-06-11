# assignment-6-Amanuel_Jordan
Server written by Jordan Stakenas

This server supports single player vs AI tictactoe games, with base settings
for up to 10 simultaneous games. Game lobbies operate under first-come first-serve
basis. The server, once started up, will continue to run and wait for connections.

The original tictactoe game was developed by Dave Ogle. This server uses Daves'
original program as the code base. All net-enabled additions were done by Jordan. 

This version has been modified to once again use socket streams. Functionality for 
sequencing of messages remains intact, however most logic to handle lost/duplicate
datagrams has been removed as it is deprecated

Using protocol v6

Protocol 6  supports a singular multicast datagram socket which listens for clients who 
are actively looking for a resume game, in the case that they lose connection with their 
initial host.

datagram format, where each segment is a single byte:
 |version|command|choice|game#|sequence#|
 in case of command = RESUME, datagram format is
 |version|command|choice|game#|sequence#|gameBoardTiles[9bytes]|
 

Usage is as follows

    make
	
    ./tictactoeServer <local-port>
  
  
Known bugs:
	No currently known bugs