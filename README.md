# RemoteBASH
Linux client-server C program that imitates some of the functionality of SSH

Due to lack of encryption and other security measures, this program in its current state is not meant to be used as a substitute for SSH, and it is not recommended for use with important documents or personally-identifiable information

#### To Run Server:
1. Clone the repository or download the files onto a Linux machine you want to act as the host for remote access
2. Open a terminal and change to the directory where the files are
3. Compile the server by entering the command "make server"
4. Run the server by entering "./server"
5. The server should now be active and ready to accept client connections
6. To close all client connections and stop the server, enter [Ctrl+C]
7. Closing the terminal will stop the server process and close all client connections, so be sure to leave the server terminal open until you are finished connecting to the host machine

#### To Run Client:
1. Download "client.c" and "Makefile" on a Linux machine you'd like to remotely access the host from
2. Open a terminal and change to the directory with the files
3. Compile the client program with the command "make client"
4. While the server is active, run the client with "./client [IP_ADDRESS]" where [IP_ADDRESS] is the ipv4 address of the machine the server is running on
5. The client should now be connected to the server running on the host machine, and all commands (except "exit") will be routed to the host machine and executed there, with the result of each command displayed in the client terminal
6. To exit the program and close the connection with the host machine, use the command "exit" or [Ctrl+C]
