#include "include/client.h"
#include <sys/select.h>

void showMenu();
void insertCommand();

int main(int argc, char *const argv[]) {
    ClientContext context;
    vector<unsigned char> buffer;
    fd_set fds;
    int maxfd;
    int option = -1;


    string input;
    string peer;
    string username;
    string password;
    string message;
    //unsigned char *buffer = NULL;
    vector<string> onlineUsers;
   
    try {
        //buffer = new (nothrow) unsigned char[MAX_MESSAGE_SIZE];
        //if(!buffer) throw runtime_error("Buffer not allocated.");
        cout << "\n-------Authentication-------" << endl;
        context.clientSocket->makeConnection();
        receive(context.clientSocket, buffer);
        cout << "Connection confirmed: " << buffer.data() << endl;
        buffer.clear();
        username = readFromStdout("Insert username: ");
        password = readPassword();
        context.crypto->readPrivateKey(username, password, context.prvKeyClient);
        authentication(context, username);
        cout << "-----------------------------" << endl << endl;

        while (true) {
            maxfd = (context.clientSocket->getMasterFD() > STDIN_FILENO) ? context.clientSocket->getMasterFD() : STDIN_FILENO;
            FD_ZERO(&fds);
            FD_SET(context.clientSocket->getMasterFD(), &fds); 
            FD_SET(STDIN_FILENO, &fds); 

            showMenu();
            cout << "--> ";
            cout.flush();  
            
            select(maxfd+1, &fds, NULL, NULL, NULL); 

            if(FD_ISSET(0, &fds)) {  
                cin >> option;
                cin.ignore();
            }

            if(FD_ISSET(context.clientSocket->getMasterFD(), &fds)) {
                receive(context.clientSocket, buffer);
                
                if(buffer.at(0) == OP_REQUEST_TO_TALK) option = 3;
            }

            switch(option) {
                case 1:
                    cout << "--------- Online User List ---------" << endl;
                    break;
                case 2:
                    cout << "\n-------Request to talk-------" << endl;
                    //peer = readFromStdout("Insert username: ");
                    sendRequestToTalk(context);
                    break;
                case 3:
                    cout << "\n-------Received request to talk-------" << endl;
                    receiveRequestToTalk(context, buffer);
                    break;
                case 0:
                    cout << "Bye." << endl;
                    return 0;
                default:
                    cout << "Insert a valid command." << endl;
            }
        }
    } catch (const exception &e) {
        //if(buffer != nullptr) delete[] buffer;
        cout << "Exit due to an error:\n" << endl;
        cerr << e.what() << endl;
        return 0;
    }

    //delete[] buffer;
    return 0;
}

void showMenu() {
    cout << endl;
    cout << "1. Online users" << endl;
    cout << "2. Request to talk" << endl;
    cout << "0. Exit" << endl;
}

